/*
 * fnx/drivers/usb/xhci->c
 *
 * xHCI host controller driver (QEMU qemu-xhci 1B36:000D / nec-usb-xhci
 * 1033:0194, class 0x0C0330 prog-if 0x30). M0 (HCD bring-up:
 * caps, HCRST, command + event rings, RS run, ports, IRQ) + M1 (USB
 * core: device model, EP0 control transfers, enumeration).
 *
 * The QEMU contract (qemu-10.0.11+ds/hw/usb/hcd-xhci->c):
 * - CAP 0x00-0x3F, OP at CAPLENGTH (0x40), runtime at RTSOFF (0x1000),
 *   doorbells at DBOFF (0x2000); BAR0 is 64-bit, 0x4000 bytes.
 * - Command ring: TRBs written with cycle bit; QEMU reads from its own
 *   dequeue until the cycle bit stops matching. LINK TRB (type 6)
 *   with TC follows to the next segment and flips the cycle.
 * - Event ring: ERSTSZ must be 1; events carry cycle bit; after
 *   processing, write ERDP (with EHB=bit 3) to re-enable.
 * - Input context control is NOT the spec order: QEMU wants
 *   ictl[0]=0, ictl[1]=0x3 for Address Device and ictl[0]=0,
 *   ictl[1]=0x1 for Configure Endpoint.
 * - Port: PORTSC CCS -> write PR (bit 4) -> PED + PRC; speed from
 *   PORTSC.SPEED (bits 10-13).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/asm.h>
#include <fnx/irq.h>
#include <fnx/msix.h>
#include <fnx/mm.h>
#include <fnx/pci.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

int usb_kbd_init(int slotid, unsigned char *configdesc);
int usb_mouse_init(int slotid, unsigned char *configdesc);
int usb_storage_init(int slotid, unsigned char *configdesc);
int usb_hub_init(int slotid, unsigned char *configdesc);
int usb_net_init(int slotid, unsigned char *configdesc);

/* ---------------- MMIO layout (QEMU) ---------------- */
#define XHCI_MMIO_VA		0xFFFFBC0000000000UL	/* pml4[505] */
#define XHCI_MMIO_SIZE		0x4000

#define CAP_CAPLENGTH		0x00
#define CAP_HCSPARAMS1		0x04
#define CAP_HCCPARAMS		0x10
#define CAP_DBOFF		0x14
#define CAP_RTSOFF		0x18

#define OP_USBCMD		0x00
#define OP_USBSTS		0x04
#define OP_CRCR_L		0x18
#define OP_CRCR_H		0x1C
#define OP_DCBAAP_L		0x30
#define OP_DCBAAP_H		0x34
#define OP_CONFIG		0x38
#define OP_PORTSC(n)		(0x400 + 0x10 * (n))

#define RT_IMAN			0x20
#define RT_IMOD			0x24
#define RT_ERSTSZ		0x28
#define RT_ERSTBA_L		0x30
#define RT_ERSTBA_H		0x34
#define RT_ERDP_L		0x38
#define RT_ERDP_H		0x3C

#define CMD_HCRST		0x0002
#define CMD_INTE		0x0004
#define CMD_RS			0x0001
#define STS_HCH			0x0001
#define STS_EINT		0x0008

#define CRCR_RCS		0x0001
#define CONFIG_MAXSLOTS		0x00FF

#define PORT_CCS		0x00000001
#define PORT_PED		0x00000002
#define PORT_PR			0x00000010
#define PORT_PP			0x00000200
#define PORT_CSC		0x00020000
#define PORT_PEC		0x00040000
#define PORT_PRC		0x00200000
#define PORT_OCC		0x00100000
#define PORT_SPEED_MASK		0x00003C00
#define PORT_SPEED_FULL		0x00000400
#define PORT_SPEED_LOW		0x00000800
#define PORT_SPEED_HIGH		0x00000C00
#define PORT_SPEED_SUPER	0x00001000

#define DB_CMD			0x0000
#define DB_DEV(slot)		(4 * (slot))	/* doorbell register offset */
#define DB_TARGET(epid)		(epid)		/* 0 = command, else EP id */

/* ---------------- TRBs ---------------- */
#define TRB_TYPE_SHIFT		10
#define TRB_TYPE_MASK		0x3F
#define TRB_C			0x00000001
#define TRB_TC			0x00000002
#define TRB_IOC			0x00000020
#define TRB_IDT			0x00000040	/* immediate data (SETUP) */
#define TRB_DIR			0x00010000	/* data/status direction */
#define TRB_TR_LEN		0x0001FFFF	/* length field */
#define TRB_CCODE_SHIFT		24
#define TRB_CCODE_MASK		0xFF
#define TRB_SLOTID_SHIFT	24
#define TRB_SLOTID_MASK		0xFF

#define TRB_RESERVED		0
#define TR_NORMAL		1
#define TR_SETUP			2
#define TR_DATA			3
#define TR_STATUS		4
#define TR_ISOCH		5
#define TR_LINK			6
#define TR_EVDATA		7
#define TR_NOOP			8
#define CR_ENABLE_SLOT		9
#define CR_DISABLE_SLOT		10
#define CR_ADDRESS_DEVICE	11
#define CR_CONFIGURE_EP		12
#define CR_EVALUATE_CONTEXT	13
#define CR_RESET_EP		14
#define CR_STOP_EP		15
#define CR_SET_TR_DEQ		16
#define CR_NOOP			23
#define ER_TRANSFER		32
#define ER_COMMAND_COMPLETE	33
#define ER_PORT_STATUS_CHANGE	34
#define ER_HOST_CONTROLLER	37

#define CC_SUCCESS		1
#define CC_USB_TRANSACTION_ERROR	4
#define CC_TRB_ERROR		5
#define CC_SLOT_NOT_ENABLED	11

/* ---------------- contexts ---------------- */
#define SLOT_CTX_ENTRIES_SHIFT	27
#define SLOT_CTX_SPEED_SHIFT	0
#define SLOT_CTX_SPEED_MASK	0xF
#define SLOT_CTX_PORT_SHIFT	16
#define SLOT_CTX_PORT_MASK	0xFF
#define SLOT_CTX_INTR_SHIFT	22
#define SLOT_CTX_INTR_MASK	0x3FF

#define EP_CTX_TYPE_SHIFT	3
#define EP_CTX_TYPE_MASK	0x7
#define EP_CTX_MPS_SHIFT	16
#define EP_CTX_MPS_MASK		0xFFFF
#define EP_CTX_DCS		0x1	/* bit 0 of dequeue dword */
#define EP_TYPE_CONTROL		4
#define EP_TYPE_BULK_OUT	2
#define EP_TYPE_BULK_IN		6
#define EP_TYPE_INTR_OUT	3
#define EP_TYPE_INTR_IN		7

/* USB protocol constants */
#define USB_DIR_IN		0x80
#define USB_DIR_OUT		0x00
#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_ADDRESS	0x05
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_DT_DEVICE		0x01
#define USB_DT_CONFIG		0x02
#define USB_DT_STRING		0x03

struct xhci_event {
	unsigned long parameter;
	unsigned int status;
	unsigned int control;
};

/* ---------------- driver state ---------------- */
#define CMD_RING_TRBS	32
#define EVT_RING_TRBS	128
#define XFER_RING_TRBS	64
#define MAX_SLOTS	16
#define XHCI_MAX_PORTS	16

struct xhci_dev {
	int slotid;
	int port;		/* root port (the hub's root port for behind-hub devices) */
	int route;		/* route string (hub port path; 0 = root-port device) */
	int speed;		/* 0=low 1=full 2=high 3=super */
	int addr;
	struct usb_ring ep0;
	unsigned char *octx;	/* output (device) context, V2P-able */
	unsigned long octx_phys;
	unsigned char *ictx;	/* input context */
	unsigned long ictx_phys;
	/* device descriptor cache (M1) */
	unsigned char devdesc[18];
	unsigned char configdesc[64];
};

static struct xhci_state {
	int present;
	unsigned long mmio;		/* kernel VA of BAR0 */
	unsigned int caps[2];		/* hcsparams1, hccparams */
	int numports, numslots, numintrs;
	unsigned long dboff, rtoff;
	struct usb_ring cmd;
	struct usb_ring evt;
	unsigned long evt_phys;
	unsigned long erst_phys;	/* event ring segment table */
	unsigned long dcbaa_phys;	/* device context base addr array */
	int run;
	unsigned char irq;
	int pending_port;
	int pending_port_valid;
	struct xhci_dev devs[MAX_SLOTS];
} *xhci;

/* set while a synchronous command/control waiter owns the event ring */
static int xhci_sync_waiting;

/* the HCD vtable this driver registers with the USB core */
static struct usb_hcd xhci_hcd = {
	.name		= "xhci",
	.control	= xhci_control,
	.configure_ep	= xhci_configure_ep,
	.submit		= xhci_submit,
	.transfer	= xhci_transfer,
	.transfer_zlp	= xhci_transfer_zlp,
	.set_transfer_cb = xhci_set_transfer_cb,
	.kick_ep	= xhci_kick_ep,
	.poll		= xhci_poll,
	.ring_init	= xhci_ring_init,
	.reset_ep0	= xhci_reset_ep0,
	.slot_root_port	= xhci_slot_root_port,
	.slot_speed	= xhci_slot_speed,
	.disable	= xhci_disable_slot,
	.enumerate	= xhci_enumerate,
};

extern int map_page64(unsigned long, unsigned long, unsigned long);

/* ---------------- MMIO accessors ---------------- */
static unsigned int xhci_reg_r(unsigned long off)
{
	return *(volatile unsigned int *)(xhci->mmio + off);
}
static void xhci_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(xhci->mmio + off) = val;
}

/* ---------------- rings ---------------- */
int xhci_ring_init(struct usb_ring *r, int trbs)
{
	int i;

	if(!(r->trbs = (struct xhci_trb *)kmalloc(trbs * 16))) {
		return -ENOMEM;
	}
	memset_b(r->trbs, 0, trbs * 16);
	r->phys = (unsigned long)V2P((addr_t)r->trbs);
	r->size = trbs;
	r->enq = 0;
	r->ccs = 1;

	/* trailing LINK back to the start, TC flips the cycle */
	i = trbs - 1;
	r->trbs[i].parameter = r->phys;
	r->trbs[i].status = 0;
	r->trbs[i].control = (TR_LINK << TRB_TYPE_SHIFT) | TRB_TC | TRB_C;
	return 0;
}

/* append a TRB to a ring; returns its physical address (0 = full).
 * Sets the cycle bit; refreshes the trailing LINK on wrap. */
unsigned long xhci_ring_put(struct usb_ring *r, struct xhci_trb *t)
{
	int slot;

	slot = r->enq;
	if(slot >= r->size - 1) {
		return 0;	/* ring full (LINK occupies the last slot) */
	}
	t->control = (t->control & ~1) | (r->ccs ? 1 : 0);
	r->trbs[slot] = *t;
	r->enq = slot + 1;

	if(r->enq == r->size - 1) {
		/* refresh the LINK's cycle bit for the next pass */
		r->trbs[r->size - 1].control &= ~1;
		r->trbs[r->size - 1].control |= (r->ccs ? 1 : 0);
		r->ccs ^= 1;
		r->enq = 0;
	}
	return r->phys + slot * 16;
}

/* ---------------- event ring ---------------- */
static void xhci_dispatch_event(struct xhci_event *ev);	/* fwd */
static void xhci_port_probe(int port);	/* fwd (boot + hotplug re-entry) */
static void xhci_hotplug_port(int port);	/* fwd */
int xhci_enumerate(int root_port, int route, int speed);	/* fwd */

static int xhci_event_wait(struct xhci_event *ev, int timeout)
{
	volatile struct xhci_trb *t;
	int idx;
	unsigned long spin;

	idx = xhci->evt.enq;
	for(spin = 0; spin < (unsigned long)timeout; spin++) {
		t = &((volatile struct xhci_trb *)xhci->evt.trbs)[idx];
		if(((t->control >> 0) & 1) == xhci->evt.ccs) {
			ev->parameter = t->parameter;
			ev->status = t->status;
			ev->control = t->control;
			idx++;
			if(idx == xhci->evt.size) {
				idx = 0;
				xhci->evt.ccs ^= 1;
			}
			xhci->evt.enq = idx;
			/* re-arm: ERDP = next slot, with EHB set */
			xhci_reg_w(xhci->rtoff + RT_ERDP_L,
				   (unsigned int)(xhci->evt.phys + idx * 16) | 0x8);
			xhci_reg_w(xhci->rtoff + RT_ERDP_H,
				   (unsigned int)((xhci->evt.phys + idx * 16) >> 32));
			return 0;
		}
	}
	return -EAGAIN;
}

static void xhci_event_clear_pending(void)
{
	/* clear the interrupter pending bit + re-arm EHB */
	xhci_reg_w(xhci->rtoff + RT_IMAN,
		   xhci_reg_r(xhci->rtoff + RT_IMAN) & ~1);
	xhci_reg_w(xhci->rtoff + RT_ERDP_L,
		   (unsigned int)(xhci->evt.phys + xhci->evt.enq * 16) | 0x8);
	xhci_reg_w(xhci->rtoff + RT_ERDP_H,
		   (unsigned int)((xhci->evt.phys + xhci->evt.enq * 16) >> 32));
}

/* ---------------- IRQ ---------------- */
static void xhci_irq_handler(int num, struct sigcontext *sc)
{
	/* the event ring is polled by the callers; just re-arm so a
	 * pending event keeps the line coherent */
	xhci_event_clear_pending();
}

/* ---------------- commands ---------------- */
static int xhci_cmd(struct xhci_trb *trb, int *ccode_out, int *slotid_out)
{
	struct xhci_event ev;
	unsigned long trb_addr;
	int ret;

	if(!(trb_addr = xhci_ring_put(&xhci->cmd, trb))) {
		return -EAGAIN;
	}

	/* the flag must be set BEFORE the doorbell: a timer-BH xhci_poll
	 * running in the window would steal this command's completion
	 * event and the sync wait would time out */
	xhci_sync_waiting = 1;
	xhci_reg_w(xhci->dboff + DB_CMD, 0);

	/* wait for the matching command-completion event */
	for(;;) {
		if((ret = xhci_event_wait(&ev, 1000000)) < 0) {
			xhci_sync_waiting = 0;
			return ret;
		}
		if(((ev.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) !=
		   ER_COMMAND_COMPLETE) {
			/* transfer completions for async devices must still
			 * reach their callbacks (kbd/mouse) */
			xhci_dispatch_event(&ev);
			continue;
		}
		if(ev.parameter != trb_addr) {
			continue;	/* not our command */
		}
		break;
	}

	if(ccode_out) {
		*ccode_out = (ev.status >> TRB_CCODE_SHIFT) & TRB_CCODE_MASK;
	}
	if(slotid_out) {
		*slotid_out = (ev.control >> TRB_SLOTID_SHIFT) & TRB_SLOTID_MASK;
	}
	xhci_sync_waiting = 0;
	return 0;
}

/* ---------------- ports ---------------- */
static int xhci_port_wait_enabled(int port, int timeout)
{
	unsigned long spin;

	for(spin = 0; spin < (unsigned long)timeout; spin++) {
		if(xhci_reg_r(0x440 + 0x10 * port) & PORT_PED) {
			return 0;
		}
	}
	return -EAGAIN;
}

/* ---------------- M1: USB core ---------------- */

/* EP0 control transfer on a slot's transfer ring.
 * bmRequestType: the full setup byte (0x80/0x00 standard, 0xA0/0x20
 * class). wLength > 0 implies a data stage. */
int xhci_control(int slotid, unsigned char bmRequestType,
			unsigned char bRequest, unsigned short wValue,
			unsigned short wIndex, unsigned short wLength,
			void *data)
{
	struct usb_ring *r = &xhci->devs[slotid].ep0;
	struct xhci_trb t;
	struct xhci_event ev;
	unsigned long setup;
	int dir_in = (bmRequestType & 0x80) != 0;
	int ret;

	if(xhci->devs[slotid].slotid != slotid) {
		return -EINVAL;
	}

	setup = bmRequestType;
	setup |= (unsigned long)bRequest << 8;
	setup |= (unsigned long)wValue << 16;
	setup |= (unsigned long)wIndex << 32;
	setup |= (unsigned long)wLength << 48;

	/* SETUP */
	memset_b(&t, 0, sizeof(t));
	t.parameter = setup;
	t.status = 8;					/* 8-byte setup */
	t.control = (TR_SETUP << TRB_TYPE_SHIFT) | TRB_IDT;
	if((ret = xhci_ring_put(r, &t)) < 0) {
		return ret;
	}

	/* DATA (optional) */
	if(wLength) {
		memset_b(&t, 0, sizeof(t));
		t.parameter = (unsigned long)V2P((addr_t)data);
		t.status = wLength;
		t.control = (TR_DATA << TRB_TYPE_SHIFT) |
			    (dir_in ? TRB_DIR : 0);
		if((ret = xhci_ring_put(r, &t)) < 0) {
			return ret;
		}
	}

	/* STATUS (direction opposite the data stage; IOC) */
	memset_b(&t, 0, sizeof(t));
	t.control = (TR_STATUS << TRB_TYPE_SHIFT) |
		    (dir_in ? TRB_DIR : 0) | TRB_IOC;
	if((ret = xhci_ring_put(r, &t)) < 0) {
		return ret;
	}

	/* kick the endpoint (EP0 = endpoint id 1) */
	xhci_sync_waiting = 1;
	xhci_reg_w(xhci->dboff + DB_DEV(slotid), DB_TARGET(1));

	/* wait for the transfer-completion event */
	for(;;) {
		if((ret = xhci_event_wait(&ev, 2000000)) < 0) {
			xhci_sync_waiting = 0;
			return ret;
		}
		if(((ev.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) ==
		   ER_TRANSFER) {
			if(((ev.control >> TRB_SLOTID_SHIFT) & TRB_SLOTID_MASK) ==
			   (unsigned int)slotid) {
				if(((ev.status >> TRB_CCODE_SHIFT) &
				    TRB_CCODE_MASK) != CC_SUCCESS) {
					xhci_sync_waiting = 0;
					return -EIO;
				}
				xhci_sync_waiting = 0;
				return 0;
			}
			xhci_dispatch_event(&ev);
		}
	}
}

/* configure a non-EP0 endpoint: build the input context (ictl[1] = slot
 * ctx + the endpoint's bit) and issue Configure Endpoint. */
int xhci_configure_ep(int slotid, int epid, int type, int mps,
		      int interval, unsigned long ring_phys)
{
	struct xhci_dev *d;
	struct xhci_trb t;
	volatile unsigned int *ictl;
	volatile unsigned int *ep;
	int ccode, ret;

	if(slotid < 1 || slotid >= MAX_SLOTS || epid < 2 || epid > 31) {
		return -EINVAL;
	}
	d = &xhci->devs[slotid];
	if(d->slotid != slotid) {
		return -EINVAL;
	}

	ictl = (volatile unsigned int *)P2V(d->ictx_phys);
	ictl[0] = 0x0;			/* drop none */
	ictl[1] = 0x1 | (1 << epid);	/* add slot ctx + ep ctx */

	/* slot context (required to be present) */
	((volatile unsigned int *)P2V(d->ictx_phys) + 8)[0] = 0;
	((volatile unsigned int *)P2V(d->ictx_phys) + 8)[1] = 0;
	((volatile unsigned int *)P2V(d->ictx_phys) + 8)[2] = 0;
	((volatile unsigned int *)P2V(d->ictx_phys) + 8)[3] = 0;

	/* endpoint context at ictx + 32 + 32*epid */
	ep = (volatile unsigned int *)(P2V(d->ictx_phys) + 32 + 32 * epid);
	ep[0] = (unsigned int)(interval & 0xFF) << 16;
	ep[1] = ((unsigned int)type << EP_CTX_TYPE_SHIFT) |
		((unsigned int)mps << EP_CTX_MPS_SHIFT);
	ep[2] = (unsigned int)ring_phys | EP_CTX_DCS;
	ep[3] = (unsigned int)(ring_phys >> 32);
	ep[4] = 0;

	memset_b(&t, 0, sizeof(t));
	t.parameter = d->ictx_phys;
	t.control = (CR_CONFIGURE_EP << TRB_TYPE_SHIFT) |
		    ((slotid & TRB_SLOTID_MASK) << TRB_SLOTID_SHIFT);
	if((ret = xhci_cmd(&t, &ccode, NULL)) < 0) {
		return ret;
	}
	if(ccode != CC_SUCCESS) {
		return -EIO;
	}
	return 0;
}

/* ---------------- async transfers (class drivers) ---------------- */
#define XHCI_MAX_CB	8

struct xhci_cb {
	int slotid;
	int epid;
	void (*fn)(int, int, int, int, void *);	/* (slotid, epid, ccode, length, data) */
	void *data;
	int used;
};
static struct xhci_cb xhci_cbs[XHCI_MAX_CB];

/* register an async completion callback for one (slotid, epid) */
void xhci_set_transfer_cb(int slotid, int epid, void (*fn)(int, int, int, int, void *),
			  void *data)
{
	int n;

	for(n = 0; n < XHCI_MAX_CB; n++) {
		if(xhci_cbs[n].used && xhci_cbs[n].slotid == slotid &&
		   xhci_cbs[n].epid == epid) {
			xhci_cbs[n].fn = fn;
			xhci_cbs[n].data = data;
			return;
		}
	}
	for(n = 0; n < XHCI_MAX_CB; n++) {
		if(!xhci_cbs[n].used) {
			xhci_cbs[n].slotid = slotid;
			xhci_cbs[n].epid = epid;
			xhci_cbs[n].fn = fn;
			xhci_cbs[n].data = data;
			xhci_cbs[n].used = 1;
			return;
		}
	}
}

/* re-kick an endpoint after a completion */
void xhci_kick_ep(int slotid, int epid)
{
	xhci_reg_w(xhci->dboff + DB_DEV(slotid), DB_TARGET(epid));
}

int xhci_submit(int slotid, int epid, int dir_in, void *buf, int len,
		struct usb_ring *ring)
{
	struct xhci_trb t;

	if(!buf || len <= 0) {
		return -EINVAL;
	}

	memset_b(&t, 0, sizeof(t));
	t.parameter = XHCI_V2P(buf);
	t.status = len;
	t.control = (TR_NORMAL << TRB_TYPE_SHIFT) |
		    (dir_in ? TRB_DIR : 0) | TRB_IOC;
	if(!xhci_ring_put(ring, &t)) {
		return -EAGAIN;
	}

	xhci_reg_w(xhci->dboff + DB_DEV(slotid), DB_TARGET(epid));
	return 0;
}

int xhci_transfer(int slotid, int epid, int dir_in, void *buf, int len,
		  struct usb_ring *ring)
{
	struct xhci_event ev;
	int ret;

	/* the flag must be set BEFORE the doorbell (see xhci_cmd): a
	 * timer-BH xhci_poll running in the window would consume this
	 * transfer's completion event and the sync wait would time out */
	xhci_sync_waiting = 1;
	if((ret = xhci_submit(slotid, epid, dir_in, buf, len, ring)) < 0) {
		xhci_sync_waiting = 0;
		return ret;
	}

	for(;;) {
		if((ret = xhci_event_wait(&ev, 2000000)) < 0) {
			xhci_sync_waiting = 0;
			return ret;
		}
		if(((ev.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) !=
		   ER_TRANSFER) {
			continue;
		}
		if(((ev.control >> TRB_SLOTID_SHIFT) & TRB_SLOTID_MASK) ==
		   (unsigned int)slotid &&
		   ((ev.control >> 16) & 0xFF) == (unsigned int)epid) {
			if(((ev.status >> TRB_CCODE_SHIFT) &
			    TRB_CCODE_MASK) != CC_SUCCESS) {
				xhci_sync_waiting = 0;
				return -EIO;
			}
			xhci_sync_waiting = 0;
			return 0;
		}
		/* someone else's transfer completed (async kbd/mouse):
		 * deliver it to its callback so it can re-submit */
		xhci_dispatch_event(&ev);
	}
}

/* queue a zero-length transfer on an endpoint ring (flushes a
 * 64-multiple frame on QEMU's usb-net gadget). xhci_submit refuses
 * len <= 0, so build the TR_NORMAL directly and kick the endpoint. */
int xhci_transfer_zlp(int slotid, int epid, struct usb_ring *ring)
{
	struct xhci_trb t;

	memset_b(&t, 0, sizeof(t));
	t.control = (TR_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC;
	if(!xhci_ring_put(ring, &t)) {
		return -EAGAIN;
	}
	xhci_reg_w(xhci->dboff + DB_DEV(slotid), DB_TARGET(epid));
	return 0;
}

/* drain the event ring and dispatch async transfer completions.
 * Called from the timer BH; skips while a synchronous waiter owns the
 * ring (probe-time commands/control transfers). */
static void xhci_dispatch_event(struct xhci_event *ev)
{
	int type, slotid, epid, n;

	type = (ev->control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK;
	if(type == ER_PORT_STATUS_CHANGE) {
		/* a device appeared/disappeared while a sync waiter owned
		 * the ring: re-queue it for the poll's hotplug pass */
		xhci->pending_port = (ev->parameter >> 24) & 0xFF;
		xhci->pending_port_valid = 1;
		return;
	}
	if(type != ER_TRANSFER) {
		return;	/* command completions are consumed by the sync paths */
	}
	slotid = (ev->control >> TRB_SLOTID_SHIFT) & TRB_SLOTID_MASK;
	epid = (ev->control >> 16) & 0xFF;
	for(n = 0; n < XHCI_MAX_CB; n++) {
		if(xhci_cbs[n].used && xhci_cbs[n].slotid == slotid &&
		   xhci_cbs[n].epid == epid) {
			xhci_cbs[n].fn(slotid, epid,
				(ev->status >> TRB_CCODE_SHIFT) & TRB_CCODE_MASK,
				ev->status & 0xFFFFFF,	/* transferred length */
				xhci_cbs[n].data);
			break;
		}
	}
}

void xhci_poll(void)
{
	struct xhci_event ev;

	if(!xhci->present) {
		return;
	}
	if(xhci_sync_waiting) {
		return;
	}
	{
		int ports[XHCI_MAX_PORTS], nports = 0;
		int i;

		/* drain ALL pending events first, then act: a port status
		 * change that arrives while the probe for the same port is
		 * still queued would otherwise be swallowed by the probe's
		 * synchronous event waits (or acted on before the drain) */
		while(!xhci_event_wait(&ev, 1)) {
			int type = (ev.control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK;
			if(type == ER_PORT_STATUS_CHANGE) {
				int port = (ev.parameter >> 24) & 0xFF;
				if(nports < XHCI_MAX_PORTS && port >= 1 &&
				   port <= xhci->numports) {
					ports[nports++] = port;
				}
				continue;
			}
			xhci_dispatch_event(&ev);
		}
		for(i = 0; i < nports; i++) {
			xhci_hotplug_port(ports[i]);
		}
		if(xhci->pending_port_valid) {
			int p = xhci->pending_port;
			xhci->pending_port_valid = 0;
			if(p >= 1 && p <= xhci->numports) {
				xhci_hotplug_port(p);
			}
		}
	}
}

/* root-port hotplug: a device was added or removed on `port`.
 * (Re)runs the per-port enumeration; disables a stale slot on remove. */
static void xhci_hotplug_port(int port)
{
	unsigned int ps;
	int slotid;

	if(port < 1 || port > xhci->numports) {
		return;
	}
	ps = xhci_reg_r(0x440 + 0x10 * (port - 1));

	/* clear the latched change bits (write-1-to-clear) */
	xhci_reg_w(0x440 + 0x10 * (port - 1),
		   ps & (PORT_CSC | PORT_PEC | PORT_PRC | PORT_OCC));
	ps &= ~(PORT_CSC | PORT_PEC | PORT_PRC | PORT_OCC);

	/* a slot already exists for this port? (device stays put) */
	for(slotid = 1; slotid < MAX_SLOTS; slotid++) {
		if(xhci->devs[slotid].slotid == slotid &&
		   xhci->devs[slotid].port == port &&
		   xhci->devs[slotid].route == 0) {
			return;	/* reset/overcurrent etc.: nothing to do */
		}
	}

	if(ps & PORT_CCS) {
		/* a NEW device appeared: enumerate */
		xhci_port_probe(port);
		return;
	}

	/* disconnected: find and disable the slot on this port */
	for(slotid = 1; slotid < MAX_SLOTS; slotid++) {
		if(xhci->devs[slotid].slotid == slotid &&
		   xhci->devs[slotid].port == port) {
			struct xhci_trb t;
			int n;

			printk("xhci: port %d: device removed (slot %d)\n",
				port, slotid);
			for(n = 0; n < XHCI_MAX_CB; n++) {
				if(xhci_cbs[n].used && xhci_cbs[n].slotid == slotid) {
					xhci_cbs[n].used = 0;
				}
			}
			memset_b(&t, 0, sizeof(t));
			t.control = (CR_DISABLE_SLOT << TRB_TYPE_SHIFT) |
				    ((unsigned long)slotid << TRB_SLOTID_SHIFT);
			xhci_cmd(&t, NULL, NULL);
			xhci->devs[slotid].slotid = 0;
			break;
		}
	}
}

/* ---------------- probe / init ---------------- */
static int xhci_init_hcd(void)
{
	unsigned long spin;
	int ret;


	/* host controller reset */
	xhci_reg_w(0x40 + OP_USBCMD, CMD_HCRST);
	for(spin = 0; spin < 1000000; spin++) {
		if(xhci_reg_r(0x40 + OP_USBSTS) & STS_HCH) {
			break;
		}
	}
	if(spin == 1000000) {
		printk("xhci: HCRST timeout\n");
		return -EIO;
	}

	/* event ring segment table (ERSTSZ = 1) */
	*(volatile unsigned long *)((unsigned long)xhci->evt.trbs -
		((unsigned long)xhci->evt.trbs & 0)) = 0;	/* no-op guard */
	/* ERST: 16 bytes at erst_phys: addr_low, addr_high, size, rsvd */
	*(volatile unsigned int *)P2V(xhci->erst_phys) =
		(unsigned int)xhci->evt_phys;
	*(volatile unsigned int *)(P2V(xhci->erst_phys) + 4) =
		(unsigned int)(xhci->evt_phys >> 32);
	*(volatile unsigned int *)(P2V(xhci->erst_phys) + 8) =
		EVT_RING_TRBS;	/* segment size in TRBs */
	*(volatile unsigned int *)(P2V(xhci->erst_phys) + 12) = 0;

	xhci_reg_w(xhci->rtoff + RT_ERSTSZ, 1);
	xhci_reg_w(xhci->rtoff + RT_ERSTBA_L, (unsigned int)xhci->erst_phys);
	xhci_reg_w(xhci->rtoff + RT_ERSTBA_H, (unsigned int)(xhci->erst_phys >> 32));
	xhci_reg_w(xhci->rtoff + RT_ERDP_L, (unsigned int)xhci->evt_phys | 0x8);
	xhci_reg_w(xhci->rtoff + RT_ERDP_H, (unsigned int)(xhci->evt_phys >> 32));
	xhci_reg_w(xhci->rtoff + RT_IMAN, 0x2);	/* IE = 1 */
	xhci_reg_w(xhci->rtoff + RT_IMOD, 0);

	/* command ring (RCS = 1), DCBAA, max slots */
	xhci_reg_w(0x40 + OP_CRCR_L, (unsigned int)xhci->cmd.phys | CRCR_RCS);
	xhci_reg_w(0x40 + OP_CRCR_H, (unsigned int)(xhci->cmd.phys >> 32));
	xhci_reg_w(0x40 + OP_DCBAAP_L, (unsigned int)xhci->dcbaa_phys);
	xhci_reg_w(0x40 + OP_DCBAAP_H, (unsigned int)(xhci->dcbaa_phys >> 32));
	xhci_reg_w(0x40 + OP_CONFIG, 1);	/* MaxSlotsEn = 1 */

	/* run + interrupt enable */
	xhci_reg_w(0x40 + OP_USBCMD, CMD_RS | CMD_INTE);
	for(spin = 0; spin < 1000000; spin++) {
		if(!(xhci_reg_r(0x40 + OP_USBSTS) & STS_HCH)) {
			break;
		}
	}
	if(spin == 1000000) {
		printk("xhci: RUN timeout\n");
		return -EIO;
	}
	xhci->run = 1;
	ret = 0;
	return ret;
}

static int xhci_enable_slot(int *slotid)
{
	struct xhci_trb t;
	int ccode, sid, ret;

	memset_b(&t, 0, sizeof(t));
	t.control = CR_ENABLE_SLOT << TRB_TYPE_SHIFT;
	if((ret = xhci_cmd(&t, &ccode, &sid)) < 0) {
		return ret;
	}
	if(ccode != CC_SUCCESS) {
		return -EIO;
	}
	*slotid = sid;
	return 0;
}

static int xhci_address_device(struct xhci_dev *d)
{
	struct xhci_trb t;
	volatile unsigned int *ictl;
	volatile unsigned int *sctx;
	volatile unsigned int *ep0;
	int ccode, ret;

	/* input context: ictl[0]=0 ictl[1]=3 (QEMU order!) */
	ictl = (volatile unsigned int *)P2V(d->ictx_phys);
	ictl[0] = 0x0;
	ictl[1] = 0x3;

	/* slot context (at ictx + 32) */
	sctx = (volatile unsigned int *)(P2V(d->ictx_phys) + 32);
	sctx[0] = 1 << SLOT_CTX_ENTRIES_SHIFT |	/* 1 context (slot) */
		 (d->route & 0xFFFFF);		/* route string (0 = root port) */
	sctx[1] = (d->speed & SLOT_CTX_SPEED_MASK) << SLOT_CTX_SPEED_SHIFT |
		  (d->port & SLOT_CTX_PORT_MASK) << SLOT_CTX_PORT_SHIFT;
	sctx[2] = 0 << SLOT_CTX_INTR_SHIFT;	/* interrupter 0 */
	sctx[3] = 0;

	/* EP0 context (at ictx + 64): control, mps, dequeue ring */
	ep0 = (volatile unsigned int *)(P2V(d->ictx_phys) + 64);
	ep0[0] = 0;	/* interval 0 */
	ep0[1] = ((unsigned int)EP_TYPE_CONTROL << EP_CTX_TYPE_SHIFT) |
		  (64 << EP_CTX_MPS_SHIFT);	/* max packet 64 */
	ep0[2] = (unsigned int)d->ep0.phys | EP_CTX_DCS;
	ep0[3] = (unsigned int)(d->ep0.phys >> 32);
	ep0[4] = 0;

	/* DCBAA[slotid] = output context (zeroed device context) */
	*(volatile unsigned long *)P2V(xhci->dcbaa_phys + 8 * d->slotid) =
		d->octx_phys;

	memset_b(&t, 0, sizeof(t));
	t.parameter = d->ictx_phys;
	t.control = (CR_ADDRESS_DEVICE << TRB_TYPE_SHIFT) |
		    ((d->slotid & TRB_SLOTID_MASK) << TRB_SLOTID_SHIFT);
	if((ret = xhci_cmd(&t, &ccode, NULL)) < 0) {
		return ret;
	}
	if(ccode != CC_SUCCESS) {
		return -EIO;
	}
	return 0;
}

int xhci_probe(void)
{
	unsigned long bar;
	unsigned char irq;
	struct pci_device *pd;
	unsigned long phys;
	int i, port, slotid, ret;

	/* the HCD state must live on the HEAP: FNX64 maps the kernel image
	 * at both the low-identity and high-half VAs (different physical
	 * copies of the statics!), so a static xhci_state would give the
	 * sync paths and the timer-BH poll two independent states (the
	 * sync flag + event-ring enq/ccs would diverge and the poll would
	 * steal the syncs' events). Same fix as psaux_table/tty_table. */
	if(!(xhci = (struct xhci_state *)kmalloc(sizeof(struct xhci_state)))) {
		printk("xhci: no memory\n");
		return -ENOMEM;
	}
	memset_b(xhci, 0, sizeof(struct xhci_state));

	/* find the xHCI controller (class 0x0C0330) */
	pd = pci_device_table;
	while(pd) {
		if((pd->class == 0x0C03 && pd->prog_if == 0x30) ||
		   (pd->vendor_id == 0x1B36 && pd->device_id == 0x000D) ||
		   (pd->vendor_id == 0x1033 && pd->device_id == 0x0194)) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	bar = pd->bar[0];
	if(pd->flags[0] & PCI_F_ADDR_MEM_64) {
		bar |= (unsigned long)pd->bar[1] << 32;
	}
	irq = pd->irq;
	xhci->irq = irq;

	/* map BAR0 (0x4000 bytes) at a fixed kernel VA */
	for(i = 0; i < XHCI_MMIO_SIZE / 4096; i++) {
		if(map_page64(XHCI_MMIO_VA + i * 4096, bar + i * 4096, 0x003)) {
			printk("xhci: unable to map BAR0\n");
			return -ENOMEM;
		}
	}
	xhci->mmio = XHCI_MMIO_VA;

	/* read capabilities */
	xhci->caps[0] = xhci_reg_r(CAP_HCSPARAMS1);
	xhci->numports = (xhci->caps[0] >> 24) & 0xFF;
	xhci->numintrs = (xhci->caps[0] >> 8) & 0xFF;
	xhci->numslots = xhci->caps[0] & 0xFF;
	xhci->dboff = xhci_reg_r(CAP_DBOFF);
	xhci->rtoff = xhci_reg_r(CAP_RTSOFF);

	printk("xhci: xHCI at 0x%lx, %d ports, %d slots, %d intrs\n",
		bar, xhci->numports, xhci->numslots, xhci->numintrs);

	/* allocate rings + contexts (page-aligned, DMA-visible) */
	if(xhci_ring_init(&xhci->cmd, CMD_RING_TRBS) < 0) {
		return -ENOMEM;
	}
	if(xhci_ring_init(&xhci->evt, EVT_RING_TRBS) < 0) {
		return -ENOMEM;
	}
	xhci->evt_phys = xhci->evt.phys;

	if(!(phys = (unsigned long)V2P((addr_t)kmalloc(4096)))) {
		return -ENOMEM;
	}
	memset_b((void *)P2V(phys), 0, 4096);
	xhci->erst_phys = phys;
	if(!(phys = (unsigned long)V2P((addr_t)kmalloc(4096)))) {
		return -ENOMEM;
	}
	memset_b((void *)P2V(phys), 0, 4096);
	xhci->dcbaa_phys = phys;

	if((ret = xhci_init_hcd()) < 0) {
		return ret;
	}

	/* MSI-X (preferred) or INTx */
	{
		static struct interrupt irq_config_xhci =
			{ 0, "xhci", &xhci_irq_handler, NULL };
		if(!msix_pci_setup(pd, MSIX_VEC_BASE)) {
			register_msix(0, &irq_config_xhci);
			printk("xhci: MSI-X active on IDT vector 0x%x\n",
				MSIX_VEC_BASE);
		} else if(irq) {
			register_irq(irq, &irq_config_xhci);
			if(irq >= 8) {
				outport_b(0xA1, 0xFF & ~(1 << (irq - 8)));
			} else {
				outport_b(0x21, 0xFE & ~(1 << irq));
			}
			printk("xhci: INTx on IRQ %d\n", irq);
		}
	}

	/* register the HCD BEFORE the port scan: enumeration during
	 * xhci_port_probe() routes class-driver control transfers through
	 * the usb_* wrappers, which need the active HCD set */
	xhci->present = 1;
	usb_hcd_register(&xhci_hcd);

	/* scan ports: reset any connected device and bring up a slot */
	for(port = 1; port <= xhci->numports; port++) {
		xhci_port_probe(port);
	}

	return 0;
}

/* enumerate (or re-enumerate after a hotplug) one root port: reset,
 * address, fetch descriptors, hand off to the class drivers */
static void xhci_port_probe(int port)
{
	unsigned long phys;
	int slotid, ret;
	unsigned int ps;

	ps = xhci_reg_r(0x440 + 0x10 * (port - 1));
	if(!(ps & PORT_CCS)) {
		return;
	}
	printk("xhci: port %d: device connected\n", port);

	/* port reset */
	xhci_reg_w(0x440 + 0x10 * (port - 1), PORT_PR);
	if((ret = xhci_port_wait_enabled(port - 1, 2000000)) < 0) {
		printk("xhci: port %d reset timeout\n", port);
		return;
	}
		ps = xhci_reg_r(0x440 + 0x10 * (port - 1));
		printk("xhci: port %d enabled\n", port);

	xhci_enumerate(port, 0, (ps & PORT_SPEED_MASK) == PORT_SPEED_HIGH ?
		2 : (ps & PORT_SPEED_MASK) == PORT_SPEED_FULL ? 1 :
		(ps & PORT_SPEED_MASK) == PORT_SPEED_SUPER ? 3 : 0);
}

/* shared enumeration core: enable a slot, address the device, fetch the
 * descriptors, hand it to the class drivers. root_port = the xhci root
 * port; route = the hub port path (0 for a root-port device); speed =
 * the xhci speed encoding (0=low 1=full 2=high 3=super). */
/* disable a slot + drop its async callbacks (used on hotplug removal,
 * including devices behind a hub) */
/* the root port a slot's device is attached to (the hub's root port
 * for devices behind a hub) */
/* reset EP0 after a stall so the hub's control pipe works again */
void xhci_reset_ep0(int slotid)
{
	struct xhci_trb t;

	if(slotid < 1 || slotid >= MAX_SLOTS ||
	   xhci->devs[slotid].slotid != slotid) {
		return;
	}
	memset_b(&t, 0, sizeof(t));
	t.control = (CR_RESET_EP << TRB_TYPE_SHIFT) |
		    (1UL << 16) |	/* endpoint id 1 (EP0) */
		    ((unsigned long)slotid << TRB_SLOTID_SHIFT);
	xhci_cmd(&t, NULL, NULL);
}

int xhci_slot_root_port(int slotid)
{
	if(slotid < 1 || slotid >= MAX_SLOTS) {
		return 0;
	}
	return xhci->devs[slotid].port;
}

/* the speed a slot's device enumerated at (0=low 1=full 2=high 3=super) */
int xhci_slot_speed(int slotid)
{
	if(slotid < 1 || slotid >= MAX_SLOTS) {
		return 0;
	}
	return xhci->devs[slotid].speed;
}

void xhci_disable_slot(int slotid)
{
	struct xhci_trb t;
	int n;

	if(slotid < 1 || slotid >= MAX_SLOTS ||
	   xhci->devs[slotid].slotid != slotid) {
		return;
	}
	for(n = 0; n < XHCI_MAX_CB; n++) {
		if(xhci_cbs[n].used && xhci_cbs[n].slotid == slotid) {
			xhci_cbs[n].used = 0;
		}
	}
	memset_b(&t, 0, sizeof(t));
	t.control = (CR_DISABLE_SLOT << TRB_TYPE_SHIFT) |
		    ((unsigned long)slotid << TRB_SLOTID_SHIFT);
	xhci_cmd(&t, NULL, NULL);
	xhci->devs[slotid].slotid = 0;
	xhci->devs[slotid].route = 0;
}

int xhci_enumerate(int root_port, int route, int speed)
{
	unsigned long phys;
	int slotid, ret;

	/* enable a slot for the device */
	if((ret = xhci_enable_slot(&slotid)) < 0) {
		printk("xhci: enable slot failed (%d)\n", ret);
		return ret;
	}
	if(slotid < 1 || slotid > MAX_SLOTS) {
		printk("xhci: bad slot %d\n", slotid);
		return -EINVAL;
	}

	xhci->devs[slotid].slotid = slotid;
	xhci->devs[slotid].port = root_port;
	xhci->devs[slotid].route = route;
	xhci->devs[slotid].speed = speed;

		if(xhci_ring_init(&xhci->devs[slotid].ep0, XFER_RING_TRBS) < 0) {
			return -EIO;
		}
		if(!(phys = (unsigned long)V2P((addr_t)kmalloc(4096)))) {
			return -EIO;
		}
		memset_b((void *)P2V(phys), 0, 4096);
		xhci->devs[slotid].octx_phys = phys;
		xhci->devs[slotid].octx = (unsigned char *)P2V(phys);
		if(!(phys = (unsigned long)V2P((addr_t)kmalloc(4096)))) {
			return -EIO;
		}
		memset_b((void *)P2V(phys), 0, 4096);
		xhci->devs[slotid].ictx_phys = phys;
		xhci->devs[slotid].ictx = (unsigned char *)P2V(phys);

		if((ret = xhci_address_device(&xhci->devs[slotid])) < 0) {
			struct xhci_trb t;

			printk("xhci: address device failed (%d)\n", ret);
			/* the device went away mid-probe: drop the partial slot */
			memset_b(&t, 0, sizeof(t));
			t.control = (CR_DISABLE_SLOT << TRB_TYPE_SHIFT) |
				    ((unsigned long)slotid << TRB_SLOTID_SHIFT);
			xhci_cmd(&t, NULL, NULL);
			xhci->devs[slotid].slotid = 0;
			return -EIO;
		}
		xhci->devs[slotid].addr = slotid;
		printk("xhci: slot %d addressed (port %d, speed %d)\n",
			slotid, root_port, xhci->devs[slotid].speed);

		/* M1: read the device descriptor (control transfer) */
		if(!xhci_control(slotid, 0x80, USB_REQ_GET_DESCRIPTOR,
				 USB_DT_DEVICE << 8, 0, 18,
				 xhci->devs[slotid].devdesc)) {
			printk("xhci: device descriptor: idVendor %x idProduct %x "
				"bcdUSB %x class %x\n",
				xhci->devs[slotid].devdesc[8] |
					(xhci->devs[slotid].devdesc[9] << 8),
				xhci->devs[slotid].devdesc[10] |
					(xhci->devs[slotid].devdesc[11] << 8),
				xhci->devs[slotid].devdesc[2] |
					(xhci->devs[slotid].devdesc[3] << 8),
				xhci->devs[slotid].devdesc[4]);
		} else {
			printk("xhci: GET_DESCRIPTOR failed\n");
		}

		/* M2: class drivers - fetch the config descriptor and hand
		 * the device to the class drivers */
		if(!xhci_control(slotid, 0x80, USB_REQ_GET_DESCRIPTOR,
				 USB_DT_CONFIG << 8, 0, 64,
				 xhci->devs[slotid].configdesc)) {
			if(usb_kbd_init(slotid, xhci->devs[slotid].configdesc) < 0) {
				if(usb_mouse_init(slotid, xhci->devs[slotid].configdesc) < 0) {
					if(usb_storage_init(slotid, xhci->devs[slotid].configdesc) < 0) {
						if(usb_hub_init(slotid, xhci->devs[slotid].configdesc) < 0) {
							usb_net_init(slotid, xhci->devs[slotid].configdesc);
						}
					}
				}
			}
		}

	/* root-port probes only: the device may have been unplugged
	 * while we probed it (behind-hub devices are checked by the hub) */
	if(!route && !(xhci_reg_r(0x440 + 0x10 * (root_port - 1)) & PORT_CCS)) {
		struct xhci_trb t;
		int n;

		printk("xhci: port %d: device vanished during probe\n", root_port);
		for(slotid = 1; slotid < MAX_SLOTS; slotid++) {
			if(xhci->devs[slotid].slotid == slotid &&
			   xhci->devs[slotid].port == root_port &&
			   xhci->devs[slotid].route == 0) {
				for(n = 0; n < XHCI_MAX_CB; n++) {
					if(xhci_cbs[n].used && xhci_cbs[n].slotid == slotid) {
						xhci_cbs[n].used = 0;
					}
				}
				memset_b(&t, 0, sizeof(t));
				t.control = (CR_DISABLE_SLOT << TRB_TYPE_SHIFT) |
					    ((unsigned long)slotid << TRB_SLOTID_SHIFT);
				xhci_cmd(&t, NULL, NULL);
				xhci->devs[slotid].slotid = 0;
				break;
			}
		}
	}
}
