/*
 * fnx/drivers/usb/usb-hub.c
 *
 * USB class-9 hub driver. Enumerates an external hub (hub descriptor,
 * port power), polls its EP1 IN interrupt endpoint for port changes,
 * and (re)enumerates the devices behind the hub's ports as new xHCI
 * slots with the hub-port route string.
 *
 * QEMU contract (qemu-10.0.11+ds/hw/usb/dev-hub.c):
 * - iface class 9, EP1 IN interrupt, mps = 1 + ceil(MAX_PORTS/8) = 2,
 *   bInterval 0xff; the interrupt data is a port-change bitmap with
 *   bit N set when port N has a change (NAK when idle).
 * - GetPortStatus (0xA300 | port) returns wPortStatus (CCS=1, ENABLE=2,
 *   LOW_SPEED=0x200, HIGH_SPEED=0x400) + wPortChange (C_CONNECTION=1).
 * - ClearPortFeature (0x2300) with wValue=C_PORT_CONNECTION (16).
 * - The hub's ports are on the USB bus's free list: a plain
 *   `device_add usb-kbd` after `device_add usb-hub` attaches to the
 *   hub's first free port (path "rootport.port").
 * - xhci slots for behind-hub devices carry the route string in slot
 *   context word 0 (5 x 4-bit nibbles) and the root port in word 1;
 *   QEMU's xhci_lookup_uport matches them to the USB port path.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/irq.h>
#include <fnx/mm.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

#define USB_DIR_IN		0x80
#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_CLEAR_FEATURE	0x01
#define USB_REQ_GET_STATUS	0x00
#define USB_DT_CONFIG		0x02
#define USB_DT_HUB		0x29
#define USB_CLASS_HUB		0x09
#define C_PORT_CONNECTION	16
#define PORT_STAT_CONNECTION	0x0001
#define PORT_STAT_LOW_SPEED	0x0200
#define PORT_STAT_HIGH_SPEED	0x0400
#define HUB_MAX_PORTS		8

struct usb_hub {
	int slotid;
	int root_port;
	int epid;		/* xhci EP id (3 = EP1 IN) */
	int mps;
	int nports;
	struct xhci_ring ring;
	unsigned char *buf;	/* change bitmap (2 bytes) */
	unsigned char pending;	/* latched change bitmap for the BH */
	int child_slots[HUB_MAX_PORTS + 1];	/* behind-hub slots, 0 = none */
} hub;

static void usb_hub_bh_fn(struct sigcontext *sc);	/* fwd */
static struct bh usb_hub_bh = { 0, &usb_hub_bh_fn, NULL };

static void usb_hub_submit(struct usb_hub *h)
{
	memset_b(h->buf, 0, h->mps);
	xhci_submit(h->slotid, h->epid, 1, h->buf, h->mps, &h->ring);
}

/* enumerate (or drop) the device on one hub port */
static void usb_hub_port_event(struct usb_hub *h, int port)
{
	unsigned char *st;
	int speed, child, ret;

	/* GetPortStatus: wPortStatus + wPortChange (DMA buffer!) */
	if(!(st = (unsigned char *)kmalloc(4))) {
		return;
	}
	if((ret = xhci_control(h->slotid, 0xA3, USB_REQ_GET_STATUS, 0, port,
			      4, st))) {
		printk("usb-hub: GetPortStatus(%d) failed (%d)\n", port, ret);
		kfree(st);
		return;
	}
	/* ClearPortFeature (class | OTHER recipient) - clears the
	 * connect-change flag so the EP1 IN stops re-firing. On a failure
	 * (e.g. a non-QEMU hub), reset EP0 so the hub's control pipe
	 * recovers for the next request. */
	if(xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			C_PORT_CONNECTION, port, 0, NULL) < 0) {
		xhci_reset_ep0(h->slotid);
	}
	child = h->child_slots[port];
	if(st[0] & PORT_STAT_CONNECTION) {
		if(child) {
			kfree(st);
			return;		/* already enumerated */
		}
		speed = (st[0] & PORT_STAT_HIGH_SPEED) ? 2 :
			(st[0] & PORT_STAT_LOW_SPEED) ? 0 : 1;
		printk("usb-hub: device on port %d (speed %d)\n", port, speed);
		child = xhci_enumerate(h->root_port, port, speed);
		h->child_slots[port] = child;
	} else if(child) {
		printk("usb-hub: device removed from port %d (slot %d)\n",
			port, child);
		xhci_disable_slot(child);
		h->child_slots[port] = 0;
	}
	kfree(st);
}

/* transfer completion callback (runs from xhci_poll / timer BH) */
static void usb_hub_cb(int slotid, int epid, int ccode, void *data)
{
	struct usb_hub *h = &hub;

	if(slotid != h->slotid || epid != h->epid) {
		return;
	}
	if(ccode != 1 /* CC_SUCCESS */ && ccode != 13 /* CC_SHORT_PACKET */) {
		usb_hub_submit(h);
		return;
	}

	/* latch the change bitmap and defer the (synchronous) port
	 * processing to a bottom half: the sync controls cannot run
	 * inside the event dispatch, and the change must be cleared
	 * before the EP1 IN is re-armed (else QEMU completes the
	 * re-submitted transfer instantly, flooding the ring) */
	h->pending = h->buf[0] | (h->buf[1] << 8);
	usb_hub_bh.flags |= BH_ACTIVE;
	add_bh(&usb_hub_bh);
}

/* bottom half: process the latched port changes, then re-arm EP1 IN */
static void usb_hub_bh_fn(struct sigcontext *sc)
{
	struct usb_hub *h = &hub;
	int port, i;

	if(!h->pending) {
		return;
	}
	for(port = 1; port <= h->nports; port++) {
		i = port / 8;
		if(h->pending & (1 << (port % 8))) {
			usb_hub_port_event(h, port);
		}
	}
	h->pending = 0;
	usb_hub_submit(h);
}

/* parse the config: hub interface + EP1 IN interrupt */
static int usb_hub_parse_config(struct usb_hub *h, unsigned char *c)
{
	int len, i, iface_class, ep_addr, ep_mps;

	if(c[1] != USB_DT_CONFIG) {
		return 0;
	}
	len = c[2] | (c[3] << 8);
	iface_class = ep_addr = ep_mps = 0;
	i = 9;
	while(i + 2 < len) {
		if(c[i + 1] == 4) {	/* interface descriptor */
			iface_class = c[i + 5];
		} else if(c[i + 1] == 5) {	/* endpoint descriptor */
			if((c[i + 3] & 3) == 3) {	/* interrupt */
				ep_addr = c[i + 2];
				ep_mps = c[i + 4] | (c[i + 5] << 8);
			}
		}
		i += c[i];
	}
	if(iface_class != USB_CLASS_HUB || !ep_addr) {
		return 0;
	}
	h->epid = ((ep_addr & 0x0F) * 2) + ((ep_addr & 0x80) ? 1 : 0);
	h->mps = ep_mps ? ep_mps : 2;
	return 1;
}

int usb_hub_init(int slotid, unsigned char *configdesc)
{
	struct usb_hub *h = &hub;
	struct usb_hub tmp;
	unsigned char *hubdesc;
	int ret, port;

	/* parse into a LOCAL first (see usb_kbd_init) */
	memset_b(&tmp, 0, sizeof(tmp));
	if(!usb_hub_parse_config(&tmp, configdesc)) {
		return -ENODEV;
	}
	h->slotid = slotid;
	h->epid = tmp.epid;
	h->mps = tmp.mps;

	if((ret = xhci_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0,
			       0, NULL)) < 0) {
		printk("usb-hub: SET_CONFIGURATION failed (%d)\n", ret);
		return ret;
	}
	/* hub descriptor (type 0x29): bNbrPorts at byte 2 (DMA buffer!) */
	if(!(hubdesc = (unsigned char *)kmalloc(10))) {
		return -ENOMEM;
	}
	if(xhci_control(slotid, 0xA0, USB_REQ_GET_DESCRIPTOR,
			USB_DT_HUB << 8, 0, 10, hubdesc)) {
		printk("usb-hub: GET_DESCRIPTOR(hub) failed\n");
		kfree(hubdesc);
		return -EIO;
	}
	h->nports = hubdesc[2];
	kfree(hubdesc);
	if(h->nports > HUB_MAX_PORTS) {
		h->nports = HUB_MAX_PORTS;
	}

	if(xhci_ring_init(&h->ring, 16) < 0) {
		return -ENOMEM;
	}
	if(!(h->buf = (unsigned char *)kmalloc(h->mps))) {
		return -ENOMEM;
	}
	if((ret = xhci_configure_ep(slotid, h->epid, XHCI_EP_INTR_IN,
				    h->mps, 1, h->ring.phys)) < 0) {
		printk("usb-hub: configure EP failed (%d)\n", ret);
		return ret;
	}

	/* note: QEMU hubs are always powered (port_power=false, so a
	 * SetPortFeature(PORT_POWER) request would STALL and halt EP0).
	 * Real hubs with power switching would need it here. */
	h->root_port = xhci_slot_root_port(slotid);
	h->child_slots[0] = 0;
	xhci_set_transfer_cb(slotid, h->epid, usb_hub_cb, NULL);
	usb_hub_submit(h);
	printk("usb-hub: %d ports on slot %d (root port %d)\n",
		h->nports, slotid, h->root_port);
	return 0;
}
