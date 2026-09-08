/*
 * fnx/drivers/net/pcnet.c
 *
 * AMD PCnet (LANCE, Am79C970A) PCI NIC (QEMU's pcnet, PCI 1022:2000)
 * behind the ext_* API. The Lance is the classic 24-bit DMA NIC: the
 * driver builds an initialization block in guest RAM (mode, MAC,
 * multicast filter, RX/TX ring bases) and two rings of 8-byte
 * descriptors; the chip walks the rings and DMA's frames through the
 * buffers.
 *
 *   Register access: 16-bit RAP/RDP indirect ports at I/O 0x10-0x1F
 *   (RAP at I/O+0x12 with the register number, RDP at I/O+0x10 with
 *   the data, a READ at I/O+0x14 resets the chip). The APROM with the
 *   MAC occupies I/O 0x00-0x0F (bytes 0-5, readable in 16-bit mode).
 *
 *   CSR0 (w1c status in bits 8-15): INIT=0x0001, STRT=0x0002,
 *   STOP=0x0004, TDMD=0x0008 (TX kick), INEA=0x0040; IDON=0x0100,
 *   TINT=0x0200, RINT=0x0400, MERR=0x0800. The IRQ asserts when
 *   (csr0 & ~csr3) & 0x5f00 - CSR3 is the interrupt mask (0 = all
 *   unmasked). CSR1/2 = the init block address. CSR12-14 = the MAC
 *   after a reset.
 *
 *   Init block (24 bytes): u16 mode, u16 padr[3] (MAC), u16 ladrf[4]
 *   (multicast), u32 rdra = rx ring base | (rlen << 29), u32 tdra =
 *   tx ring base | (tlen << 29); rlen/tlen select 1 << rlen
 *   descriptors. The chip reads it on CSR0 = INIT.
 *
 *   Descriptor (8 bytes): word0 = 24-bit buffer address | (status <<
 *   16) (byte 3 = status bits 8-15: ENP=8, STP=9, OWN=15); u16 length
 *   (high nibble 0xf = ONES, low 12 = BCNT); u16 status (RX: the chip
 *   writes MCNT = frame size + 4 here; TX: misc). RX length semantics:
 *   the buffer is (4096 - BCNT) bytes. TX length semantics: the frame
 *   is (4096 - BCNT) bytes.
 *
 * All DMA memory (init block, rings, buffers) must sit below 16MB: the
 * Lance's 24-bit addresses + CSR2 = 0 (the high bits come from CSR2).
 * The TX buffers are pre-allocated at probe time - the low window can
 * exhaust under load and a per-send alloc then returns pages above
 * 16MB (the allocator keeps handing out the same high page, so even a
 * retry loop fails) - the in-order OWN poll guards the reuse.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/asm.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/mm.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/net.h>
#include <fnx/net/ext_net.h>

#ifdef CONFIG_NET

#define PCNET_VENDOR	0x1022	/* AMD */
#define PCNET_DEVICE	0x2000	/* LANCE / Am79C970A */

#define PCNET_IO_RDP	0x10	/* register data port (u16) */
#define PCNET_IO_RAP	0x12	/* register address port (u16) */
#define PCNET_IO_RESET	0x14	/* read = reset */
#define PCNET_IO_APROM	0x00	/* the MAC PROM (bytes 0-5, 16-bit mode) */

#define CSR0_INIT	0x0001
#define CSR0_STRT	0x0002
#define CSR0_STOP	0x0004
#define CSR0_TDMD	0x0008
#define CSR0_INEA	0x0040
#define CSR0_IDON	0x0100
#define CSR0_TINT	0x0200
#define CSR0_RINT	0x0400
#define CSR0_MERR	0x0800

#define RING_ENTRIES	16	/* 1 << 4 (rlen = 4 in the init block) */
#define RX_BUF_SIZE	2048
#define TX_BUF_SIZE	2048
#define DMA_MAX		0x1000000	/* 16MB: all DMA memory below this */

#define STATUS_OWN	0x80000000	/* bit 31: byte 3 of the descriptor word */
#define STATUS_STP	0x02000000
#define STATUS_ENP	0x01000000
#define LENGTH_ONES	0xf000		/* the mandatory ONES nibble */
#define RX_LEN_WORD	(LENGTH_ONES | (4096 - RX_BUF_SIZE))
#define TX_LEN_WORD(len)	(LENGTH_ONES | (4096 - (len)))

/* the 8-byte Lance descriptor */
struct pcnet_desc {
	unsigned int word0;		/* buffer addr (24) | status << 16 */
	unsigned short length;		/* ONES nibble | BCNT */
	unsigned short status;		/* RX: MCNT after the chip fills it */
};

struct pcnet_device {
	int present;
	unsigned short iobase;
	unsigned char irq;
	unsigned char mac[6];
	unsigned int initblk_phys;	/* the init block */
	struct pcnet_desc *rx_ring;	/* kernel VA of the RX ring */
	struct pcnet_desc *tx_ring;	/* kernel VA of the TX ring */
	unsigned int rx_buf_phys[16];	/* phys of each RX buffer */
	unsigned int rx_cur;		/* next RX descriptor to clean */
	unsigned int tx_cur;		/* next TX descriptor to use */
	unsigned int tx_buf_phys[4];	/* pre-allocated TX DMA buffers */
	int rx_wait;			/* wait channel for ext_recvfrom sleepers */
};

static struct pcnet_device pcnet;

extern unsigned long alloc_pages64(int);
extern void free_pages64(unsigned long, int);

/* allocate one DMA page below 16MB (the Lance's 24-bit addressing) */
static unsigned long pcnet_alloc_dma_page(void)
{
	unsigned long p;
	int n;

	for(n = 0; n < 32; n++) {
		p = alloc_pages64(1);
		if(!p) {
			return 0;
		}
		if(p >= 0x100000 && p < DMA_MAX) {
			return p;
		}
		free_pages64(p, 1);	/* out of range: drop it */
	}
	return 0;
}

static void pcnet_csr_w(unsigned int reg, unsigned short val)
{
	outport_w(pcnet.iobase + PCNET_IO_RAP, reg);
	outport_w(pcnet.iobase + PCNET_IO_RDP, val);
}

static unsigned short pcnet_csr_r(unsigned int reg)
{
	outport_w(pcnet.iobase + PCNET_IO_RAP, reg);
	return inport_w(pcnet.iobase + PCNET_IO_RDP);
}


static void pcnet_irq_handler(int num, struct sigcontext *sc)
{
	unsigned short status;

	(void)num; (void)sc;
	if(!pcnet.present) {
		return;
	}
	status = pcnet_csr_r(0);
	if(status & 0x7F00) {
		if(status & CSR0_RINT) {
			wakeup(&pcnet.rx_wait);
		}
		pcnet_csr_w(0, status & 0x7F00);	/* w1c the event bits */
	}
}

static int pcnet_rx_pending(void)
{
	int i;

	if(!pcnet.present) {
		return 0;
	}
	/* the chip clears OWN on the descriptor it has filled; it walks
	 * the ring in order as long as it never runs out of OWN
	 * descriptors, so check rx_cur first, then the whole ring */
	if(!(pcnet.rx_ring[pcnet.rx_cur].word0 & STATUS_OWN)) {
		return 1;
	}
	for(i = 0; i < RING_ENTRIES; i++) {
		if(!(pcnet.rx_ring[i].word0 & STATUS_OWN)) {
			return 1;
		}
	}
	return 0;
}

static int pcnet_rx_dequeue(void *buffer, __size_t count)
{
	struct pcnet_desc *desc;
	unsigned char *frame = (unsigned char *)buffer;
	unsigned int len;
	unsigned int n;
	int i, idx;

	/* the chip walks the ring in order as long as descriptors stay
	 * OWN, but if it ever runs out it scans for the last free one
	 * (out of order) - so find whichever descriptor got filled */
	for(i = 0; i < RING_ENTRIES; i++) {
		idx = (pcnet.rx_cur + i) % RING_ENTRIES;
		desc = &pcnet.rx_ring[idx];
		if(desc->word0 & STATUS_OWN) {
			continue;
		}
		/* MCNT (bytes 6-7) = frame size + 4 (the CRC is counted
		 * but not stored - the buffer holds the frame without it) */
		len = desc->status & 0x0FFF;
		if(len < 4 || len > RX_BUF_SIZE + 4) {
			desc->word0 = pcnet.rx_buf_phys[idx] | STATUS_OWN;
			desc->length = RX_LEN_WORD;
			desc->status = 0;
			pcnet.rx_cur = (idx + 1) % RING_ENTRIES;
			return -EAGAIN;
		}
		n = (len - 4 < count) ? (len - 4) : count;
		memcpy_b(frame, (unsigned char *)P2V(pcnet.rx_buf_phys[idx]), n);
		/* refill the descriptor */
		desc->word0 = pcnet.rx_buf_phys[idx] | STATUS_OWN;
		desc->length = RX_LEN_WORD;
		desc->status = 0;
		pcnet.rx_cur = (idx + 1) % RING_ENTRIES;
		return n;
	}
	return -EAGAIN;
}

static int pcnet_tx_send(const void *frame, unsigned int len)
{
	struct pcnet_desc *desc;
	unsigned long buf;
	unsigned int slot, spin;

	if(!pcnet.present) {
		return -ENODEV;
	}
	if(len >= TX_BUF_SIZE) {
		return -EMSGSIZE;
	}
	slot = pcnet.tx_cur % RING_ENTRIES;
	for(spin = 0; spin < 100000; spin++) {
		if(!(pcnet.tx_ring[slot].word0 & STATUS_OWN)) {
			break;
		}
	}
	if(spin == 100000) {
		return -EAGAIN;
	}
	/* the TX buffers are pre-allocated at probe time (the low DMA
	 * window below 16MB can exhaust under load; the per-send allocator
	 * then returns pages above it and the Lance's 24-bit addressing
	 * cannot reach them). The in-order OWN poll guarantees the chip is
	 * done with the buffer before it is reused. */
	buf = pcnet.tx_buf_phys[slot % 4];
	memcpy_b((unsigned char *)P2V(buf), frame, len);
	desc = &pcnet.tx_ring[slot];
	/* write the data fields first, OWN last (real-silicon ordering:
	 * the chip polls the OWN bit continuously) */
	desc->length = TX_LEN_WORD(len);
	desc->status = 0;
	__asm__ __volatile__("" ::: "memory");
	desc->word0 = (unsigned int)buf | STATUS_OWN | STATUS_STP | STATUS_ENP;
	/* kick the transmit (TDMD) */
	pcnet_csr_w(0, CSR0_TDMD | CSR0_INEA);
	for(spin = 0; spin < 100000; spin++) {
		if(!(desc->word0 & STATUS_OWN)) {
			break;
		}
	}
	if(spin == 100000) {
		desc->word0 = 0;	/* hand the slot back */
		desc->length = LENGTH_ONES;
		__asm__ __volatile__("" ::: "memory");
		pcnet.tx_cur++;
		return -EAGAIN;
	}
	pcnet.tx_cur++;
	return len;
}

static int pcnet_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return pcnet.present ? 0 : -ENODEV;
}

static int pcnet_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int pcnet_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int pcnet_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int pcnet_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int pcnet_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int pcnet_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int pcnet_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return pcnet_tx_send(buffer, count);
}

static int pcnet_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!pcnet.present) {
		return -ENODEV;
	}
	for(;;) {
		n = pcnet_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(pcnet_rx_pending()) {
				break;
			}
		}
		if(pcnet_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&pcnet.rx_wait, PROC_INTERRUPTIBLE);
			if(!current->timeout) {
				current->timeout = 0;
				return -EAGAIN;	/* timed out, no frame */
			}
			current->timeout = 0;
			if(woken) {
				return -EINTR;	/* interrupted by a signal */
			}
		}
	}
}

static int pcnet_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return pcnet_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int pcnet_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return pcnet_tx_send(buffer, count);
}

static int pcnet_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!pcnet.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return pcnet_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops pcnet_ops = {
	pcnet_ext_open,
	pcnet_ext_close,
	pcnet_ext_bind,
	pcnet_ext_listen,
	pcnet_ext_connect,
	pcnet_ext_accept,
	pcnet_ext_ioctl,
	pcnet_ext_sendto,
	pcnet_ext_recvfrom,
	pcnet_ext_read,
	pcnet_ext_write,
	pcnet_ext_poll,
};

struct ext_net_ops *pcnet_probe(void)
{
	struct pci_device *pd;
	unsigned short iobase;
	unsigned long p;
	unsigned char *initblk;
	int i;

	pcnet.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == PCNET_VENDOR && pd->device_id == PCNET_DEVICE) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return NULL;
	}

	iobase = (unsigned short)(pd->bar[0] & 0xFFFC);
	if(!iobase) {
		return NULL;
	}
	pcnet.iobase = iobase;
	pcnet.irq = pd->irq;

	/* command: IO | MASTER */
	pci_write_short(pd, 0x04, 0x0005);

	/* reset: a READ at the reset port; this also loads the MAC into
	 * CSR12-14 */
	/* the MAC lives in the APROM at I/O 0x00-0x05 (16-bit mode) */
	pcnet.mac[0] = inport_b(iobase + PCNET_IO_APROM + 0);
	pcnet.mac[1] = inport_b(iobase + PCNET_IO_APROM + 1);
	pcnet.mac[2] = inport_b(iobase + PCNET_IO_APROM + 2);
	pcnet.mac[3] = inport_b(iobase + PCNET_IO_APROM + 3);
	pcnet.mac[4] = inport_b(iobase + PCNET_IO_APROM + 4);
	pcnet.mac[5] = inport_b(iobase + PCNET_IO_APROM + 5);
	/* reset (loads the CSR defaults) */
	inport_w(iobase + PCNET_IO_RESET);

	/* the init block: mode | MAC | multicast filter | ring bases */
	if(!(p = pcnet_alloc_dma_page())) {
		return NULL;
	}
	pcnet.initblk_phys = (unsigned int)p;
	initblk = (unsigned char *)P2V(p);
	memset_b(initblk, 0, 24);
	initblk[0] = initblk[1] = 0;	/* mode = 0x0000 */
	initblk[2] = pcnet.mac[0];	/* padr[0] */
	initblk[3] = pcnet.mac[1];
	initblk[4] = pcnet.mac[2];	/* padr[1] */
	initblk[5] = pcnet.mac[3];
	initblk[6] = pcnet.mac[4];	/* padr[2] */
	initblk[7] = pcnet.mac[5];

	/* the RX ring */
	if(!(p = pcnet_alloc_dma_page())) {
		return NULL;
	}
	pcnet.rx_ring = (struct pcnet_desc *)P2V(p);
	/* the TX ring */
	if(!(p = pcnet_alloc_dma_page())) {
		return NULL;
	}
	pcnet.tx_ring = (struct pcnet_desc *)P2V(p);

	memset_b(pcnet.rx_ring, 0, RING_ENTRIES * 8);
	memset_b(pcnet.tx_ring, 0, RING_ENTRIES * 8);
	for(i = 0; i < RING_ENTRIES; i++) {
		if(!(p = pcnet_alloc_dma_page())) {
			return NULL;
		}
		pcnet.rx_buf_phys[i] = (unsigned int)p;
		pcnet.rx_ring[i].word0 = (unsigned int)p | STATUS_OWN;
		pcnet.rx_ring[i].length = RX_LEN_WORD;
		pcnet.rx_ring[i].status = 0;
	}
	pcnet.rx_cur = 0;
	pcnet.tx_cur = 0;
	for(i = 0; i < 4; i++) {
		if(!(p = pcnet_alloc_dma_page())) {
			return NULL;
		}
		pcnet.tx_buf_phys[i] = (unsigned int)p;
	}

	/* the ring bases: 24-bit address | (rlen << 29) for 16 entries */
	*(unsigned int *)(initblk + 16) = (unsigned int)V2P((addr_t)pcnet.rx_ring) | (4 << 29);
	*(unsigned int *)(initblk + 20) = (unsigned int)V2P((addr_t)pcnet.tx_ring) | (4 << 29);

	/* init: CSR1/2 = the init block address, CSR0 = INIT, then STRT */
	pcnet_csr_w(1, (unsigned short)(pcnet.initblk_phys & 0xFFFF));
	pcnet_csr_w(2, (unsigned short)(pcnet.initblk_phys >> 16));
	pcnet_csr_w(0, CSR0_INIT);
	pcnet_csr_w(0, CSR0_STRT);
	/* CSR3 = 0: no interrupt masking; INEA on */
	pcnet_csr_w(3, 0x0000);
	pcnet_csr_w(0, CSR0_INEA | CSR0_STRT);

	if(pcnet.irq) {
		static struct interrupt irq_config_pcnet = { 0, "pcnet", &pcnet_irq_handler, NULL };
		register_irq(pcnet.irq, &irq_config_pcnet);
		enable_irq(pcnet.irq);
	}

	printk("pcnet: NIC %x:%x at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		PCNET_VENDOR, PCNET_DEVICE, iobase, pcnet.irq,
		pcnet.mac[0], pcnet.mac[1], pcnet.mac[2],
		pcnet.mac[3], pcnet.mac[4], pcnet.mac[5]);
	pcnet.present = 1;
	memcpy_b(pcnet_ops.mac, pcnet.mac, 6);
	return &pcnet_ops;
}

#endif /* CONFIG_NET */
