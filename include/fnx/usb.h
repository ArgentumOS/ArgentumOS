/*
 * fnx/include/fnx/usb.h
 *
 * USB host-controller dispatch + shared HCD interface. Class drivers
 * (usb-kbd, usb-mouse, usb-storage, usb-hub, usb-net) run control and
 * data transfers through the usb_* wrappers, which route to whichever
 * HCD is registered (xHCI, EHCI, UHCI). Each HCD implements the
 * struct usb_hcd vtable; usb_init() probes in order (xhci -> ehci ->
 * uhci) and registers the first controller found.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_USB_H
#define _FNX_USB_H

#include <fnx/types.h>

/* xHCI transfer request block; the transfer ring is an array of these
 * (page-aligned, DMA-visible). UHCI/EHCI do not use TRBs. */
struct xhci_trb {
	unsigned long parameter;
	unsigned int status;
	unsigned int control;
};

/* transfer ring: caller kmallocs the TRB array (page-aligned). The
 * layout is the xHCI TRB-ring shape; UHCI/EHCI ignore the ring fields
 * and keep their own schedule state keyed by (dev, epid). */
struct usb_ring {
	struct xhci_trb *trbs;
	unsigned long phys;
	int size;
	int enq;
	int ccs;
};

/* endpoint type encodings (xHCI endpoint-context type values; each HCD
 * maps them onto its own schedule: interrupt -> periodic, bulk ->
 * async/control-bulk) */
#define USB_EP_INTR_IN		7
#define USB_EP_BULK_IN		6
#define USB_EP_BULK_OUT		2

/* HCD vtable. dev = USB device address (1..127; 0 = default address
 * during SET_ADDRESS). epid = xHCI endpoint-id encoding, so the raw
 * endpoint is epnum = epid >> 1, dir_in = epid & 1. */
struct usb_hcd {
	const char *name;

	/* EP0 control transfer; returns 0 on success */
	int (*control)(int dev, unsigned char bmRequestType,
		       unsigned char bRequest, unsigned short wValue,
		       unsigned short wIndex, unsigned short wLength,
		       void *data);

	/* configure a non-EP0 endpoint; type = USB_EP_*; ring_phys = the
	 * transfer ring phys (xHCI only) */
	int (*configure_ep)(int dev, int epid, int type, int mps,
			    int interval, unsigned long ring_phys);

	/* queue an async transfer (completion via set_transfer_cb + poll) */
	int (*submit)(int dev, int epid, int dir_in, void *buf, int len,
		      struct usb_ring *ring);

	/* one-shot synchronous transfer */
	int (*transfer)(int dev, int epid, int dir_in, void *buf, int len,
			struct usb_ring *ring);

	/* zero-length transfer (bulk flush) */
	int (*transfer_zlp)(int dev, int epid, struct usb_ring *ring);

	/* async completion callback for one (dev, epid):
	 * fn(dev, epid, status, length, data) */
	void (*set_transfer_cb)(int dev, int epid,
				void (*fn)(int, int, int, int, void *),
				void *data);

	/* re-kick an endpoint after a completion */
	void (*kick_ep)(int dev, int epid);

	/* drain completions (timer BH hook) */
	void (*poll)(void);

	/* allocate a transfer ring */
	int (*ring_init)(struct usb_ring *r, int trbs);

	/* hub support */
	void (*reset_ep0)(int dev);
	int (*slot_root_port)(int dev);
	int (*slot_speed)(int dev);
	void (*disable)(int dev);

	/* enumerate one root port (HCD-specific; route/speed for
	 * behind-hub devices) */
	int (*enumerate)(int root_port, int route, int speed);
};

/* register the active HCD (called by the HCD probes) */
void usb_hcd_register(struct usb_hcd *hcd);

/* dispatch wrappers: route to the active HCD (return -ENODEV when no
 * HCD is present) */
int usb_control(int dev, unsigned char bmRequestType,
		unsigned char bRequest, unsigned short wValue,
		unsigned short wIndex, unsigned short wLength, void *data);
int usb_configure_ep(int dev, int epid, int type, int mps, int interval,
		     unsigned long ring_phys);
int usb_submit(int dev, int epid, int dir_in, void *buf, int len,
	       struct usb_ring *ring);
int usb_transfer(int dev, int epid, int dir_in, void *buf, int len,
		 struct usb_ring *ring);
int usb_transfer_zlp(int dev, int epid, struct usb_ring *ring);
void usb_set_transfer_cb(int dev, int epid,
			 void (*fn)(int, int, int, int, void *), void *data);
void usb_kick_ep(int dev, int epid);
void usb_poll(void);
int usb_ring_init(struct usb_ring *r, int trbs);
void usb_reset_ep0(int dev);
int usb_slot_root_port(int dev);
int usb_slot_speed(int dev);
void usb_disable(int dev);
int usb_enumerate(int root_port, int route, int speed);

/* HCD probes; usb_init() calls them in order until one registers */
int xhci_probe(void);
int ehci_probe(void);
int uhci_probe(void);
void usb_init(void);

#endif /* _FNX_USB_H */
