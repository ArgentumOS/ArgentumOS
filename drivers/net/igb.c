/*
 * fnx/drivers/net/igb.c
 *
 * Intel 82576 PCI NIC (QEMU's igb, PCI 8086:10C9) behind the ext_*
 * API. The igb core is a superset of the e1000e core but ALWAYS uses
 * advanced (32-byte) RX descriptors (igb_rx_use_legacy_descriptor is
 * hardcoded false), while the TX path still accepts classic legacy
 * descriptors (igb_process_tx_desc falls through to the fragment-add
 * code when DEXT is clear). Register layout is the same as the e1000e
 * (CTRL/ICR/IMS/RCTL/TCTL + the queue-0 ring registers).
 *
 * QEMU's igb advanced RX descriptor (union e1000_adv_rx_desc, 16B in
 * this QEMU - read and wb overlap): the driver writes pkt_addr (the
 * buffer PA) at +0 and the chip DMA's the frame there, then overwrites
 * the whole 16 bytes with the writeback: pkt_info/hdr_info at +0..3,
 * rss at +4..7, status_error at +8 (DD = bit 0), length at +12 (bits
 * 0-13), vlan at +14. So DD is read from word2 and length from word3 -
 * SWAPPED vs the legacy layout - and the buffer PA must come from the
 * driver's own array (pkt_addr is clobbered).
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
#include <fnx/mm.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/net.h>
#include <fnx/net/ext_net.h>

#ifdef CONFIG_NET

#define E1000_VENDOR	0x8086	/* Intel */
#define IGB_DEVICE	0x10C9	/* 82576 */

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
#define E1000_MDIC		0x00020
#define E1000_STATUS_LU	0x00002
#define E1000_MDIC_REG_SHIFT	16
#define E1000_MDIC_PHY_SHIFT	21
#define E1000_MDIC_OP_WRITE	0x04000000
#define MII_BMCR_SPEED1000	(1 << 6)
#define MII_BMCR_FD		(1 << 8)
#define MII_BMCR_ANRESTART	(1 << 9)
#define MII_BMCR_AUTOEN		(1 << 12)
#define E1000_TCTL_EN	0x00000002
#define E1000_TCTL_PSP	0x00000008
#define E1000_TCTL_CT	0x000000F0	/* collision threshold 15 */

#define E1000_ICR_TXDW	0x00000001
#define E1000_ICR_LSC	0x00000004
#define E1000_ICR_RXT0	0x00000080

#define E1000_TXD_CMD_EOP	0x01000000
#define E1000_TXD_CMD_IFCS	0x02000000
#define E1000_TXD_CMD_RS	0x08000000

#define E1000_RXD_STAT_DD	0x01	/* descriptor done (RX status byte) */
#define E1000_TXD_STAT_DD	0x01	/* descriptor done (TX word3 bit 0) */

#define RING_ENTRIES	16
#define RX_BUF_SIZE	2048
#define TX_BUF_SIZE	2048

/* the 16-byte legacy descriptor */
struct igb_desc {
	unsigned long buffer_addr;	/* u64 DMA address */
	unsigned int word2;		/* TX: cmd_and_length; RX: length+cksum */
	unsigned int word3;		/* TX: status; RX: status/errors/special */
};

struct igb_device {
	int present;
	unsigned long mmio;		/* kernel VA of the mapped BAR0 */
	unsigned char irq;
	unsigned char mac[6];
	struct igb_desc *rx_ring;	/* kernel VA of the RX ring */
	struct igb_desc *tx_ring;	/* kernel VA of the TX ring */
	unsigned int rx_buf_phys[16];	/* phys of each RX buffer */
	unsigned int rx_cur;		/* next RX descriptor to clean */
	unsigned int tx_cur;		/* next TX descriptor to use */
	int rx_wait;			/* wait channel for ext_recvfrom sleepers */
};

static struct igb_device igb;

static unsigned int igb_reg_r(unsigned int reg)
{
	return *(volatile unsigned int *)(igb.mmio + reg);
}

static void igb_reg_w(unsigned int reg, unsigned int val)
{
	*(volatile unsigned int *)(igb.mmio + reg) = val;
}

static void igb_irq_handler(int num, struct sigcontext *sc)
{
	unsigned int cause;

	(void)num; (void)sc;
	if(!igb.present) {
		return;
	}
	cause = igb_reg_r(E1000_ICR);
	if(cause) {
		igb_reg_w(E1000_ICR, cause);	/* w1c */
		if(cause & E1000_ICR_RXT0) {
			wakeup(&igb.rx_wait);
		}
	}
}

static int igb_rx_pending(void)
{
	if(!igb.present) {
		return 0;
	}
	/* advanced RX descriptor: status_error at +8 (word2), DD = bit 0 */
	return (igb.rx_ring[igb.rx_cur].word2 & E1000_RXD_STAT_DD) ? 1 : 0;
}

static int igb_rx_dequeue(void *buffer, __size_t count)
{
	struct igb_desc *desc;
	unsigned char *frame = (unsigned char *)buffer;
	unsigned int len;
	unsigned int n;

	desc = &igb.rx_ring[igb.rx_cur];
	if(!(desc->word2 & E1000_RXD_STAT_DD)) {
		return -EAGAIN;
	}
	/* advanced writeback: length at +12 (word3 bits 0-15) */
	len = desc->word3 & 0xFFFF;
	if(len < 14 || len > RX_BUF_SIZE) {
		/* bogus: hand the descriptor back and resync */
		desc->word2 = 0;
		desc->word3 = 0;
		igb.rx_cur = (igb.rx_cur + 1) % RING_ENTRIES;
		/* igb: RDT is a wrapped index; the core's ring_empty treats
		 * RDT >= dlen/16 as empty (the classic e1000 tolerated RDT +
		 * ring length, the igb core does not) */
		igb_reg_w(E1000_RDT, (igb.rx_cur + RING_ENTRIES - 1) % RING_ENTRIES);
		return -EAGAIN;
	}
	/* pkt_addr is clobbered by the writeback - use our own buffer array */
	n = (len < count) ? len : count;
	memcpy_b(frame, (unsigned char *)P2V(igb.rx_buf_phys[igb.rx_cur]), n);
	/* refill the descriptor + hand it back to the chip (the tail) */
	desc->word2 = 0;
	desc->word3 = 0;
	igb.rx_cur = (igb.rx_cur + 1) % RING_ENTRIES;
	igb_reg_w(E1000_RDT, (igb.rx_cur + RING_ENTRIES - 1) % RING_ENTRIES);
	return n;
}

static int igb_tx_send(const void *frame, unsigned int len)
{
	struct igb_desc *desc;
	unsigned char *buf;
	unsigned int slot, spin;

	if(!igb.present) {
		return -ENODEV;
	}
	if(len >= TX_BUF_SIZE) {
		return -EMSGSIZE;
	}
	slot = igb.tx_cur % RING_ENTRIES;
	for(spin = 0; spin < 100000; spin++) {
		if(!(igb.tx_ring[slot].word3 & E1000_TXD_STAT_DD)) {
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
	desc = &igb.tx_ring[slot];
	desc->buffer_addr = (unsigned long)V2P((addr_t)buf);
	desc->word2 = (len & 0xFFFF) | E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS |
		      E1000_TXD_CMD_IFCS;
	desc->word3 = 0;
	__asm__ __volatile__("" ::: "memory");
	/* kick the transmit (writing TDT runs start_xmit); e1000e's
	 * ring_empty treats TDT >= dlen/16 as empty, so wrap the index */
	igb_reg_w(E1000_TDT, (igb.tx_cur + 1) % RING_ENTRIES);
	for(spin = 0; spin < 100000; spin++) {
		if(desc->word3 & E1000_TXD_STAT_DD) {
			break;
		}
	}
	if(spin == 100000) {
		printk("igb: TX timeout slot=%d tdh=%x tdt=%x\n", slot,
			igb_reg_r(E1000_TDH), igb_reg_r(E1000_TDT));
		/* hand the descriptor back so the ring stays alive */
		desc->word3 = 0;
		desc->buffer_addr = 0;
		__asm__ __volatile__("" ::: "memory");
		kfree((addr_t)buf);
		igb.tx_cur++;
		return -EAGAIN;
	}
	/* the e1000's DD is chip-set: clear it so the slot-free check of
	 * the next send (which waits for DD clear) can pass */
	desc->word3 = 0;
	__asm__ __volatile__("" ::: "memory");
	kfree((addr_t)buf);
	igb.tx_cur++;
	return len;
}

static int igb_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return igb.present ? 0 : -ENODEV;
}

static int igb_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int igb_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int igb_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int igb_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int igb_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int igb_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int igb_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return igb_tx_send(buffer, count);
}

static int igb_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!igb.present) {
		return -ENODEV;
	}
	for(;;) {
		n = igb_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(igb_rx_pending()) {
				break;
			}
		}
		if(igb_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&igb.rx_wait, PROC_INTERRUPTIBLE);
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

static int igb_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return igb_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int igb_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return igb_tx_send(buffer, count);
}

static int igb_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!igb.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return igb_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops igb_ops = {
	igb_ext_open,
	igb_ext_close,
	igb_ext_bind,
	igb_ext_listen,
	igb_ext_connect,
	igb_ext_accept,
	igb_ext_ioctl,
	igb_ext_sendto,
	igb_ext_recvfrom,
	igb_ext_read,
	igb_ext_write,
	igb_ext_poll,
};

/* the kernel's page tables identity-map only the low 1GB + the RAM;
 * the e1000's BAR0 lands in the 2GB+ PCI hole, so map it into a fixed
 * kernel VA (pml4[510], unused by the kernel) */
#define E1000_MMIO_VA	0xFFFFC00000000000UL
#define E1000_MMIO_SIZE	0x20000		/* PNPMMIO_SIZE */
extern int map_page64(unsigned long vaddr, unsigned long paddr, unsigned long flags);

struct ext_net_ops *igb_probe(void)
{
	struct pci_device *pd;
	unsigned long mmio;
	unsigned int ra;
	int i;

	igb.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == E1000_VENDOR &&
		   (pd->device_id == IGB_DEVICE)) {
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
	igb.mmio = E1000_MMIO_VA;
	igb.irq = pd->irq;

	/* command: MEM | MASTER */
	pci_write_short(pd, 0x04, 0x0006);

	/* the reset pre-loads the MAC into RA (with the AV bit) */
	ra = igb_reg_r(E1000_RA);
	igb.mac[0] = ra & 0xFF;
	igb.mac[1] = (ra >> 8) & 0xFF;
	igb.mac[2] = (ra >> 16) & 0xFF;
	igb.mac[3] = (ra >> 24) & 0xFF;
	ra = igb_reg_r(E1000_RA + 4);
	igb.mac[4] = ra & 0xFF;
	igb.mac[5] = (ra >> 8) & 0xFF;

	/* rings + buffers (64-bit DMA: no low-window limit) */
	if(!(igb.rx_ring = (struct igb_desc *)kmalloc(RING_ENTRIES * 16))) {
		return NULL;
	}
	if(!(igb.tx_ring = (struct igb_desc *)kmalloc(RING_ENTRIES * 16))) {
		return NULL;
	}
	memset_b(igb.rx_ring, 0, RING_ENTRIES * 16);
	memset_b(igb.tx_ring, 0, RING_ENTRIES * 16);
	for(i = 0; i < RING_ENTRIES; i++) {
		if(!(igb.rx_buf_phys[i] = (unsigned int)V2P((addr_t)kmalloc(RX_BUF_SIZE)))) {
			return NULL;
		}
		igb.rx_ring[i].buffer_addr = igb.rx_buf_phys[i];
		igb.rx_ring[i].word2 = 0;
		igb.rx_ring[i].word3 = 0;
	}
	igb.rx_cur = 0;
	igb.tx_cur = 0;

	/* TX ring (the rings sit below 4GB, so the high dwords are 0) */
	igb_reg_w(E1000_TDBAL, (unsigned int)V2P((addr_t)igb.tx_ring));
	igb_reg_w(E1000_TDBAL + 4, 0);	/* TDBAH */
	igb_reg_w(E1000_TDLEN, RING_ENTRIES * 16);
	igb_reg_w(E1000_TDH, 0);
	igb_reg_w(E1000_TDT, 0);
	/* RX ring */
	igb_reg_w(E1000_RDBAL, (unsigned int)V2P((addr_t)igb.rx_ring));
	igb_reg_w(E1000_RDBAL + 4, 0);	/* RDBAH */
	igb_reg_w(E1000_RDLEN, RING_ENTRIES * 16);
	igb_reg_w(E1000_RDH, 0);
	igb_reg_w(E1000_RDT, 0);

	/* enable receive (2048 buffers, broadcast, strip CRC): the e1000e
	 * core pads total_size by 4 (FCS) unless SECRC is set, and its
	 * 10.0.11 loop then writes the 4 FCS bytes into a SECOND descriptor
	 * (len=4, EOP) - strip CRC to keep one descriptor per frame */
	igb_reg_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
	igb_reg_w(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT);

	/* the igb's STATUS reset has no LU bit (unlike the e1000e), so the
	 * link only comes up after the 500ms autoneg timer completes - arm
	 * it by writing the PHY BMCR with ANRESTART via the MDIC register */
	igb_reg_w(E1000_MDIC, (1 << E1000_MDIC_PHY_SHIFT) | (0 << E1000_MDIC_REG_SHIFT) |
		  E1000_MDIC_OP_WRITE | MII_BMCR_SPEED1000 | MII_BMCR_FD |
		  MII_BMCR_AUTOEN | MII_BMCR_ANRESTART);
	/* wait for autoneg to complete (the 500ms timer sets STATUS LU;
	 * RX stays disabled until the link is up) */
	{
		unsigned int spin;
		for(spin = 0; spin < 3000000; spin++) {
			if(igb_reg_r(E1000_STATUS) & E1000_STATUS_LU) {
				break;
			}
		}
	}

	/* hand all RX descriptors to the chip (the tail, wrapped: the e1000e
	 * core's ring_empty treats RDT >= dlen/16 as empty) */
	igb_reg_w(E1000_RDT, RING_ENTRIES - 1);

	if(igb.irq) {
		static struct interrupt irq_config_igb = { 0, "igb", &igb_irq_handler, NULL };
		register_irq(igb.irq, &irq_config_igb);
		igb_reg_w(E1000_ICR, 0xFFFFFFFF);	/* clear pending */
		if(igb.irq >= 8) {
			outport_b(0xA1, 0xFF & ~(1 << (igb.irq - 8)));
		} else {
			outport_b(0x21, 0xFE & ~(1 << igb.irq));
		}
		igb_reg_w(E1000_IMS, E1000_ICR_RXT0);
	}

	printk("igb: NIC %x:%x at 0x%lx, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		E1000_VENDOR, pd->device_id, mmio, igb.irq,
		igb.mac[0], igb.mac[1], igb.mac[2],
		igb.mac[3], igb.mac[4], igb.mac[5]);
	igb.present = 1;
	memcpy_b(igb_ops.mac, igb.mac, 6);
	return &igb_ops;
}

#endif /* CONFIG_NET */
