/*
 * fnx/drivers/net/rtl8139.c
 *
 * RealTek RTL8139 PCI NIC behind the ext_* API. The 8139 is PIO-only
 * (single I/O BAR), has one INTx line and a simple TX/RX ring model:
 * RX is a fixed-size circular DMA buffer (8K here) that the chip fills
 * with a 4-byte status/length header per packet; TX is a ring of 4
 * descriptor slots (TSAD = buffer phys addr, TSD = length|OWN). The
 * MAC is EEPROM-autoloaded into IDR0-5 at reset.
 *
 * IRQ rule (same as virtio-net): the handler only ACKs the ISR and
 * wakes sleepers - the RX path always polls the ring (CAPR vs CBR), so
 * it can never race with the driver.
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

#define RTL8139_VENDOR	0x10EC
#define RTL8139_DEVICE	0x8139

/* registers (I/O space) */
#define R_MAC0		0x00	/* 6 bytes, EEPROM-autoloaded */
#define R_TXSTAT0	0x10	/* 4 x 32-bit TX status descriptors */
#define R_TXADDR0	0x20	/* 4 x 32-bit TX buffer physical addresses */
#define R_RBSTART	0x30	/* 32-bit RX ring physical address */
#define R_CR		0x37	/* command register (8-bit) */
#define R_CAPR		0x38	/* 16-bit current address of packet read */
#define R_CBR		0x3A	/* 16-bit current buffer read (chip) */
#define R_IMR		0x3C	/* 16-bit interrupt mask */
#define R_ISR		0x3E	/* 16-bit interrupt status (w1c) */
#define R_RCR		0x44	/* 32-bit receive configuration */

/* CR bits */
#define CR_RST		0x10	/* software reset */
#define CR_RE		0x08	/* receiver enable */
#define CR_TE		0x04	/* transmitter enable */

/* ISR bits */
#define ISR_ROK		0x0001	/* RX OK */
#define ISR_RER		0x0002	/* RX error */
#define ISR_TOK		0x0004	/* TX OK */
#define ISR_TER		0x0008	/* TX error */
#define ISR_RXOVW	0x0010	/* RX overrun */
#define ISR_RXFOVW	0x0020	/* RX FIFO overrun */
#define ISR_INT_EN	0x003F

/* RCR bits (the real RTL8139 RxConfig layout: the accept bits are the
 * LOW bits - AcceptBroadcast=0x08, AcceptMulticast=0x04, AcceptMyPhys=
 * 0x02, AcceptAllPhys=0x01 - and bit 7 is RxNoWrap, so it must stay
 * clear for the ring to wrap) */
#define RCR_AB		0x0008	/* accept broadcast */
#define RCR_AM		0x0004	/* accept multicast */
#define RCR_APM		0x0002	/* accept physical match */
#define RCR_AA		0x0001	/* accept all (promiscuous) */

/* TSD bit 13: TxHostOwns. 1 = the HOST (driver) owns the descriptor
 * (free / transmit done); the driver CLEARS it to hand the descriptor
 * to the NIC, which transmits and sets it back (with TxStatOK) */
#define TSD_HOST_OWNS	0x2000

#define RX_RING_SIZE	8192	/* 8K RX ring (2 contiguous pages) */
#define TX_NUM		4	/* TX descriptor slots */
#define TX_BUF_SIZE	2048	/* per-TX buffer (full MTU frame) */

struct rtl8139_device {
	int present;
	unsigned short iobase;
	unsigned char irq;
	unsigned char mac[6];
	unsigned char *ring;		/* kernel VA of the RX ring */
	addr_t ring_phys;
	unsigned short capr;		/* our RX ring read pointer */
	int tx_cur;			/* next TX descriptor slot */
	int rx_wait;			/* wait channel for ext_recvfrom sleepers */
};

static struct rtl8139_device r8139;

static void r8139_irq_handler(int num, struct sigcontext *sc)
{
	unsigned short isr;

	(void)num; (void)sc;
	if(!r8139.present) {
		return;
	}
	isr = inport_w(r8139.iobase + R_ISR);
	if(isr) {
		outport_w(r8139.iobase + R_ISR, isr);	/* write-1-to-clear */
		if(isr & (ISR_ROK | ISR_RER | ISR_RXOVW | ISR_RXFOVW)) {
			wakeup(&r8139.rx_wait);
		}
	}
}

/* 2 contiguous DMA pages for the RX ring (same window as the virtio
 * queues: phys in [1MB, 128MB) where the device can bus-master) */
static int r8139_alloc_ring(void)
{
	extern unsigned long alloc_pages64(int);
	extern void free_pages64(unsigned long, int);
	unsigned long a, b;
	int n;

	for(n = 0; n < 32; n++) {
		a = alloc_pages64(1);
		if(!a) {
			return -ENOMEM;
		}
		if(a < 0x100000 || a >= 0x8000000) {
			free_pages64(a, 1);	/* out of range: drop it, try the next */
			continue;
		}
		b = alloc_pages64(1);
		if(!b) {
			free_pages64(a, 1);
			return -ENOMEM;
		}
		if(b < 0x100000 || b >= 0x8000000) {
			free_pages64(b, 1);
			free_pages64(a, 1);
			continue;
		}
		if(b == a + 0x1000) {
			r8139.ring_phys = a;
			r8139.ring = (unsigned char *)P2V(a);
			return 0;
		}
		free_pages64(b, 1);
		free_pages64(a, 1);
	}
	return -ENOMEM;
}

static int r8139_rx_pending(void)
{
	unsigned short cbr;

	if(!r8139.present) {
		return 0;
	}
	/* re-assert CAPR: QEMU queues incoming packets while its flow
	 * control says the ring is full, and only delivers them when the
	 * driver writes CAPR (rtl8139_RxBufPtr_write calls
	 * qemu_flush_queued_packets). Re-writing the same value is a
	 * harmless poke that flushes any queued frame. */
	outport_w(r8139.iobase + R_CAPR, (r8139.capr >= 0x10) ? (r8139.capr - 0x10) : 0);
	cbr = inport_w(r8139.iobase + R_CBR);
	return (cbr != r8139.capr) ? 1 : 0;
}

/* take the next packet out of the RX ring; returns the frame length or
 * -EAGAIN when the ring is empty */
static int r8139_rx_dequeue(void *buffer, __size_t count)
{
	unsigned short cbr, len, off, first;
	unsigned char *p;
	int n;

	cbr = inport_w(r8139.iobase + R_CBR);
	if(cbr == r8139.capr) {
		return -EAGAIN;
	}
	off = r8139.capr;
	/* 32-bit header: low 16 bits = RX status (RxStatusOK = bit 0),
	 * high 16 bits = length (size + 4, i.e. frame + CRC span) */
	len = *(unsigned short *)(r8139.ring + off + 2);
	{
		int k;
			for(k = 0; k < 340; k++) {
			printk(" %02x", r8139.ring[off + 4 + k]);
		}
		printk("\n");
	}
	if(len > RX_RING_SIZE - 4) {
		/* bad status or bogus length: resync to the chip */
		r8139.capr = cbr;
		outport_w(r8139.iobase + R_CAPR, (cbr >= 0x10) ? (cbr - 0x10) : 0);
		return -EAGAIN;
	}
	p = r8139.ring + off + 4;
	first = (off + 4 <= RX_RING_SIZE) ? (RX_RING_SIZE - off - 4) : 0;
	if(len <= first) {
		n = (len < count) ? len : count;
		memcpy_b(buffer, p, n);
	} else {
		/* the packet wraps the ring end: copy both pieces */
		n = (len < count) ? len : count;
		if(first) {
			memcpy_b(buffer, p, (first < n) ? first : n);
		}
		if(n > first) {
			memcpy_b(buffer + first, r8139.ring, n - first);
		}
	}
	/* advance past the packet, 4-byte aligned, wrapping the ring */
	off = off + 4 + len;
	off = (off + 3) & ~3;
	if(off >= RX_RING_SIZE) {
		off -= RX_RING_SIZE;
	}
	r8139.capr = off;
	/* QEMU's CAPR write handler adds 16 bytes of headroom ("this value
	 * is off by 16"), so write next_pos - 16 to keep the chip's view of
	 * the free space correct - writing the raw position made avail=16
	 * and every later packet 'overflow' (dropped) */
	outport_w(r8139.iobase + R_CAPR, (off >= 0x10) ? (off - 0x10) : 0);
	return (len < count) ? len : count;
}

static int r8139_tx_send(const void *frame, unsigned int len)
{
	unsigned char *buf;
	unsigned int slot, spin;

	if(!r8139.present) {
		return -ENODEV;
	}
	if(len > TX_BUF_SIZE) {
		return -EMSGSIZE;
	}
	/* the descriptors are filled in order (0,1,2,3,0,...): QEMU's
	 * transmitter only processes the descriptor at its internal
	 * currTxDesc, so a free-slot scan would silently lose frames */
	slot = r8139.tx_cur % TX_NUM;
	if(!(inport_l(r8139.iobase + R_TXSTAT0 + slot * 4) & TSD_HOST_OWNS)) {
		return -EAGAIN;	/* the slot is still busy: chip never finished */
	}
	if(!(buf = (unsigned char *)kmalloc(TX_BUF_SIZE))) {
		return -ENOMEM;
	}
	memcpy_b(buf, frame, len);
	outport_l(r8139.iobase + R_TXADDR0 + slot * 4, (unsigned int)V2P((addr_t)buf));
	__asm__ __volatile__("" ::: "memory");
	/* submit: write the size WITHOUT TxHostOwns - clearing bit 13 hands
	 * the descriptor to the NIC, which DMA's the frame and sets the bit
	 * back (plus TxStatOK) when done */
	outport_l(r8139.iobase + R_TXSTAT0 + slot * 4, len & 0x1FFF);
	/* wait for the chip to finish (TxHostOwns set again) before freeing;
	 * on timeout the chip may still be DMA'ing, so LEAK the buffer (a
	 * stale free would be a use-after-free) and report the failure */
	for(spin = 0; spin < 100000; spin++) {
		if(inport_l(r8139.iobase + R_TXSTAT0 + slot * 4) & TSD_HOST_OWNS) {
			break;
		}
	}
	if(spin == 100000) {
		return -EAGAIN;	/* buffer leaked on purpose */
	}
	kfree((addr_t)buf);
	r8139.tx_cur++;
	return len;
}

static int r8139_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return r8139.present ? 0 : -ENODEV;
}

static int r8139_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int r8139_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int r8139_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int r8139_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int r8139_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int r8139_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

static int r8139_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return r8139_tx_send(buffer, count);
}

static int r8139_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	unsigned int spin;
	int n;

	(void)fd_ext; (void)addr; (void)addrlen;
	if(!r8139.present) {
		return -ENODEV;
	}
	for(;;) {
		n = r8139_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		/* brief busy-wait before sleeping so a frame arriving right
		 * now is picked up without a wakeup we never get */
		for(spin = 0; spin < 10000; spin++) {
			if(r8139_rx_pending()) {
				break;
			}
		}
		if(r8139_rx_pending()) {
			continue;
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&r8139.rx_wait, PROC_INTERRUPTIBLE);
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

static int r8139_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return r8139_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int r8139_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return r8139_tx_send(buffer, count);
}

static int r8139_ext_poll(int fd_ext, int flag)
{
	(void)fd_ext;
	if(!r8139.present) {
		return 0;
	}
	if(flag == SEL_R) {
		return r8139_rx_pending() ? 1 : 0;
	}
	return 1;
}

struct ext_net_ops rtl8139_ops = {
	r8139_ext_open,
	r8139_ext_close,
	r8139_ext_bind,
	r8139_ext_listen,
	r8139_ext_connect,
	r8139_ext_accept,
	r8139_ext_ioctl,
	r8139_ext_sendto,
	r8139_ext_recvfrom,
	r8139_ext_read,
	r8139_ext_write,
	r8139_ext_poll,
};

struct ext_net_ops *rtl8139_probe(void)
{
	struct pci_device *pd;
	unsigned short iobase;
	int n;

	r8139.present = 0;

	/* find the RealTek 8139 device */
	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == RTL8139_VENDOR && pd->device_id == RTL8139_DEVICE) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return NULL;
	}

	/* PIO-only: BAR0 is I/O space */
	iobase = (unsigned short)(pd->bar[0] & 0xFFFC);
	if(!iobase) {
		return NULL;
	}
	r8139.iobase = iobase;
	r8139.irq = pd->irq;

	/* command: IO | MASTER */
	pci_write_short(pd, 0x04, 0x0005);

	/* software reset, then wait for it to clear */
	outport_b(iobase + R_CR, CR_RST);
	for(n = 0; n < 100000 && (inport_b(iobase + R_CR) & CR_RST); n++);
	if(n == 100000) {
		return NULL;
	}
	/* the TX status descriptors reset to TxHostOwns (0x2000) = 'host
	 * owns, slot free' in QEMU; assert it explicitly so a device that
	 * powers them up as 0 doesn't wedge the first send */
	for(n = 0; n < TX_NUM; n++) {
		outport_l(iobase + R_TXSTAT0 + n * 4, TSD_HOST_OWNS);
	}

	/* the MAC is EEPROM-autoloaded into IDR0-5 */
	for(n = 0; n < 6; n++) {
		r8139.mac[n] = inport_b(iobase + R_MAC0 + n);
	}

	/* RX ring */
	if(r8139_alloc_ring()) {
		return NULL;
	}
	memset_b(r8139.ring, 0, RX_RING_SIZE);
	outport_l(iobase + R_RBSTART, (unsigned int)r8139.ring_phys);
	outport_w(iobase + R_CAPR, 0);
	outport_w(iobase + R_CBR, 0);
	r8139.capr = 0;

	/* receive config: accept broadcast/multicast/physical; bit 7 clear
	 * = wrap enabled; 8K ring (RBLEN bits 11-12 = 00) */
	outport_l(iobase + R_RCR, RCR_AB | RCR_AM | RCR_APM);

	/* make sure the MAC filter matches us */
	for(n = 0; n < 6; n++) {
		outport_b(iobase + R_MAC0 + n, r8139.mac[n]);
	}

	if(r8139.irq) {
		static struct interrupt irq_config_r8139 = { 0, "rtl8139", &r8139_irq_handler, NULL };
		register_irq(r8139.irq, &irq_config_r8139);
		/* drop any pending interrupt, then unmask only this line on the
		 * slave (NOT the master cascade - same as virtio-net) */
		inport_w(iobase + R_ISR);
		outport_w(iobase + R_ISR, 0xFFFF);
		if(r8139.irq >= 8) {
			outport_b(0xA1, 0xFF & ~(1 << (r8139.irq - 8)));
		} else {
			outport_b(0x21, 0xFE & ~(1 << r8139.irq));
		}
	}
	/* interrupt mask: RX/TX events (only if we have an INTx line to
	 * take them on; the RX path polls the ring regardless) */
	if(r8139.irq) {
		outport_w(iobase + R_IMR, ISR_INT_EN);
	}

	/* start the chip: receiver + transmitter */
	outport_b(iobase + R_CR, CR_RE | CR_TE);

	printk("rtl8139: NIC %x:%x at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		RTL8139_VENDOR, RTL8139_DEVICE, iobase, r8139.irq,
		r8139.mac[0], r8139.mac[1], r8139.mac[2],
		r8139.mac[3], r8139.mac[4], r8139.mac[5]);
	r8139.present = 1;
	memcpy_b(rtl8139_ops.mac, r8139.mac, 6);
	return &rtl8139_ops;
}

#endif /* CONFIG_NET */
