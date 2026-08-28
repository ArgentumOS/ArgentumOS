/*
 * fnx/include/fnx/xhci.h
 *
 * xHCI host controller driver internals. The public USB API (struct
 * usb_ring, usb_control, usb_submit, ...) lives in <fnx/usb.h>; the
 * class drivers route through it. xhci.c registers its HCD vtable via
 * usb_hcd_register() and the functions below are its implementations.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_XHCI_H
#define _FNX_XHCI_H

#include <fnx/mm.h>
#include <fnx/types.h>
#include <fnx/usb.h>


unsigned long xhci_ring_put(struct usb_ring *r, struct xhci_trb *t);

/* xHCI HCD vtable implementations (see struct usb_hcd in usb.h) */
int xhci_ring_init(struct usb_ring *r, int trbs);
int xhci_control(int slotid, unsigned char bmRequestType,
		 unsigned char bRequest, unsigned short wValue,
		 unsigned short wIndex, unsigned short wLength, void *data);
int xhci_configure_ep(int slotid, int epid, int type, int mps,
		      int interval, unsigned long ring_phys);
int xhci_submit(int slotid, int epid, int dir_in, void *buf, int len,
		struct usb_ring *ring);
int xhci_transfer(int slotid, int epid, int dir_in, void *buf, int len,
		  struct usb_ring *ring);
int xhci_transfer_zlp(int slotid, int epid, struct usb_ring *ring);
void xhci_kick_ep(int slotid, int epid);
void xhci_set_transfer_cb(int slotid, int epid,
			  void (*fn)(int, int, int, int, void *), void *data);
void xhci_poll(void);
void xhci_reset_ep0(int slotid);
int xhci_slot_root_port(int slotid);
int xhci_slot_speed(int slotid);
void xhci_disable_slot(int slotid);
int xhci_enumerate(int root_port, int route, int speed);

/* DMA helper: kernel VA -> guest physical */
#define XHCI_V2P(a)	V2P((addr_t)(a))

#endif /* _FNX_XHCI_H */
