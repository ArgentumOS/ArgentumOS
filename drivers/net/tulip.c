/*
 * fnx/drivers/net/tulip.c
 *
 * DEC 21143 PCI NIC (QEMU's tulip, PCI 1011:0019) behind the ext_* API.
 * The 21143 is the classic descriptor-ring NIC: the driver builds an RX
 * ring and a TX ring of 16-byte descriptors in guest RAM (status,
 * control, buf_addr1, buf_addr2) and the chip DMA's frames through
 * them. The address filter is programmed by a 192-byte SETUP frame sent
 * through the TX ring (16 x 12-byte entries, MAC bytes at offsets 0-1,
 * 4-5, 8-9 of each entry).
 *
 *   CSRs (32-bit, both BAR0 I/O and BAR1 MMIO, 8-byte spacing):
 *   CSR0=0x00 (SWR bit 0 = software reset), CSR1=0x08 (write = start
 *   the TX poll), CSR2=0x10 (write = flush queued RX), CSR3=0x18 (RX
 *   ring base), CSR4=0x20 (TX ring base), CSR5=0x28 (status, w1c),
 *   CSR6=0x30 (SR bit 1 start RX, ST bit 13 start TX), CSR7=0x38
 *   (interrupt mask). The IRQ asserts when a masked status bit sets
 *   CSR5_NIS/AIS and that summary bit is also masked-enabled.
 *
 *   RX descriptor: status OWN (bit 31) handed to the chip, cleared when
 *   filled (FL = frame length + 4 in bits 16-29; the buffer holds the
 *   frame without the CRC). Control = buf1 size (bits 0-10) | RER
 *   (bit 25) on the last ring entry. TX descriptor: status OWN set by
 *   the driver, control = buf1 size | FS (bit 29) | LS (bit 30) | TER
 *   on the last; the chip clears OWN when the frame is sent.
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

#define TULIP_VENDOR	0x1011	/* DEC */
#define TULIP_DEVICE	0x0019	/* 21143 */

/* CSRs (I/O, 8-byte spacing) */
#define CSR0		0x00	/* bus mode / software reset */
#define CSR1		0x08	/* transmit poll demand */
#define CSR2		0x10	/* receive poll demand */
#define CSR3		0x18	/* RX ring base */
#define CSR4		0x20	/* TX ring base */
#define CSR5		0x28	/* status (w1c) */
#define CSR6		0x30	/* operation mode */
#define CSR7		0x38	/* interrupt mask */
#define CSR9		0x48	/* MII/SROM management */

#define CSR0_SWR	0x00000001

#define CSR5_TI		0x00000001	/* TX interrupt */
#define CSR5_TU		0x00000004	/* TX unavailable */
#define CSR5_RI		0x00000040	/* RX interrupt */
#define CSR5_RU		0x00000080	/* RX unavailable */
#define CSR5_AIS	0x00008000	/* abnormal int summary */
#define CSR5_NIS	0x00010000	/* normal int summary */

#define CSR6_SR		0x00000002	/* start receive */
#define CSR6_ST		0x00002000	/* start transmit */

#define CSR7_TIM	0x00000001
#define CSR7_TUM	0x00000004
#define CSR7_RIM	0x00000040
#define CSR7_RUM	0x00000080
#define CSR7_AIM	0x00008000
#define CSR7_NIM	0x00010000
#define CSR7_INT_EN	0x000180C5

/* CSR9 (MII/SROM) bits */
#define CSR9_SR_CS	0x00000001	/* SROM chip select */
#define CSR9_SR_SK	0x00000002	/* SROM clock */
#define CSR9_SR_DI	0x00000004	/* SROM data in */
#define CSR9_SR_DO	0x00000008	/* SROM data out */
#define CSR9_SR		0x00000800	/* SROM interface enable */

#define OWN		0x80000000	/* descriptor ownership (both rings) */

/* descriptor control bits */
#define TDES1_BUF1	0x000007FF	/* buf1 size bits 0-10 */
#define TDES1_FS	0x20000000	/* first segment */
#define TDES1_LS	0x40000000	/* last segment */
#define TDES1_SET	0x08000000	/* setup frame */
#define TDES1_TER	0x02000000	/* end of ring (TX) */
#define RDES1_BUF1	0x000007FF
#define RDES1_RER	0x02000000	/* end of ring (RX) */

#define RX_STATUS_FL	0x3FFF0000	/* frame length (incl. CRC) */

#define RX_RING		16
#define TX_RING		8
#define RX_BUF_SIZE	0x7FF		/* 2047 bytes per RX buffer */
#define TX_BUF_SIZE	2048
#define SETUP_LEN	192

struct tulip_desc {
	unsigned int status;
	unsigned int control;
	unsigned int buf_addr1;
	unsigned int buf_addr2;
};

struct tulip_device {
	int present;
	unsigned short iobase;
	unsigned char irq;
	unsigned char mac[6];
	struct tulip_desc *rx_ring;	/* kernel VA of the RX ring */
	struct tulip_desc *tx_ring;	/* kernel VA of the TX ring */
	unsigned char *rx_buf[RX_RING];	/* kernel VA of the RX buffers */
	unsigned int rx_cur;		/* next RX descriptor to clean */
	unsigned int tx_cur;		/* next TX descriptor to use */
	int rx_wait;			/* wait channel for ext_recvfrom sleepers */
};

static struct tulip_device tulip;

static void tulip_csr_w(unsigned int csr, unsigned int val)
{
	outport_l(tulip.iobase + csr, val);
}

static unsigned int tulip_csr_r(unsigned int csr)
{
	return inport_l(tulip.iobase + csr);
}

static void tulip_irq_handler(int num, struct sigcontext *sc)
{
	unsigned int status;

	(void)num; (void)sc;
	if(!tulip.present) {
		return;
	}
	status = tulip_csr_r(CSR5);
	if(status) {
		tulip_csr_w(CSR5, status);	/* write-1-to-clear */
		if(status & (CSR5_RI | CSR5_RU)) {
			wakeup(&tulip.rx_wait);
		}
	}
}

static int tulip_rx_pending(void)
{
	if(!tulip.present) {
		return 0;
	}
	/* the chip clears OWN on the descriptor it has filled */
	return (tulip.rx_ring[tulip.rx_cur].status & OWN) ? 0 : 1;
}

static int tulip_rx_dequeue(void *buffer, __size_t count)
{
	struct tulip_desc *desc;
	unsigned char *frame = (unsigned char *)buffer;
	unsigned int len;
	unsigned int n;

	desc = &tulip.rx_ring[tulip.rx_cur];
	if(desc->status & OWN) {
		return -EAGAIN;
	}
	/* frame length includes the CRC: the buffer holds the frame
	 * without it, so strip 4 */
	len = (desc->status >> 16) & 0x3FFF;
	if(len < 4 || len > RX_BUF_SIZE + 4) {
		/* bogus: hand the descriptor back and resync */
		desc->status = OWN;
		tulip.rx_cur = (tulip.rx_cur + 1) % RX_RING;
		tulip_csr_w(CSR2, 0);
		return -EAGAIN;
	}
	n = (len - 4 < count) ? (len - 4) : count;
	memcpy_b(frame, tulip.rx_buf[tulip.rx_cur], n);
	/* refill the descriptor */
	desc->status = OWN;
	tulip.rx_cur = (tulip.rx_cur + 1) % RX_RING;
	/* kick the chip to process any queued frames */
	tulip_csr_w(CSR2, 0);
	return n;
}

static int tulip_tx_send(const void *frame, unsigned int len)
{
	struct tulip_desc *desc;
	unsigned char *buf;
	unsigned int slot, spin;

	if(!tulip.present) {
		return -ENODEV;
	}
	if(len >= TX_BUF_SIZE) {
		return -EMSGSIZE;	/* the 11-bit BUF1 field caps at 2047 */
	}
	slot = tulip.tx_cur % TX_RING;
	/* wait for the slot to be free (OWN clear) */
	for(spin = 0; spin < 100000; spin++) {
		if(!(tulip.tx_ring[slot].status & OWN)) {
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
	desc = &tulip.tx_ring[slot];
	desc->buf_addr1 = (unsigned int)V2P((addr_t)buf);
	desc->buf_addr2 = 0;
	desc->control = (len & TDES1_BUF1) | TDES1_FS | TDES1_LS;
	if(slot == TX_RING - 1) {
		desc->control |= TDES1_TER;
	}
	__asm__ __volatile__("" ::: "memory");
	desc->status = OWN;
	/* kick the transmit poll */
	tulip_csr_w(CSR1, 0);
	/* wait for the chip to send the frame (OWN clear) */
	for(spin = 0; spin < 100000; spin++) {
		if(!(desc->status & OWN)) {
			break;
		}
	}
	if(spin == 100000) {
		/* hand the descriptor back so the ring stays alive */
		desc->status = 0;
		desc->buf_addr1 = 0;
		__asm__ __volatile__("" ::: "memory");
		kfree((addr_t)buf);
		tulip.tx_cur++;
		return -EAGAIN;
	}
	kfree((addr_t)buf);
	tulip.tx_cur++;
	return len;
}

static int tulip_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return tulip.present ? 0 : -ENODEV;
}

static int tulip_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int tulip_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int tulip_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int tulip_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int tulip_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int tulip_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int tulip_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return tulip_tx_send(buffer, count);
}

static int tulip_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!tulip.present) {
		return -ENODEV;
	}
	for(;;) {
		n = tulip_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(tulip_rx_pending()) {
				break;
			}
		}
		if(tulip_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&tulip.rx_wait, PROC_INTERRUPTIBLE);
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

static int tulip_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return tulip_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int tulip_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return tulip_tx_send(buffer, count);
}

static int tulip_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!tulip.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return tulip_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops tulip_ops = {
	tulip_ext_open,
	tulip_ext_close,
	tulip_ext_bind,
	tulip_ext_listen,
	tulip_ext_connect,
	tulip_ext_accept,
	tulip_ext_ioctl,
	tulip_ext_sendto,
	tulip_ext_recvfrom,
	tulip_ext_read,
	tulip_ext_write,
	tulip_ext_poll,
};

static int tulip_alloc_rings(void)
{
	int i;

	if(!(tulip.rx_ring = (struct tulip_desc *)kmalloc(RX_RING * 16))) {
		return -ENOMEM;
	}
	if(!(tulip.tx_ring = (struct tulip_desc *)kmalloc(TX_RING * 16))) {
		return -ENOMEM;
	}
	memset_b(tulip.rx_ring, 0, RX_RING * 16);
	memset_b(tulip.tx_ring, 0, TX_RING * 16);
	for(i = 0; i < RX_RING; i++) {
		if(!(tulip.rx_buf[i] = (unsigned char *)kmalloc(RX_BUF_SIZE))) {
			return -ENOMEM;
		}
		tulip.rx_ring[i].status = OWN;
		tulip.rx_ring[i].control = RX_BUF_SIZE;
		if(i == RX_RING - 1) {
			tulip.rx_ring[i].control |= RDES1_RER;
		}
		tulip.rx_ring[i].buf_addr1 = (unsigned int)V2P((addr_t)tulip.rx_buf[i]);
		tulip.rx_ring[i].buf_addr2 = 0;
	}
	tulip.rx_cur = 0;
	tulip.tx_cur = 0;
	return 0;
}

/* send the 192-byte setup frame that programs the address filter (the
 * 21143 has no autoloaded filter - physical matches come from here) */
static int tulip_send_setup_frame(void)
{
	struct tulip_desc *desc;
	unsigned char *setup;
	unsigned int spin;

	if(!(setup = (unsigned char *)kmalloc(SETUP_LEN))) {
		return -ENOMEM;
	}
	memset_b(setup, 0, SETUP_LEN);
	/* entry 0 = our MAC: bytes 0-1, 4-5, 8-9 of the 12-byte slot */
	setup[0] = tulip.mac[0];
	setup[1] = tulip.mac[1];
	setup[4] = tulip.mac[2];
	setup[5] = tulip.mac[3];
	setup[8] = tulip.mac[4];
	setup[9] = tulip.mac[5];

	desc = &tulip.tx_ring[0];
	desc->buf_addr1 = (unsigned int)V2P((addr_t)setup);
	desc->buf_addr2 = 0;
	desc->control = SETUP_LEN | TDES1_SET | TDES1_FS | TDES1_LS | TDES1_TER;
	__asm__ __volatile__("" ::: "memory");
	desc->status = OWN;
	tulip_csr_w(CSR1, 0);
	for(spin = 0; spin < 100000; spin++) {
		if(!(desc->status & OWN)) {
			break;
		}
	}
	if(spin == 100000) {
		desc->status = 0;
		desc->buf_addr1 = 0;
		__asm__ __volatile__("" ::: "memory");
	}
	kfree((addr_t)setup);
	return (spin == 100000) ? -EAGAIN : 0;
}

/* read one 16-bit word from the on-board EEPROM via the CSR9 SROM
 * bit-bang (93C46: 2 start bits + 2 opcode bits + 6 address bits, then
 * 16 data bits MSB first on the SK rising edge) */
static void tulip_eeprom_clock(int bit)
{
	unsigned int v = CSR9_SR | CSR9_SR_CS | (bit ? CSR9_SR_DI : 0);

	outport_l(tulip.iobase + CSR9, v);
	outport_l(tulip.iobase + CSR9, v | CSR9_SR_SK);
}

static int tulip_eeprom_bit(void)
{
	unsigned int v = CSR9_SR | CSR9_SR_CS;

	outport_l(tulip.iobase + CSR9, v);
	outport_l(tulip.iobase + CSR9, v | CSR9_SR_SK);
	return (inport_l(tulip.iobase + CSR9) & CSR9_SR_DO) ? 1 : 0;
}

static unsigned short tulip_eeprom_read(unsigned int addr)
{
	unsigned short val = 0;
	int i;

	outport_l(tulip.iobase + CSR9, CSR9_SR);		/* CS low */
	outport_l(tulip.iobase + CSR9, CSR9_SR | CSR9_SR_CS);	/* CS high: cycle start */
	tulip_eeprom_clock(0);					/* 1st start bit (0) */
	tulip_eeprom_clock(1);					/* 2nd start bit (1) */
	tulip_eeprom_clock(1);					/* READ opcode (10) */
	tulip_eeprom_clock(0);
	for(i = 5; i >= 0; i--) {				/* 6 address bits */
		tulip_eeprom_clock((addr >> i) & 1);
	}
	for(i = 15; i >= 0; i--) {				/* 16 data bits */
		val = (val << 1) | tulip_eeprom_bit();
	}
	outport_l(tulip.iobase + CSR9, CSR9_SR);		/* CS low: end */
	return val;
}

struct ext_net_ops *tulip_probe(void)
{
	struct pci_device *pd;
	unsigned short iobase;
	int n;

	tulip.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == TULIP_VENDOR && pd->device_id == TULIP_DEVICE) {
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
	tulip.iobase = iobase;
	tulip.irq = pd->irq;

	/* command: IO | MASTER */
	pci_write_short(pd, 0x04, 0x0005);

	/* software reset (self-clears on real silicon), then clear */
	tulip_csr_w(CSR0, CSR0_SWR);
	for(n = 0; n < 100000 && (tulip_csr_r(CSR0) & CSR0_SWR); n++);
	tulip_csr_w(CSR0, 0);
	/* stop RX/TX */
	tulip_csr_w(CSR6, 0);

	/* the MAC lives in the EEPROM at words 10-12 (each a LE u16) */
	{
		unsigned short w;

		w = tulip_eeprom_read(10);
		tulip.mac[0] = w & 0xFF;
		tulip.mac[1] = w >> 8;
		w = tulip_eeprom_read(11);
		tulip.mac[2] = w & 0xFF;
		tulip.mac[3] = w >> 8;
		w = tulip_eeprom_read(12);
		tulip.mac[4] = w & 0xFF;
		tulip.mac[5] = w >> 8;
	}

	/* rings + RX buffers */
	if(tulip_alloc_rings()) {
		return NULL;
	}
	tulip_csr_w(CSR3, (unsigned int)V2P((addr_t)tulip.rx_ring));
	tulip_csr_w(CSR4, (unsigned int)V2P((addr_t)tulip.tx_ring));

	/* start RX + TX (the ST write auto-runs the TX poll; with no
	 * owned TX descriptors it just reports TU - harmless) */
	tulip_csr_w(CSR6, CSR6_SR | CSR6_ST);

	/* program the address filter via the setup frame */
	if(tulip_send_setup_frame()) {
		return NULL;
	}

	if(tulip.irq) {
		static struct interrupt irq_config_tulip = { 0, "tulip", &tulip_irq_handler, NULL };
		register_irq(tulip.irq, &irq_config_tulip);
		tulip_csr_w(CSR5, 0xFFFFFFFF);	/* clear pending */
		if(tulip.irq >= 8) {
			outport_b(0xA1, 0xFF & ~(1 << (tulip.irq - 8)));
		} else {
			outport_b(0x21, 0xFE & ~(1 << tulip.irq));
		}
		tulip_csr_w(CSR7, CSR7_INT_EN);
	}

	printk("tulip: NIC %x:%x at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		TULIP_VENDOR, TULIP_DEVICE, iobase, tulip.irq,
		tulip.mac[0], tulip.mac[1], tulip.mac[2],
		tulip.mac[3], tulip.mac[4], tulip.mac[5]);
	tulip.present = 1;
	memcpy_b(tulip_ops.mac, tulip.mac, 6);
	return &tulip_ops;
}

#endif /* CONFIG_NET */
