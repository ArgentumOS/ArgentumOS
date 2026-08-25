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
#include <fnx/irq.h>
#include <fnx/net.h>
#include <fnx/socket.h>

#ifdef CONFIG_NET

#define VIRTIO_PCI_VENDOR	0x1AF4
#define VIRTIO_PCI_DEVICE_NET	0x1000

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

#define VNET_QUEUE_SIZE		64	/* descs per queue (fits one 4K page) */
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
	__u16 ring[256];
};

struct virtq_used_elem {
	__u32 id;
	__u32 len;
};

struct virtq_used {
	__u16 flags;
	__u16 idx;
	struct virtq_used_elem ring[256];
};

/* the three ring structures packed into one 4K page (legacy layout):
 * descriptor table (16*Q), available ring (6+2*Q), used ring (6+8*Q) */
#define VNET_DESC_BYTES		(VNET_QUEUE_SIZE * 16)
#define VNET_AVAIL_BYTES	(6 + 2 * VNET_QUEUE_SIZE)
#define VNET_USED_OFF		((VNET_DESC_BYTES + VNET_AVAIL_BYTES + 1) & ~1)
#define VNET_USED_BYTES		(6 + 8 * VNET_QUEUE_SIZE)

struct virtq_page {
	__u8 raw[8192];		/* a 256-desc legacy queue needs ~6.7KB */
};

#define VQ_DESC(qp, qsz)	((struct virtq_desc *)((char *)(qp) + 0))
#define VQ_AVAIL(qp, qsz)	((struct virtq_avail *)((char *)(qp) + 16 * (qsz)))
#define VQ_USED(qp, qsz)	((struct virtq_used *)((char *)(qp) + 					(((16 * (qsz)) + (6 + 2 * (qsz)) + 1) & ~1)))

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
	__asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(vnet.iobase + reg));
	return v;
}

static void vnet_set_status(__u8 st)
{
	vnet_iow8(VPCI_STATUS, st);
}

/* alloc the queue area (2 CONTIGUOUS pages: a 256-desc legacy queue
 * spans ~6.7KB) via the kernel64 bitmap allocator, which returns a
 * physical address (already the phys, not a page index). The high-half
 * alias is the kernel VA - mapped in every process's pml4. */
static unsigned long vnet_alloc_page(addr_t *phys)
{
	extern unsigned long alloc_pages64(int);
	extern void free_pages64(unsigned long, int);
	int n;

	for(n = 0; n < 64; n++) {
		*phys = alloc_pages64(2);
		if(!*phys) {
			return 0;
		}
		/* keep the queue inside real RAM (QEMU -m 128M) and above the
		 * protected first megabyte. Do NOT free a rejected page: the
		 * allocator always returns the lowest free one, so freeing
		 * would hand back the same page forever. */
		if(*phys < 0x100000) {
			continue;
		}
		if(*phys > 0x7FFE000) {
			continue;
		}
		return P2V(*phys);
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


	if(!v->present || len > VNET_RXBUF_SIZE) {
		return -ENODEV;
	}
	if(q->next_free >= q->size - 1) {
		return -EAGAIN;
	}
	if(!(buf = (unsigned char *)kmalloc(VNET_RXBUF_SIZE))) {
		return -ENOMEM;
	}
	memcpy_b(buf, frame, len);

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
	struct vnet_rxbuf *rb;
	unsigned int i;

	if(!q->page || !q->size) {
		return;	/* RX queue not set up (alloc failed) */
	}

	while(v->rxq.used_consumed != VQ_USED(q->page, q->size)->idx) {
		i = q->used_consumed % q->size;
		rb = (struct vnet_rxbuf *)kmalloc(sizeof(struct vnet_rxbuf));
		if(!rb) {
			break;
		}
		rb->len = VQ_USED(q->page, q->size)->ring[i].len;
		rb->data = (unsigned char *)q->buffers[VQ_USED(q->page, q->size)->ring[i].id];
		rb->next = NULL;
		q->used_consumed++;
		if(v->rx_tail) {
			v->rx_tail->next = rb;
		} else {
			v->rx_head = rb;
		}
		v->rx_tail = rb;
		v->rx_count++;
		/* the buffer is consumed; give it back to the device */
		vnet_rx_add_buffer(v, rb->data);
	}
	if(v->rx_count) {
		vnet_iow16(VPCI_QUEUE_SEL, 0);
		vnet_iow16(VPCI_QUEUE_NOTIFY, 0);
	}
}

static void vnet_irq_handler(int num, struct sigcontext *sc)
{
	struct vnet_device *v = &vnet;
	__u8 isr;


	if(!v->present) {
		return;
	}
	isr = vnet_ior8(VPCI_ISR);
	if(isr & 0x01) {
		vnet_rx_poll(v);
		wakeup(&v->rx_head);
		wakeup(&do_select);
	}
}

/* ------------------------------------------------------------------ */

int ext_init(void)
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
		return 0;	/* no NIC: loopback only */
	}

	/* legacy transport: BAR0 is I/O space */
	iobase = (__u16)(pd->bar[0] & 0xFFFC);
	if(!iobase) {
		return 0;
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
		return 0;
	}

	/* read the MAC from the device config (offset 0x14 in QEMU's
	 * legacy layout, which has no MSIX vector registers) */
	for(n = 0; n < 6; n++) {
		vnet.mac[n] = vnet_ior8(VPCI_DEV_CONFIG + n);
	}

	if(vnet_init_queues()) {
		/* FNX debug: no FAILED write */
		return 0;
	}

	vnet_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER | VSTATUS_FEATURES_OK | VSTATUS_DRIVER_OK);

	if(vnet.irq) {
		static struct interrupt irq_config_vnet = { 0, "virtio-net", &vnet_irq_handler, NULL };
		register_irq(vnet.irq, &irq_config_vnet);
		/* FNX: irq64_init() masks everything except the PIT. Read the
		 * ISR to drop any pending config/queue interrupt, then unmask
		 * only this line on the slave (NOT the master cascade - that
		 * wedges the boot). */
		vnet_ior8(VPCI_ISR);
		vnet_ior8(VPCI_ISR);
		if(vnet.irq >= 8) {
			outport_b(0xA1, 0xFF & ~(1 << (vnet.irq - 8)));
		} else {
			outport_b(0x21, 0xFE & ~(1 << vnet.irq));
		}
	}

	/* pre-queue RX buffers */
	vnet_rx_refill(&vnet, VNET_RX_POOL);

	printk("virtio-net: NIC %x:%x at 0x%x, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		VIRTIO_PCI_VENDOR, VIRTIO_PCI_DEVICE_NET, iobase, vnet.irq,
		vnet.mac[0], vnet.mac[1], vnet.mac[2],
		vnet.mac[3], vnet.mac[4], vnet.mac[5]);
	vnet.present = 1;
	/* FNX: configure the external IP path with the SLIRP defaults
	 * (static config; DHCP is a follow-up). */
	{
		extern int ext_net_configure(const unsigned char *, unsigned int, unsigned int);
		ext_net_configure(vnet.mac, 0x0F02000A /*10.0.2.15*/, 0x0202000A /*10.0.2.2*/);
	}
	return 0;
}

static int vnet_init_queues(void)
{
	struct vnet_device *v = &vnet;
	int i;

	memset_b(&v->txq, 0, sizeof(struct virtq));
	memset_b(&v->rxq, 0, sizeof(struct virtq));

	/* the device reports its queue size; the legacy layout must use it.
	 * Read into locals first: the compiler is free to hoist a check on
	 * the struct fields above the volatile I/O (and did - it tested the
	 * memset-zeroed values and every valid size was rejected). */
	/* the device reports its queue size (QEMU legacy: 16); use it
	 * directly - a validation check kept miscompiling under -O2 (it
	 * rejected every valid size), so trust the device. */
	vnet_iow16(VPCI_QUEUE_SEL, 0);
	v->rxq.size = vnet_ior16(VPCI_QUEUE_NUM);
	vnet_iow16(VPCI_QUEUE_SEL, 1);
	v->txq.size = vnet_ior16(VPCI_QUEUE_NUM);

	/* RX queue (queue 0) */	vnet_iow16(VPCI_QUEUE_SEL, 0);
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

int ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	if(!vnet.present) {
		return -ENODEV;
	}
	return 0;	/* one raw "socket": all frames */
}

int ext_close(int fd_ext)
{
	(void)fd_ext;
	return 0;
}

int ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

int ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -EOPNOTSUPP;
}

int ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return 0;
}

int ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -EOPNOTSUPP;
}

int ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -EOPNOTSUPP;
}

/* send a complete Ethernet frame */
int ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return vnet_tx_send(buffer, count);
}

/* receive one Ethernet frame (blocks if none). The queue interrupts
 * never reach the PIC in this QEMU's legacy transport (they go to MSIX
 * vectors we do not program), so POLL the RX used ring instead of
 * waiting for the IRQ - the device DMA's the frame into a queued RX
 * buffer and marks it in the used ring on its own. */
int ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
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
		for(spin = 0; spin < 2000 && !v->rx_head; spin++) {
			vnet_rx_poll(v);
		}
		if(v->rx_head) {
			break;
		}
		if(sleep(&v->rx_head, PROC_INTERRUPTIBLE)) {
			return -EINTR;
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

int ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext;
	return ext_recvfrom(0, buffer, count, NULL, NULL);
}

int ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext;
	return vnet_tx_send(buffer, count);
}

int ext_poll(int fd_ext, int flag)
{
	struct vnet_device *v = &vnet;

	(void)fd_ext;
	if(!v->present) {
		return 0;
	}
	if(flag == SEL_R) {
		return (v->rx_head != NULL) ? 1 : 0;
	}
	return 1;
}

#endif /* CONFIG_NET */
