/*
 * fnx/drivers/usb/usb-storage.c
 *
 * USB mass-storage Bulk-Only Transport (BOT) class driver. Presents the
 * device as a SCSI disk block device (major 8, /dev/sda). Runs on the
 * xHCI host controller: configures the bulk endpoints and issues
 * CBW/data/CSW transfers for READ(10)/WRITE(10).
 *
 * QEMU contract (qemu-10.0.11+ds/hw/usb/dev-storage.c): after a port
 * reset the device sits in CBW mode; EP1 OUT accepts the 31-byte CBW
 * (sig 0x43425355 LE), the data stage follows the direction flag, and
 * the 13-byte CSW (sig 0x53425355) is read back on EP1 IN.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/buffer.h>
#include <fnx/devices.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/mm.h>
#include <fnx/part.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/devices.h>
#include <fnx/types.h>
#include <fnx/xhci.h>

#define USB_DIR_IN		0x80
#define USB_DIR_OUT		0x00
#define USB_REQ_GET_DESCRIPTOR	0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_DT_CONFIG		0x02
#define USB_CLASS_MASS_STORAGE	0x08
#define USB_SUBCLASS_SCSI	0x06
#define USB_PROTO_BOT		0x50

#define CBW_SIG			0x43425355
#define CSW_SIG			0x53425355
#define CBW_LEN			31
#define CSW_LEN			13
#define USB_STORAGE_MAJOR	8

#define SCSI_INQUIRY		0x12
#define SCSI_TEST_UNIT_READY	0x00
#define SCSI_READ_CAPACITY	0x25
#define SCSI_READ_10		0x28
#define SCSI_WRITE_10		0x2A

struct usb_storage {
	int slotid;
	int ep_out;		/* xhci EP id for EP1 OUT (2) */
	int ep_in;		/* xhci EP id for EP1 IN (3) */
	int mps;
	struct xhci_ring ring_out;
	struct xhci_ring ring_in;
	unsigned char *cbw;	/* 31-byte CBW (DMA) */
	unsigned char *csw;	/* 13-byte CSW (DMA) */
	unsigned char *databuf;	/* one-sector data buffer (DMA) */
	unsigned int nr_sects;	/* total sectors */
	unsigned int sector_size;
	unsigned int tag;
} usb_st;

/* ---------------- BOT transport ---------------- */

/* one BOT command: CBW out, optional data, CSW in. Returns 0 on success. */
static int usb_st_bot(int dir_in, unsigned char *cdb, int cdblen,
		      unsigned int data_len, unsigned char *data)
{
	struct usb_storage *s = &usb_st;
	int ret;

	memset_b(s->cbw, 0, CBW_LEN);
	s->cbw[0] = CBW_SIG & 0xFF;
	s->cbw[1] = (CBW_SIG >> 8) & 0xFF;
	s->cbw[2] = (CBW_SIG >> 16) & 0xFF;
	s->cbw[3] = (CBW_SIG >> 24) & 0xFF;
	s->tag++;
	s->cbw[4] = s->tag & 0xFF;
	s->cbw[5] = (s->tag >> 8) & 0xFF;
	s->cbw[6] = (s->tag >> 16) & 0xFF;
	s->cbw[7] = (s->tag >> 24) & 0xFF;
	s->cbw[8] = data_len & 0xFF;
	s->cbw[9] = (data_len >> 8) & 0xFF;
	s->cbw[10] = (data_len >> 16) & 0xFF;
	s->cbw[11] = (data_len >> 24) & 0xFF;
	s->cbw[12] = dir_in ? USB_DIR_IN : USB_DIR_OUT;
	s->cbw[13] = 0;		/* LUN */
	s->cbw[14] = cdblen;
	memcpy_b(&s->cbw[15], cdb, cdblen);

	/* CBW -> bulk OUT */
	if((ret = xhci_transfer(s->slotid, s->ep_out, 0, s->cbw, CBW_LEN,
				&s->ring_out)) < 0) {
		return ret;
	}

	/* data stage */
	if(data_len) {
		if((ret = xhci_transfer(s->slotid, dir_in ? s->ep_in : s->ep_out,
					dir_in, data, data_len,
					dir_in ? &s->ring_in : &s->ring_out)) < 0) {
			return ret;
		}
	}

	/* CSW <- bulk IN */
	if((ret = xhci_transfer(s->slotid, s->ep_in, 1, s->csw, CSW_LEN,
				&s->ring_in)) < 0) {
		return ret;
	}

	if(s->csw[0] != (CSW_SIG & 0xFF) || s->csw[1] != ((CSW_SIG >> 8) & 0xFF) ||
	   s->csw[2] != ((CSW_SIG >> 16) & 0xFF) || s->csw[3] != ((CSW_SIG >> 24) & 0xFF)) {
		return -EIO;
	}
	if(s->csw[12] != 0) {	/* bCSWStatus */
		return -EIO;
	}
	return 0;
}

/* ---------------- SCSI commands ---------------- */

static int usb_st_inquiry(void)
{
	unsigned char cdb[16];

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_INQUIRY;
	cdb[4] = 36;		/* allocation length */
	if(usb_st_bot(1, cdb, 6, 36, usb_st.databuf) < 0) {
		return -EIO;
	}
	return 0;
}

static int usb_st_test_unit_ready(void)
{
	unsigned char cdb[16];

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_TEST_UNIT_READY;
	return usb_st_bot(1, cdb, 6, 0, NULL);
}

static int usb_st_read_capacity(void)
{
	unsigned char cdb[16];
	unsigned char *buf = usb_st.databuf;

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_READ_CAPACITY;
	if(usb_st_bot(1, cdb, 10, 8, buf) < 0) {
		return -EIO;
	}
	usb_st.nr_sects =
		((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]) + 1;
	usb_st.sector_size = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	return 0;
}

static int usb_st_read10(unsigned int lba, int blocks, unsigned char *data)
{
	unsigned char cdb[16];

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_READ_10;
	cdb[2] = (lba >> 24) & 0xFF;
	cdb[3] = (lba >> 16) & 0xFF;
	cdb[4] = (lba >> 8) & 0xFF;
	cdb[5] = lba & 0xFF;
	cdb[7] = (blocks >> 8) & 0xFF;
	cdb[8] = blocks & 0xFF;
	return usb_st_bot(1, cdb, 10, blocks * usb_st.sector_size, data);
}

static int usb_st_write10(unsigned int lba, int blocks, unsigned char *data)
{
	unsigned char cdb[16];

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_WRITE_10;
	cdb[2] = (lba >> 24) & 0xFF;
	cdb[3] = (lba >> 16) & 0xFF;
	cdb[4] = (lba >> 8) & 0xFF;
	cdb[5] = lba & 0xFF;
	cdb[7] = (blocks >> 8) & 0xFF;
	cdb[8] = blocks & 0xFF;
	return usb_st_bot(0, cdb, 10, blocks * usb_st.sector_size, data);
}

/* ---------------- block device interface ---------------- */

static int usb_st_open(struct inode *i, struct fd *f)
{
	return 0;
}

static int usb_st_close(struct inode *i, struct fd *f)
{
	return 0;
}

static int usb_st_ioctl(struct inode *i, struct fd *f, int cmd, unsigned long arg)
{
	return -EINVAL;
}

static __loff_t usb_st_llseek(struct inode *i, __loff_t off)
{
	return -EINVAL;
}

static int usb_st_read(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned int lba = block * (blksize / usb_st.sector_size);
	int blocks = blksize / usb_st.sector_size;

	if(usb_st_read10(lba, blocks, usb_st.databuf) < 0) {
		return -EIO;
	}
	memcpy_b(buffer, usb_st.databuf, blksize);
	return blksize;
}

static int usb_st_write(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned int lba = block * (blksize / usb_st.sector_size);
	int blocks = blksize / usb_st.sector_size;

	memcpy_b(usb_st.databuf, buffer, blksize);
	if(usb_st_write10(lba, blocks, usb_st.databuf) < 0) {
		return -EIO;
	}
	return blksize;
}

static struct fs_operations usb_st_fsop = {
	0,
	0,

	usb_st_open,
	usb_st_close,
	NULL,			/* read */
	NULL,			/* write */
	usb_st_ioctl,
	usb_st_llseek,
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lockup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	NULL,			/* create */
	NULL,			/* rename */

	usb_st_read,
	usb_st_write,

	NULL,			/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* statfs */
	NULL,			/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

static struct device usb_st_device = {
	"usb-storage",
	USB_STORAGE_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&usb_st_fsop,
	NULL,
	NULL,
	NULL
};

/* ---------------- probe ---------------- */

static int usb_st_parse_config(struct usb_storage *s, unsigned char *c)
{
	int len, i, iface_class, iface_sub, iface_proto, ep_out, ep_in, ep_mps;

	if(c[1] != USB_DT_CONFIG) {
		return 0;
	}
	len = c[2] | (c[3] << 8);
	iface_class = iface_sub = iface_proto = 0;
	ep_out = ep_in = ep_mps = 0;
	i = 9;
	while(i + 2 < len) {
		if(c[i + 1] == 4) {	/* interface descriptor */
			iface_class = c[i + 5];
			iface_sub = c[i + 6];
			iface_proto = c[i + 7];
		} else if(c[i + 1] == 5) {	/* endpoint descriptor */
			if((c[i + 3] & 3) == 2) {	/* bulk */
				if(c[i + 2] & 0x80) {
					ep_in = c[i + 2];
				} else {
					ep_out = c[i + 2];
				}
				ep_mps = c[i + 4] | (c[i + 5] << 8);
			}
		}
		i += c[i];
	}
	if(iface_class != USB_CLASS_MASS_STORAGE ||
	   iface_sub != USB_SUBCLASS_SCSI ||
	   iface_proto != USB_PROTO_BOT || !ep_out || !ep_in) {
		return 0;
	}
	s->ep_out = (ep_out & 0x0F) * 2;		/* EP1 OUT -> xhci EP 2 */
	s->ep_in = ((ep_in & 0x0F) * 2) + 1;		/* EP1 IN  -> xhci EP 3 */
	s->mps = ep_mps ? ep_mps : 512;
	return 1;
}

int usb_storage_init(int slotid, unsigned char *configdesc)
{
	struct usb_storage *s = &usb_st;
	struct device *d;
	int ret;

	s->slotid = slotid;
	s->tag = 0;
	if(!usb_st_parse_config(s, configdesc)) {
		return -ENODEV;
	}

	/* SET_CONFIGURATION(1) */
	if((ret = xhci_control(slotid, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0,
			       0, NULL)) < 0) {
		printk("usb-storage: SET_CONFIGURATION failed (%d)\n", ret);
		return ret;
	}

	/* bulk rings + DMA buffers */
	if(xhci_ring_init(&s->ring_out, 16) < 0 ||
	   xhci_ring_init(&s->ring_in, 16) < 0) {
		return -ENOMEM;
	}
	if(!(s->cbw = (unsigned char *)kmalloc(CBW_LEN)) ||
	   !(s->csw = (unsigned char *)kmalloc(CSW_LEN)) ||
	   !(s->databuf = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}

	/* configure bulk OUT (EP2) and bulk IN (EP3) */
	if((ret = xhci_configure_ep(slotid, s->ep_out, XHCI_EP_BULK_OUT,
				    s->mps, 0, s->ring_out.phys)) < 0) {
		printk("usb-storage: configure EP%d failed (%d)\n", s->ep_out, ret);
		return ret;
	}
	if((ret = xhci_configure_ep(slotid, s->ep_in, XHCI_EP_BULK_IN,
				    s->mps, 0, s->ring_in.phys)) < 0) {
		printk("usb-storage: configure EP%d failed (%d)\n", s->ep_in, ret);
		return ret;
	}

	/* SCSI: INQUIRY + READ CAPACITY */
	if(usb_st_inquiry() < 0) {
		printk("usb-storage: INQUIRY failed\n");
		return -EIO;
	}
	if(usb_st_read_capacity() < 0) {
		printk("usb-storage: READ CAPACITY failed\n");
		return -EIO;
	}
	usb_st_test_unit_ready();	/* warm the device up */
	printk("usb-storage: %d sectors of %d bytes (%d MB) on slot %d\n",
		s->nr_sects, s->sector_size, s->nr_sects * s->sector_size / 1048576,
		slotid);

	/* register the block device (major 8 = /dev/sda) */
	SET_MINOR(usb_st_device.minors, 0);	/* /dev/sda */
	if(!(d = get_device(BLK_DEV, MKDEV(USB_STORAGE_MAJOR, 0)))) {
		if(register_device(BLK_DEV, &usb_st_device)) {
			printk("usb-storage: register_device failed\n");
			return -EINVAL;
		}
		if(!(d = get_device(BLK_DEV, MKDEV(USB_STORAGE_MAJOR, 0)))) {
			return -EINVAL;
		}
	}
	/* device_data[minor] = size in KB */
	((unsigned int *)d->device_data)[0] =
		s->nr_sects * s->sector_size / 1024;

	return 0;
}
