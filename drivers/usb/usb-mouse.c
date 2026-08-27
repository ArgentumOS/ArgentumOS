/*
 * fnx/drivers/usb/usb-mouse.c
 *
 * USB HID boot mouse / tablet class driver. Runs on the xHCI host
 * controller: configures EP1 IN (interrupt) and delivers pointer
 * reports to the PS/2 mouse device (/dev/psaux) as synthesized
 * 3-byte PS/2 packets, so existing userland mouse consumers work
 * unchanged.
 *
 * QEMU contract (qemu-10.0.11+ds/hw/usb/dev-hid.c):
 * - usb-mouse:  interface proto 0x02, EP1 IN, report = 4 bytes
 *   [buttons, X delta (s8), Y delta (s8), wheel (s8)].
 * - usb-tablet: interface proto 0x00, EP1 IN, report = 8 bytes
 *   [buttons, pad, X (u16 LE), Y (u16 LE), wheel, pad] - absolute.
 * PS/2 packet: [Ysign|Xsign|0x08|buttons, X, -Y] (Y up = positive).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/psaux.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

#define USB_DIR_IN		0x80
#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_DT_CONFIG		0x02
#define USB_CLASS_HID		0x03
#define USB_HID_PROTO_MOUSE	0x02
#define USB_HID_PROTO_TABLET	0x00

struct usb_mouse {
	int slotid;
	int epid;		/* xhci EP id (3 = EP1 IN) */
	int mps;
	int tablet;		/* 1 = absolute 8-byte report */
	struct xhci_ring ring;
	unsigned char *buf;
	int last_x, last_y;
} mouse;

static void usb_mouse_submit(struct usb_mouse *m)
{
	memset_b(m->buf, 0, m->mps);
	xhci_submit(m->slotid, m->epid, 1, m->buf, m->mps, &m->ring);
}

/* transfer completion callback (runs from xhci_poll / timer BH) */
static void usb_mouse_cb(int slotid, int epid, int ccode, void *data)
{
	struct usb_mouse *m = &mouse;
	unsigned char pkt[3];
	int buttons, dx, dy;

	if(slotid != m->slotid || epid != m->epid) {
		return;
	}
	if(ccode != 1 /* CC_SUCCESS */ && ccode != 13 /* CC_SHORT_PACKET */) {
		usb_mouse_submit(m);
		return;
	}

	if(m->tablet) {
		int x = m->buf[2] | (m->buf[3] << 8);
		int y = m->buf[4] | (m->buf[5] << 8);
		buttons = m->buf[0] & 7;
		dx = x - m->last_x;
		dy = y - m->last_y;
		m->last_x = x;
		m->last_y = y;
	} else {
		buttons = m->buf[0] & 7;
		dx = (signed char)m->buf[1];
		dy = (signed char)m->buf[2];
	}

	/* PS/2 packet: Y up = positive, so negate the HID Y delta */
	{
		int xd = dx;
		int yd = -dy;

		pkt[0] = 0x08 |
			 ((yd & 0x80) ? 0x20 : 0) |
			 ((xd & 0x80) ? 0x10 : 0) |
			 (buttons & 0x07);
		pkt[1] = xd & 0xFF;
		pkt[2] = yd & 0xFF;
	}
	psaux_synth_packet(pkt, 3);

	usb_mouse_submit(m);
}

/* returns 1 if the config descriptor describes a HID pointer */
static int usb_mouse_parse_config(struct usb_mouse *m, unsigned char *c)
{
	int len, i, iface_class, iface_proto, ep_addr, ep_mps;

	if(c[1] != USB_DT_CONFIG) {
		return 0;
	}
	len = c[2] | (c[3] << 8);
	iface_class = iface_proto = 0;
	ep_addr = ep_mps = 0;
	i = 9;
	while(i + 2 < len) {
		if(c[i + 1] == 4) {	/* interface descriptor */
			iface_class = c[i + 5];
			iface_proto = c[i + 7];
		} else if(c[i + 1] == 5) {	/* endpoint descriptor */
			if((c[i + 3] & 3) == 3) {	/* interrupt */
				ep_addr = c[i + 2];
				ep_mps = c[i + 4] | (c[i + 5] << 8);
			}
		}
		i += c[i];
	}
	if(iface_class != USB_CLASS_HID ||
	   (iface_proto != USB_HID_PROTO_MOUSE && iface_proto != USB_HID_PROTO_TABLET) ||
	   !ep_addr) {
		return 0;
	}
	m->epid = ((ep_addr & 0x0F) * 2) + ((ep_addr & 0x80) ? 1 : 0);
	m->mps = ep_mps ? ep_mps : 4;
	m->tablet = (iface_proto == USB_HID_PROTO_TABLET);
	if(m->mps < (m->tablet ? 8 : 4)) {
		m->mps = m->tablet ? 8 : 4;
	}
	return 1;
}

int usb_mouse_init(int slotid, unsigned char *configdesc)
{
	struct usb_mouse *m = &mouse;
	struct usb_mouse tmp;
	int ret;

	/* parse into a LOCAL first (see usb_kbd_init) */
	memset_b(&tmp, 0, sizeof(tmp));
	if(!usb_mouse_parse_config(&tmp, configdesc)) {
		return -ENODEV;
	}
	m->slotid = slotid;
	m->epid = tmp.epid;
	m->mps = tmp.mps;
	m->tablet = tmp.tablet;
	m->last_x = m->last_y = 0;

	if((ret = xhci_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0,
			       0, NULL)) < 0) {
		printk("usb-mouse: SET_CONFIGURATION failed (%d)\n", ret);
		return ret;
	}

	if(xhci_ring_init(&m->ring, 16) < 0) {
		return -ENOMEM;
	}
	if(!(m->buf = (unsigned char *)kmalloc(m->mps))) {
		return -ENOMEM;
	}
	if((ret = xhci_configure_ep(slotid, m->epid, XHCI_EP_INTR_IN,
				    m->mps, 1, m->ring.phys)) < 0) {
		printk("usb-mouse: configure EP failed (%d)\n", ret);
		return ret;
	}

	xhci_set_transfer_cb(slotid, m->epid, usb_mouse_cb, NULL);
	usb_mouse_submit(m);
	printk("usb-mouse: %s on slot %d (epid %d, mps %d)\n",
		m->tablet ? "tablet" : "mouse", slotid, m->epid, m->mps);
	return 0;
}
