/*
 * fnx/drivers/usb/ohci.c
 *
 * OHCI (Open Host Controller Interface, USB 1.1) host controller driver.
 * Low/full-speed devices only (12/1.5 Mbps). Schedule model: a 256-byte
 * HCCA (32-entry interrupt table + frame/done), a shared ED/TD pool, and
 * the classic three lists: control (EP0 transfers), bulk, and periodic
 * (interrupt endpoints). QEMU processes the lists on a 1ms frame timer.
 *
 * The FNX model (mirrors the UHCI driver): control transfers run a
 * 3-TD chain (SETUP -> DATA -> STATUS) on a per-device EP0 ED queued via
 * HcControlHeadED + the ControlListFilled flag; the guest polls the
 * status TD's condition code. Interrupt endpoints use non-R TDs on
 * periodic EDs in the HCCA's interrupt table; the poll re-arms them.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/types.h>
#include <fnx/usb.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/asm.h>

#define OHCI_MMIO_VA		0xFFFFB40000000000UL	/* pml4[498] */

/* registers (offsets; HcRhPortStatus[0] at 0x54 + 4*port) */
#define OHCI_HCREVISION		0x00
#define OHCI_HCCONTROL		0x04
#define OHCI_HCCOMMANDSTATUS	0x08
#define OHCI_HCINTERRUPTSTATUS	0x0C
#define OHCI_HCINTERRUPTENABLE	0x10
#define OHCI_HCINTERRUPTDISABLE	0x14
#define OHCI_HCHCCA		0x18
#define OHCI_HCPERIODCURRENTED	0x1C
#define OHCI_HCCONTROLHEADED	0x20
#define OHCI_HCCONTROLCURRENTED	0x24
#define OHCI_HCBULKHEADED	0x28
#define OHCI_HCBULKCURRENTED	0x2C
#define OHCI_HCDONEHEAD		0x30
#define OHCI_HCFMINTERVAL	0x34
#define OHCI_HCFMREMAINING	0x38
#define OHCI_HCFMNUMBER		0x3C
#define OHCI_HCPERIODICSTART	0x40
#define OHCI_HCLSTHRESHOLD	0x44
#define OHCI_HCRHDESCRIPTORA	0x48
#define OHCI_HCRHDESCRIPTORB	0x4C
#define OHCI_HCRHSTATUS		0x50
#define OHCI_HCRHPORTSTATUS	0x54

/* HcControl bits */
#define OHCI_CTL_PLE		(1 << 2)
#define OHCI_CTL_CLE		(1 << 4)
#define OHCI_CTL_BLE		(1 << 5)
#define OHCI_CTL_HCFS		(3 << 6)
#define OHCI_CTL_HCFS_RESET	(0 << 6)
#define OHCI_CTL_HCFS_OPER	(2 << 6)

/* HcCommandStatus bits */
#define OHCI_CMD_HCR		(1 << 0)
#define OHCI_CMD_CLF		(1 << 1)
#define OHCI_CMD_BLF		(1 << 2)

/* HcRhPortStatus bits (read) */
#define OHCI_PORT_CCS		(1 << 0)
#define OHCI_PORT_PES		(1 << 1)
#define OHCI_PORT_PSS		(1 << 2)
#define OHCI_PORT_PRS		(1 << 4)
#define OHCI_PORT_PPS		(1 << 8)
#define OHCI_PORT_LSDA		(1 << 9)

/* ED flags */
#define OHCI_ED_FA(v)		((v) << 0)
#define OHCI_ED_EN(v)		((v) << 7)
#define OHCI_ED_MPS(v)		((v) << 16)
#define OHCI_ED_S_LS		(1 << 13)	/* low-speed */
/* ED headp bits */
#define OHCI_ED_H		1		/* halted */
#define OHCI_ED_C		2		/* toggle carry */

/* TD flags */
#define OHCI_TD_R		(1 << 18)	/* read (re-arm) */
#define OHCI_TD_DP_SHIFT	19
#define OHCI_TD_DP_SETUP	(0 << 19)
#define OHCI_TD_DP_OUT		(1 << 19)
#define OHCI_TD_DP_IN		(2 << 19)
#define OHCI_TD_CC_SHIFT	28
#define OHCI_CC_NOTACCESSED	15
#define OHCI_CC_NOERROR		0
#define OHCI_CC_NAK		15		/* not accessed = still pending */

#define OHCI_DPTR_MASK		0xFFFFFFF0

#define OHCI_MAX_DEVS		16
#define OHCI_MAX_EPS		8
#define OHCI_MAX_CB		8

#define OHCI_MAX_PKT		512

struct ohci_ed {
	volatile unsigned int flags;	/* +0 */
	volatile unsigned int tailp;	/* +4 */
	volatile unsigned int headp;	/* +8 */
	volatile unsigned int nexted;	/* +12 */
};

struct ohci_td {
	volatile unsigned int flags;	/* +0 */
	volatile unsigned int cbp;	/* +4 */
	volatile unsigned int nexttd;	/* +8 */
	volatile unsigned int be;	/* +12 */
};

/* Host Controller Communication Area (256 bytes, page-aligned DMA) */
struct ohci_hcca {
	volatile unsigned int intr[32];	/* +0 */
	volatile unsigned short frame;	/* +128 */
	volatile unsigned short pad;	/* +130 */
	volatile unsigned int done;	/* +132 */
};

struct ohci_ep {
	int used;
	int dev;
	int epid;
	int type;
	int mps;
	unsigned long ed_phys;	/* periodic ED in the HCCA interrupt table */
	unsigned long active_td;
	int stride;
};

struct ohci_cb {
	int used;
	int dev;
	int epid;
	void (*fn)(int, int, int, int, void *);
	void *data;
};

static struct ohci_state {
	unsigned char *mmio;
	int present;
	unsigned char irq;
	int nports;
	/* DMA-visible structures (kmalloc = page-aligned) */
	unsigned char *hcca;
	unsigned long hcca_phys;
	unsigned char *pool;
	unsigned long pool_phys;
	int pool_next;
	/* scratch areas (must be DMA-visible kernel VAs) */
	unsigned char setup[8];
	unsigned char status_in[64];
	unsigned char desc18[18];
	unsigned char desc64[64];
	/* the default-address EP0 ED (SET_ADDRESS targets dev 0) */
	unsigned long ep0_ed_phys;
	struct {
		int port;
		int speed;	/* 0=low 1=full */
		unsigned long ed_phys;	/* EP0 ED */
	} devs[OHCI_MAX_DEVS];
	struct ohci_ep eps[OHCI_MAX_EPS];
	struct ohci_cb cbs[OHCI_MAX_CB];
} *o;

static unsigned int ohci_reg_r(unsigned long off)
{
	return *(volatile unsigned int *)(o->mmio + off);
}

static void ohci_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(o->mmio + off) = val;
}

static void *ohci_alloc_slot(void)
{
	void *p;

	if(o->pool_next + 16 > 4096) {
		return NULL;
	}
	p = o->pool + o->pool_next;
	o->pool_next += 16;
	memset_b(p, 0, 16);
	return p;
}

static unsigned long ohci_slot_phys(void *p)
{
	return o->pool_phys + ((unsigned char *)p - o->pool);
}

static unsigned long ohci_build_td(int dp, int len, void *buf,
				   unsigned int next)
{
	struct ohci_td *td;
	unsigned long td_phys;

	td = ohci_alloc_slot();
	if(!td) {
		return 0;
	}
	td_phys = ohci_slot_phys(td);
	td->flags = (unsigned int)dp | (OHCI_CC_NOTACCESSED << OHCI_TD_CC_SHIFT);
	td->cbp = buf ? (unsigned int)V2P((addr_t)buf) : 0;
	td->be = buf ? (unsigned int)V2P((addr_t)buf) + len - 1 : 0;
	td->nexttd = next;
	return td_phys;
}

/* wait for a TD to leave NOTACCESSED (the HC writes the CC at the frame
 * boundary); poll HcFmNumber so TCG exits and the 1ms frame timer runs */
static int ohci_wait_td(unsigned long td_phys, int timeout)
{
	volatile struct ohci_td *td = (volatile struct ohci_td *)P2V(td_phys);

	while(timeout--) {
		if(((td->flags >> OHCI_TD_CC_SHIFT) & 0xF) !=
		   OHCI_CC_NOTACCESSED) {
			return ((td->flags >> OHCI_TD_CC_SHIFT) & 0xF) ==
				OHCI_CC_NOERROR ? 0 : -EIO;
		}
		ohci_reg_r(OHCI_HCFMNUMBER);
	}
	return -ETIMEDOUT;
}

static int ohci_control(int dev, unsigned char bmRequestType,
			unsigned char bRequest, unsigned short wValue,
			unsigned short wIndex, unsigned short wLength,
			void *data)
{
	unsigned long t_setup, t_data = 0, t_status, ed_phys;
	struct ohci_ed *ed;
	int dir_in = (bmRequestType & 0x80) != 0;

	o->setup[0] = bmRequestType;
	o->setup[1] = bRequest;
	o->setup[2] = wValue & 0xFF;
	o->setup[3] = wValue >> 8;
	o->setup[4] = wIndex & 0xFF;
	o->setup[5] = wIndex >> 8;
	o->setup[6] = wLength & 0xFF;
	o->setup[7] = wLength >> 8;

	/* SETUP -> [DATA] -> STATUS -> T */
	if(dir_in) {
		/* control READ: data IN, status OUT */
		t_status = ohci_build_td(OHCI_TD_DP_OUT, 0, NULL, 0);
	} else {
		/* control WRITE: data OUT, status IN */
		t_status = ohci_build_td(OHCI_TD_DP_IN, 0, NULL, 0);
	}
	if(!t_status) {
		return -ENOMEM;
	}
	if(wLength) {
		t_data = ohci_build_td(dir_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT,
				       wLength, data, (unsigned int)t_status);
		if(!t_data) {
			return -ENOMEM;
		}
		t_setup = ohci_build_td(OHCI_TD_DP_SETUP, 8, o->setup,
					(unsigned int)t_data);
	} else {
		t_setup = ohci_build_td(OHCI_TD_DP_SETUP, 8, o->setup,
					(unsigned int)t_status);
	}
	if(!t_setup) {
		return -ENOMEM;
	}

	/* the EP0 ED (dev 0 = the default-address ED; else the device's) */
	if(dev == 0) {
		ed_phys = o->ep0_ed_phys;
	} else {
		ed_phys = o->devs[dev].ed_phys;
	}
	/* the ED's tail is the FIRST UNPROCESSED TD marker: QEMU walks
	 * TDs while (head != tail), so the tail must be the anchor (0),
	 * not the last TD - otherwise the status TD is never processed */
	ed = (struct ohci_ed *)P2V(ed_phys);
	ed->headp = (unsigned int)t_setup;	/* H/C clear */
	ed->tailp = 0;

	/* queue the ED on the control list and fill it */
	ohci_reg_w(OHCI_HCCONTROLHEADED, (unsigned int)ed_phys);
	ohci_reg_w(OHCI_HCCOMMANDSTATUS, OHCI_CMD_CLF);

	return ohci_wait_td(t_status, 20000000);
}

/* synchronous bulk transfer (usb-storage): data TDs on a fresh bulk ED
 * via the bulk list (no status stage) */
static int ohci_transfer(int dev, int epid, int dir_in, void *buf, int len,
			 struct usb_ring *ring)
{
	unsigned long first = 0, prev = 0, qtd_phys, ed_phys;
	struct ohci_ed *ed;
	int epnum = epid >> 1;
	int remaining = len, off = 0, chunk;

	while(remaining > 0) {
		chunk = remaining > OHCI_MAX_PKT ? OHCI_MAX_PKT : remaining;
		qtd_phys = ohci_build_td(dir_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT,
					 chunk, (unsigned char *)buf + off, 0);
		if(!qtd_phys) {
			return -ENOMEM;
		}
		if(prev) {
			((struct ohci_td *)P2V(prev))->nexttd =
				(unsigned int)qtd_phys;
		} else {
			first = qtd_phys;
		}
		prev = qtd_phys;
		off += chunk;
		remaining -= chunk;
	}
	if(!first) {
		return -EINVAL;
	}

	if(!(ed = ohci_alloc_slot())) {
		return -ENOMEM;
	}
	ed_phys = ohci_slot_phys(ed);
	ed->flags = OHCI_ED_FA(dev) | OHCI_ED_EN(epnum) |
		    OHCI_ED_MPS(16);
	ed->headp = (unsigned int)first;
	ed->tailp = 0;	/* anchor: the tail is not processed */
	ed->nexted = 0;

	ohci_reg_w(OHCI_HCBULKHEADED, (unsigned int)ed_phys);
	ohci_reg_w(OHCI_HCCOMMANDSTATUS, OHCI_CMD_BLF);

	return ohci_wait_td(prev, 20000000);
}

/* ---------------- interrupt endpoints ---------------- */

static int ohci_configure_ep(int dev, int epid, int type, int mps,
			     int interval, unsigned long ring_phys)
{
	struct ohci_ep *ep = NULL;
	struct ohci_ed *ed;
	unsigned long ed_phys;
	struct ohci_hcca *hcca;
	int i, stride, f;

	for(i = 0; i < OHCI_MAX_EPS; i++) {
		if(!o->eps[i].used) {
			ep = &o->eps[i];
			break;
		}
	}
	if(!ep) {
		return -ENOMEM;
	}

	if(!(ed = ohci_alloc_slot())) {
		return -ENOMEM;
	}
	ed_phys = ohci_slot_phys(ed);
	ed->flags = OHCI_ED_FA(dev) | OHCI_ED_EN(epid >> 1) |
		    OHCI_ED_MPS(mps) |
		    (o->devs[dev].speed == 0 ? OHCI_ED_S_LS : 0);
	ed->headp = 0;
	ed->tailp = 0;
	ed->nexted = 0;

	ep->used = 1;
	ep->dev = dev;
	ep->epid = epid;
	ep->type = type;
	ep->mps = mps;
	ep->ed_phys = ed_phys;
	ep->active_td = 0;

	if(type == USB_EP_INTR_IN) {
		/* place the ED in the HCCA interrupt table at the stride;
		 * the ED at entry n is polled when (frame & 31) == n */
		stride = 1 << (interval > 3 ? interval - 3 : 0);
		if(stride > 32) {
			stride = 32;
		}
		ep->stride = stride;
		hcca = (struct ohci_hcca *)o->hcca;
		for(f = 0; f < 32; f++) {
			if((f & (stride - 1)) == 0) {
				hcca->intr[f] = (unsigned int)ed_phys;
			}
		}
	}

	printk("ohci: dev %d ep %d (%s, mps %d%s) configured\n",
		dev, epid, type == USB_EP_INTR_IN ? "intr-in" : "bulk",
		mps, type == USB_EP_INTR_IN ? "" : ", async");
	return 0;
}

static int ohci_submit(int dev, int epid, int dir_in, void *buf, int len,
		       struct usb_ring *ring)
{
	struct ohci_ep *ep = NULL;
	struct ohci_ed *ed;
	unsigned long td_phys;
	int i;

	for(i = 0; i < OHCI_MAX_EPS; i++) {
		if(o->eps[i].used && o->eps[i].dev == dev &&
		   o->eps[i].epid == epid) {
			ep = &o->eps[i];
			break;
		}
	}
	if(!ep) {
		return -EINVAL;
	}

	/* one IN TD per outstanding report; the class drivers re-submit
	 * from the completion callback. The TD retires when the data
	 * arrives (NAK leaves the CC NOTACCESSED); the poll re-arms. */
	td_phys = ohci_build_td(dir_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT,
				len, buf, 0);
	if(!td_phys) {
		return -ENOMEM;
	}
	ep->active_td = td_phys;

	ed = (struct ohci_ed *)P2V(ep->ed_phys);
	ed->headp = (unsigned int)td_phys;
	ed->tailp = 0;	/* anchor: the tail is not processed */
	return 0;
}

static void ohci_set_transfer_cb(int dev, int epid,
				 void (*fn)(int, int, int, int, void *),
				 void *data)
{
	int i;

	for(i = 0; i < OHCI_MAX_CB; i++) {
		if(!o->cbs[i].used) {
			o->cbs[i].used = 1;
			o->cbs[i].dev = dev;
			o->cbs[i].epid = epid;
			o->cbs[i].fn = fn;
			o->cbs[i].data = data;
			return;
		}
	}
}

static void ohci_poll(void)
{
	int i, j, len;
	volatile struct ohci_td *td;

	for(i = 0; i < OHCI_MAX_EPS; i++) {
		struct ohci_ep *ep = &o->eps[i];

		if(!ep->used || !ep->active_td ||
		   ep->type != USB_EP_INTR_IN) {
			continue;
		}
		td = (volatile struct ohci_td *)P2V(ep->active_td);
		if(((td->flags >> OHCI_TD_CC_SHIFT) & 0xF) !=
		   OHCI_CC_NOERROR) {
			continue;	/* still pending (NAK) */
		}
		len = ep->mps;
		ep->active_td = 0;
		/* the HC retired the TD (head advanced); re-arm by
		 * restoring the head. The TD keeps the received data. */
		((struct ohci_ed *)P2V(ep->ed_phys))->headp = 0;
		((struct ohci_ed *)P2V(ep->ed_phys))->tailp = 0;
		td->flags = OHCI_TD_DP_IN |
			    (OHCI_CC_NOTACCESSED << OHCI_TD_CC_SHIFT);
		td->cbp = 0;
		td->be = 0;
		for(j = 0; j < OHCI_MAX_CB; j++) {
			if(o->cbs[j].used && o->cbs[j].dev == ep->dev &&
			   o->cbs[j].epid == ep->epid) {
				o->cbs[j].fn(ep->dev, ep->epid, 1, len,
					     o->cbs[j].data);
				break;
			}
		}
	}
}

/* ---------------- vtable stubs (hub support / ring) ---------------- */

static int ohci_ring_init(struct usb_ring *r, int trbs)
{
	memset_b(r, 0, sizeof(*r));
	return 0;
}

static void ohci_reset_ep0(int dev)
{
}

static int ohci_slot_root_port(int dev)
{
	if(dev < 0 || dev >= OHCI_MAX_DEVS || !o->devs[dev].port) {
		return 0;
	}
	return o->devs[dev].port;
}

static int ohci_slot_speed(int dev)
{
	if(dev < 0 || dev >= OHCI_MAX_DEVS) {
		return 0;
	}
	return o->devs[dev].speed;
}

static void ohci_disable(int dev)
{
	int i;

	if(dev < 0 || dev >= OHCI_MAX_DEVS) {
		return;
	}
	for(i = 0; i < OHCI_MAX_EPS; i++) {
		if(o->eps[i].used && o->eps[i].dev == dev) {
			o->eps[i].used = 0;
			o->eps[i].active_td = 0;
		}
	}
	for(i = 0; i < OHCI_MAX_CB; i++) {
		if(o->cbs[i].used && o->cbs[i].dev == dev) {
			o->cbs[i].used = 0;
		}
	}
	o->devs[dev].port = 0;
	o->devs[dev].ed_phys = 0;
}

int ohci_enumerate(int root_port, int route, int speed)
{
	unsigned char *devdesc, *configdesc;
	struct ohci_ed *ed;
	int dev = 1, ret, i;

	/* behind-hub devices work through the same control path (the hub
	 * driver already reset the downstream port) */
	devdesc = o->desc18;
	configdesc = o->desc64;

	for(i = 1; i < OHCI_MAX_DEVS; i++) {
		if(!o->devs[i].port) {
			dev = i;
			break;
		}
	}

	if((ret = ohci_control(0, 0x00, 5 /* SET_ADDRESS */, dev, 0, 0, NULL))) {
		printk("ohci: port %d SET_ADDRESS failed (%d)\n", root_port, ret);
		return ret;
	}
	o->devs[dev].port = root_port;
	o->devs[dev].speed = speed;
	/* the device's EP0 ED (FA = the address, MPS 8) */
	if(!(ed = ohci_alloc_slot())) {
		return -ENOMEM;
	}
	o->devs[dev].ed_phys = ohci_slot_phys(ed);
	ed->flags = OHCI_ED_FA(dev) | OHCI_ED_MPS(8) |
		    (speed == 0 ? OHCI_ED_S_LS : 0);
	ed->headp = 0;
	ed->tailp = 0;
	ed->nexted = 0;
	printk("ohci: dev %d addressed (port %d, speed %d)\n",
		dev, root_port, speed);

	if(!ohci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x01 << 8, 0, 18, devdesc)) {
		printk("ohci: device descriptor: idVendor %x idProduct %x "
			"bcdUSB %x class %x\n",
			devdesc[8] | (devdesc[9] << 8),
			devdesc[10] | (devdesc[11] << 8),
			devdesc[2] | (devdesc[3] << 8), devdesc[4]);
	}

	/* config descriptor: first the 9-byte header for wTotalLength */
	configdesc[0] = configdesc[1] = configdesc[2] = 0;
	if(!ohci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
			 0x02 << 8, 0, 9, configdesc)) {
		int totlen = configdesc[2] | (configdesc[3] << 8);
		if(totlen > 255) {
			totlen = 255;
		}
		if(!ohci_control(dev, 0x80, 6 /* GET_DESCRIPTOR */,
				 0x02 << 8, 0, totlen, configdesc)) {
			if(usb_kbd_init(dev, configdesc) < 0) {
				if(usb_mouse_init(dev, configdesc) < 0) {
					if(usb_storage_init(dev, configdesc) < 0) {
						if(usb_hub_init(dev, configdesc) < 0) {
							usb_net_init(dev, configdesc);
						}
					}
				}
			}
		}
	}
	return dev;
}

/* ---------------- probe ---------------- */

static void ohci_irq_handler(int num, struct sigcontext *sc)
{
	if(o) {
		ohci_reg_w(OHCI_HCINTERRUPTSTATUS, 0xFFFFFFFF);
	}
}

static struct usb_hcd ohci_hcd = {
	.name		= "ohci",
	.control	= ohci_control,
	.configure_ep	= ohci_configure_ep,
	.submit		= ohci_submit,
	.transfer	= ohci_transfer,
	.transfer_zlp	= NULL,
	.set_transfer_cb = ohci_set_transfer_cb,
	.kick_ep	= NULL,
	.poll		= ohci_poll,
	.ring_init	= ohci_ring_init,
	.reset_ep0	= ohci_reset_ep0,
	.slot_root_port	= ohci_slot_root_port,
	.slot_speed	= ohci_slot_speed,
	.disable	= ohci_disable,
	.enumerate	= ohci_enumerate,
};

int ohci_probe(void)
{
	struct pci_device *pd;
	struct ohci_ed *ed;
	unsigned long bar;
	unsigned int i, nports, ps;
	int port, ret;

	/* find an OHCI controller (class 0x0C03, prog-if 0x10) */
	pd = pci_device_table;
	while(pd) {
		if(pd->class == 0x0C03 && pd->prog_if == 0x10) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	if(!(o = (struct ohci_state *)kmalloc(sizeof(struct ohci_state)))) {
		printk("ohci: no memory\n");
		return -ENOMEM;
	}
	memset_b(o, 0, sizeof(*o));
	o->irq = (unsigned char)pd->irq;

	bar = pd->bar[0];
	if(pd->flags[0] & PCI_F_ADDR_MEM_64) {
		bar |= (unsigned long)pd->bar[1] << 32;
	}

	/* map BAR0 */
	for(i = 0; i < 4096 / 4096; i++) {
		if(map_page64(OHCI_MMIO_VA + i * 4096, bar + i * 4096, 0x003)) {
			printk("ohci: unable to map BAR0\n");
			return -ENOMEM;
		}
	}
	o->mmio = (unsigned char *)OHCI_MMIO_VA;

	/* software reset (HCR), then the frame interval etc. */
	ohci_reg_w(OHCI_HCCOMMANDSTATUS, OHCI_CMD_HCR);
	for(i = 0; i < 2000000; i++) {
		if(!(ohci_reg_r(OHCI_HCCOMMANDSTATUS) & OHCI_CMD_HCR)) {
			break;
		}
	}

	if(!(o->hcca = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	memset_b(o->hcca, 0, 4096);
	o->hcca_phys = (unsigned long)V2P((addr_t)o->hcca);

	if(!(o->pool = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	o->pool_phys = (unsigned long)V2P((addr_t)o->pool);

	/* the default-address EP0 ED (MPS 8) */
	if(!(ed = ohci_alloc_slot())) {
		return -ENOMEM;
	}
	o->ep0_ed_phys = ohci_slot_phys(ed);
	ed->flags = OHCI_ED_MPS(8);
	ed->headp = 0;
	ed->tailp = 0;
	ed->nexted = 0;

	/* program the controller: frame interval 0x2EDF (1ms),
	 * periodic start at 90% (the Linux value), LS threshold */
	ohci_reg_w(OHCI_HCFMINTERVAL, 0x2EDF);
	ohci_reg_w(OHCI_HCPERIODICSTART, 0x2A2F);
	ohci_reg_w(OHCI_HCLSTHRESHOLD, 0x628);
	ohci_reg_w(OHCI_HCHCCA, (unsigned int)o->hcca_phys);
	ohci_reg_w(OHCI_HCCONTROL, OHCI_CTL_HCFS_OPER |
		   OHCI_CTL_CLE | OHCI_CTL_PLE);
	for(i = 0; i < 2000000; i++) {
		if((ohci_reg_r(OHCI_HCCONTROL) & OHCI_CTL_HCFS) ==
		   OHCI_CTL_HCFS_OPER) {
			break;
		}
	}

	nports = ohci_reg_r(OHCI_HCRHDESCRIPTORA) & 0xF;
	o->nports = nports;
	printk("ohci: OHCI at %08lx, %d ports, IRQ %d\n", bar, nports, o->irq);

	/* register the INTx */
	{
		static struct interrupt irq_config_ohci = { 0, "ohci", &ohci_irq_handler, NULL };
		if(o->irq) {
			register_irq(o->irq, &irq_config_ohci);
			enable_irq(o->irq);
		}
	}

	o->present = 1;
	usb_hcd_register(&ohci_hcd);

	/* enumerate connected devices */
	for(port = 0; port < nports; port++) {
		unsigned long off = OHCI_HCRHPORTSTATUS + port * 4;
		ps = ohci_reg_r(off);
		if(!(ps & OHCI_PORT_CCS)) {
			continue;
		}
		/* power + reset the port (the reset is immediate in QEMU) */
		ohci_reg_w(off, OHCI_PORT_PPS | OHCI_PORT_PRS);
		for(i = 0; i < 2000000; i++) {
			ohci_reg_r(off);
		}
		ps = ohci_reg_r(off);
		if(!(ps & OHCI_PORT_CCS)) {
			continue;
		}
		ret = (ps & OHCI_PORT_LSDA) ? 0 : 1;
		ohci_enumerate(port + 1, 0, ret);
	}
	return 0;
}
