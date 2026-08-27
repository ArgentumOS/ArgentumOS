/*
 * fnx/include/fnx/xhci.h
 *
 * Public API of the xHCI host controller driver. Used by USB class
 * drivers (usb-kbd, usb-storage, ...) to run control transfers and
 * per-endpoint data transfers.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_XHCI_H
#define _FNX_XHCI_H

#include <fnx/mm.h>
#include <fnx/types.h>

#define XHCI_EP_INTR_IN		7
#define XHCI_EP_BULK_IN		6
#define XHCI_EP_BULK_OUT	2

/* transfer ring: caller kmallocs the TRB array (page-aligned) */
struct xhci_trb {
	unsigned long parameter;
	unsigned int status;
	unsigned int control;
};

struct xhci_ring {
	struct xhci_trb *trbs;
	unsigned long phys;
	int size;
	int enq;
	int ccs;
};

int xhci_ring_init(struct xhci_ring *r, int trbs);
unsigned long xhci_ring_put(struct xhci_ring *r, struct xhci_trb *t);

/* EP0 control transfer; returns 0 on success */
int xhci_control(int slotid, unsigned char bmRequestType,
		   unsigned char bRequest,
		 unsigned short wValue, unsigned short wIndex,
		 unsigned short wLength, void *data);

/* configure a non-EP0 endpoint; type = XHCI_EP_*; ring_phys = transfer ring */
int xhci_configure_ep(int slotid, int epid, int type, int mps,
		      int interval, unsigned long ring_phys);

/* queue a transfer on an endpoint ring (TR_NORMAL + IOC) and kick it;
 * completion arrives via the async callback (xhci_poll) */
int xhci_submit(int slotid, int epid, int dir_in, void *buf, int len,
		struct xhci_ring *ring);

/* one-shot data transfer on an endpoint ring (TR_NORMAL + IOC) */
int xhci_transfer(int slotid, int epid, int dir_in, void *buf, int len,
		  struct xhci_ring *ring);

/* kick an endpoint (doorbell); used to re-queue after a completion */
void xhci_kick_ep(int slotid, int epid);

/* async transfer completion callback for one (slotid, epid):
 * fn(slotid, epid, ccode, data) */
void xhci_set_transfer_cb(int slotid, int epid,
			  void (*fn)(int, int, int, void *), void *data);

/* drain the event ring (timer BH hook); no-op when no xHCI is present */
void xhci_poll(void);

/* DMA helper: kernel VA -> guest physical */
#define XHCI_V2P(a)	V2P((addr_t)(a))

#endif /* _FNX_XHCI_H */

/* hub support (drivers/usb/usb-hub.c + xhci.c) */
int xhci_enumerate(int root_port, int route, int speed);
void xhci_disable_slot(int slotid);
int xhci_slot_root_port(int slotid);
void xhci_reset_ep0(int slotid);
