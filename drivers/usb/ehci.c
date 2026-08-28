/*
 * fnx/drivers/usb/ehci.c
 *
 * EHCI (USB 2.0, 480Mbps) host controller driver.
 *
 * QEMU: -device usb-ehci (Intel 82801D / ICH4, 8086:24CD, class 0x0C03
 * prog-if 0x20, MMIO BAR0, INTx). QEMU's plain usb-ehci accepts
 * full-speed and low-speed devices directly (no companion routing);
 * the ich9-usb-ehci* controllers are companion-mode and deferred.
 *
 * Schedule model: a 1024-entry periodic frame list (PERIODICLISTBASE,
 * entries are 32-byte-aligned pointers with bit0=T, bits2-1=type where
 * 1=QH) for interrupt endpoints; a single async QH (ASYNCLISTADDR,
 * ASE) for control/bulk. qTDs hang off the QH's next_qtd; the HC
 * clears the qTD ACTIVE bit on completion (the driver polls, reading
 * USBSTS to yield TCG so QEMU's 1ms frame timer runs).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/usb.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/asm.h>

#define EHCI_MMIO_VA		0xFFFFB80000000000UL	/* pml4[501] */
#define EHCI_MMIO_SIZE		0x2000

/* capability registers */
#define EHCI_CAPLENGTH		0x00
#define EHCI_HCSPARAMS		0x04
#define EHCI_HCSPARAMS_NPORTS	0x0000000f

/* operational registers (base = CAPLENGTH) */
#define EHCI_USBCMD		0x00
#define EHCI_USBCMD_RUNSTOP	(1 << 0)
#define EHCI_USBCMD_HCRESET	(1 << 1)
#define EHCI_USBCMD_ASE		(1 << 5)
#define EHCI_USBCMD_PSE		(1 << 4)
#define EHCI_USBSTS		0x04
#define EHCI_USBSTS_HCHALTED	(1 << 0)
#define EHCI_USBINTR		0x08
#define EHCI_FRINDEX		0x0C
#define EHCI_CTRLDSSEGMENT	0x10
#define EHCI_PERIODICLISTBASE	0x14
#define EHCI_ASYNCLISTADDR	0x18
#define EHCI_CONFIGFLAG		0x40
#define EHCI_PORTSC		0x44

#define EHCI_PORTSC_CCS		(1 << 0)
#define EHCI_PORTSC_PED		(1 << 1)
#define EHCI_PORTSC_PEDC	(1 << 2)
#define EHCI_PORTSC_RESET	(1 << 8)
#define EHCI_PORTSC_PPOWER	(1 << 12)
#define EHCI_PORTSC_OWNER	(1 << 13)
#define EHCI_PORTSC_LS_SH	14

/* link encodings: bit0 = T (1 = terminated), bits2-1 = type (1 = QH) */
#define EHCI_LINK_T		0x00000001
#define EHCI_LINK_QH		0x00000002

#define EHCI_FRAMES		1024
#define EHCI_MAX_PKT		16384	/* 4 pages x 4KB */

/* qTD token */
#define QTD_ACTIVE		(1 << 7)
#define QTD_IOC			(1 << 15)
#define QTD_CERR		(3 << 10)
#define QTD_PID_SH		8
#define QTD_PID_OUT		0
#define QTD_PID_IN		1
#define QTD_PID_SETUP		2
#define QTD_TBYTES_SH		16
#define QTD_HALT		(1 << 6)

#define EHCI_MAX_EPS	8
#define EHCI_MAX_CB	8
#define EHCI_MAX_DEVS	16

struct ehci_qh {
	unsigned int next;	/* async: next QH phys | T */
	unsigned int epchar;
	unsigned int epcap;
	unsigned int cur_qtd;
	unsigned int next_qtd;
	unsigned int altnext;
	unsigned int token;	/* overlay */
	unsigned int bufptr[5];
};

struct ehci_qtd {
	unsigned int next;
	unsigned int altnext;
	unsigned int token;
	unsigned int bufptr[5];
};

/* one configured non-EP0 endpoint (interrupt) */
struct ehci_ep {
	int used;
	int dev;
	int epid;
	int type;
	int mps;
	unsigned long qh_phys;	/* periodic QH in the frame list */
	int stride;		/* frame stride */
	int start_frame;
	unsigned long active_qtd;	/* phys of the outstanding qTD */
};

struct ehci_cb {
	int used;
	int dev;
	int epid;
	void (*fn)(int, int, int, int, void *);
	void *data;
};

/* state must live on the HEAP (same rule as xhci/uhci) */
static struct ehci_state {
	unsigned char *mmio;
	unsigned char irq;
	int present;
	int nports;
	/* periodic frame list (1024 x 4B, DMA-visible) */
	unsigned int *framelist;
	unsigned long framelist_phys;
	/* the async QH (control/bulk) */
	struct ehci_qh *async_qh;
	unsigned long async_qh_phys;
	/* aligned pool for QHs/qTDs (32-byte slots) */
	unsigned char *pool;
	unsigned long pool_phys;
	int pool_next;
	/* scratch (DMA-visible) */
	unsigned char setup[8];
	unsigned char status_in[64];
	unsigned char desc18[18];
	unsigned char desc64[64];
	struct ehci_ep eps[EHCI_MAX_EPS];
	struct ehci_cb cbs[EHCI_MAX_CB];
	struct {
		int port;
		int speed;	/* 0=low 1=full 2=high */
	} devs[EHCI_MAX_DEVS];
} *e;

/* ---------------- MMIO + pool ---------------- */

static unsigned int ehci_reg_r(unsigned long off)
{
	return *(volatile unsigned int *)(e->mmio + off);
}

static void ehci_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(e->mmio + off) = val;
}

static void *ehci_alloc_slot(void)
{
	void *p;

	if(e->pool_next + 32 > 4096) {
		return NULL;
	}
	p = e->pool + e->pool_next;
	e->pool_next += 32;
	memset_b(p, 0, 32);
	return p;
}

static unsigned long ehci_slot_phys(void *p)
{
	return e->pool_phys + ((unsigned long)p - (unsigned long)e->pool);
}

/* ---------------- qTD/QH helpers ---------------- */

/* build one qTD; returns its phys (0 = no pool) */
static unsigned long ehci_build_qtd(unsigned int pid, int tbytes, void *buf,
				    unsigned long next_link)
{
	struct ehci_qtd *qtd = ehci_alloc_slot();
	unsigned long qtd_phys;

	if(!qtd) {
		return 0;
	}
	qtd_phys = ehci_slot_phys(qtd);
	qtd->next = (unsigned int)next_link;
	qtd->token = QTD_ACTIVE | QTD_CERR |
		     (pid << QTD_PID_SH) |
		     ((unsigned int)tbytes << QTD_TBYTES_SH);
	if(buf) {
		qtd->bufptr[0] = (unsigned int)V2P((addr_t)buf);
	}
	return qtd_phys;
}

/* wait for a qTD (by phys) to leave ACTIVE; polls USBSTS so TCG exits
 * and QEMU's 1ms work timer runs. Returns 0 on completion, -EIO timeout. */
static int ehci_wait_qtd(unsigned long qtd_phys, int timeout)
{
	volatile struct ehci_qtd *qtd =
		(volatile struct ehci_qtd *)P2V(qtd_phys);
	int i;

	for(i = 0; i < timeout; i++) {
		if(!(qtd->token & QTD_ACTIVE)) {
			return 0;
		}
		if(i & 0xFF) {
			ehci_reg_r(EHCI_USBSTS);
		}
	}
	return -EIO;
}

/* ---------------- control transfers ---------------- */

static int ehci_control(int dev, unsigned char bmRequestType,
			unsigned char bRequest, unsigned short wValue,
			unsigned short wIndex, unsigned short wLength,
			void *data)
{
	unsigned long t_setup, t_data = 0, t_status;
	int dir_in = (bmRequestType & 0x80) != 0;
	volatile struct ehci_qtd *td;
	int ret;

	e->setup[0] = bmRequestType;
	e->setup[1] = bRequest;
	e->setup[2] = wValue & 0xFF;
	e->setup[3] = wValue >> 8;
	e->setup[4] = wIndex & 0xFF;
	e->setup[5] = wIndex >> 8;
	e->setup[6] = wLength & 0xFF;
	e->setup[7] = wLength >> 8;

	/* SETUP -> [DATA] -> STATUS -> T */
	if(dir_in) {
		/* control READ: data IN, status OUT */
		t_status = ehci_build_qtd(QTD_PID_OUT, 0, NULL, EHCI_LINK_T);
	} else {
		/* control WRITE: data OUT, status IN */
		t_status = ehci_build_qtd(QTD_PID_IN, 0, NULL, EHCI_LINK_T);
	}
	if(!t_status) {
		return -ENOMEM;
	}
	if(wLength) {
		t_data = ehci_build_qtd(dir_in ? QTD_PID_IN : QTD_PID_OUT,
					wLength, data, t_status);
		if(!t_data) {
			return -ENOMEM;
		}
		t_setup = ehci_build_qtd(QTD_PID_SETUP, 8, e->setup, t_data);
	} else {
		t_setup = ehci_build_qtd(QTD_PID_SETUP, 8, e->setup, t_status);
	}
	if(!t_setup) {
		return -ENOMEM;
	}

	/* IOC on the status qTD */
	((struct ehci_qtd *)P2V(t_status))->token |= QTD_IOC;

	/* each control transfer gets a FRESH QH (the EHCI per-URB model:
	 * QEMU's queue is keyed by the QH address and caches the packet
	 * state, so reusing one QH leaves a stale packet). The transfer QH
	 * is CHAINED off the permanent head QH's next field (ASYNCLISTADDR
	 * cannot be changed while the async schedule is enabled). */
	{
		struct ehci_qh *qh = ehci_alloc_slot();
		if(!qh) {
			return -ENOMEM;
		}
		qh->next = EHCI_LINK_T;
		qh->epchar = (8 << 16) |	/* MPLEN */
			     (1 << 27) |	/* C */
			     (1 << 14) |	/* DTC */
			     (2 << 12) |	/* EPS: high-speed */
			     ((unsigned int)dev & 0x7f);
		qh->cur_qtd = 0;
		qh->next_qtd = (unsigned int)t_setup;
		qh->token = 0;
		/* the async QH links carry the QH type in bits 2-1 */
		e->async_qh->next =
			(unsigned int)ehci_slot_phys(qh) | EHCI_LINK_QH;
		/* reset the head's overlay: QEMU's writeback leaves a stale
		 * next_qtd in the head after each transfer, and a non-T overlay
		 * qTD takes priority over the head's next chain */
		e->async_qh->cur_qtd = 0;
		e->async_qh->next_qtd = EHCI_LINK_T;
		e->async_qh->token = 0;
	}

	ret = ehci_wait_qtd(t_status, 20000000);
	if(ret) {
		printk("ehci: control to dev %d timed out\n", dev);
		return -EIO;
	}
	/* an error clears ACTIVE and sets HALT/XACTERR */
	td = (volatile struct ehci_qtd *)P2V(t_status);
	if(td->token & (QTD_HALT | (1 << 3) | (1 << 4))) {
		return -EIO;
	}
	return 0;
}

/* synchronous bulk transfer (usb-storage) */
static int ehci_transfer(int dev, int epid, int dir_in, void *buf, int len,
			 struct usb_ring *ring)
{
	unsigned long first = 0, prev = 0, qtd_phys;
	int epnum = epid >> 1;
	int remaining = len, off = 0, chunk;

	while(remaining > 0) {
		chunk = remaining > EHCI_MAX_PKT ? EHCI_MAX_PKT : remaining;
		qtd_phys = ehci_build_qtd(dir_in ? QTD_PID_IN : QTD_PID_OUT,
					 chunk, (unsigned char *)buf + off,
					 EHCI_LINK_T);
		if(!qtd_phys) {
			return -ENOMEM;
		}
		if(prev) {
			((struct ehci_qtd *)P2V(prev))->next =
				(unsigned int)qtd_phys;
		} else {
			first = qtd_phys;
		}
		prev = qtd_phys;
		off += chunk;
		remaining -= chunk;
	}
	if(!first) {
		return -EINVAL;
	}
	/* a bulk endpoint needs its own QH; use the async QH with the
	 * endpoint encoded in the qTD? no - the QH carries the endpoint.
	 * For now route bulk through the async QH as EP0-style (storage
	 * uses control-sized transfers). */
	e->async_qh->next_qtd = (unsigned int)first;
	return ehci_wait_qtd(prev, 20000000);
}

/* ---------------- interrupt endpoints ---------------- */

static int ehci_configure_ep(int dev, int epid, int type, int mps,
			     int interval, unsigned long ring_phys)
{
	struct ehci_ep *ep = NULL;
	struct ehci_qh *qh;
	unsigned long qh_phys;
	int i, stride, f;
	unsigned int epchar;

	for(i = 0; i < EHCI_MAX_EPS; i++) {
		if(!e->eps[i].used) {
			ep = &e->eps[i];
			break;
		}
	}
	if(!ep) {
		return -ENOMEM;
	}

	qh = ehci_alloc_slot();
	if(!qh) {
		return -ENOMEM;
	}
	qh_phys = ehci_slot_phys(qh);

	epchar = ((unsigned int)mps << 16) |
		 (2 << 12) |		/* EPS = high-speed */
		 (((unsigned int)(epid >> 1) & 0xf) << 8) |
		 ((unsigned int)dev & 0x7f);
	qh->epchar = epchar;
	qh->cur_qtd = 0;
	qh->next_qtd = EHCI_LINK_T;
	qh->token = 0;

	ep->used = 1;
	ep->dev = dev;
	ep->epid = epid;
	ep->type = type;
	ep->mps = mps;
	ep->qh_phys = qh_phys;
	ep->active_qtd = 0;

	if(type == USB_EP_INTR_IN) {
		stride = 1 << (interval > 3 ? interval - 3 : 0);
		ep->stride = stride;
		/* link the QH into the periodic list at every stride-th
		 * frame (entry type = QH) */
		for(f = 0; f < EHCI_FRAMES; f++) {
			if((f & (stride - 1)) == 0) {
				e->framelist[f] =
					(unsigned int)qh_phys | EHCI_LINK_QH;
			}
		}
		ep->start_frame = 0;
	}

	printk("ehci: dev %d ep %d (%s, mps %d%s) configured\n",
		dev, epid, type == USB_EP_INTR_IN ? "intr-in" : "bulk",
		mps, type == USB_EP_INTR_IN ? "" : ", async");
	return 0;
}

static int ehci_submit(int dev, int epid, int dir_in, void *buf, int len,
		       struct usb_ring *ring)
{
	struct ehci_ep *ep = NULL;
	struct ehci_qh *qh;
	unsigned long qtd_phys;
	int i;

	for(i = 0; i < EHCI_MAX_EPS; i++) {
		if(e->eps[i].used && e->eps[i].dev == dev &&
		   e->eps[i].epid == epid) {
			ep = &e->eps[i];
			break;
		}
	}
	if(!ep) {
		return -EINVAL;
	}

	qtd_phys = ehci_build_qtd(dir_in ? QTD_PID_IN : QTD_PID_OUT,
				 len, buf, EHCI_LINK_T);
	if(!qtd_phys) {
		return -ENOMEM;
	}
	((struct ehci_qtd *)P2V(qtd_phys))->token |= QTD_IOC;
	ep->active_qtd = qtd_phys;

	if(ep->type == USB_EP_INTR_IN) {
		qh = (struct ehci_qh *)P2V(ep->qh_phys);
		qh->cur_qtd = 0;
		qh->next_qtd = (unsigned int)qtd_phys;
		qh->token = 0;
	} else {
		/* bulk without a persistent QH: run through the async QH */
		e->async_qh->next_qtd = (unsigned int)qtd_phys;
		ehci_wait_qtd(qtd_phys, 20000000);
		ep->active_qtd = 0;
	}
	return 0;
}

static void ehci_set_transfer_cb(int dev, int epid,
				 void (*fn)(int, int, int, int, void *),
				 void *data)
{
	int i;

	for(i = 0; i < EHCI_MAX_CB; i++) {
		if(!e->cbs[i].used) {
			e->cbs[i].used = 1;
			e->cbs[i].dev = dev;
			e->cbs[i].epid = epid;
			e->cbs[i].fn = fn;
			e->cbs[i].data = data;
			return;
		}
	}
}

static void ehci_poll(void)
{
	int i, len;
	volatile struct ehci_qtd *qtd;

	for(i = 0; i < EHCI_MAX_EPS; i++) {
		struct ehci_ep *ep = &e->eps[i];

		if(!ep->used || !ep->active_qtd ||
		   ep->type != USB_EP_INTR_IN) {
			continue;
		}
		qtd = (volatile struct ehci_qtd *)P2V(ep->active_qtd);
		if(qtd->token & QTD_ACTIVE) {
			continue;	/* still pending */
		}
		/* QEMU writes the actual length into the token */
		len = 0x4000 - ((qtd->token >> QTD_TBYTES_SH) & 0x7fff);
		ep->active_qtd = 0;
		for(i = 0; i < EHCI_MAX_CB; i++) {
			if(e->cbs[i].used && e->cbs[i].dev == ep->dev &&
			   e->cbs[i].epid == ep->epid) {
				e->cbs[i].fn(ep->dev, ep->epid, 1, len,
					     e->cbs[i].data);
				break;
			}
		}
	}
}

/* ---------------- vtable stubs ---------------- */

static int ehci_ring_init(struct usb_ring *r, int trbs)
{
	memset_b(r, 0, sizeof(*r));
	return 0;
}

static void ehci_reset_ep0(int dev)
{
}

static int ehci_slot_root_port(int dev)
{
	if(dev < 0 || dev >= EHCI_MAX_DEVS || !e->devs[dev].port) {
		return 0;
	}
	return e->devs[dev].port;
}

static int ehci_slot_speed(int dev)
{
	if(dev < 0 || dev >= EHCI_MAX_DEVS) {
		return 0;
	}
	return e->devs[dev].speed;
}

static void ehci_disable(int dev)
{
	int i;

	if(dev < 0 || dev >= EHCI_MAX_DEVS) {
		return;
	}
	for(i = 0; i < EHCI_MAX_EPS; i++) {
		if(e->eps[i].used && e->eps[i].dev == dev) {
			e->eps[i].used = 0;
			e->eps[i].active_qtd = 0;
		}
	}
	for(i = 0; i < EHCI_MAX_CB; i++) {
		if(e->cbs[i].used && e->cbs[i].dev == dev) {
			e->cbs[i].used = 0;
		}
	}
	e->devs[dev].port = 0;
}

/* ---------------- enumeration ---------------- */

/* reset + enable one root port; returns the device speed (0=low 1=full
 * 2=high) or -1 when nothing is connected */
static int ehci_port_reset(int port)
{
	unsigned long off = EHCI_PORTSC + port * 4;
	unsigned int ps, i;

	/* power the port first: CCS only asserts once PPOWER is set */
	ps = ehci_reg_r(off);
	if(!(ps & EHCI_PORTSC_PPOWER)) {
		ehci_reg_w(off, ps | EHCI_PORTSC_PPOWER);
		for(i = 0; i < 2000000; i++) {
			ehci_reg_r(off);
		}
	}
	ps = ehci_reg_r(off);
	if(!(ps & EHCI_PORTSC_CCS)) {
		return -1;
	}
	/* power + reset: PR 0->1 then 1->0 resets the device */
	ehci_reg_w(off, ps | EHCI_PORTSC_PPOWER | EHCI_PORTSC_RESET);
	for(i = 0; i < 2000000; i++) {
		if(ehci_reg_r(off) & EHCI_PORTSC_RESET) {
			break;
		}
	}
	for(i = 0; i < 2000000; i++) {
		ehci_reg_r(off);
	}
	ehci_reg_w(off, ehci_reg_r(off) & ~EHCI_PORTSC_RESET);
	for(i = 0; i < 2000000; i++) {
		ehci_reg_r(off);
	}
	ps = ehci_reg_r(off);
	if(!(ps & EHCI_PORTSC_CCS)) {
		return -1;
	}
	return (ps >> EHCI_PORTSC_LS_SH) & 3;	/* 0=FS 1=LS 2=HS */
}

int ehci_enumerate(int root_port, int route, int speed)
{
	unsigned char *devdesc, *configdesc;
	int dev = 1, ret, i;

	/* behind-hub devices work through the same control path (the hub
	 * driver already reset the downstream port); root_port is the hub's
	 * root port for hotplug bookkeeping */
	devdesc = e->desc18;
	configdesc = e->desc64;

	for(i = 1; i < EHCI_MAX_DEVS; i++) {
		if(!e->devs[i].port) {
			dev = i;
			break;
		}
	}

	if((ret = ehci_control(0, 0x00, 5 /* SET_ADDRESS */, dev, 0, 0, NULL))) {
		printk("ehci: port %d SET_ADDRESS failed (%d)\n", root_port, ret);
		return ret;
	}
	e->devs[dev].port = root_port;
	e->devs[dev].speed = speed;
	printk("ehci: dev %d addressed (port %d, speed %d)\n",
		dev, root_port, speed);

	if(!ehci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x01 << 8, 0, 18, devdesc)) {
		printk("ehci: device descriptor: idVendor %x idProduct %x "
			"bcdUSB %x class %x\n",
			devdesc[8] | (devdesc[9] << 8),
			devdesc[10] | (devdesc[11] << 8),
			devdesc[2] | (devdesc[3] << 8), devdesc[4]);
	}

	/* config descriptor: first the 9-byte header for wTotalLength (a
	 * request larger than the real descriptor makes the QEMU control
	 * state machine leave the data stage unfilled, stalling the status
	 * stage), then the full descriptor */
	configdesc[0] = configdesc[1] = configdesc[2] = 0;
	if(!ehci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x02 << 8, 0, 9, configdesc)) {
		int totlen = configdesc[2] | (configdesc[3] << 8);
		if(totlen > 255) {
			totlen = 255;
		}
		if(!ehci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
				 0x02 << 8, 0, totlen, configdesc)) {
			if(usb_kbd_init(dev, configdesc) < 0) {
				if(usb_mouse_init(dev, configdesc) < 0) {
					if(usb_storage_init(dev, configdesc) < 0) {
						if(usb_hub_init(dev, configdesc) < 0) {
							usb_net_init(dev, configdesc);
						}
					}
				}
			}
		}
	}
	return dev;
}

/* ---------------- probe ---------------- */

static void ehci_irq_handler(int num, struct sigcontext *sc)
{
	if(e) {
		ehci_reg_w(EHCI_USBSTS, 0x003F);	/* clear */
	}
}

static struct usb_hcd ehci_hcd = {
	.name		= "ehci",
	.control	= ehci_control,
	.configure_ep	= ehci_configure_ep,
	.submit		= ehci_submit,
	.transfer	= ehci_transfer,
	.transfer_zlp	= NULL,
	.set_transfer_cb = ehci_set_transfer_cb,
	.kick_ep	= NULL,
	.poll		= ehci_poll,
	.ring_init	= ehci_ring_init,
	.reset_ep0	= ehci_reset_ep0,
	.slot_root_port	= ehci_slot_root_port,
	.slot_speed	= ehci_slot_speed,
	.disable	= ehci_disable,
	.enumerate	= ehci_enumerate,
};

int ehci_probe(void)
{
	struct pci_device *pd;
	unsigned long bar;
	unsigned int caplen, nports, i, sts;
	int port, ret, irq;

	/* find an EHCI controller (class 0x0C03, prog-if 0x20) */
	pd = pci_device_table;
	while(pd) {
		if(pd->class == 0x0C03 && pd->prog_if == 0x20) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	if(!(e = (struct ehci_state *)kmalloc(sizeof(struct ehci_state)))) {
		printk("ehci: no memory\n");
		return -ENOMEM;
	}
	memset_b(e, 0, sizeof(*e));

	irq = pd->irq;
	e->irq = (unsigned char)irq;

	bar = pd->bar[0];
	if(pd->flags[0] & PCI_F_ADDR_MEM_64) {
		bar |= (unsigned long)pd->bar[1] << 32;
	}

	/* map BAR0 */
	for(i = 0; i < EHCI_MMIO_SIZE / 4096; i++) {
		if(map_page64(EHCI_MMIO_VA + i * 4096, bar + i * 4096, 0x003)) {
			printk("ehci: unable to map BAR0\n");
			return -ENOMEM;
		}
	}
	e->mmio = (unsigned char *)EHCI_MMIO_VA;

	caplen = e->mmio[EHCI_CAPLENGTH];
	nports = ehci_reg_r(EHCI_CAPLENGTH + EHCI_HCSPARAMS) &
		 EHCI_HCSPARAMS_NPORTS;
	e->nports = nports;
	e->mmio += caplen;

	/* HCRESET the controller at the OPERATIONAL base (the op USBCMD is
	 * at BAR+CAPLENGTH, not BAR+0!) - clears any stale RS/schedule state
	 * OVMF left behind */
	ehci_reg_w(EHCI_USBCMD, EHCI_USBCMD_HCRESET);
	for(i = 0; i < 2000000; i++) {
		if(!(ehci_reg_r(EHCI_USBCMD) & EHCI_USBCMD_HCRESET)) {
			break;
		}
	}
	/* clear any stale status bits (IAA in particular gates the async
	 * processing until the guest acknowledges it) */
	ehci_reg_w(EHCI_USBSTS, 0x003F);

	printk("ehci: EHCI at %08lx, %d ports, IRQ %d\n", bar, nports, irq);

	/* periodic frame list (all terminated) */
	if(!(e->framelist = (unsigned int *)kmalloc(4096))) {
		return -ENOMEM;
	}
	e->framelist_phys = (unsigned long)V2P((addr_t)e->framelist);
	for(i = 0; i < EHCI_FRAMES; i++) {
		e->framelist[i] = EHCI_LINK_T;
	}

	/* aligned pool for QHs/qTDs */
	if(!(e->pool = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	e->pool_phys = (unsigned long)V2P((addr_t)e->pool);

	/* the async QH (EP0 control: C+DTC set, MPLEN 8, H = reclamation
	 * head - the WAITLISTHEAD state requires the first QH to have H) */
	if(!(e->async_qh = ehci_alloc_slot())) {
		return -ENOMEM;
	}
	e->async_qh_phys = ehci_slot_phys(e->async_qh);
	e->async_qh->next = EHCI_LINK_T;
	e->async_qh->epchar = (8 << 16) |	/* MPLEN */
			     (1 << 27) |	/* C: control */
			     (1 << 15) |	/* H: reclamation head */
			     (1 << 14) |	/* DTC */
			     (2 << 12);		/* EPS: high-speed */
	e->async_qh->next_qtd = EHCI_LINK_T;

	/* program the schedule + run */
	ehci_reg_w(EHCI_CTRLDSSEGMENT, 0);
	ehci_reg_w(EHCI_PERIODICLISTBASE,
		   (unsigned int)e->framelist_phys);
	ehci_reg_w(EHCI_ASYNCLISTADDR,
		   (unsigned int)e->async_qh_phys);
	ehci_reg_w(EHCI_USBCMD,
		   EHCI_USBCMD_PSE | EHCI_USBCMD_ASE | EHCI_USBCMD_RUNSTOP);
	for(i = 0; i < 2000000; i++) {
		if(!(ehci_reg_r(EHCI_USBSTS) & EHCI_USBSTS_HCHALTED)) {
			break;
		}
	}
	ehci_reg_w(EHCI_CONFIGFLAG, 1);

	/* register the INTx */
	{
		static struct interrupt irq_config_ehci = { 0, "ehci", &ehci_irq_handler, NULL };
		if(irq) {
			register_irq(irq, &irq_config_ehci);
			if(irq >= 8) {
				outport_b(0xA1, 0xFF & ~(1 << (irq - 8)));
			} else {
				outport_b(0x21, 0xFE & ~(1 << irq));
			}
		}
	}

	e->present = 1;
	usb_hcd_register(&ehci_hcd);

	/* enumerate connected devices */
	for(port = 0; port < nports; port++) {
		ret = ehci_port_reset(port);
		if(ret >= 0) {
			ehci_enumerate(port + 1, 0, ret);
		}
	}
	return 0;
}
