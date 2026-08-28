/*
 * fnx/drivers/usb/usb-net.c
 *
 * USB CDC-ECM network adapter (QEMU's "usb-net": the Ethernet/RNDIS
 * gadget, vendor 0x0525 product 0xa4a2, device class 0x02). Registers as
 * the FNX ext_* NIC when no PCI NIC is configured.
 *
 * QEMU contract (qemu-10.0.11+ds/hw/usb/dev-network.c):
 * - TWO configurations: 2 = RNDIS (the default listed first!), 1 = CDC.
 *   The guest MUST SetConfiguration(1) to get the plain CDC-ECM.
 * - CDC config: iface 0 = CDC control (class 0x02, subclass 0x06
 *   Ethernet) with the class descriptors (Header/Union/Ethernet - the
 *   Ethernet descriptor's iMACAddress = string 3 = "400102030405");
 *   iface 1 = CDC data (class 0x0A) with alt 0 (0 EPs) and alt 1
 *   (bulk IN 0x82 + bulk OUT 0x02, mps 64). The guest MUST
 *   SetInterface(1, 1) to activate the bulk EPs.
 * - TX (bulk OUT): a transfer flushes the frame when its size is NOT a
 *   multiple of 64 (or is zero). A 64-multiple frame is buffered and
 *   needs a trailing zero-length transfer to flush it.
 * - RX (bulk IN): completes with one ethernet frame; NAK when idle.
 * - The link is up by default; the interrupt-in (0x81) status EP is
 *   ignored (the ioctl is a no-op like the PCI NICs).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/net/ext_net.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/usb.h>

#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE	0x0B
#define USB_DT_CONFIG		0x02
#define USB_DT_STRING		0x03
#define USB_CLASS_CDC		0x02
#define USB_CLASS_CDC_DATA	0x0A
#define CDC_CONFIG_VALUE	1
#define CDC_DATA_IFACE		1
#define CDC_DATA_ALT		1
#define USB_NET_MTU		1518
#define USB_NET_MPS		64
#define USB_NET_RX_TRBS		16
#define USB_NET_TX_TRBS		16

struct usb_net {
	int slotid;
	int in_epid;	/* 5 = EP2 IN (bulk) */
	int out_epid;	/* 4 = EP2 OUT (bulk) */
	int mps;
	struct usb_ring in_ring, out_ring;
	unsigned char *rxbuf;	/* DMA buffer for one RX frame */
	unsigned char *txbuf;	/* DMA buffer for one TX frame (the
				 * network stack hands us stack frames) */
	int rx_len;
	int rx_pending;
	unsigned int rx_wait;	/* wakeup key for the recvfrom sleepers */
	unsigned char mac[6];
	struct ext_net_ops ops;
	int present;
};

static struct usb_net unet;

/* ---------------- RX ---------------- */

static void usb_net_rx_arm(void)
{
	memset_b(unet.rxbuf, 0, USB_NET_MTU);
	usb_submit(unet.slotid, unet.in_epid, 1, unet.rxbuf, USB_NET_MTU,
		    &unet.in_ring);
}

static void usb_net_rx_cb(int slotid, int epid, int ccode, int length,
			  void *data)
{
	(void)data;
	if(slotid != unet.slotid || epid != unet.in_epid) {
		return;
	}
	if(ccode != 1 /* CC_SUCCESS */ && ccode != 13 /* CC_SHORT_PACKET */) {
		usb_net_rx_arm();
		return;
	}
	/* the event's length field is the RESIDUAL (bytes NOT transferred):
	 * 0 on CC_SUCCESS (full 1518-byte frame), >0 on CC_SHORT_PACKET */
	unet.rx_len = (ccode == 13) ? (USB_NET_MTU - length) : USB_NET_MTU;
	if(unet.rx_len <= 0) {
		usb_net_rx_arm();
		return;
	}
	unet.rx_pending = 1;
	wakeup(&unet.rx_wait);
	wakeup(&do_select);
	/* do NOT re-arm here: recvfrom() re-arms after copying the frame */
}

static int usb_net_recvfrom(void *buffer, __size_t count)
{
	unsigned int flags;
	int woken;

	for(;;) {
		/* the RX callback runs from the timer BH (IRQs on): make the
		 * rx_pending check + sleep-hash insertion atomic against it,
		 * otherwise a frame completing in that window wakes nobody
		 * and the single-shot RX is never re-armed -> lost frame */
		SAVE_FLAGS(flags);
		CLI();
		if(unet.rx_pending) {
			RESTORE_FLAGS(flags);
			if(unet.rx_len > (int)count) {
				return -EMSGSIZE;
			}
			memcpy_b(buffer, unet.rxbuf, unet.rx_len);
			unet.rx_pending = 0;
			usb_net_rx_arm();
			return unet.rx_len;
		}
		/* sleep() saves/restores flags itself (restoring to the
		 * CLI'd state here); the hash insertion happens under CLI */
		woken = sleep(&unet.rx_wait, PROC_INTERRUPTIBLE);
		RESTORE_FLAGS(flags);
		if(!woken && unet.rx_pending == 0) {
			return -EINTR;
		}
	}
}

static int usb_net_poll(void)
{
	return unet.rx_pending;
}

/* ---------------- TX ---------------- */

static int usb_net_tx_send(const void *frame, unsigned int len)
{
	int ret;

	if(!unet.present) {
		return -ENODEV;
	}
	if(len > USB_NET_MTU) {
		return -EMSGSIZE;
	}
	/* the caller's frame may live on the kernel stack: copy into the
	 * driver-owned DMA buffer (V2P of a stack address is garbage) */
	memcpy_b(unet.txbuf, frame, len);
	/* a transfer that is a 64-byte multiple is buffered by QEMU and
	 * must be flushed with a trailing zero-length transfer */
	if((len % unet.mps) == 0) {
		if((ret = usb_transfer(unet.slotid, unet.out_epid, 0,
					unet.txbuf, len, &unet.out_ring))) {
			return ret;
		}
		if((ret = usb_transfer_zlp(unet.slotid, unet.out_epid,
					   &unet.out_ring)) < 0) {
			return ret;
		}
		return len;
	}
	if((ret = usb_transfer(unet.slotid, unet.out_epid, 0,
				unet.txbuf, len, &unet.out_ring))) {
		return ret;
	}
	return len;
}

/* ---------------- ext_net_ops wrappers ---------------- */

static int usb_net_ext_open(int fd, int flags, int mode)
{
	(void)fd; (void)flags; (void)mode;
	return 0;
}
static int usb_net_ext_close(int fd) { (void)fd; return 0; }
static int usb_net_ext_bind(int fd, const struct sockaddr *addr, int len)
{
	(void)fd; (void)addr; (void)len;
	return -EOPNOTSUPP;
}
static int usb_net_ext_listen(int fd, int backlog) { (void)fd; (void)backlog; return -EOPNOTSUPP; }
static int usb_net_ext_connect(int fd, const struct sockaddr *addr, int len)
{
	(void)fd; (void)addr; (void)len;
	return -EOPNOTSUPP;
}
static int usb_net_ext_accept(int fd, struct sockaddr *addr, unsigned int *len)
{
	(void)fd; (void)addr; (void)len;
	return -EOPNOTSUPP;
}
static int usb_net_ext_sendto(int fd, const void *buf, __size_t len,
			      const struct sockaddr *addr, int alen)
{
	(void)fd; (void)addr; (void)alen;
	return usb_net_tx_send(buf, len);
}
static int usb_net_ext_recvfrom(int fd, void *buf, __size_t len,
				  struct sockaddr *addr, int *alen)
{
	(void)fd; (void)addr; (void)alen;
	return usb_net_recvfrom(buf, len);
}
static int usb_net_ext_read(int fd, void *buf, __size_t len)
{
	(void)fd;
	return usb_net_recvfrom(buf, len);
}
static int usb_net_ext_write(int fd, const void *buf, __size_t len)
{
	(void)fd;
	return usb_net_tx_send(buf, len);
}
static int usb_net_ext_poll(int fd, int flag)
{
	(void)fd; (void)flag;
	return usb_net_poll();
}
static int usb_net_ext_ioctl(int fd, int request, void *arg)
{
	(void)fd;
	return -EOPNOTSUPP;
}

/* ---------------- config parsing ---------------- */

/* returns 1 + fills the endpoints when the config is a CDC-ECM */
static int usb_net_parse_config(struct usb_net *n, unsigned char *c,
				int *macstr)
{
	int len, i, cdc_if, data_if, ep_in, ep_out;

	if(c[1] != USB_DT_CONFIG) {
		return 0;
	}
	len = c[2] | (c[3] << 8);
	cdc_if = data_if = ep_in = ep_out = 0;
	*macstr = 0;
	i = 9;
	while(i + 2 < len) {
		int dlen = c[i], dtype = c[i + 1];

		if(dtype == 4) {	/* interface descriptor */
			if(c[i + 5] == USB_CLASS_CDC) {
				cdc_if = 1;
			} else if(c[i + 5] == USB_CLASS_CDC_DATA) {
				data_if = 1;
			}
		} else if(dtype == 0x24) {	/* CDC class descriptor */
			if(c[i + 2] == 0x0F && cdc_if) {
				/* Ethernet functional descriptor:
				 * bLength bDescriptorType bDescriptorSubtype
				 * iMACAddress ... */
				*macstr = c[i + 3];
			}
		} else if(dtype == 5) {	/* endpoint descriptor */
			if((c[i + 3] & 3) == 2) {	/* bulk */
				if(c[i + 2] & 0x80) {
					ep_in = c[i + 2];
				} else {
					ep_out = c[i + 2];
				}
			}
		}
		if(dlen < 2) {
			return 0;
		}
		i += dlen;
	}
	if(!cdc_if || !data_if || !ep_in || !ep_out) {
		return 0;
	}
	n->in_epid = ((ep_in & 0x0F) * 2) + 1;	/* EP2 IN = 5 */
	n->out_epid = (ep_out & 0x0F) * 2;	/* EP2 OUT = 4 */
	return 1;
}

/* the MAC: the Ethernet descriptor points at a string of hex digits */
static int usb_net_get_mac(int slotid, int macstr)
{
	unsigned char *buf;
	int i;

	if(!(buf = (unsigned char *)kmalloc(64))) {
		return -ENOMEM;
	}
	if(usb_control(slotid, 0x80, USB_REQ_GET_DESCRIPTOR,
			(USB_DT_STRING << 8) | macstr, 0, 64, buf)) {
		printk("usb-net: string descriptor failed\n");
		kfree((addr_t)buf);
		return -EIO;
	}
	/* the string data is UTF-16LE: digits only */
	for(i = 0; i < 12 && (2 + 2 * i) < buf[0]; i++) {
		unsigned char ch = buf[2 + 2 * i];

		if(ch >= '0' && ch <= '9') {
			unet.mac[i / 2] = (i % 2) ?
				(unet.mac[i / 2] << 4) | (ch - '0') :
				(ch - '0');
		} else if(ch >= 'a' && ch <= 'f') {
			unet.mac[i / 2] = (i % 2) ?
				(unet.mac[i / 2] << 4) | (ch - 'a' + 10) :
				(ch - 'a' + 10);
		}
	}
	kfree((addr_t)buf);
	return 0;
}

int usb_net_init(int slotid, unsigned char *configdesc)
{
	struct usb_net tmp;
	unsigned char *hdr, *cfg;
	int cfg_len, macstr, ret;

	(void)configdesc;
	memset_b(&tmp, 0, sizeof(tmp));
	/* QEMU lists the RNDIS config first (index 0, 67 bytes): the CDC
	 * config is index 1 (80 bytes) and may exceed the 64-byte one-shot
	 * the enumerator read. Get its header for the length, then the
	 * full config, before parsing. (kmalloc'd buffers only: the
	 * control-transfer DMA can't target the kernel stack.) */
	if(!(hdr = (unsigned char *)kmalloc(8))) {
		return -ENOMEM;
	}
	if((ret = usb_control(slotid, 0x80, USB_REQ_GET_DESCRIPTOR,
				(USB_DT_CONFIG << 8) | 1, 0, 8, hdr))) {
		printk("usb-net: config#1 header failed (%d)\n", ret);
		kfree((addr_t)hdr);
		return ret;
	}
	cfg_len = hdr[2] | (hdr[3] << 8);
	kfree((addr_t)hdr);
	if(cfg_len < 9 || cfg_len > 256 ||
	   !(cfg = (unsigned char *)kmalloc(cfg_len))) {
		return -ENOMEM;
	}
	if((ret = usb_control(slotid, 0x80, USB_REQ_GET_DESCRIPTOR,
				(USB_DT_CONFIG << 8) | 1, 0, cfg_len, cfg))) {
		printk("usb-net: config#1 re-GET failed (%d)\n", ret);
		kfree((addr_t)cfg);
		return ret;
	}
	if(!usb_net_parse_config(&tmp, cfg, &macstr)) {
		kfree((addr_t)cfg);
		return -ENODEV;
	}
	if(!macstr) {
		kfree((addr_t)cfg);
		return -ENODEV;
	}
	kfree((addr_t)cfg);

	memset_b(&unet, 0, sizeof(unet));
	unet.slotid = slotid;
	unet.in_epid = tmp.in_epid;
	unet.out_epid = tmp.out_epid;
	unet.mps = USB_NET_MPS;

	/* QEMU's default config is the RNDIS (2): select the CDC (1) */
	if((ret = usb_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION,
			       CDC_CONFIG_VALUE, 0, 0, NULL)) < 0) {
		printk("usb-net: SetConfiguration failed (%d)\n", ret);
		return ret;
	}
	/* the data interface alt 1 carries the bulk endpoints */
	if((ret = usb_control(slotid, 0x01, USB_REQ_SET_INTERFACE,
			       CDC_DATA_ALT, CDC_DATA_IFACE, 0, NULL)) < 0) {
		printk("usb-net: SetInterface failed (%d)\n", ret);
		return ret;
	}
	if((ret = usb_net_get_mac(slotid, macstr)) < 0) {
		return ret;
	}

	if(usb_ring_init(&unet.in_ring, USB_NET_RX_TRBS) < 0 ||
	   usb_ring_init(&unet.out_ring, USB_NET_TX_TRBS) < 0) {
		if(unet.in_ring.trbs) {
			kfree((addr_t)unet.in_ring.trbs);
		}
		if(unet.out_ring.trbs) {
			kfree((addr_t)unet.out_ring.trbs);
		}
		return -ENOMEM;
	}
	if(!(unet.rxbuf = (unsigned char *)kmalloc(USB_NET_MTU))) {
		kfree((addr_t)unet.in_ring.trbs);
		kfree((addr_t)unet.out_ring.trbs);
		return -ENOMEM;
	}
	if(!(unet.txbuf = (unsigned char *)kmalloc(USB_NET_MTU))) {
		kfree((addr_t)unet.rxbuf);
		kfree((addr_t)unet.in_ring.trbs);
		kfree((addr_t)unet.out_ring.trbs);
		return -ENOMEM;
	}
	if((ret = usb_configure_ep(slotid, unet.in_epid, USB_EP_BULK_IN,
				    unet.mps, 0, unet.in_ring.phys)) < 0 ||
	   (ret = usb_configure_ep(slotid, unet.out_epid, USB_EP_BULK_OUT,
				    unet.mps, 0, unet.out_ring.phys)) < 0) {
		printk("usb-net: configure EP failed (%d)\n", ret);
		kfree((addr_t)unet.rxbuf);
		kfree((addr_t)unet.txbuf);
		kfree((addr_t)unet.in_ring.trbs);
		kfree((addr_t)unet.out_ring.trbs);
		return ret;
	}

	usb_set_transfer_cb(slotid, unet.in_epid, usb_net_rx_cb, NULL);
	unet.present = 1;
	usb_net_rx_arm();

	/* the ext_net_ops table (same layout as the PCI NICs) */
	unet.ops.open = usb_net_ext_open;
	unet.ops.close = usb_net_ext_close;
	unet.ops.bind = usb_net_ext_bind;
	unet.ops.listen = usb_net_ext_listen;
	unet.ops.connect = usb_net_ext_connect;
	unet.ops.accept = usb_net_ext_accept;
	unet.ops.ioctl = usb_net_ext_ioctl;
	unet.ops.sendto = usb_net_ext_sendto;
	unet.ops.recvfrom = usb_net_ext_recvfrom;
	unet.ops.read = usb_net_ext_read;
	unet.ops.write = usb_net_ext_write;
	unet.ops.poll = usb_net_ext_poll;
	memcpy_b(unet.ops.mac, unet.mac, 6);

	ret = ext_net_register_nic(&unet.ops);
	printk("usb-net: CDC-ECM on slot %d (%02x:%02x:%02x:%02x:%02x:%02x)%s\n",
		slotid, unet.mac[0], unet.mac[1], unet.mac[2],
		unet.mac[3], unet.mac[4], unet.mac[5],
		ret ? " (NIC already configured)" : "");
	return 0;
}
