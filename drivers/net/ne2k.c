/*
 * fnx/drivers/net/ne2k.c
 *
 * NE2000-compatible PCI NIC (QEMU's ne2k_pci = RealTek 8029, PCI
 * 10EC:8029) behind the ext_* API. The NE2000 is a DP8390 with the
 * 16KB SRAM ring ON THE CARD: all data moves through the Remote DMA
 * port (programmed I/O), there is no guest-RAM DMA at all.
 *
 *   I/O map (BAR0, 256 bytes): 0x00-0x0F page-selectable 8390
 *   registers, 0x10 = RDMAP (remote DMA data port), 0x1F = reset.
 *
 *   RX ring: circular buffer of 256-byte pages between PSTART and
 *   PSTOP. The chip writes received frames at CURR (page 1, reg 0x07);
 *   the driver reads from BNRY (page 0, reg 0x03). Each packet has a
 *   4-byte header: status (bit 0 = OK), next page, len-lo, len-hi
 *   (len = frame size + 4). The RDMAP wraps at PSTOP back to PSTART.
 *
 *   TX: write the frame into the SRAM at TPSR via Remote DMA, set the
 *   byte count, then write CR with the TRANS bit - QEMU sends it
 *   synchronously and raises ISR bit 1 (ENISR_TX).
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

#define NE2K_VENDOR	0x10EC	/* RealTek */
#define NE2K_DEVICE	0x8029	/* RTL8029AS PCI NE2000 clone */
#define NE2K_ISA_IOBASE	0x300	/* the ISA NE2000's fixed I/O port */
#define NE2K_ISA_IRQ	9	/* and its default IRQ */

/* 8390 registers (I/O offsets; pages selected by CR bits 6-7) */
#define N_CR		0x00	/* command register (all pages) */
#define N_STARTPG	0x01	/* page 0: RX ring start page (WR) */
#define N_STOPPG	0x02	/* page 0: RX ring end page+1 (WR) */
#define N_BOUNDARY	0x03	/* page 0: BNRY - driver read page (RD/WR) */
#define N_TSR		0x04	/* page 0: TX status (RD) */
#define N_TPSR		0x04	/* page 0: TX start page (WR) */
#define N_TCNTLO	0x05	/* page 0: TX byte count low (WR) */
#define N_TCNTHI	0x06	/* page 0: TX byte count high (WR) */
#define N_ISR		0x07	/* page 0: interrupt status (RD/WR, w1c) */
#define N_RSARLO	0x08	/* page 0: remote DMA start addr low (WR) */
#define N_RSARHI	0x09	/* page 0: remote DMA start addr high (WR) */
#define N_RCNTLO	0x0A	/* page 0: remote DMA count low (WR) */
#define N_RCNTHI	0x0B	/* page 0: remote DMA count high (WR) */
#define N_RXCR		0x0C	/* page 0: receive config (WR) */
#define N_DCFG		0x0E	/* page 0: data config (WR) */
#define N_IMR		0x0F	/* page 0: interrupt mask (WR) */
#define N_RDMAP		0x10	/* remote DMA data port */
#define N_RESET		0x1F	/* reset port (read triggers a reset) */
#define N1_CURPAG	0x07	/* page 1: CURR - chip write page (RD/WR) */
#define N1_PHYS		0x01	/* page 1: PAR0-5 physical address (RD/WR) */

/* CR bits */
#define CR_STOP		0x01	/* stop the chip */
#define CR_START	0x02	/* start the chip (clears reset) */
#define CR_TRANS	0x04	/* transmit a frame */
#define CR_RREAD	0x08	/* remote DMA read */
#define CR_RWRITE	0x10	/* remote DMA write */
#define CR_NODMA	0x20	/* no remote DMA operation */
#define CR_PAGE1	0x40	/* page 1 register select */

/* ISR bits */
#define ISR_RX		0x01	/* packet received */
#define ISR_TX		0x02	/* packet transmitted */
#define ISR_RXERR	0x04
#define ISR_TXERR	0x08
#define ISR_OVER	0x10	/* RX ring overflow */
#define ISR_RDC		0x40	/* remote DMA complete */
#define ISR_INT_EN	0x1F

/* RX config bits */
#define RXCR_AB		0x04	/* accept broadcast */
#define RXCR_AM		0x08	/* accept multicast */
#define RXCR_PROMISC	0x10	/* promiscuous (accept all) */

/* SRAM layout: 16KB of card memory, pages 0x40-0x80 */
#define TX_PAGE		0x40	/* TX buffer: pages 0x40-0x46 */
#define RX_START	0x46	/* RX ring: pages 0x46-0x7F */
#define RX_STOP		0x80
#define RING_PAGES	(RX_STOP - RX_START)
#define TX_MAX_LEN	((RX_START - TX_PAGE) * 256)

struct ne2k_device {
	int present;
	unsigned short iobase;
	unsigned char irq;
	unsigned char mac[6];
	unsigned char bnry;	/* our ring read position (page) */
	int rx_wait;		/* wait channel for ext_recvfrom sleepers */
};

static struct ne2k_device ne2k;

static void ne2k_irq_handler(int num, struct sigcontext *sc)
{
	unsigned char isr;

	(void)num; (void)sc;
	if(!ne2k.present) {
		return;
	}
	outport_b(ne2k.iobase + N_CR, CR_NODMA);	/* pin page 0 */
	isr = inport_b(ne2k.iobase + N_ISR);
	if(isr) {
		outport_b(ne2k.iobase + N_ISR, isr);	/* write-1-to-clear */
		if(isr & (ISR_RX | ISR_OVER)) {
			wakeup(&ne2k.rx_wait);
		}
	}
}

/* remote DMA helpers: set the start address + count, then move bytes
 * through the RDMAP port (8-bit mode: DCFG bit 0 clear, so one port
 * access moves one byte; the address wraps PSTOP->PSTART) */
static void ne2k_rdma_set(unsigned short addr, unsigned int count)
{
	outport_b(ne2k.iobase + N_RSARLO, addr & 0xFF);
	outport_b(ne2k.iobase + N_RSARHI, addr >> 8);
	outport_b(ne2k.iobase + N_RCNTLO, count & 0xFF);
	outport_b(ne2k.iobase + N_RCNTHI, count >> 8);
}

static void ne2k_rdma_read(unsigned short addr, void *dst, unsigned int count)
{
	unsigned char *p = (unsigned char *)dst;
	unsigned int i;

	ne2k_rdma_set(addr, count);
	/* real 8390/RTL8029 silicon needs the RREAD direction bit (+ START)
	 * to activate the remote DMA channel; QEMU routes the port
	 * unconditionally, so this is a no-op there but required on HW */
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_START | CR_RREAD);
	for(i = 0; i < count; i++) {
		p[i] = inport_b(ne2k.iobase + N_RDMAP);
	}
}

static void ne2k_rdma_write(unsigned short addr, const void *src, unsigned int count)
{
	const unsigned char *p = (const unsigned char *)src;
	unsigned int i;

	ne2k_rdma_set(addr, count);
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_START | CR_RWRITE);
	for(i = 0; i < count; i++) {
		outport_b(ne2k.iobase + N_RDMAP, p[i]);
	}
}

static unsigned char ne2k_curr(void)
{
	unsigned char curr;

	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_PAGE1);
	curr = inport_b(ne2k.iobase + N1_CURPAG);
	/* restore page 0: the caller's next register accesses (BNRY,
	 * TPSR, TBCR, ...) are all page-0 registers */
	outport_b(ne2k.iobase + N_CR, CR_NODMA);
	return curr;
}

static int ne2k_rx_pending(void)
{
	if(!ne2k.present) {
		return 0;
	}
	return (ne2k.bnry != ne2k_curr()) ? 1 : 0;
}

/* take the next packet out of the card's RX ring; returns the frame
 * length or -EAGAIN when the ring is empty */
static int ne2k_rx_dequeue(void *buffer, __size_t count)
{
	unsigned char hdr[4];
	unsigned char *frame = (unsigned char *)buffer;
	unsigned int total, n;
	unsigned char curr, bnry;

	curr = ne2k_curr();
	bnry = ne2k.bnry;
	if(bnry == curr) {
		return -EAGAIN;
	}
	/* one remote DMA of the 4-byte header + the frame: the address
	 * wraps at the ring end, so a wrapped packet reads in one pass */
	ne2k_rdma_read(bnry << 8, hdr, 4);
	if(!(hdr[0] & 0x01) || hdr[1] < RX_START || hdr[1] >= RX_STOP) {
		/* bad status or out-of-ring next page: advance past this slot
		 * when the next-page hint is sane, else resync to the chip
		 * (a straight jump to CURR could skip a real frame) */
		if(hdr[1] >= RX_START && hdr[1] < RX_STOP) {
			ne2k.bnry = hdr[1];
			outport_b(ne2k.iobase + N_BOUNDARY, hdr[1]);
		} else {
			ne2k.bnry = curr;
			outport_b(ne2k.iobase + N_BOUNDARY, curr);
		}
		return -EAGAIN;
	}
	total = hdr[2] | (hdr[3] << 8);	/* frame size + 4 */
	if(total < 4 || total > TX_MAX_LEN + 4) {
		ne2k.bnry = hdr[1];
		outport_b(ne2k.iobase + N_BOUNDARY, hdr[1]);
		return -EAGAIN;
	}
	n = (total - 4 < count) ? (total - 4) : count;
	ne2k_rdma_read((bnry << 8) + 4, frame, n);
	ne2k.bnry = hdr[1];
	outport_b(ne2k.iobase + N_BOUNDARY, hdr[1]);
	return n;
}

static int ne2k_tx_send(const void *frame, unsigned int len)
{
	unsigned int spin;

	if(!ne2k.present) {
		return -ENODEV;
	}
	if(len > TX_MAX_LEN) {
		return -EMSGSIZE;
	}
	/* force page 0: a previous CURR read left the CR on page 1, which
	 * would redirect the TPSR/TBCR writes to the wrong registers */
	outport_b(ne2k.iobase + N_CR, CR_NODMA);
	/* write the frame into the card SRAM at TPSR, then kick the TX */
	ne2k_rdma_write(TX_PAGE << 8, frame, len);
	outport_b(ne2k.iobase + N_TPSR, TX_PAGE);
	outport_b(ne2k.iobase + N_TCNTLO, len & 0xFF);
	outport_b(ne2k.iobase + N_TCNTHI, len >> 8);
	/* CR = NODMA | START | TRANS: QEMU sends synchronously and sets
	 * TSR bit 0 (ENISR_TX status). Poll TSR, not ISR - the IRQ handler
	 * clears ISR bits, so an ISR poll would race with it */
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_START | CR_TRANS);
	for(spin = 0; spin < 100000; spin++) {
		if(inport_b(ne2k.iobase + N_TSR) & 0x01) {
			break;
		}
	}
	if(spin == 100000) {
		return -EAGAIN;
	}
	/* clear the TX interrupt if the handler has not already */
	outport_b(ne2k.iobase + N_ISR, ISR_TX);
	return len;
}

static int ne2k_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return ne2k.present ? 0 : -ENODEV;
}

static int ne2k_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int ne2k_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int ne2k_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int ne2k_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int ne2k_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int ne2k_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int ne2k_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return ne2k_tx_send(buffer, count);
}

static int ne2k_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!ne2k.present) {
		return -ENODEV;
	}
	for(;;) {
		n = ne2k_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		/* brief busy-wait before sleeping */
		for(spin = 0; spin < 10000; spin++) {
			if(ne2k_rx_pending()) {
				break;
			}
		}
		if(ne2k_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&ne2k.rx_wait, PROC_INTERRUPTIBLE);
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

static int ne2k_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return ne2k_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int ne2k_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return ne2k_tx_send(buffer, count);
}

static int ne2k_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!ne2k.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return ne2k_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops ne2k_ops = {
	ne2k_ext_open,
	ne2k_ext_close,
	ne2k_ext_bind,
	ne2k_ext_listen,
	ne2k_ext_connect,
	ne2k_ext_accept,
	ne2k_ext_ioctl,
	ne2k_ext_sendto,
	ne2k_ext_recvfrom,
	ne2k_ext_read,
	ne2k_ext_write,
	ne2k_ext_poll,
};

/* shared DP8390 setup: reset, MAC, rings, IRQ, banner. Returns the ops
 * table when the device answers, NULL otherwise. */
static struct ext_net_ops *ne2k_setup(void)
{
	unsigned char prom[12];
	int n, spin;

	ne2k.present = 0;

	/* reset the chip (reading the reset port pulses it), then stop */
	inport_b(ne2k.iobase + N_RESET);
	for(spin = 0; spin < 100000; spin++) {
		if(inport_b(ne2k.iobase + N_ISR) & 0x80) {	/* ENISR_RESET */
			break;
		}
	}
	outport_b(ne2k.iobase + N_CR, CR_STOP);

	/* 8-bit remote DMA transfers */
	outport_b(ne2k.iobase + N_DCFG, 0x48);

	/* read the MAC: the reset autoloads it into the card SRAM with
	 * each byte duplicated (mem[0..11] = mac0 mac0 mac1 mac1 ...),
	 * so take the even bytes */
	ne2k_rdma_read(0, prom, 12);
	for(n = 0; n < 6; n++) {
		ne2k.mac[n] = prom[2 * n];
	}

	/* physical address filter (page 1 is selected in CR) */
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_PAGE1);
	for(n = 0; n < 6; n++) {
		outport_b(ne2k.iobase + N1_PHYS + n, ne2k.mac[n]);
	}

	/* RX ring: pages RX_START..RX_STOP-1; start the chip with the
	 * read pointer at PSTART (CURR=PSTART too, so the ring is empty) */
	outport_b(ne2k.iobase + N_CR, CR_NODMA);
	outport_b(ne2k.iobase + N_STARTPG, RX_START);
	outport_b(ne2k.iobase + N_STOPPG, RX_STOP);
	outport_b(ne2k.iobase + N_BOUNDARY, RX_START);
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_PAGE1);
	outport_b(ne2k.iobase + N1_CURPAG, RX_START);
	outport_b(ne2k.iobase + N_CR, CR_NODMA);
	ne2k.bnry = RX_START;

	/* receive config: accept broadcast + physical (physical has no
	 * enable bit on the 8390 - it is always matched) */
	outport_b(ne2k.iobase + N_RXCR, RXCR_AB);

	/* TX buffer location */
	outport_b(ne2k.iobase + N_TPSR, TX_PAGE);

	/* clear pending interrupts, then unmask only this line */
	if(ne2k.irq) {
		static struct interrupt irq_config_ne2k = { 0, "ne2k", &ne2k_irq_handler, NULL };
		register_irq(ne2k.irq, &irq_config_ne2k);
		outport_b(ne2k.iobase + N_ISR, 0x7F);
		enable_irq(ne2k.irq);
		outport_b(ne2k.iobase + N_IMR, ISR_INT_EN);
	}

	/* start the chip (also clears the reset bit) */
	outport_b(ne2k.iobase + N_CR, CR_NODMA | CR_START);

	printk("ne2k: NIC at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		ne2k.iobase, ne2k.irq,
		ne2k.mac[0], ne2k.mac[1], ne2k.mac[2],
		ne2k.mac[3], ne2k.mac[4], ne2k.mac[5]);
	ne2k.present = 1;
	memcpy_b(ne2k_ops.mac, ne2k.mac, 6);
	return &ne2k_ops;
}

/* the PCI NE2000 clone (RealTek 8029) */
struct ext_net_ops *ne2k_probe(void)
{
	struct pci_device *pd;
	unsigned short iobase;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == NE2K_VENDOR && pd->device_id == NE2K_DEVICE) {
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
	ne2k.iobase = iobase;
	ne2k.irq = pd->irq;
	pci_write_short(pd, 0x04, 0x0005);	/* IO | MASTER */
	return ne2k_setup();
}

/* the ISA NE2000 (QEMU's ne2k_isa, fixed I/O 0x300 + IRQ 9). Detection:
 * the reset pulse + the MAC read; an absent device returns all-ones */
struct ext_net_ops *ne2k_isa_probe(void)
{
	unsigned char prom[12];
	int n, spin;

	ne2k.iobase = NE2K_ISA_IOBASE;
	ne2k.irq = NE2K_ISA_IRQ;

	inport_b(ne2k.iobase + N_RESET);
	for(spin = 0; spin < 100000; spin++) {
		if(inport_b(ne2k.iobase + N_ISR) & 0x80) {
			break;
		}
	}
	outport_b(ne2k.iobase + N_CR, CR_STOP);
	outport_b(ne2k.iobase + N_DCFG, 0x48);
	ne2k_rdma_read(0, prom, 12);
	for(n = 0; n < 6; n++) {
		if(prom[2 * n] != 0xFF) {
			break;
		}
	}
	if(n == 6) {
		return NULL;	/* no ISA NE2000 at the port */
	}
	return ne2k_setup();
}

#endif /* CONFIG_NET */
