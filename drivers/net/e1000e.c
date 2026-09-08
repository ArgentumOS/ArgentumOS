/*
 * fnx/drivers/net/e1000e.c
 *
 * Intel 82574L PCI NIC (QEMU's e1000e, PCI 8086:10D3) behind the
 * ext_* API. The e1000e is the successor of the classic e1000 and in
 * legacy mode (RFCTL_EXTEN clear) accepts the same 16-byte descriptors
 * and the same register layout (CTRL/ICR/IMS/RCTL/TCTL + the queue-0
 * ring registers RDBAL0/RDLEN0/RDH0/RDT0/TDBAL0/TDLEN0/TDH0/TDT0), so
 * this driver is the e1000 driver with the 82574L device ID. MSI-X is
 * not enabled; the legacy INTx line is used (ICR read clears it).
 *
 *   Registers (32-bit, MMIO at the raw BAR0 phys - the kernel identity
 *   maps the 1GB-4GB PCI hole): CTRL=0x00, STATUS=0x08, ICR=0xC0
 *   (interrupt cause, read + w1c), IMS=0xD0 (mask set), IMC=0xD8
 *   (mask clear), RCTL=0x100 (EN=0x2, BAM=0x8000, buffer 2048),
 *   TCTL=0x400 (EN=0x2, PSP=0x8), RDBAL=0x2800/RDLEN=0x2808/
 *   RDH=0x2810/RDT=0x2818, TDBAL=0x3800/TDLEN=0x3808/TDH=0x3810/
 *   TDT=0x3818, RA=0x5400 (the MAC; RA+1 bit 31 = the address-valid
 *   bit - the reset pre-loads both, so the driver just reads them).
 *
 *   Descriptor (16 bytes): u64 buffer_addr | u32 word2 | u32 word3.
 *   TX: word2 = (len & 0xffff) | (EOP|RS|IFCS << 24); the chip sets
 *   word3 bit 0 (DD) when sent. RX: word2 = length + checksum, word3
 *   byte 0 = status (DD bit 0) - the chip writes both. The TX is
 *   kicked by writing TDT (set_tctl -> start_xmit); the RX by writing
 *   RDT (the tail - the chip owns the descriptors [RDH, RDT), so the
 *   driver keeps RDT = next-to-clean + ring length).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/asm.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/msix.h>
#include <fnx/mm.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/net.h>
#include <fnx/net/ext_net.h>

#ifdef CONFIG_NET

#define E1000_VENDOR	0x8086	/* Intel */
#define E1000E_DEVICE	0x10D3	/* 82574L */

#define E1000_CTRL	0x00000
#define E1000_STATUS	0x00008
#define E1000_ICR	0x000C0
#define E1000_IMS	0x000D0
#define E1000_IMC	0x000D8
#define E1000_RCTL	0x00100
#define E1000_TCTL	0x00400
#define E1000_RDBAL	0x02800
#define E1000_RDLEN	0x02808
#define E1000_RDH	0x02810
#define E1000_RDT	0x02818
#define E1000_TDBAL	0x03800
#define E1000_TDLEN	0x03808
#define E1000_TDH	0x03810
#define E1000_TDT	0x03818
#define E1000_RA	0x05400

#define E1000_RCTL_EN	0x00000002
#define E1000_RCTL_BAM	0x00008000
#define E1000_RCTL_SECRC	0x04000000
#define E1000_TCTL_EN	0x00000002
#define E1000_TCTL_PSP	0x00000008
#define E1000_TCTL_CT	0x000000F0	/* collision threshold 15 */

#define E1000_ICR_TXDW	0x00000001
#define E1000_ICR_LSC	0x00000004
#define E1000_ICR_RXT0	0x00000080
#define E1000_ICR_RXQ0	0x00100000	/* per-queue RX cause (MSI-X) */
#define E1000_IVAR	0x000E4		/* interrupt vector allocation */
#define E1000_IVAR_VALID	0x8	/* entry valid bit */

#define E1000_TXD_CMD_EOP	0x01000000
#define E1000_TXD_CMD_IFCS	0x02000000
#define E1000_TXD_CMD_RS	0x08000000

#define E1000_RXD_STAT_DD	0x01	/* descriptor done (RX status byte) */
#define E1000_TXD_STAT_DD	0x01	/* descriptor done (TX word3 bit 0) */

#define RING_ENTRIES	16
#define RX_BUF_SIZE	2048
#define TX_BUF_SIZE	2048

/* the 16-byte legacy descriptor */
struct e1000e_desc {
	unsigned long buffer_addr;	/* u64 DMA address */
	unsigned int word2;		/* TX: cmd_and_length; RX: length+cksum */
	unsigned int word3;		/* TX: status; RX: status/errors/special */
};

struct e1000e_device {
	int present;
	unsigned long mmio;		/* kernel VA of the mapped BAR0 */
	unsigned char irq;
	int msix;			/* using MSI-X instead of INTx */
	unsigned char mac[6];
	struct e1000e_desc *rx_ring;	/* kernel VA of the RX ring */
	struct e1000e_desc *tx_ring;	/* kernel VA of the TX ring */
	unsigned int rx_buf_phys[16];	/* phys of each RX buffer */
	unsigned int rx_cur;		/* next RX descriptor to clean */
	unsigned int tx_cur;		/* next TX descriptor to use */
	int rx_wait;			/* wait channel for ext_recvfrom sleepers */
};

static struct e1000e_device e1000e;

static unsigned int e1000e_reg_r(unsigned int reg)
{
	return *(volatile unsigned int *)(e1000e.mmio + reg);
}

static void e1000e_reg_w(unsigned int reg, unsigned int val)
{
	*(volatile unsigned int *)(e1000e.mmio + reg) = val;
}

static void e1000e_irq_handler(int num, struct sigcontext *sc)
{
	unsigned int cause;

	(void)num; (void)sc;
	if(!e1000e.present) {
		return;
	}
	cause = e1000e_reg_r(E1000_ICR);
	if(cause) {
		e1000e_reg_w(E1000_ICR, cause);	/* w1c */
		if(cause & (e1000e.msix ? E1000_ICR_RXQ0 : E1000_ICR_RXT0)) {
			wakeup(&e1000e.rx_wait);
		}
	}
}

static int e1000e_rx_pending(void)
{
	if(!e1000e.present) {
		return 0;
	}
	return (e1000e.rx_ring[e1000e.rx_cur].word3 & E1000_RXD_STAT_DD) ? 1 : 0;
}

static int e1000e_rx_dequeue(void *buffer, __size_t count)
{
	struct e1000e_desc *desc;
	unsigned char *frame = (unsigned char *)buffer;
	unsigned int len;
	unsigned int n;

	desc = &e1000e.rx_ring[e1000e.rx_cur];
	if(!(desc->word3 & E1000_RXD_STAT_DD)) {
		return -EAGAIN;
	}
	len = desc->word2 & 0xFFFF;
	if(len < 14 || len > RX_BUF_SIZE) {
		/* bogus: hand the descriptor back and resync */
		desc->word2 = 0;
		desc->word3 = 0;
		e1000e.rx_cur = (e1000e.rx_cur + 1) % RING_ENTRIES;
		/* e1000e: RDT is a wrapped index; the core's ring_empty treats
		 * RDT >= dlen/16 as empty (the classic e1000 tolerated RDT +
		 * ring length, the e1000e core does not) */
		e1000e_reg_w(E1000_RDT, (e1000e.rx_cur + RING_ENTRIES - 1) % RING_ENTRIES);
		return -EAGAIN;
	}
	n = (len < count) ? len : count;
	memcpy_b(frame, (unsigned char *)P2V(desc->buffer_addr), n);
	/* refill the descriptor + hand it back to the chip (the tail) */
	desc->word2 = 0;
	desc->word3 = 0;
	e1000e.rx_cur = (e1000e.rx_cur + 1) % RING_ENTRIES;
	e1000e_reg_w(E1000_RDT, (e1000e.rx_cur + RING_ENTRIES - 1) % RING_ENTRIES);
	return n;
}

static int e1000e_tx_send(const void *frame, unsigned int len)
{
	struct e1000e_desc *desc;
	unsigned char *buf;
	unsigned int slot, spin;

	if(!e1000e.present) {
		return -ENODEV;
	}
	if(len >= TX_BUF_SIZE) {
		return -EMSGSIZE;
	}
	slot = e1000e.tx_cur % RING_ENTRIES;
	for(spin = 0; spin < 100000; spin++) {
		if(!(e1000e.tx_ring[slot].word3 & E1000_TXD_STAT_DD)) {
			break;
		}
	}
	if(spin == 100000) {
		return -EAGAIN;
	}
	if(!(buf = (unsigned char *)kmalloc(TX_BUF_SIZE))) {
		return -ENOMEM;
	}
	memcpy_b(buf, frame, len);
	desc = &e1000e.tx_ring[slot];
	desc->buffer_addr = (unsigned long)V2P((addr_t)buf);
	desc->word2 = (len & 0xFFFF) | E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS |
		      E1000_TXD_CMD_IFCS;
	desc->word3 = 0;
	__asm__ __volatile__("" ::: "memory");
	/* kick the transmit (writing TDT runs start_xmit); e1000e's
	 * ring_empty treats TDT >= dlen/16 as empty, so wrap the index */
	e1000e_reg_w(E1000_TDT, (e1000e.tx_cur + 1) % RING_ENTRIES);
	for(spin = 0; spin < 100000; spin++) {
		if(desc->word3 & E1000_TXD_STAT_DD) {
			break;
		}
	}
	if(spin == 100000) {
		/* hand the descriptor back so the ring stays alive */
		desc->word3 = 0;
		desc->buffer_addr = 0;
		__asm__ __volatile__("" ::: "memory");
		kfree((addr_t)buf);
		e1000e.tx_cur++;
		return -EAGAIN;
	}
	/* the e1000's DD is chip-set: clear it so the slot-free check of
	 * the next send (which waits for DD clear) can pass */
	desc->word3 = 0;
	__asm__ __volatile__("" ::: "memory");
	kfree((addr_t)buf);
	e1000e.tx_cur++;
	return len;
}

static int e1000e_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return e1000e.present ? 0 : -ENODEV;
}

static int e1000e_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int e1000e_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int e1000e_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int e1000e_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int e1000e_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int e1000e_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int e1000e_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return e1000e_tx_send(buffer, count);
}

static int e1000e_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!e1000e.present) {
		return -ENODEV;
	}
	for(;;) {
		n = e1000e_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(e1000e_rx_pending()) {
				break;
			}
		}
		if(e1000e_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&e1000e.rx_wait, PROC_INTERRUPTIBLE);
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

static int e1000e_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return e1000e_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int e1000e_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return e1000e_tx_send(buffer, count);
}

static int e1000e_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!e1000e.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return e1000e_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops e1000e_ops = {
	e1000e_ext_open,
	e1000e_ext_close,
	e1000e_ext_bind,
	e1000e_ext_listen,
	e1000e_ext_connect,
	e1000e_ext_accept,
	e1000e_ext_ioctl,
	e1000e_ext_sendto,
	e1000e_ext_recvfrom,
	e1000e_ext_read,
	e1000e_ext_write,
	e1000e_ext_poll,
};

/* the kernel's page tables identity-map only the low 1GB + the RAM;
 * the e1000's BAR0 lands in the 2GB+ PCI hole, so map it into a fixed
 * kernel VA (pml4[510], unused by the kernel) */
#define E1000_MMIO_VA	0xFFFFC00000000000UL
#define E1000_MMIO_SIZE	0x20000		/* PNPMMIO_SIZE */
extern int map_page64(unsigned long vaddr, unsigned long paddr, unsigned long flags);

struct ext_net_ops *e1000e_probe(void)
{
	struct pci_device *pd;
	unsigned long mmio;
	unsigned int ra;
	int i;

	e1000e.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == E1000_VENDOR &&
		   (pd->device_id == E1000E_DEVICE)) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return NULL;
	}

	mmio = pd->bar[0] & 0xFFFFFFF0;
	if(!mmio) {
		return NULL;
	}
	/* map the MMIO region into the kernel VA space */
	for(i = 0; i < E1000_MMIO_SIZE / 4096; i++) {
		if(map_page64(E1000_MMIO_VA + i * 4096, mmio + i * 4096, 0x003)) {
			return NULL;
		}
	}
	e1000e.mmio = E1000_MMIO_VA;
	e1000e.irq = pd->irq;

	/* command: MEM | MASTER */
	pci_write_short(pd, 0x04, 0x0006);

	/* the reset pre-loads the MAC into RA (with the AV bit) */
	ra = e1000e_reg_r(E1000_RA);
	e1000e.mac[0] = ra & 0xFF;
	e1000e.mac[1] = (ra >> 8) & 0xFF;
	e1000e.mac[2] = (ra >> 16) & 0xFF;
	e1000e.mac[3] = (ra >> 24) & 0xFF;
	ra = e1000e_reg_r(E1000_RA + 4);
	e1000e.mac[4] = ra & 0xFF;
	e1000e.mac[5] = (ra >> 8) & 0xFF;

	/* rings + buffers (64-bit DMA: no low-window limit) */
	if(!(e1000e.rx_ring = (struct e1000e_desc *)kmalloc(RING_ENTRIES * 16))) {
		return NULL;
	}
	if(!(e1000e.tx_ring = (struct e1000e_desc *)kmalloc(RING_ENTRIES * 16))) {
		return NULL;
	}
	memset_b(e1000e.rx_ring, 0, RING_ENTRIES * 16);
	memset_b(e1000e.tx_ring, 0, RING_ENTRIES * 16);
	for(i = 0; i < RING_ENTRIES; i++) {
		if(!(e1000e.rx_buf_phys[i] = (unsigned int)V2P((addr_t)kmalloc(RX_BUF_SIZE)))) {
			return NULL;
		}
		e1000e.rx_ring[i].buffer_addr = e1000e.rx_buf_phys[i];
		e1000e.rx_ring[i].word2 = 0;
		e1000e.rx_ring[i].word3 = 0;
	}
	e1000e.rx_cur = 0;
	e1000e.tx_cur = 0;

	/* TX ring (the rings sit below 4GB, so the high dwords are 0) */
	e1000e_reg_w(E1000_TDBAL, (unsigned int)V2P((addr_t)e1000e.tx_ring));
	e1000e_reg_w(E1000_TDBAL + 4, 0);	/* TDBAH */
	e1000e_reg_w(E1000_TDLEN, RING_ENTRIES * 16);
	e1000e_reg_w(E1000_TDH, 0);
	e1000e_reg_w(E1000_TDT, 0);
	/* RX ring */
	e1000e_reg_w(E1000_RDBAL, (unsigned int)V2P((addr_t)e1000e.rx_ring));
	e1000e_reg_w(E1000_RDBAL + 4, 0);	/* RDBAH */
	e1000e_reg_w(E1000_RDLEN, RING_ENTRIES * 16);
	e1000e_reg_w(E1000_RDH, 0);
	e1000e_reg_w(E1000_RDT, 0);

	/* enable receive (2048 buffers, broadcast, strip CRC): the e1000e
	 * core pads total_size by 4 (FCS) unless SECRC is set, and its
	 * 10.0.11 loop then writes the 4 FCS bytes into a SECOND descriptor
	 * (len=4, EOP) - strip CRC to keep one descriptor per frame */
	e1000e_reg_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
	e1000e_reg_w(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT);

	/* hand all RX descriptors to the chip (the tail, wrapped: the e1000e
	 * core's ring_empty treats RDT >= dlen/16 as empty) */
	e1000e_reg_w(E1000_RDT, RING_ENTRIES - 1);

	if(e1000e.irq) {
		static struct interrupt irq_config_e1000e = { 0, "e1000e", &e1000e_irq_handler, NULL };
		if(!msix_pci_setup(pd, MSIX_VEC_BASE)) {
			/* MSI-X: RXQ0 -> vector 0 (IDT 0x30), no 8259 involvement */
			register_msix(MSIX_VEC_BASE - MSIX_VEC_BASE, &irq_config_e1000e);
			e1000e.msix = 1;
			printk("e1000e: MSI-X active on IDT vector 0x%x\n", MSIX_VEC_BASE);
			e1000e_reg_w(E1000_IVAR, (0 << 0) | E1000_IVAR_VALID);	/* RXQ0 */
			e1000e_reg_w(E1000_ICR, 0xFFFFFFFF);	/* clear pending */
			e1000e_reg_w(E1000_IMS, E1000_ICR_RXQ0);
		} else {
			register_irq(e1000e.irq, &irq_config_e1000e);
			e1000e_reg_w(E1000_ICR, 0xFFFFFFFF);	/* clear pending */
			enable_irq(e1000e.irq);
			e1000e_reg_w(E1000_IMS, E1000_ICR_RXT0);
		}
	}

	printk("e1000e: NIC %x:%x at 0x%lx, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		E1000_VENDOR, pd->device_id, mmio, e1000e.irq,
		e1000e.mac[0], e1000e.mac[1], e1000e.mac[2],
		e1000e.mac[3], e1000e.mac[4], e1000e.mac[5]);
	e1000e.present = 1;
	memcpy_b(e1000e_ops.mac, e1000e.mac, 6);
	return &e1000e_ops;
}

#endif /* CONFIG_NET */
