/*
 * fnx/drivers/net/virtio_net.c
 *
 * FNX real-NIC support: a legacy (virtio 0.9.5) virtio-net PCI driver
 * implemented over the kernel's ext_* network API. Legacy transport is
 * used (QEMU: -device virtio-net-pci,disable-modern=on) because it has
 * a plain I/O BAR with no capability/MSI-X machinery, and its INTx
 * interrupt reaches the kernel through the 8259 PIC via irq64_handler.
 *
 * The driver owns two split virtqueues (RX/TX). ext_sendto() pushes a
 * complete Ethernet frame into the TX queue and notifies the device;
 * the IRQ handler drains the RX used ring into a small receive queue
 * that ext_recvfrom() consumes. All queue/buffer addresses are physical
 * (the legacy QUEUE_PFN register takes a 4K-aligned physical address).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/asm.h>
#include <fnx/stdio.h>
#include <fnx/mm.h>
#include <fnx/pci.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/net.h>
#include <fnx/net/ext_net.h>

extern struct ext_net_ops virtio_ops;
#include <fnx/socket.h>

#ifdef CONFIG_NET

#define VIRTIO_PCI_VENDOR	0x1AF4
#define VIRTIO_PCI_DEVICE_NET	0x1000

/* the virtio-net device prepends a 10-byte virtio_net_hdr to every
 * packet (all zeros = no offloads); the Ethernet frame goes after it */
#define VNET_HDR_SIZE	10

/* legacy virtio-pci I/O registers (BAR0) */
#define VPCI_HOST_FEATURES	0x00
#define VPCI_GUEST_FEATURES	0x04
#define VPCI_QUEUE_PFN		0x08
#define VPCI_QUEUE_NUM		0x0c
#define VPCI_QUEUE_SEL		0x0e
#define VPCI_QUEUE_NOTIFY	0x10
#define VPCI_STATUS		0x12
#define VPCI_ISR		0x13
#define VPCI_DEV_CONFIG		0x14	/* legacy net: MAC (6) + status (1) */

#define VSTATUS_ACKNOWLEDGE	0x01
#define VSTATUS_DRIVER		0x02
#define VSTATUS_DRIVER_OK	0x04
#define VSTATUS_FEATURES_OK	0x08
#define VSTATUS_FAILED		0x80

#define VIRTIO_NET_F_MAC	5
#define VIRTIO_NET_F_STATUS	16

#define VRING_DESC_F_NEXT	1
#define VRING_DESC_F_WRITE	2

#define VNET_QUEUE_SIZE		256	/* max descs per queue (device-reported) */
#define VNET_RXBUF_SIZE		2048	/* per-frame buffer (MTU friendly) */
#define VNET_RX_POOL		16	/* pre-queued RX buffers */

struct virtq_desc {
	__u64 addr;
	__u32 len;
	__u16 flags;
	__u16 next;
};

struct virtq_avail {
	__u16 flags;
	__u16 idx;
	__u16 ring[VNET_QUEUE_SIZE];
};

struct virtq_used_elem {
	__u32 id;
	__u32 len;
};

struct virtq_used {
	__u16 flags;
	__u16 idx;
	struct virtq_used_elem ring[VNET_QUEUE_SIZE];
};

/* The legacy vring layout ALIGNS each section to 4096: desc@0,
 * avail@16*num, then the used ring at the next 4K boundary. For
 * num=256 the used ring is at 8192 and the whole queue spans
 * 8192 + 6 + 8*256 = 10246 bytes (3 pages). VQ_DESC/VQ_AVAIL/VQ_USED
 * compute the offsets from the device-reported qsz. */

struct virtq_page {
	__u8 raw[12288];	/* 3 pages: desc + avail + aligned used ring */
};

#define VQ_DESC(qp, qsz)	((struct virtq_desc *)((char *)(qp) + 0))
#define VQ_AVAIL(qp, qsz)	((struct virtq_avail *)((char *)(qp) + 16 * (qsz)))
#define VQ_USED(qp, qsz)	((struct virtq_used *)((char *)(qp) + 					(((16 * (qsz)) + (6 + 2 * (qsz)) + 4095) & ~4095)))

struct virtq {
	struct virtq_page *page;	/* kernel VA of the queue page */
	unsigned long page_phys;	/* physical address for QUEUE_PFN */
	unsigned int size;		/* ring size (VNET_QUEUE_SIZE) */
	unsigned int next_free;		/* next free desc index */
	unsigned int avail_idx;		/* next available-ring index to push */
	unsigned int used_consumed;	/* last consumed used index */
	unsigned long buffers[256];	/* per-desc buffer VAs */
};

struct vnet_device {
	unsigned short iobase;		/* BAR0 I/O base */
	unsigned char irq;		/* PCI INTx line */
	unsigned char mac[6];		/* device MAC */
	int present;
	struct virtq txq, rxq;
	/* receive queue filled by the IRQ handler, drained by recvfrom */
	struct vnet_rxbuf {
		struct vnet_rxbuf *next;
		unsigned int len;
		unsigned char *data;	/* kernel VA (also in rxq.buffers) */
	} *rx_head, *rx_tail;
	int rx_count;
	unsigned int rx_pending;	/* frames awaiting refill */
};

static struct vnet_device vnet;

static int vnet_init_queues(void);
static void vnet_irq_handler(int num, struct sigcontext *sc);

/* ------------------------------------------------------------------ */

static __u16 vnet_ior16(unsigned short reg)
{
	return inport_w(vnet.iobase + reg);
}

static __u32 vnet_ior32(unsigned short reg)
{
	return inport_l(vnet.iobase + reg);
}

static void vnet_iow16(unsigned short reg, __u16 val)
{
	outport_w(vnet.iobase + reg, val);
}

static void vnet_iow32(unsigned short reg, __u32 val)
{
	outport_l(vnet.iobase + reg, val);
}

static void vnet_iow8(unsigned short reg, __u8 val)
{
	outport_b(vnet.iobase + reg, val);
}

static __u8 vnet_ior8(unsigned short reg)
{
	__u8 v;

	v = 0;
	__asm__ __volatile__("inb %1, %0" : "=a"(v) : "d"((unsigned short)(vnet.iobase + reg)));
	return v;
}

static void vnet_set_status(__u8 st)
{
	vnet_iow8(VPCI_STATUS, st);
}

/* Allocate the queue's 3 CONTIGUOUS pages (a 256-desc legacy vring
 * spans 10246 bytes; the used ring is 4K-aligned at 8192) via the
 * kernel64 bitmap allocator, which returns a physical address. The
 * high-half alias is the kernel VA - mapped in every process's pml4.
 * The bitmap hands out the lowest free page each call, so consecutive
 * calls return adjacent pages when the low region is dense; take the
 * trio only when contiguous. Keep the pages in real RAM (QEMU -m 128M)
 * and above the DMA-protected first megabyte. */
static unsigned long vnet_alloc_page(addr_t *phys)
{
	extern unsigned long alloc_pages64(int);
	extern void free_pages64(unsigned long, int);
	unsigned long a, b, c;
	int n;

	/* the legacy vring spans 3 pages (used ring is 4K-aligned at 8192);
	 * allocate single pages and take them when they are contiguous */
	for(n = 0; n < 32; n++) {
		a = alloc_pages64(1);
		if(!a) {
			return 0;
		}
		if(a < 0x100000 || a >= 0x8000000) {
			continue;	/* out of range: leak it, try the next */
		}
		b = alloc_pages64(1);
		if(!b) {
			return 0;
		}
		if(b < 0x100000 || b >= 0x8000000) {
			free_pages64(b, 1);
			continue;
		}
		c = alloc_pages64(1);
		if(!c) {
			return 0;
		}
		if(c < 0x100000 || c >= 0x8000000) {
			free_pages64(c, 1);
			continue;
		}
		if(b == a + 0x1000 && c == a + 0x2000) {
			*phys = a;
			return P2V(a);	/* 3 contiguous pages: the queue lives here */
		}
		free_pages64(b, 1);
		free_pages64(c, 1);
	}
	return 0;
}

/* give one empty buffer to the RX virtqueue */
static int vnet_rx_add_buffer(struct vnet_device *v, unsigned char *buf)
{
	struct virtq *q = &v->rxq;
	unsigned int d, a;

	if(q->next_free >= q->size - 1) {
		return -ENOMEM;
	}
	if(!V2P((addr_t)buf)) {
		return -ENOMEM;	/* phys 0: the device can't use it */
	}
	d = q->next_free++;
	a = q->avail_idx++ % q->size;

	VQ_DESC(q->page, q->size)[d].addr = V2P((addr_t)buf);
	VQ_DESC(q->page, q->size)[d].len = VNET_RXBUF_SIZE;
	VQ_DESC(q->page, q->size)[d].flags = VRING_DESC_F_WRITE;
	VQ_DESC(q->page, q->size)[d].next = 0;
	VQ_AVAIL(q->page, q->size)->ring[a] = d;
	q->buffers[d] = (unsigned long)buf;
	/* memory barrier then update avail idx */
	__asm__ __volatile__("" ::: "memory");
	VQ_AVAIL(q->page, q->size)->idx = q->avail_idx;
	return 0;
}

/* re-arm a consumed RX buffer under its ORIGINAL desc id (the used ring
 * returns it); unlike vnet_rx_add_buffer this does not consume a fresh
 * desc slot, so a long-running stream cannot exhaust the descriptor
 * table and silently stop receiving */
static void vnet_rx_readd_buffer(struct vnet_device *v, unsigned int id,
				 unsigned char *buf)
{
	struct virtq *q = &v->rxq;
	unsigned int a;

	a = q->avail_idx++ % q->size;
	VQ_DESC(q->page, q->size)[id].addr = V2P((addr_t)buf);
	VQ_DESC(q->page, q->size)[id].len = VNET_RXBUF_SIZE;
	VQ_DESC(q->page, q->size)[id].flags = VRING_DESC_F_WRITE;
	VQ_DESC(q->page, q->size)[id].next = 0;
	VQ_AVAIL(q->page, q->size)->ring[a] = id;
	q->buffers[id] = (unsigned long)buf;
	__asm__ __volatile__("" ::: "memory");
	VQ_AVAIL(q->page, q->size)->idx = q->avail_idx;
}

static int vnet_rx_refill(struct vnet_device *v, int want)
{
	int n = 0;
	unsigned char *buf;

	while(n < want && v->rxq.next_free < v->rxq.size - 1) {
		extern unsigned long alloc_pages64(int);
		addr_t p;

		/* RX buffers must live at a phys the device will DMA to: the
		 * real kernel's buddy hands out the very first pages (phys
		 * ~0x1000), which QEMU protects from virtio DMA. The kernel64
		 * bitmap allocator gives higher phys (like the queue pages). */
		p = alloc_pages64(1);
		if(!p) {
			break;
		}
		if(p < 0x100000) {
			extern void free_pages64(unsigned long, int);
			free_pages64(p, 1);
			continue;	/* low first page: not DMA-able */
		}
		buf = (unsigned char *)P2V(p);
		if(vnet_rx_add_buffer(v, buf) < 0) {
			extern void free_pages64(unsigned long, int);
			free_pages64(p, 1);
			break;
		}
		n++;
	}
	if(n) {
		vnet_iow16(VPCI_QUEUE_SEL, 0);	/* RX queue */
		vnet_iow16(VPCI_QUEUE_NOTIFY, 0);	/* notify RX queue */
	}
	return n;
}

static int vnet_tx_send(const void *frame, unsigned int len)
{
	struct vnet_device *v = &vnet;
	struct virtq *q = &v->txq;
	unsigned int d, a;
	unsigned char *buf;


	if(!v->present || len + VNET_HDR_SIZE > VNET_RXBUF_SIZE) {
		return -ENODEV;
	}
	if(q->next_free >= q->size - 1) {
		return -EAGAIN;
	}
	if(!(buf = (unsigned char *)kmalloc(VNET_RXBUF_SIZE))) {
		return -ENOMEM;
	}
	/* the device expects a 10-byte virtio_net_hdr (all zeros = no
	 * offloads) before the Ethernet frame */
	memset_b(buf, 0, VNET_HDR_SIZE);
	memcpy_b(buf + VNET_HDR_SIZE, frame, len);
	len += VNET_HDR_SIZE;

	d = q->next_free++;
	a = q->avail_idx++ % q->size;
	VQ_DESC(q->page, q->size)[d].addr = V2P((addr_t)buf);
	VQ_DESC(q->page, q->size)[d].len = len;
	VQ_DESC(q->page, q->size)[d].flags = VRING_DESC_F_NEXT;	/* needs next (no chaining) */
	VQ_DESC(q->page, q->size)[d].flags = 0;
	VQ_DESC(q->page, q->size)[d].next = 0;
	VQ_AVAIL(q->page, q->size)->ring[a] = d;
	q->buffers[d] = (unsigned long)buf;
	__asm__ __volatile__("" ::: "memory");
	VQ_AVAIL(q->page, q->size)->idx = q->avail_idx;

	vnet_iow16(VPCI_QUEUE_SEL, 1);	/* TX queue */
	vnet_iow16(VPCI_QUEUE_NOTIFY, 1);	/* notify TX queue */
	return len;
}

/* process completed RX buffers from the used ring */
static void vnet_rx_poll(struct vnet_device *v)
{
	struct virtq *q = &v->rxq;
	struct virtq_used *u;
	struct vnet_rxbuf *rb;
	unsigned int i;

	if(!q->page || !q->size) {
		return;	/* RX queue not set up (alloc failed) */
	}

	/* The device advances the used ring asynchronously (DMA), so its
	 * idx must be re-read through a volatile access on every iteration.
	 * The value must also be CONSUMED immediately (in the exit test)
	 * before any body code runs: -O2 kept the read value in a register
	 * across the loop and the inlined re-arm clobbered it, so the
	 * back-edge compared garbage and the poll never terminated. */
	u = VQ_USED(q->page, q->size);
	for(;;) {
		unsigned int cur_idx = *(volatile __u16 *)&u->idx;

		/* the device's idx is 16-bit and wraps at 65536, while
		 * used_consumed is a monotonic u32: compare the low 16 bits */
		if((v->rxq.used_consumed & 0xFFFF) == cur_idx) {
			break;
		}
		i = q->used_consumed % q->size;
		/* sanity: a used entry must reference a descriptor we own */
		if(u->ring[i].id >= q->size) {
			break;	/* corrupt/raced entry: stop consuming */
		}
		rb = (struct vnet_rxbuf *)kmalloc(sizeof(struct vnet_rxbuf));
		if(!rb) {
			break;
		}
		rb->len = u->ring[i].len;
		rb->data = (unsigned char *)q->buffers[u->ring[i].id];
		/* the device wrote the 10-byte virtio_net_hdr first; the
		 * Ethernet frame starts after it */
		if(rb->len > VNET_HDR_SIZE) {
			rb->data += VNET_HDR_SIZE;
			rb->len -= VNET_HDR_SIZE;
		}
		rb->next = NULL;
		q->used_consumed++;
		if(v->rx_tail) {
			v->rx_tail->next = rb;
		} else {
			v->rx_head = rb;
		}
		v->rx_tail = rb;
		v->rx_count++;
		/* the buffer is consumed; give it back to the device under
		 * its original desc id (the raw buffer start: rb->data may
		 * have been advanced past the virtio_net_hdr) */
		vnet_rx_readd_buffer(v, u->ring[i].id,
				     (unsigned char *)q->buffers[u->ring[i].id]);
	}
	if(v->rx_count) {
		vnet_iow16(VPCI_QUEUE_SEL, 0);
		vnet_iow16(VPCI_QUEUE_NOTIFY, 0);
	}
}

static void vnet_irq_handler(int num, struct sigcontext *sc)
{
	struct vnet_device *v = &vnet;

	if(!v->present) {
		return;
	}
	/* The receive path polls the used ring itself, so from IRQ context
	 * we only ACK the interrupt (reading the ISR deasserts INTx) and
	 * wake sleepers. Polling here would race with ext_recvfrom()'s poll
	 * on the shared used_consumed/avail_idx/queue state: both would
	 * consume the same used-ring entries, used_consumed would run ahead
	 * of the device and the poll would never terminate. */
	(void)vnet_ior8(VPCI_ISR);
	wakeup(&v->rx_head);
	wakeup(&do_select);
}

struct ext_net_ops *virtio_net_probe(void)
{
	struct pci_device *pd;
	__u32 features;
	__u16 iobase;
	int n;

	vnet.present = 0;

	/* find the legacy virtio-net device */
	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == VIRTIO_PCI_VENDOR && pd->device_id == VIRTIO_PCI_DEVICE_NET) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return NULL;	/* no virtio-net NIC */
	}

	/* legacy transport: BAR0 is I/O space */
	iobase = (__u16)(pd->bar[0] & 0xFFFC);
	if(!iobase) {
		return NULL;
	}
	vnet.iobase = iobase;
	vnet.irq = pd->irq;

	/* enable I/O + bus mastering */
	pci_write_short(pd, 0x04, 0x0007);	/* command: IO | MEM | MASTER */

	/* reset, then acknowledge and enter DRIVER state one step at a
	 * time (the legacy device tracks the transitions) */
	vnet_set_status(0);
	__asm__ __volatile__("" ::: "memory");
	vnet_set_status(VSTATUS_ACKNOWLEDGE);
	vnet_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER);

	/* negotiate features (MAC + link status) */
	features = vnet_ior32(VPCI_HOST_FEATURES);
	features &= (1 << VIRTIO_NET_F_MAC) | (1 << VIRTIO_NET_F_STATUS);
	vnet_iow32(VPCI_GUEST_FEATURES, features);
	vnet_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER | VSTATUS_FEATURES_OK);

	if(!(vnet_ior8(VPCI_STATUS) & VSTATUS_FEATURES_OK)) {
		return NULL;
	}

	/* read the MAC from the device config (offset 0x14 in QEMU's
	 * legacy layout, which has no MSIX vector registers) */
	for(n = 0; n < 6; n++) {
		vnet.mac[n] = vnet_ior8(VPCI_DEV_CONFIG + n);
	}

	if(vnet_init_queues()) {
		return NULL;
	}

	vnet_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER | VSTATUS_FEATURES_OK | VSTATUS_DRIVER_OK);

	if(vnet.irq) {
		static struct interrupt irq_config_vnet = { 0, "virtio-net", &vnet_irq_handler, NULL };
		register_irq(vnet.irq, &irq_config_vnet);
		/* irq64_init() masks everything except the PIT. Read the ISR
		 * to drop any pending config/queue interrupt, then unmask
		 * only this line on the slave (NOT the master cascade - that
		 * wedges the boot). The handler ACKs the ISR and wakes sleepers;
		 * it must NOT poll the rings (races with the recv path). */
		vnet_ior8(VPCI_ISR);
		vnet_ior8(VPCI_ISR);
		enable_irq(vnet.irq);
	}

	/* pre-queue RX buffers */
	vnet_rx_refill(&vnet, VNET_RX_POOL);

	printk("virtio-net: NIC %x:%x at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		VIRTIO_PCI_VENDOR, VIRTIO_PCI_DEVICE_NET, iobase, vnet.irq,
		vnet.mac[0], vnet.mac[1], vnet.mac[2],
		vnet.mac[3], vnet.mac[4], vnet.mac[5]);
	vnet.present = 1;
	memcpy_b(virtio_ops.mac, vnet.mac, 6);
	return &virtio_ops;
}

static int vnet_init_queues(void)
{
	struct vnet_device *v = &vnet;
	int i;

	memset_b(&v->txq, 0, sizeof(struct virtq));
	memset_b(&v->rxq, 0, sizeof(struct virtq));

	/* the device reports its queue size; the legacy layout uses it
	 * for the ring offsets */
	vnet_iow16(VPCI_QUEUE_SEL, 0);
	v->rxq.size = vnet_ior16(VPCI_QUEUE_NUM);
	vnet_iow16(VPCI_QUEUE_SEL, 1);
	v->txq.size = vnet_ior16(VPCI_QUEUE_NUM);

	/* RX queue (queue 0) */
	if(!(v->rxq.page = (struct virtq_page *)vnet_alloc_page(&v->rxq.page_phys))) {
		return -ENOMEM;
	}
	vnet_iow16(VPCI_QUEUE_SEL, 0);
	vnet_iow32(VPCI_QUEUE_PFN, (__u32)(v->rxq.page_phys >> 12));

	/* TX queue (queue 1) */
	if(!(v->txq.page = (struct virtq_page *)vnet_alloc_page(&v->txq.page_phys))) {
		return -ENOMEM;
	}
	vnet_iow16(VPCI_QUEUE_SEL, 1);
	vnet_iow32(VPCI_QUEUE_PFN, (__u32)(v->txq.page_phys >> 12));

	/* pre-fill the TX buffers with empty pages (freed on send) */
	for(i = 0; i < VNET_QUEUE_SIZE; i++) {
		v->txq.buffers[i] = 0;
	}
	return 0;
}

/* ---- ext_* API: raw Ethernet frame access over the NIC ---- */

static int virtio_ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	if(!vnet.present) {
		return -ENODEV;
	}
	return 0;	/* one raw "socket": all frames */
}

static int virtio_ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

static int virtio_ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int virtio_ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

static int virtio_ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

static int virtio_ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

static int virtio_ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

/* send a complete Ethernet frame */
static int virtio_ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return vnet_tx_send(buffer, count);
}

/* receive one Ethernet frame (blocks if none). We POLL the RX used
 * ring instead of relying on the IRQ: the INTx line does fire (the IRQ
 * handler ACKs it and wakes sleepers, but never touches the ring, so it
 * cannot race with the poll). The device DMA's the frame into a queued
 * RX buffer and marks it in the used ring on its own. */
static int virtio_ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	struct vnet_device *v = &vnet;
	struct vnet_rxbuf *rb;
	unsigned int spin;
	int n;

	(void)fd_ext;
	if(!v->present) {
		return -ENODEV;
	}
	for(;;) {
		vnet_rx_poll(v);
		if(v->rx_head) {
			break;
		}
		/* brief busy-wait before sleeping so a frame arriving right
		 * now is picked up without a wakeup we never get */
		for(spin = 0; spin < 10000 && !v->rx_head; spin++) {
			vnet_rx_poll(v);
		}
		if(v->rx_head) {
			break;
		}
		/* the IRQ handler only wakes sleepers (it must not poll), so a
		 * sleep here is woken either by the IRQ or by this short timeout -
		 * give up if it expires with no frame (the caller retries) */
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&v->rx_head, PROC_INTERRUPTIBLE);
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
	rb = v->rx_head;
	v->rx_head = rb->next;
	if(!v->rx_head) {
		v->rx_tail = NULL;
	}
	v->rx_count--;
	n = (count < rb->len) ? count : rb->len;
	memcpy_b(buffer, rb->data, n);
	kfree((addr_t)rb);
	return n;
}

static int virtio_ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return virtio_ext_recvfrom(0, buffer, count, NULL, NULL);
}

static int virtio_ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return vnet_tx_send(buffer, count);
}

static int virtio_ext_poll(int fd_ext, int flag)
{
	struct vnet_device *v = &vnet;

	(void)fd_ext;
	if(!v->present) {
		return 0;
	}
	if(flag == SEL_R) {
		/* the queue interrupts never reach the PIC in this QEMU's
		 * legacy transport, so pull completed frames out of the used
		 * ring before reporting readability - otherwise poll() on an
		 * external socket never sees an arriving frame */
		vnet_rx_poll(v);
		return (v->rx_head != NULL) ? 1 : 0;
	}
	return 1;
}

/* the virtio-net ops table (the active NIC dispatcher uses this when
 * the virtio device is present) */
struct ext_net_ops virtio_ops = {
	virtio_ext_open,
	virtio_ext_close,
	virtio_ext_bind,
	virtio_ext_listen,
	virtio_ext_connect,
	virtio_ext_accept,
	virtio_ext_ioctl,
	virtio_ext_sendto,
	virtio_ext_recvfrom,
	virtio_ext_read,
	virtio_ext_write,
	virtio_ext_poll,
};

#endif /* CONFIG_NET */
