/*
 * fnx/drivers/usb/usb-kbd.c
 *
 * USB HID boot-protocol keyboard class driver. Runs on the xHCI
 * host controller: parses the config descriptor, configures EP1 IN
 * (interrupt), and delivers 8-byte boot reports as set-1 scancodes
 * into the FNX keyboard pipeline (keymap-driven).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/keyboard.h>
#include <fnx/mm.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/tty.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

#define USB_DIR_IN		0x80
#define USB_DIR_OUT		0x00
#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_DT_CONFIG		0x02
#define USB_CLASS_HID		0x03
#define USB_HID_PROTO_KEYBOARD	0x01

#define HID_REPORT_SIZE		8

/* HID keyboard usages 0x04..0x67 -> set-1 scancode; bit 7 = E0-prefixed */
static const unsigned char hid2sc[0x64] = {
	/* 0x04 a .. 0x1d z */
	0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,
	0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
	0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
	0x15, 0x2C,
	/* 0x1e 1 .. 0x27 0 */
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0A, 0x0B,
	/* 0x28 enter .. 0x38 / */
	0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,
	0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34,
	0x35,
	/* 0x39 caps */
	0x3A,
	/* 0x3a F1 .. 0x45 F12 */
	0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
	0x43, 0x44, 0x57, 0x58,
	/* 0x46 printscreen, 0x47 scrolllock, 0x48 pause */
	0x80 | 0x37, 0x46, 0x00,
	/* 0x49 insert .. 0x52 up */
	0x80 | 0x52, 0x80 | 0x47, 0x80 | 0x49, 0x80 | 0x53,
	0x80 | 0x4F, 0x80 | 0x51, 0x80 | 0x4D, 0x80 | 0x4B,
	0x80 | 0x50, 0x80 | 0x48,
	/* 0x53 numlock, 0x54 kp/, 0x55 kp*, 0x56 kp-, 0x57 kp+, 0x58 kp-enter */
	0x45, 0x80 | 0x35, 0x37, 0x4A, 0x4E, 0x80 | 0x1C,
	/* 0x59 kp1 .. 0x61 kp9, 0x62 kp0, 0x63 kp. */
	0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48,
	0x49, 0x52, 0x53,
	/* 0x64 non-us |, 0x65 app, 0x66/0x67 unused */
	0x56, 0x80 | 0x5D, 0x00, 0x00
};

/* modifier usages 0xE0..0xE7 (sent via the report modifier byte; we also
 * emit press/release scancodes from the 6-key rollover list, so these are
 * only a fallback for keys not in the list) */
static const unsigned char hidmod2sc[8] = {
	0x1D, 0x2A, 0x38, 0x80 | 0x5B,	/* lctrl lshift lalt lgui */
	0x80 | 0x1D, 0x36, 0x80 | 0x38, 0x80 | 0x5C	/* rctrl rshift ralt rgui */
};

struct usb_kbd {
	int slotid;
	int epid;		/* xhci endpoint id (3 = EP1 IN) */
	int mps;
	struct xhci_ring ring;
	unsigned char *buf;	/* 8-byte boot report (kmalloc'd) */
	unsigned char prev[6];
	unsigned char *config;
} kbd;

static void usb_kbd_submit(struct usb_kbd *k)
{
	int i;


	/* fresh report buffer (must be DMA-visible kernel VA) */
	for(i = 0; i < HID_REPORT_SIZE; i++) {
		k->buf[i] = 0;
	}
	xhci_submit(k->slotid, k->epid, 1, k->buf, HID_REPORT_SIZE, &k->ring);
}

/* HID usage -> set-1 scancode (modifiers 0xE0-0xE7 included) */
static unsigned char usb_kbd_hid2sc(unsigned char usage)
{
	if(usage >= 0x04 && usage < 0x68) {
		return hid2sc[usage - 0x04];
	}
	if(usage >= 0xE0 && usage <= 0xE7) {
		return hidmod2sc[usage - 0xE0];
	}
	return 0;
}

/* transfer completion callback (runs from xhci_poll / timer BH) */
static void usb_kbd_cb(int slotid, int epid, int ccode, void *data)
{
	struct usb_kbd *k = &kbd;
	int i, j, sc, changed;



if(slotid != k->slotid || epid != k->epid) {
		return;
	}
	if(ccode != 1 /* CC_SUCCESS */ && ccode != 13 /* CC_SHORT_PACKET */) {
		usb_kbd_submit(k);	/* retry */
		return;
	}

	/* diff: keys in prev but not in buf -> release; in buf not prev -> press */
	for(i = 0; i < 6; i++) {
		if(!k->prev[i]) {
			continue;
		}
		changed = 1;
		for(j = 0; j < 6; j++) {
			if(k->buf[2 + j] == k->prev[i]) {
				changed = 0;
				break;
			}
		}
		if(changed) {
			sc = usb_kbd_hid2sc(k->prev[i]);
			if(sc) {
				kbd_process_scancode((sc & 0x7F) | 0x80, !!(sc & 0x80));
			}
		}
	}
	for(i = 0; i < 6; i++) {
		if(!k->buf[2 + i]) {
			continue;
		}
		changed = 1;
		for(j = 0; j < 6; j++) {
			if(k->prev[j] == k->buf[2 + i]) {
				changed = 0;
				break;
			}
		}
		if(changed) {
			sc = usb_kbd_hid2sc(k->buf[2 + i]);
			if(sc) {
				kbd_process_scancode(sc & 0x7F, !!(sc & 0x80));
			}
		}
	}

	/* save the new key set */
	for(i = 0; i < 6; i++) {
		k->prev[i] = k->buf[2 + i];
	}

usb_kbd_submit(k);
}

/* returns 1 if the config descriptor describes a boot keyboard */
static int usb_kbd_parse_config(struct usb_kbd *k)
{
	unsigned char *c = k->config;
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
				if(iface_class == USB_CLASS_HID &&
				   iface_proto == USB_HID_PROTO_KEYBOARD) {
					break;
				}
			}
		}
		i += c[i];
	}
	if(iface_class != USB_CLASS_HID ||
	   iface_proto != USB_HID_PROTO_KEYBOARD || !ep_addr) {
		return 0;
	}

	/* xHCI EP id: 1=EP0, 2=EP1-OUT, 3=EP1-IN, ... */
	k->epid = ((ep_addr & 0x0F) * 2) + ((ep_addr & 0x80) ? 1 : 0);
	k->mps = ep_mps;
	if(k->mps < 8) {
		k->mps = 8;
	}
	return 1;
}

int usb_kbd_init(int slotid, unsigned char *configdesc)
{
	struct usb_kbd *k = &kbd;
	struct usb_kbd tmp;
	int ret;

	/* parse into a LOCAL first: the probe calls this for every
	 * device (the kbd class check is the first in the chain), so
	 * touching the shared struct before the class match would let a
	 * later device (mouse/storage) clobber a live keyboard's state */
	memset_b(&tmp, 0, sizeof(tmp));
	tmp.config = configdesc;
	if(!usb_kbd_parse_config(&tmp)) {
		return -ENODEV;
	}
	k->slotid = slotid;
	k->epid = tmp.epid;
	k->mps = tmp.mps;
	k->config = configdesc;

	/* SET_CONFIGURATION(1) */
	if((ret = xhci_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0,
			       0, NULL)) < 0) {
		printk("usb-kbd: SET_CONFIGURATION failed (%d)\n", ret);
		return ret;
	}

	/* transfer ring + configure EP1 IN (interrupt) */
	if(xhci_ring_init(&k->ring, 16) < 0) {
		return -ENOMEM;
	}
	if(!(k->buf = (unsigned char *)kmalloc(HID_REPORT_SIZE))) {
		return -ENOMEM;
	}
	if((ret = xhci_configure_ep(slotid, k->epid, XHCI_EP_INTR_IN,
				    k->mps, 9 /* 2^9 * 125us = 64ms */,
				    k->ring.phys)) < 0) {
		printk("usb-kbd: configure EP failed (%d)\n", ret);
		return ret;
	}

	xhci_set_transfer_cb(slotid, k->epid, usb_kbd_cb, NULL);
	memset_b(k->prev, 0, sizeof(k->prev));
	usb_kbd_submit(k);
	printk("usb-kbd: boot keyboard on slot %d (epid %d, mps %d)\n",
		slotid, k->epid, k->mps);
	return 0;
}
