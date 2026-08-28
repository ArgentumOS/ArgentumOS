/*
 * fnx/drivers/usb/uhci.c
 *
 * UHCI (USB 1.1, 12Mbps) host controller driver.
 *
 * QEMU: -device piix3-usb-uhci (8086:7020), piix4-usb-uhci (8086:7112),
 * ich9-usb-uhci1/2/3 (8086:2934/2935/2936), vt82c686b-usb-uhci
 * (1106:3038) - class 0x0C03 prog-if 0x00, 16-bit I/O ports at BAR4
 * (0x20 bytes), INTx.
 *
 * Schedule model: a 1024-entry frame list (one 1ms frame per entry)
 * at FLBASEADD. Every entry links the single async QH (control/bulk);
 * interrupt endpoints get their own QH linked into the frame list at a
 * stride (2^k frames) with its link chaining back to the async QH.
 * TDs hang off the QH element links; the HC walks the current frame's
 * chain every 1ms and clears the TD ACTIVE bit when done (the driver
 * polls; QEMU also fires the INTx on IOC).
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

#define UHCI_VENDOR_INTEL	0x8086
#define UHCI_VENDOR_VIA		0x1106

/* registers (16-bit I/O) */
#define UHCI_CMD		0x00
#define UHCI_STS		0x02
#define UHCI_INTR		0x04
#define UHCI_FRNUM		0x06
#define UHCI_FLBASEADD		0x08	/* 32-bit */
#define UHCI_SOF		0x0C
#define UHCI_PORTSC1		0x10
#define UHCI_PORTSC(n)		(UHCI_PORTSC1 + ((n) << 1))

#define UHCI_CMD_RS		0x0001
#define UHCI_CMD_HCRESET	0x0002
#define UHCI_CMD_GRESET		0x0004
#define UHCI_CMD_EGSM		0x0008
#define UHCI_CMD_FGR		0x0010

#define UHCI_STS_USBINT		0x0001
#define UHCI_STS_USBERR		0x0002
#define UHCI_STS_RD		0x0004
#define UHCI_STS_HSERR		0x0008
#define UHCI_STS_HCPERR		0x0010
#define UHCI_STS_HCHALTED	0x0020

#define UHCI_PORT_CCS		0x0001	/* current connect status */
#define UHCI_PORT_CSC		0x0002
#define UHCI_PORT_EN		0x0004
#define UHCI_PORT_ENC		0x0008
#define UHCI_PORT_SUSP		0x1000
#define UHCI_PORT_PR		0x0200	/* port reset */
#define UHCI_PORT_LSDA		0x0100	/* low-speed device attached */
#define UHCI_PORT_RD		0x0040

/* link encoding: bit0 = Q (1 = QH, 0 = TD), bit1 = T (1 = terminate) */
#define UHCI_LINK_Q		0x0002
#define UHCI_LINK_T		0x0001

#define UHCI_FRAMES		1024
#define UHCI_MAX_PKT		2048	/* bytes per TD (QEMU's max_len) */

/* TD control bits */
#define TD_CTRL_ACTIVE		(1 << 23)
#define TD_CTRL_IOC		(1 << 24)
#define TD_CTRL_LS		(1 << 26)
#define TD_CTRL_ERR		(3 << 27)
#define TD_CTRL_SPD		(1 << 29)

/* token PIDs (low byte) */
#define TD_PID_SETUP		0x2d
#define TD_PID_IN		0x69
#define TD_PID_OUT		0xe1

#define UHCI_MAX_EPS	8
#define UHCI_MAX_CB	8
#define UHCI_MAX_DEVS	16

struct uhci_td {
	unsigned int link;
	unsigned int ctrl;
	unsigned int token;
	unsigned int buffer;
};

struct uhci_qh {
	unsigned int link;
	unsigned int el_link;
};

/* one configured non-EP0 endpoint (interrupt or bulk) */
struct uhci_ep {
	int used;
	int dev;
	int epid;
	int type;		/* USB_EP_* */
	int mps;
	unsigned long qh_phys;	/* periodic QH in the frame list */
	int stride;		/* periodic: frame-list stride (frames) */
	int start_frame;
	unsigned long active_td;	/* phys of the outstanding TD, 0 = idle */
};

struct uhci_cb {
	int used;
	int dev;
	int epid;
	void (*fn)(int, int, int, int, void *);
	void *data;
};

/* state must live on the HEAP: FNX64 maps the kernel image at both the
 * low-identity and high-half VAs (different physical copies of the
 * statics!), so a static uhci_state would give the probe and the timer
 * BH two independent states. Same rule as xhci.c/psaux_table. */
static struct uhci_state {
	unsigned int io;
	unsigned char irq;
	int present;
	int nports;
	/* frame list (1024 entries, page-aligned, DMA-visible) */
	unsigned int *framelist;
	unsigned long framelist_phys;
	/* the single async QH (control/bulk), linked into every frame */
	struct uhci_qh *async_qh;
	unsigned long async_qh_phys;
	/* aligned pool for TDs/QHs (16-byte slots) */
	unsigned char *pool;
	unsigned long pool_phys;
	int pool_next;
	/* scratch areas (must be DMA-visible kernel VAs) */
	unsigned char setup[8];
	unsigned char status_in[64];
	unsigned char desc18[18];
	unsigned char desc64[64];
	/* speed of the device being enumerated (used for the SET_ADDRESS
	 * control, whose target dev 0 is not yet in the devs[] table) */
	int enum_speed;
	struct uhci_ep eps[UHCI_MAX_EPS];
	struct uhci_cb cbs[UHCI_MAX_CB];
	struct {
		int port;
		int speed;	/* 0=low 1=full */
	} devs[UHCI_MAX_DEVS];
} *u;

/* ---------------- aligned pool ---------------- */

static void *uhci_alloc_slot(void)
{
	void *p;

	if(u->pool_next + 16 > 4096) {
		return NULL;
	}
	p = u->pool + u->pool_next;
	u->pool_next += 16;
	memset_b(p, 0, 16);
	return p;
}

static unsigned long uhci_slot_phys(void *p)
{
	return u->pool_phys + ((unsigned long)p - (unsigned long)u->pool);
}

/* ---------------- TD helpers ---------------- */

static unsigned int uhci_td_link(unsigned long phys, int is_qh, int term)
{
	unsigned int link = (unsigned int)phys;

	if(is_qh) {
		link |= UHCI_LINK_Q;
	}
	if(term) {
		link |= UHCI_LINK_T;
	}
	return link;
}

/* build one TD in a pool slot; returns its phys (0 = no pool) */
static unsigned long uhci_build_td(unsigned int pid, int dev, int ep,
				   int maxlen, void *buf, unsigned long next_link)
{
	struct uhci_td *td = uhci_alloc_slot();
	unsigned long td_phys;

	if(!td) {
		return 0;
	}
	td_phys = uhci_slot_phys(td);
	td->link = (unsigned int)next_link;
	td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_ERR;
	/* low-speed devices need the LS bit in every TD (the HC uses it for
	 * the 1.5Mbps handshake); dev 0 (SET_ADDRESS) takes the speed of the
	 * device being enumerated */
	if((dev >= 1 && dev < UHCI_MAX_DEVS && u->devs[dev].port &&
	    u->devs[dev].speed == 0) ||
	   (dev == 0 && u->enum_speed == 0)) {
		td->ctrl |= TD_CTRL_LS;
	}
	td->token = (unsigned int)pid |
		    ((unsigned int)(dev & 0x7f) << 8) |
		    ((unsigned int)(ep & 0xf) << 15) |
		    ((unsigned int)(maxlen & 0x7ff) << 21);
	td->buffer = buf ? (unsigned int)V2P((addr_t)buf) : 0;
	return td_phys;
}

/* wait for a TD (by phys) to leave ACTIVE; polls USBSTS so TCG exits and
 * QEMU's 1ms frame timer runs. Returns 0 on completion, -EIO on timeout. */
static int uhci_wait_td(unsigned long td_phys, int timeout)
{
	volatile struct uhci_td *td =
		(volatile struct uhci_td *)P2V(td_phys);
	int i;

	for(i = 0; i < timeout; i++) {
		if(!(td->ctrl & TD_CTRL_ACTIVE)) {
			return 0;
		}
		if(i & 0xFF) {
			inport_w(u->io + UHCI_STS);
		}
	}
	return -EIO;
}

/* ---------------- async (control/bulk) transfer ---------------- */

/* queue a chain of TDs on the async QH and wait for completion.
 * ntds/links/bufs describe the chain (each TD carries <= UHCI_MAX_PKT). */
static int uhci_async_wait(unsigned long first_td, unsigned long last_td)
{
	volatile struct uhci_qh *qh = u->async_qh;

	/* the chain's last TD must terminate the list */
	qh->el_link = uhci_td_link(first_td, 0, 0);
	return uhci_wait_td(last_td, 60000000);
}

static int uhci_control(int dev, unsigned char bmRequestType,
			unsigned char bRequest, unsigned short wValue,
			unsigned short wIndex, unsigned short wLength,
			void *data)
{
	unsigned long t_setup, t_data = 0, t_status;
	int dir_in = (bmRequestType & 0x80) != 0;
	volatile struct uhci_td *td;
	int ret;

	/* SETUP packet */
	u->setup[0] = bmRequestType;
	u->setup[1] = bRequest;
	u->setup[2] = wValue & 0xFF;
	u->setup[3] = wValue >> 8;
	u->setup[4] = wIndex & 0xFF;
	u->setup[5] = wIndex >> 8;
	u->setup[6] = wLength & 0xFF;
	u->setup[7] = wLength >> 8;

	/* build the chain from the tail so every link is correct:
	 * SETUP -> [DATA] -> STATUS -> T */
	if(dir_in) {
		/* control READ: data IN, status OUT (device ACKs) */
		t_status = uhci_build_td(TD_PID_OUT, dev, 0, 0,
					 u->status_in, UHCI_LINK_T);
	} else {
		/* control WRITE: data OUT, status IN (zero-length) */
		t_status = uhci_build_td(TD_PID_IN, dev, 0, 7,
					 u->status_in, UHCI_LINK_T);
	}
	if(!t_status) {
		return -ENOMEM;
	}
	if(wLength) {
		t_data = uhci_build_td(dir_in ? TD_PID_IN : TD_PID_OUT,
				       dev, 0, wLength - 1, data, t_status);
		if(!t_data) {
			return -ENOMEM;
		}
		t_setup = uhci_build_td(TD_PID_SETUP, dev, 0, 7, u->setup,
					t_data);
	} else {
		t_setup = uhci_build_td(TD_PID_SETUP, dev, 0, 7, u->setup,
					t_status);
	}
	if(!t_setup) {
		return -ENOMEM;
	}

	/* IOC on the status TD (QEMU raises the INTx; we also poll) */
	((struct uhci_td *)P2V(t_status))->ctrl |= TD_CTRL_IOC;

	ret = uhci_async_wait(t_setup, t_status);
	if(ret) {
		printk("uhci: control to dev %d timed out\n", dev);
		return -EIO;
	}
	/* success: ACTIVE cleared, error counter intact (3); a transfer
	 * error clears the counter bits (QEMU uhci_handle_td_error) */
	td = (volatile struct uhci_td *)P2V(t_status);
	if((td->ctrl & TD_CTRL_ERR) != TD_CTRL_ERR) {
		return -EIO;
	}
	return 0;
}

/* synchronous bulk transfer (usb-storage): chain TDs on the async QH */
static int uhci_transfer(int dev, int epid, int dir_in, void *buf, int len,
			 struct usb_ring *ring)
{
	unsigned long first = 0, prev = 0, td_phys;
	int epnum = epid >> 1;
	int remaining = len, off = 0, chunk;

	while(remaining > 0) {
		chunk = remaining > UHCI_MAX_PKT ? UHCI_MAX_PKT : remaining;
		td_phys = uhci_build_td(dir_in ? TD_PID_IN : TD_PID_OUT,
				       dev, epnum, chunk - 1,
				       (unsigned char *)buf + off, 0);
		if(!td_phys) {
			return -ENOMEM;
		}
		if(prev) {
			((struct uhci_td *)P2V(prev))->link =
				uhci_td_link(td_phys, 0, 0);
		} else {
			first = td_phys;
		}
		prev = td_phys;
		off += chunk;
		remaining -= chunk;
	}
	if(!first) {
		return -EINVAL;
	}
	return uhci_async_wait(first, prev);
}

/* ---------------- interrupt endpoints ---------------- */

static int uhci_configure_ep(int dev, int epid, int type, int mps,
			     int interval, unsigned long ring_phys)
{
	struct uhci_ep *ep = NULL;
	struct uhci_qh *qh;
	unsigned long qh_phys;
	int i, stride, start_frame;

	for(i = 0; i < UHCI_MAX_EPS; i++) {
		if(!u->eps[i].used) {
			ep = &u->eps[i];
			break;
		}
	}
	if(!ep) {
		return -ENOMEM;
	}

	qh = uhci_alloc_slot();
	if(!qh) {
		return -ENOMEM;
	}
	qh_phys = uhci_slot_phys(qh);

	/* the interrupt QH chains back to the async QH so control/bulk
	 * stays reachable from frames that host this QH */
	qh->link = uhci_td_link(u->async_qh_phys, 1, 0);
	qh->el_link = UHCI_LINK_T;

	ep->used = 1;
	ep->dev = dev;
	ep->epid = epid;
	ep->type = type;
	ep->mps = mps;
	ep->qh_phys = qh_phys;
	ep->active_td = 0;

	if(type == USB_EP_INTR_IN) {
		/* interval is xHCI-encoded: 2^interval * 125us; UHCI frames
		 * are 1ms, so the stride is 2^(interval-3) frames */
		stride = 1 << (interval > 3 ? interval - 3 : 0);
		ep->stride = stride;
		/* link the QH into the frame list at every stride-th entry */
		for(start_frame = 0; start_frame < UHCI_FRAMES; start_frame++) {
			if((start_frame & (stride - 1)) == 0) {
				u->framelist[start_frame] =
					(unsigned long)qh_phys | UHCI_LINK_Q;
			}
		}
		ep->start_frame = 0;
	} else {
		/* bulk: queued on the async QH on demand (no persistent QH) */
		ep->stride = 0;
	}

	printk("uhci: dev %d ep %d (%s, mps %d%s) configured\n",
		dev, epid, type == USB_EP_INTR_IN ? "intr-in" : "bulk",
		mps, type == USB_EP_INTR_IN ? "" : ", async");
	return 0;
}

static int uhci_submit(int dev, int epid, int dir_in, void *buf, int len,
		       struct usb_ring *ring)
{
	struct uhci_ep *ep = NULL;
	struct uhci_qh *qh;
	unsigned long td_phys;
	int i, epnum = epid >> 1;

	for(i = 0; i < UHCI_MAX_EPS; i++) {
		if(u->eps[i].used && u->eps[i].dev == dev &&
		   u->eps[i].epid == epid) {
			ep = &u->eps[i];
			break;
		}
	}
	if(!ep) {
		return -EINVAL;
	}

	/* one TD per outstanding interrupt report (the class drivers
	 * re-submit from the completion callback) */
	td_phys = uhci_build_td(dir_in ? TD_PID_IN : TD_PID_OUT,
				dev, epnum, (len - 1) & 0x7ff, buf, UHCI_LINK_T);
	if(!td_phys) {
		return -ENOMEM;
	}
	((struct uhci_td *)P2V(td_phys))->ctrl |= TD_CTRL_IOC;
	ep->active_td = td_phys;

	if(ep->type == USB_EP_INTR_IN) {
		qh = (struct uhci_qh *)P2V(ep->qh_phys);
		qh->el_link = uhci_td_link(td_phys, 0, 0);
	} else {
		/* bulk endpoint without a persistent QH: run it through
		 * the async list synchronously */
		uhci_async_wait(td_phys, td_phys);
		ep->active_td = 0;
	}
	return 0;
}

static void uhci_set_transfer_cb(int dev, int epid,
				 void (*fn)(int, int, int, int, void *),
				 void *data)
{
	int i;

	for(i = 0; i < UHCI_MAX_CB; i++) {
		if(!u->cbs[i].used) {
			u->cbs[i].used = 1;
			u->cbs[i].dev = dev;
			u->cbs[i].epid = epid;
			u->cbs[i].fn = fn;
			u->cbs[i].data = data;
			return;
		}
	}
}

static void uhci_poll(void)
{
	int i, len;
	volatile struct uhci_td *td;

	for(i = 0; i < UHCI_MAX_EPS; i++) {
		struct uhci_ep *ep = &u->eps[i];

		if(!ep->used || !ep->active_td ||
		   ep->type != USB_EP_INTR_IN) {
			continue;
		}
		td = (volatile struct uhci_td *)P2V(ep->active_td);
		if(td->ctrl & TD_CTRL_ACTIVE) {
			continue;	/* still pending */
		}
		len = (td->ctrl & 0x7ff) + 1;
		ep->active_td = 0;
		/* the QH's element was written back to T by the HC */
		for(i = 0; i < UHCI_MAX_CB; i++) {
			if(u->cbs[i].used && u->cbs[i].dev == ep->dev &&
			   u->cbs[i].epid == ep->epid) {
				u->cbs[i].fn(ep->dev, ep->epid, 1, len,
					     u->cbs[i].data);
				break;
			}
		}
	}
}

/* ---------------- vtable stubs (hub support / ring) ---------------- */

static int uhci_ring_init(struct usb_ring *r, int trbs)
{
	/* UHCI keeps its schedule state in the frame list / QHs; the
	 * ring struct is just a handle for the class drivers */
	memset_b(r, 0, sizeof(*r));
	return 0;
}

static void uhci_reset_ep0(int dev)
{
	/* no per-endpoint state to reset on UHCI; EP0 always works */
}

static int uhci_slot_root_port(int dev)
{
	if(dev < 0 || dev >= UHCI_MAX_DEVS || !u->devs[dev].port) {
		return 0;
	}
	return u->devs[dev].port;
}

static int uhci_slot_speed(int dev)
{
	if(dev < 0 || dev >= UHCI_MAX_DEVS) {
		return 0;
	}
	return u->devs[dev].speed;
}

static void uhci_disable(int dev)
{
	int i;

	if(dev < 0 || dev >= UHCI_MAX_DEVS) {
		return;
	}
	for(i = 0; i < UHCI_MAX_EPS; i++) {
		if(u->eps[i].used && u->eps[i].dev == dev) {
			u->eps[i].used = 0;
			u->eps[i].active_td = 0;
		}
	}
	for(i = 0; i < UHCI_MAX_CB; i++) {
		if(u->cbs[i].used && u->cbs[i].dev == dev) {
			u->cbs[i].used = 0;
		}
	}
	u->devs[dev].port = 0;
}

/* ---------------- enumeration ---------------- */

/* reset + enable one root port; returns the device speed (0=low 1=full)
 * or -1 when nothing is connected */
static int uhci_port_reset(int port)
{
	unsigned int io = u->io + UHCI_PORTSC(port);
	unsigned int ps, i;

	ps = inport_w(io);
	if(!(ps & UHCI_PORT_CCS)) {
		printk("uhci: port %d no device (portsc=0x%x)\n", port, ps);
		return -1;
	}
	/* reset the port (PR 0->1 resets the device in QEMU) */
	ps &= ~UHCI_PORT_EN;
	outport_w(io, ps | UHCI_PORT_PR);
	for(i = 0; i < 2000000; i++) {
		if(inport_w(io) & UHCI_PORT_PR) {
			break;
		}
	}
	for(i = 0; i < 2000000; i++) {
		inport_w(io);	/* TCG exit: let QEMU finish the reset */
	}
	/* release the reset, then enable the port */
	ps = inport_w(io);
	ps &= ~UHCI_PORT_PR;
	outport_w(io, ps);
	for(i = 0; i < 2000000; i++) {
		inport_w(io);
	}
	ps = inport_w(io);
	outport_w(io, ps | UHCI_PORT_EN);

	ps = inport_w(io);
	if(!(ps & UHCI_PORT_CCS)) {
		return -1;
	}
	return (ps & UHCI_PORT_LSDA) ? 0 : 1;
}

int uhci_enumerate(int root_port, int route, int speed)
{
	unsigned char *devdesc, *configdesc;
	int dev = 1, ret, i;

	/* the speed of the device being enumerated (the SET_ADDRESS control
	 * targets dev 0, which is not yet in the devs[] table; the LS bit in
	 * its TDs must match the device). For a behind-hub device the hub
	 * driver already reset the downstream port, so the control path
	 * below is identical to a root-port device (addr-0 packets are
	 * routed to the newly reset port); root_port is the hub's root port,
	 * kept for hotplug bookkeeping. */
	u->enum_speed = speed;

	/* the descriptor buffers must be DMA-visible kernel VAs (the kmalloc'd
	 * state, not the stack: V2P() of a stack address is wrong) */
	devdesc = u->desc18;
	configdesc = u->desc64;

	/* find a free device address */
	for(i = 1; i < UHCI_MAX_DEVS; i++) {
		if(!u->devs[i].port) {
			dev = i;
			break;
		}
	}

	/* SET_ADDRESS (device starts at address 0) */
	if((ret = uhci_control(0, 0x00, 5 /* SET_ADDRESS */, dev, 0, 0, NULL))) {
		printk("uhci: port %d SET_ADDRESS failed (%d)\n", root_port, ret);
		return ret;
	}
	u->devs[dev].port = root_port;
	u->devs[dev].speed = speed;
	printk("uhci: dev %d addressed (port %d, speed %d)\n",
		dev, root_port, speed);

	/* device descriptor (18 bytes) */
	if(!uhci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x01 << 8, 0, 18, devdesc)) {
		printk("uhci: device descriptor: idVendor %x idProduct %x "
			"bcdUSB %x class %x\n",
			devdesc[8] | (devdesc[9] << 8),
			devdesc[10] | (devdesc[11] << 8),
			devdesc[2] | (devdesc[3] << 8), devdesc[4]);
	} else {
		printk("uhci: GET_DESCRIPTOR failed\n");
	}

	/* config descriptor + class driver chain (mirrors xhci_enumerate) */
	if(!uhci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x02 << 8, 0, 64, configdesc)) {
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
	return dev;
}

/* ---------------- probe ---------------- */

static void uhci_irq_handler(int num, struct sigcontext *sc)
{
	/* completion + error status bits (cleared by writing 1) */
	if(u) {
		outport_w(u->io + UHCI_STS, 0x000F);
	}
}

static struct usb_hcd uhci_hcd = {
	.name		= "uhci",
	.control	= uhci_control,
	.configure_ep	= uhci_configure_ep,
	.submit		= uhci_submit,
	.transfer	= uhci_transfer,
	.transfer_zlp	= NULL,		/* not needed (bulk goes via transfer) */
	.set_transfer_cb = uhci_set_transfer_cb,
	.kick_ep	= NULL,
	.poll		= uhci_poll,
	.ring_init	= uhci_ring_init,
	.reset_ep0	= uhci_reset_ep0,
	.slot_root_port	= uhci_slot_root_port,
	.slot_speed	= uhci_slot_speed,
	.disable	= uhci_disable,
	.enumerate	= uhci_enumerate,
};

int uhci_probe(void)
{
	struct pci_device *pd;
	unsigned int bar, i;
	unsigned int cmd, sts;
	int port, ret;

	/* find a UHCI controller (class 0x0C03, prog-if 0x00) */
	pd = pci_device_table;
	while(pd) {
		if(pd->class == 0x0C03 && pd->prog_if == 0x00) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	if(!(u = (struct uhci_state *)kmalloc(sizeof(struct uhci_state)))) {
		printk("uhci: no memory\n");
		return -ENOMEM;
	}
	memset_b(u, 0, sizeof(*u));

	u->irq = pd->irq;
	u->io = pd->bar[4] & ~3;
	u->nports = 2;
	printk("uhci: UHCI at IO 0x%x IRQ %d\n", u->io, u->irq);

	/* HCRESET the controller */
	outport_w(u->io + UHCI_CMD, UHCI_CMD_HCRESET);
	for(i = 0; i < 2000000; i++) {
		if(!(inport_w(u->io + UHCI_CMD) & UHCI_CMD_HCRESET)) {
			break;
		}
	}

	/* frame list: one page, page-aligned (FLBASEADD low bits 0) */
	if(!(u->framelist = (unsigned int *)kmalloc(4096))) {
		return -ENOMEM;
	}
	u->framelist_phys = (unsigned long)V2P((addr_t)u->framelist);
	memset_b(u->framelist, 0, 4096);

	/* aligned pool for TDs/QHs */
	if(!(u->pool = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	u->pool_phys = (unsigned long)V2P((addr_t)u->pool);

	/* the async QH (control/bulk) is linked into every frame entry */
	if(!(u->async_qh = uhci_alloc_slot())) {
		return -ENOMEM;
	}
	u->async_qh_phys = uhci_slot_phys(u->async_qh);
	u->async_qh->link = UHCI_LINK_T;
	u->async_qh->el_link = UHCI_LINK_T;
	for(i = 0; i < UHCI_FRAMES; i++) {
		u->framelist[i] = u->async_qh_phys | UHCI_LINK_Q;
	}
	printk("uhci: framelist phys 0x%lx async_qh phys 0x%lx pool phys 0x%lx\n",
		u->framelist_phys, u->async_qh_phys, u->pool_phys);

	/* program the frame list base + frame number, then RUN. FLBASEADD
	 * is a 32-bit register exposed as two 16-bit ports (8 and 10) */
	outport_w(u->io + UHCI_CMD, 0);	/* RS=0 so the HC reloads the base */
	outport_w(u->io + UHCI_FRNUM, 0);
	outport_w(u->io + UHCI_FLBASEADD,
		  (unsigned short)(u->framelist_phys & 0xffff));
	outport_w(u->io + UHCI_FLBASEADD + 2,
		  (unsigned short)((u->framelist_phys >> 16) & 0xffff));
	outport_w(u->io + UHCI_CMD, UHCI_CMD_RS);

	/* register the INTx */
	{
		static struct interrupt irq_config_uhci = { 0, "uhci", &uhci_irq_handler, NULL };
		if(u->irq) {
			register_irq(u->irq, &irq_config_uhci);
			if(u->irq >= 8) {
				outport_b(0xA1, 0xFF & ~(1 << (u->irq - 8)));
			} else {
				outport_b(0x21, 0xFE & ~(1 << u->irq));
			}
		}
	}

	u->present = 1;
	usb_hcd_register(&uhci_hcd);

	/* enumerate connected devices */
	for(port = 0; port < u->nports; port++) {
		ret = uhci_port_reset(port);
		if(ret >= 0) {
			uhci_enumerate(port + 1, 0, ret);
		}
	}
	return 0;
}
