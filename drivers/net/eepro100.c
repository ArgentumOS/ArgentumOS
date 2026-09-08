/*
 * eepro100.c - Intel i82557/8/9 (eepro100) PCI NIC driver for FNX.
 *
 * Covers QEMU's whole eepro100 family: i82557a/b/c, i82558a/b, i82559a/b/c,
 * i82559er, i82562 and i82801 - all are the same chip with different PCI
 * revisions; the device IDs are 0x1229 (82557/8/9), 0x1209 (82559er, i82562)
 * and 0x2449 (i82801). The driver talks to the SCB (system control block)
 * in I/O space and to command blocks / receive frame areas in guest RAM.
 *
 * Cheat sheet (QEMU hw/net/eepro100.c):
 * - I/O: 0x00 status word / 0x01 ack, 0x02 command word (low nibble = RU
 *   command, upper nibble = CU command), 0x03 intmask, 0x04-0x07 pointer,
 *   0x0E-0x0F 93C46 EEPROM (CS=0x02 SK=0x01 DI=0x04 DO=0x08).
 * - CU commands: 0x00 NOP, 0x10 START (pointer = first TCB), 0x20 RESUME,
 *   0x60 CMD_BASE (pointer = CU base). RU: 0x00 NOP, 0x01 START, 0x02 RESUME,
 *   0x04 ABORT, 0x06 ADDR_LOAD (pointer = RU base).
 * - TCB (16 bytes): status, command (EL=0x8000 S=0x4000 I=0x2000, cmd
 *   field bits 2-0: 4 = CmdTx), link, tbd_array_addr, tbd_count, tcb_bytes.
 *   Simplified mode (tbd_array = 0xffffffff): the frame data sits at the
 *   TCB + 0x10 and tcb_bytes is its length; the chip sets status C|OK.
 * - RFD (16 bytes): status, command, link, rx_buf_addr, count, size. On
 *   receive the chip writes status 0xa000 + count and DMA's the frame to
 *   RFD+16 (QEMU ignores rx_buf_addr), then raises the FR interrupt
 *   (ack bit 0x40) and follows link.
 * - The CU/RU state machines run off the pointer + command word writes; a
 *   single TCB with EL|I sent per frame via CU_START is the simplest TX
 *   flow, and an RFD ring with RX_START covers the RX side.
 */

#include <fnx/limits.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/asm.h>
#include <fnx/sleep.h>
#include <fnx/pci.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/time.h>
#include <fnx/net/ext_net.h>

#define EEPRO100_VENDOR		0x8086	/* Intel */
#define EEPRO100_DEVICE_82557	0x1229	/* i82557/8/9 */
#define EEPRO100_DEVICE_82551IT	0x1209	/* i82559er, i82562 */
#define EEPRO100_DEVICE_82801	0x2449	/* 82801BA */

#define SCB_STATUS	0x00	/* status word (low byte of a 16-bit reg) */
#define SCB_ACK		0x01	/* interrupt acknowledge */
#define SCB_CMD		0x02	/* command word */
#define SCB_INTMASK	0x03	/* interrupt mask (0 = all unmasked) */
#define SCB_POINTER	0x04	/* 32-bit general purpose pointer */
#define SCB_EEPROM	0x0E	/* 93C46 EEPROM control */

/* ACK bits as ORed into the SCB ACK byte by QEMU's emulation
 * (eepro100_interrupt()): CX=0x80, CNA=0x20, FR=0x40, RNR=0x10. */
#define ACK_FR		0x40	/* frame received */
#define ACK_RNR		0x10	/* receive not ready */
#define ACK_TXDN	0x08	/* transmit done */
#define ACK_CXT		0x80	/* CU executed a command */
#define ACK_CNA		0x20	/* CU not active */

#define CU_NOP		0x00
#define CU_START	0x10
#define CU_RESUME	0x20
#define CU_CMD_BASE	0x60

#define RU_NOP		0x00
#define RU_START	0x01
#define RU_RESUME	0x02
#define RU_ABORT	0x04

#define CMD_EL		0x8000
#define CMD_S		0x4000
#define CMD_I		0x2000
#define CMD_CMDTX	0x0004	/* CmdTx (command field bits 2-0 = 4) */
#define CMD_IASETUP	0x0001	/* CmdIASetup */
#define CMD_CONFIGURE	0x0002	/* CmdConfigure */

#define STATUS_C	0x8000	/* command completed */
#define STATUS_OK	0x2000	/* no errors */

#define EEPROM_CS	0x02
#define EEPROM_SK	0x01
#define EEPROM_DI	0x04
#define EEPROM_DO	0x08

#define RX_RING_ENTRIES	16
#define RX_BUF_SIZE	1536	/* one page per frame buffer */
#define TX_SLOTS	4
#define TX_MAX_FRAME	1518

struct eepro100_rfd {
	unsigned short status;
	unsigned short command;
	unsigned int link;
	unsigned int rx_buf_addr;
	unsigned short count;
	unsigned short size;
};

struct eepro100_device {
	int present;
	unsigned long mmio;	/* kernel VA of the mapped BAR0 */
	unsigned char irq;
	unsigned char mac[6];
	/* RX ring: RFDs + frame area at RFD+16, in the low DMA window */
	unsigned long rfd_phys[RX_RING_ENTRIES];
	unsigned int rx_cur;
	/* TX pool: TCB slots with the frame area appended at +0x10 */
	unsigned long tcb_phys[TX_SLOTS];
	unsigned int tx_cur;
	unsigned int rx_wait;
};

#define EEPRO100_MMIO_VA	0xFFFFBF8000000000UL	/* pml4[509] */
#define EEPRO100_MMIO_SIZE	0x20000
extern int map_page64(unsigned long, unsigned long, unsigned long);

static struct eepro100_device eepro100;

static struct ext_net_ops eepro100_ops;
static int eepro100_recvfrom(void *, __size_t);

/* the SCB lives in MMIO space (the eepro100's BAR0 is memory, not I/O) */
static unsigned char eepro100_rb(unsigned short off)
{
	return *(volatile unsigned char *)(eepro100.mmio + off);
}
static void eepro100_wb(unsigned short off, unsigned char val)
{
	*(volatile unsigned char *)(eepro100.mmio + off) = val;
}
static void eepro100_ww(unsigned short off, unsigned short val)
{
	*(volatile unsigned short *)(eepro100.mmio + off) = val;
}
static void eepro100_wl(unsigned short off, unsigned int val)
{
	*(volatile unsigned int *)(eepro100.mmio + off) = val;
}

static void eepro100_cmd(unsigned char command)
{
	eepro100_ww(SCB_CMD, command);
}

static void eepro100_pointer(unsigned int ptr)
{
	eepro100_wl(SCB_POINTER, ptr);
}

static unsigned short eepro100_eeprom_read(unsigned char addr)
{
	unsigned short val = 0;
	unsigned char cs = 0;
	int i;

	/* 93C46 (16-bit words, as emulated by QEMU's eeprom93xx): CS high,
	 * then two start bits (0, 1), the 2-bit opcode (10 = read), the 6-bit
	 * address, then 16 data bits - all shifted in on SK rising edges;
	 * the data out (DO) is sampled after each data-clock rising edge. */
	eepro100_wb(SCB_EEPROM, 0);
	cs = 0;
	for(i = 0; i < 26; i++) {
		unsigned char bit = 0;

		if(i == 0) {
			bit = 0;		/* 1st start bit (0) */
		} else if(i == 1) {
			bit = 1;		/* 2nd start bit (1) */
		} else if(i < 4) {
			bit = (i == 2) ? 1 : 0;	/* opcode 10 = read */
		} else if(i < 10) {
			bit = (addr >> (9 - i)) & 1;
		} else {
			bit = 0;		/* data out */
		}
		if(i == 0) {
			cs = EEPROM_CS;
		}
		eepro100_wb(SCB_EEPROM, cs | (bit ? EEPROM_DI : 0));
		eepro100_wb(SCB_EEPROM, cs | (bit ? EEPROM_DI : 0) | EEPROM_SK);
		if(i >= 10) {
			val = (val << 1) | !!(eepro100_rb(SCB_EEPROM) & EEPROM_DO);
		}
	}
	eepro100_wb(SCB_EEPROM, 0);
	return val;
}

extern unsigned long alloc_pages64(int);
extern void free_pages64(unsigned long, int);

static unsigned long eepro100_alloc_dma_page(void)
{
	unsigned long p;
	int n;

	for(n = 0; n < 64; n++) {
		p = alloc_pages64(1);
		if(p >= 0x100000 && p < 0x1000000) {
			return p;
		}
		if(p) {
			free_pages64(p, 1);
		}
	}
	return 0;
}

static int eepro100_rx_pending(void)
{
	unsigned int i, idx;

	for(i = 0; i < RX_RING_ENTRIES; i++) {
		idx = (eepro100.rx_cur + i) % RX_RING_ENTRIES;
		if(*(volatile unsigned short *)P2V(eepro100.rfd_phys[idx]) & STATUS_C) {
			return 1;
		}
	}
	return 0;
}

static int eepro100_rx_dequeue(void *buffer, __size_t count)
{
	unsigned int i, idx, len;
	unsigned long buf, rfd;
	struct eepro100_rfd *r;

	for(i = 0; i < RX_RING_ENTRIES; i++) {
		idx = (eepro100.rx_cur + i) % RX_RING_ENTRIES;
		rfd = eepro100.rfd_phys[idx];
		r = (struct eepro100_rfd *)P2V(rfd);
		if(!(r->status & STATUS_C)) {
			continue;
		}
		len = r->count & 0x3FFF;
		if(len > count) {
			len = count;
		}
		/* QEMU DMA's the frame to RFD+16 (right after the descriptor),
		 * not to rx_buf_addr. */
		buf = rfd + sizeof(struct eepro100_rfd);
		memcpy_b(buffer, (void *)P2V(buf), len);
		/* reset the RFD for the next round */
		r->status = 0;
		r->count = 0;
		eepro100.rx_cur = (idx + 1) % RX_RING_ENTRIES;
		return len;
	}
	return -EAGAIN;
}

static int eepro100_tx_send(const void *frame, unsigned int len)
{
	unsigned long tcb;
	unsigned short *sp;
	unsigned int *lp;
	unsigned int spin;

	if(!eepro100.present) {
		return -ENODEV;
	}
	if(len > TX_MAX_FRAME) {
		return -EMSGSIZE;
	}
	tcb = eepro100.tcb_phys[eepro100.tx_cur % TX_SLOTS];

	/* status, command (I | CmdTx | EL), link, tbd_array (simplified),
	 * tcb_bytes, tx_threshold, tbd_count; the frame follows at +0x10.
	 * QEMU's eepro100_tx_t puts tcb_bytes at +12 (NOT +14): the driver
	 * must match, otherwise the chip sends a 0-length frame. */
	sp = (unsigned short *)P2V(tcb);
	sp[0] = 0;			/* status */
	sp[1] = CMD_I | CMD_CMDTX | CMD_EL;
	lp = (unsigned int *)((char *)sp + 4);
	lp[0] = 0;			/* link */
	lp[1] = 0xFFFFFFFF;		/* tbd_array_addr: simplified mode */
	sp = (unsigned short *)((char *)sp + 12);
	sp[0] = len;			/* tcb_bytes */
	sp[1] = 0;			/* tx_threshold + tbd_count */
	memcpy_b((void *)P2V(tcb + 0x10), frame, len);
	/* make the command visible before the chip reads it (OWN-style) */
	((volatile unsigned short *)P2V(tcb))[0] = 0;
	((volatile unsigned short *)P2V(tcb))[1] = CMD_I | CMD_CMDTX | CMD_EL;

	eepro100_pointer((unsigned int)tcb);
	eepro100_cmd(CU_START);

	for(spin = 0; spin < 100000; spin++) {
		if(*(volatile unsigned short *)P2V(tcb) & STATUS_C) {
			break;
		}
	}
	if(spin >= 100000) {
		return -EAGAIN;
	}
	eepro100.tx_cur++;
	return len;
}

static void eepro100_irq_handler(int num, struct sigcontext *sc)
{
	unsigned char ack;

	ack = eepro100_rb(SCB_ACK);
	if(!ack) {
		return;
	}
	/* write-1-to-clear the acknowledge bits */
	eepro100_wb(SCB_ACK, ack);
	if(ack & (ACK_FR | ACK_RNR)) {
		wakeup(&eepro100.rx_wait);
	}
}

static void eepro100_init_commands(void)
{
	/* one-time CU list: CmdConfigure + CmdIASetup, then EL */
	unsigned long cfg = eepro100.tcb_phys[0];
	unsigned long ias = eepro100.tcb_phys[1];
	char *p;
	unsigned int spin;
	int n;

	/* CmdConfigure: 22 config bytes at +8, mostly zeros */
	p = (char *)P2V(cfg);
	((unsigned short *)p)[0] = 0;
	((unsigned short *)p)[1] = CMD_CONFIGURE;
	*(unsigned int *)(p + 4) = (unsigned int)ias;	/* link */
	memset_b(p + 8, 0, 22);
	/* CmdIASetup: the MAC at +8 */
	p = (char *)P2V(ias);
	((unsigned short *)p)[0] = 0;
	((unsigned short *)p)[1] = CMD_IASETUP | CMD_EL;
	*(unsigned int *)(p + 4) = 0;
	memcpy_b(p + 8, eepro100.mac, 6);

	eepro100_pointer((unsigned int)cfg);
	eepro100_cmd(CU_START);
	/* the EL command's completion (the IASETUP TCB) sets STATUS_C */
	for(spin = 0; spin < 100000; spin++) {
		if(*(volatile unsigned short *)P2V(ias) & STATUS_C) {
			break;
		}
	}
	for(n = 0; n < TX_SLOTS; n++) {
		((volatile unsigned short *)P2V(eepro100.tcb_phys[n]))[0] = 0;
	}
}

static int eepro100_ext_recvfrom(int fd, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	(void)fd; (void)addr; (void)addrlen;
	return eepro100_recvfrom(buffer, count);
}

static int eepro100_recvfrom(void *buffer, __size_t count)
{
	extern unsigned int tv2ticks(const struct timeval *);
	struct timeval tv;
	unsigned int spin, woken;
	int n;

	for(;;) {
		n = eepro100_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(eepro100_rx_pending()) {
				break;
			}
		}
		if(eepro100_rx_pending()) {
			continue;
		}
		if(!current->timeout) {
			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&eepro100.rx_wait, PROC_INTERRUPTIBLE);
			if(!current->timeout) {
				current->timeout = 1;
			}
			if(woken) {
				return -EINTR;
			}
		} else {
			return -EAGAIN;
		}
	}
}

static int eepro100_poll(void)
{
	return eepro100_rx_pending();
}

static int eepro100_ext_poll(int fd, int flag)
{
	(void)fd; (void)flag;
	return eepro100_poll();
}

static int eepro100_ext_ioctl(int fd, int request, void *arg)
{
	(void)fd;
	return -EOPNOTSUPP;
}

/* the remaining ops mirror the pcnet/ne2k wrappers: the ext_* layer
 * routes everything through recvfrom/sendto/poll */
static int eepro100_ext_open(int fd, int flags, int mode) { (void)fd; (void)flags; (void)mode; return 0; }
static int eepro100_ext_close(int fd) { (void)fd; return 0; }
static int eepro100_ext_bind(int fd, const struct sockaddr *addr, int len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int eepro100_ext_listen(int fd, int backlog) { (void)fd; (void)backlog; return -EOPNOTSUPP; }
static int eepro100_ext_connect(int fd, const struct sockaddr *addr, int len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int eepro100_ext_accept(int fd, struct sockaddr *addr, unsigned int *len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int eepro100_ext_sendto(int fd, const void *buf, __size_t len, const struct sockaddr *addr, int alen)
{
	(void)fd; (void)addr; (void)alen;
	return eepro100_tx_send(buf, len);
}
static int eepro100_ext_read(int fd, void *buf, __size_t len) { (void)fd; return eepro100_recvfrom(buf, len); }
static int eepro100_ext_write(int fd, const void *buf, __size_t len) { (void)fd; return eepro100_tx_send(buf, len); }

struct ext_net_ops *eepro100_probe(void)
{
	struct pci_device *pd;
	unsigned short word0;
	unsigned long mmio, rfd;
	int i;

	eepro100.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == EEPRO100_VENDOR &&
		   (pd->device_id == EEPRO100_DEVICE_82557 ||
		    pd->device_id == EEPRO100_DEVICE_82551IT ||
		    pd->device_id == EEPRO100_DEVICE_82801)) {
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
	for(i = 0; i < EEPRO100_MMIO_SIZE / 4096; i++) {
		if(map_page64(EEPRO100_MMIO_VA + i * 4096, mmio + i * 4096, 0x003)) {
			return NULL;
		}
	}
	eepro100.mmio = EEPRO100_MMIO_VA;
	eepro100.irq = pd->irq;
	pci_write_short(pd, 0x04, 0x0006);	/* MEM | MASTER */

	/* the 93C46 holds the MAC in words 0-2 */
	word0 = eepro100_eeprom_read(0);
	if(word0 == 0xFFFF) {
		return NULL;	/* no EEPROM: not an eepro100 */
	}
	eepro100.mac[0] = word0 & 0xFF;
	eepro100.mac[1] = word0 >> 8;
	word0 = eepro100_eeprom_read(1);
	eepro100.mac[2] = word0 & 0xFF;
	eepro100.mac[3] = word0 >> 8;
	word0 = eepro100_eeprom_read(2);
	eepro100.mac[4] = word0 & 0xFF;
	eepro100.mac[5] = word0 >> 8;

	/* RX ring + TX pool in the low DMA window */
	for(i = 0; i < RX_RING_ENTRIES; i++) {
		rfd = eepro100_alloc_dma_page();
		if(!rfd) {
			return NULL;
		}
		eepro100.rfd_phys[i] = rfd;
	}
	for(i = 0; i < TX_SLOTS; i++) {
		eepro100.tcb_phys[i] = eepro100_alloc_dma_page();
		if(!eepro100.tcb_phys[i]) {
			return NULL;
		}
	}
	for(i = 0; i < RX_RING_ENTRIES; i++) {
		struct eepro100_rfd *r = (struct eepro100_rfd *)P2V(eepro100.rfd_phys[i]);
		r->status = 0;
		r->command = 0;
		r->link = (i + 1 < RX_RING_ENTRIES) ?
			(unsigned int)eepro100.rfd_phys[i + 1] :
			(unsigned int)eepro100.rfd_phys[0];
		/* QEMU ignores rx_buf_addr and writes the frame at RFD+16 */
		r->rx_buf_addr = (unsigned int)eepro100.rfd_phys[i] + sizeof(struct eepro100_rfd);
		r->count = 0;
		r->size = RX_BUF_SIZE;
	}
	eepro100.rx_cur = 0;
	eepro100.tx_cur = 0;

	{
		static struct interrupt irq_config_eepro100 = { 0, "eepro100", &eepro100_irq_handler, NULL };
		register_irq(eepro100.irq, &irq_config_eepro100);
	}
	/* PIC unmask for the IRQ line (RMW via enable_irq: a raw IMR
	 * write here would mask every other IRQ on that PIC) */
	enable_irq(eepro100.irq);

	eepro100_init_commands();

	/* start the receive unit on the RFD ring */
	eepro100_pointer((unsigned int)eepro100.rfd_phys[0]);
	eepro100_cmd(RU_START);

	eepro100.present = 1;
	memcpy_b(eepro100_ops.mac, eepro100.mac, 6);
	printk("eepro100: NIC %x:%x at 0x%lx, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		EEPRO100_VENDOR, pd->device_id, eepro100.mmio, eepro100.irq,
		eepro100.mac[0], eepro100.mac[1], eepro100.mac[2],
		eepro100.mac[3], eepro100.mac[4], eepro100.mac[5]);
	return &eepro100_ops;
}

static struct ext_net_ops eepro100_ops = {
	eepro100_ext_open,
	eepro100_ext_close,
	eepro100_ext_bind,
	eepro100_ext_listen,
	eepro100_ext_connect,
	eepro100_ext_accept,
	eepro100_ext_ioctl,
	eepro100_ext_sendto,
	eepro100_ext_recvfrom,
	eepro100_ext_read,
	eepro100_ext_write,
	eepro100_ext_poll,
};
