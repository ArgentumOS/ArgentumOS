/*
 * fnx/drivers/usb/usb.c
 *
 * USB host controller dispatch. The active HCD (xHCI, EHCI or UHCI)
 * is registered by its probe and the usb_* wrappers route to it, so
 * the class drivers are transport-agnostic. Probing is ordered - xhci
 * first (covers USB 2.0 and 3.0), then ehci (USB 2.0 + FS/LS via
 * QEMU's all-speed model), then uhci (USB 1.1).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/usb.h>

static struct usb_hcd *hcd;

void usb_hcd_register(struct usb_hcd *h)
{
	hcd = h;
	printk("usb: %s HCD active\n", h->name);
}

int usb_control(int dev, unsigned char bmRequestType,
		unsigned char bRequest, unsigned short wValue,
		unsigned short wIndex, unsigned short wLength, void *data)
{
	if(!hcd || !hcd->control) {
		return -ENODEV;
	}
	return hcd->control(dev, bmRequestType, bRequest, wValue, wIndex,
			    wLength, data);
}

int usb_configure_ep(int dev, int epid, int type, int mps, int interval,
		     unsigned long ring_phys)
{
	if(!hcd || !hcd->configure_ep) {
		return -ENODEV;
	}
	return hcd->configure_ep(dev, epid, type, mps, interval, ring_phys);
}

int usb_submit(int dev, int epid, int dir_in, void *buf, int len,
	       struct usb_ring *ring)
{
	if(!hcd || !hcd->submit) {
		return -ENODEV;
	}
	return hcd->submit(dev, epid, dir_in, buf, len, ring);
}

int usb_transfer(int dev, int epid, int dir_in, void *buf, int len,
		 struct usb_ring *ring)
{
	if(!hcd || !hcd->transfer) {
		return -ENODEV;
	}
	return hcd->transfer(dev, epid, dir_in, buf, len, ring);
}

int usb_transfer_zlp(int dev, int epid, struct usb_ring *ring)
{
	if(!hcd || !hcd->transfer_zlp) {
		return -ENODEV;
	}
	return hcd->transfer_zlp(dev, epid, ring);
}

void usb_set_transfer_cb(int dev, int epid,
			 void (*fn)(int, int, int, int, void *), void *data)
{
	if(hcd && hcd->set_transfer_cb) {
		hcd->set_transfer_cb(dev, epid, fn, data);
	}
}

void usb_kick_ep(int dev, int epid)
{
	if(hcd && hcd->kick_ep) {
		hcd->kick_ep(dev, epid);
	}
}

void usb_poll(void)
{
	if(hcd && hcd->poll) {
		hcd->poll();
	}
}

int usb_ring_init(struct usb_ring *r, int trbs)
{
	if(hcd && hcd->ring_init) {
		return hcd->ring_init(r, trbs);
	}
	return -ENODEV;
}

void usb_reset_ep0(int dev)
{
	if(hcd && hcd->reset_ep0) {
		hcd->reset_ep0(dev);
	}
}

int usb_slot_root_port(int dev)
{
	if(!hcd || !hcd->slot_root_port) {
		return 0;
	}
	return hcd->slot_root_port(dev);
}

int usb_slot_speed(int dev)
{
	if(!hcd || !hcd->slot_speed) {
		return 0;
	}
	return hcd->slot_speed(dev);
}

void usb_disable(int dev)
{
	if(hcd && hcd->disable) {
		hcd->disable(dev);
	}
}

int usb_enumerate(int root_port, int route, int speed)
{
	if(!hcd || !hcd->enumerate) {
		return -ENODEV;
	}
	return hcd->enumerate(root_port, route, speed);
}

void usb_init(void)
{
	if(!xhci_probe() || !ehci_probe() || !uhci_probe()) {
		return;
	}
	printk("usb: no host controller found\n");
}
