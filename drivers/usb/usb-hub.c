/*
 * fnx/drivers/usb/usb-hub.c
 *
 * USB class-9 hub driver. Enumerates an external hub (hub descriptor,
 * port power, port reset), polls its EP1 IN interrupt endpoint for port
 * changes, and (re)enumerates the devices behind the hub's ports as new
 * xHCI slots with the hub-port route string.
 *
 * Real-hardware sequence per port (USB 2.0 spec 11.24):
 *   connect change -> GetPortStatus -> ClearPortFeature(C_PORT_CONNECTION)
 *   -> SetPortFeature(PORT_RESET) -> wait for PORT_STAT_RESET to clear and
 *   PORT_STAT_ENABLE to set -> GetPortStatus (speed is only valid after the
 *   reset) -> enumerate. Ports are powered first if wHubCharacteristics
 *   says the hub has power switching (QEMU's hub is always powered).
 * Port processing runs in a bottom half: the synchronous control transfers
 * cannot run inside the event dispatch, and the change must be cleared
 * before re-arming the EP1 IN (else QEMU completes the re-submitted
 * transfer instantly, flooding the event ring).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/irq.h>
#include <fnx/kernel.h>
#include <fnx/mm.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_CLEAR_FEATURE	0x01
#define USB_REQ_GET_STATUS	0x00
#define USB_REQ_SET_FEATURE	0x03
#define USB_DT_CONFIG		0x02
#define USB_DT_HUB		0x29
#define USB_CLASS_HUB		0x09
#define PORT_POWER		8
#define PORT_RESET		4
#define C_PORT_CONNECTION	16
#define C_PORT_ENABLE		17
#define C_PORT_OVERCURRENT	19
#define C_PORT_RESET		20
#define PORT_STAT_CONNECTION	0x0001
#define PORT_STAT_ENABLE	0x0002
#define PORT_STAT_RESET		0x0010
#define PORT_STAT_LOW_SPEED	0x0200
#define PORT_STAT_HIGH_SPEED	0x0400
#define PORT_STAT_C_OVERCURRENT	0x0008	/* change word bit 4 */
#define HUB_MAX_PORTS		8
#define MAX_HUBS		4

struct usb_hub {
	int in_use;
	int slotid;
	int root_port;
	int is_super;		/* the hub itself is a SuperSpeed hub */
	int epid;		/* xhci EP id (3 = EP1 IN) */
	int mps;
	int nports;
	int power_ports;	/* 1 = hub needs SetPortFeature(PORT_POWER) */
	struct xhci_ring ring;
	unsigned char *buf;	/* change bitmap (2 bytes) */
	unsigned short pending;	/* latched change bitmap for the BH */
	int child_slots[HUB_MAX_PORTS + 1];	/* behind-hub slots, 0 = none */
};

static struct usb_hub hubs[MAX_HUBS];

static void usb_hub_bh_fn(struct sigcontext *sc);	/* fwd */
static struct bh usb_hub_bh = { 0, &usb_hub_bh_fn, NULL };

/* coarse busy-wait (bottom-half context: cannot sleep AND the timer
 * interrupt is disabled there, so a tick-based delay would spin forever;
 * use a raw NOP-count loop like the xhci port-reset wait). Roughly
 * 2e6 spins ~ 10ms on the emulated PIT/APIC. */
static void usb_hub_delay_ms(int ms)
{
	unsigned long n = (unsigned long)ms * 200000;

	while(n--) {
		NOP();
	}
}

static void usb_hub_submit(struct usb_hub *h)
{
	memset_b(h->buf, 0, h->mps);
	xhci_submit(h->slotid, h->epid, 1, h->buf, h->mps, &h->ring);
}

/* decode the xhci speed from a hub port status. The speed bits are only
 * meaningful after the port reset completed. For a SuperSpeed hub the
 * USB2 bits are absent; the speed comes from the port link state (bits
 * 5-8 of wPortStatus). */
static int usb_hub_speed(struct usb_hub *h, unsigned char *st)
{
	if(st[0] & PORT_STAT_HIGH_SPEED) {
		return 2;
	}
	if(st[0] & PORT_STAT_LOW_SPEED) {
		return 0;
	}
	if(h->is_super) {
		/* USB3 port link state: 2 = SS, 4 = SSP */
		unsigned int ls = (st[0] >> 5) | ((st[1] & 0x7) << 3);

		if(ls == 2 || ls == 4) {
			return 3;
		}
	}
	return 1;	/* full speed */
}

/* enumerate (or drop) the device on one hub port */
static void usb_hub_port_event(struct usb_hub *h, int port)
{
	unsigned char *st;
	unsigned long deadline;
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

	/* overcurrent: report + clear, nothing else this pass */
	if(st[2] & PORT_STAT_C_OVERCURRENT) {
		printk("usb-hub: port %d overcurrent\n", port);
		xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			     C_PORT_OVERCURRENT, port, 0, NULL);
		kfree(st);
		return;
	}

	/* ClearPortFeature (class | OTHER recipient) clears the latched
	 * change flags so the EP1 IN stops re-firing. C_ENABLE also latches
	 * on connect/reset and on detach; the guest does not consume it, so
	 * clear it on every pass. On a failure reset EP0 so the hub's
	 * control pipe recovers (a stalled control halts EP0 until the
	 * Reset Endpoint command). */
	if(xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			C_PORT_CONNECTION, port, 0, NULL) < 0 ||
	   xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			C_PORT_ENABLE, port, 0, NULL) < 0) {
		xhci_reset_ep0(h->slotid);
	}

	child = h->child_slots[port];
	if(st[0] & PORT_STAT_CONNECTION) {
		if(child) {
			kfree(st);
			return;		/* already enumerated */
		}

		/* real hardware: reset the downstream port before the
		 * device is usable; the speed is only valid afterwards.
		 * QEMU clears PORT_STAT_RESET instantly and sets ENABLE. */
		xhci_control(h->slotid, 0x23, USB_REQ_SET_FEATURE, PORT_RESET,
			     port, 0, NULL);
		usb_hub_delay_ms(20);
		deadline = 20;	/* ~200ms of polls */
		while(deadline--) {
			usb_hub_delay_ms(10);
			if(xhci_control(h->slotid, 0xA3, USB_REQ_GET_STATUS,
					0, port, 4, st)) {
				kfree(st);
				return;
			}
			if(!(st[0] & PORT_STAT_RESET)) {
				break;	/* reset completed */
			}
		}
		if(st[0] & PORT_STAT_RESET) {
			printk("usb-hub: port %d reset timeout\n", port);
			kfree(st);
			return;
		}
		xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			     C_PORT_RESET, port, 0, NULL);
		xhci_control(h->slotid, 0x23, USB_REQ_CLEAR_FEATURE,
			     C_PORT_ENABLE, port, 0, NULL);

		if(!(st[0] & PORT_STAT_ENABLE)) {
			/* reset failed or the device vanished */
			kfree(st);
			return;
		}
		speed = usb_hub_speed(h, st);
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
	struct usb_hub *h = (struct usb_hub *)data;

	if(slotid != h->slotid || epid != h->epid) {
		return;
	}
	if(ccode != 1 /* CC_SUCCESS */ && ccode != 13 /* CC_SHORT_PACKET */) {
		usb_hub_submit(h);
		return;
	}

	/* latch the change bitmap and defer the (synchronous) port
	 * processing to a bottom half. add_bh() is idempotent, so a second
	 * change while the BH is queued just ORs into h->pending. */
	h->pending = h->buf[0] | (h->buf[1] << 8);
	usb_hub_bh.flags |= BH_ACTIVE;
	add_bh(&usb_hub_bh);
}

/* bottom half: process the latched port changes of every hub, then re-arm */
static void usb_hub_bh_fn(struct sigcontext *sc)
{
	int i, port;

	for(i = 0; i < MAX_HUBS; i++) {
		struct usb_hub *h = &hubs[i];
		int k;

		if(!h->in_use || !h->pending) {
			continue;
		}
		for(port = 1; port <= h->nports; port++) {
			k = port / 8;
			if(h->pending & (1 << (port % 8))) {
				usb_hub_port_event(h, port);
			}
		}
		h->pending = 0;
		usb_hub_submit(h);
	}
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
	struct usb_hub tmp, *h = NULL;
	unsigned char *hubdesc;
	unsigned int wchar;
	int ret, port, i;

	memset_b(&tmp, 0, sizeof(tmp));
	if(!usb_hub_parse_config(&tmp, configdesc)) {
		return -ENODEV;
	}
	for(i = 0; i < MAX_HUBS; i++) {
		if(!hubs[i].in_use) {
			h = &hubs[i];
			break;
		}
	}
	if(!h) {
		printk("usb-hub: too many hubs\n");
		return -ENOMEM;
	}
	memset_b(h, 0, sizeof(*h));
	h->in_use = 1;
	h->slotid = slotid;
	h->epid = tmp.epid;
	h->mps = tmp.mps;

	if((ret = xhci_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0,
			       0, NULL)) < 0) {
		printk("usb-hub: SET_CONFIGURATION failed (%d)\n", ret);
		h->in_use = 0;
		return ret;
	}
	/* hub descriptor (type 0x29): bNbrPorts at byte 2 (DMA buffer!) */
	if(!(hubdesc = (unsigned char *)kmalloc(10))) {
		h->in_use = 0;
		return -ENOMEM;
	}
	if(xhci_control(slotid, 0xA0, USB_REQ_GET_DESCRIPTOR,
			USB_DT_HUB << 8, 0, 10, hubdesc)) {
		printk("usb-hub: GET_DESCRIPTOR(hub) failed\n");
		kfree(hubdesc);
		h->in_use = 0;
		return -EIO;
	}
	h->nports = hubdesc[2];
	if(h->nports > HUB_MAX_PORTS) {
		h->nports = HUB_MAX_PORTS;
	}
	/* wHubCharacteristics bits 1-0: 00 ganged, 01 individual power
	 * switching, 10 = no power switching (QEMU: 0x000A). Only power the
	 * ports when the hub says it switches power. */
	wchar = hubdesc[3] | (hubdesc[4] << 8);
	h->power_ports = (wchar & 0x3) != 0x2;
	kfree(hubdesc);

	if(xhci_ring_init(&h->ring, 16) < 0) {
		h->in_use = 0;
		return -ENOMEM;
	}
	if(!(h->buf = (unsigned char *)kmalloc(h->mps))) {
		h->in_use = 0;
		return -ENOMEM;
	}
	if((ret = xhci_configure_ep(slotid, h->epid, XHCI_EP_INTR_IN,
				    h->mps, 1, h->ring.phys)) < 0) {
		printk("usb-hub: configure EP failed (%d)\n", ret);
		h->in_use = 0;
		return ret;
	}

	if(h->power_ports) {
		for(port = 1; port <= h->nports; port++) {
			xhci_control(slotid, 0x23, USB_REQ_SET_FEATURE,
				     PORT_POWER, port, 0, NULL);
		}
		usb_hub_delay_ms(50);	/* power-on stabilization */
	}

	h->root_port = xhci_slot_root_port(slotid);
	h->is_super = xhci_slot_speed(slotid) == 3;
	h->child_slots[0] = 0;
	xhci_set_transfer_cb(slotid, h->epid, usb_hub_cb, h);
	usb_hub_submit(h);
	printk("usb-hub: %d ports on slot %d (root port %d%s)\n",
		h->nports, slotid, h->root_port,
		h->is_super ? ", superspeed" : "");
	return 0;
}
