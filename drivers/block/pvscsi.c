/*
 * fnx/drivers/block/pvscsi.c
 *
 * VMware PVSCSI (paravirtual SCSI) driver.
 *
 * QEMU: -device pvscsi -drive file=X,if=none,id=d0 \
 *           -device scsi-hd,drive=d0
 * (pvscsi = VMware PVSCSI 15AD:07C0, class 0x0100, MMIO on BAR0)
 *
 * The guest/host contract is a pair of DMA rings (request + completion)
 * over a shared state page plus an MMIO doorbell (KICK_RW_IO). A request
 * descriptor carries a 16-byte CDB and a single contiguous DMA buffer
 * (no SG list). Completions land in the cmp ring with the context id of
 * the request; the host sets INTR_STATUS.CMPL_0 and raises INTx.
 *
 * Copyright 2018-2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/buffer.h>
#include <fnx/devices.h>
#include <fnx/part.h>
#include <fnx/ioctl.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/ata.h>

#define PVSCSI_VENDOR		0x15AD
#define PVSCSI_DEVICE		0x07C0
#define PVSCSI_MAJOR		8

#define PVSCSI_MMIO_VA		0xFFFFBE8000000000UL	/* pml4[382] */
#define PVSCSI_MMIO_SIZE	0x8000		/* 8 pages */

#define PVSCSI_REG_OFFSET_COMMAND		0x0
#define PVSCSI_REG_OFFSET_COMMAND_DATA		0x4
#define PVSCSI_REG_OFFSET_COMMAND_STATUS	0x8
#define PVSCSI_REG_OFFSET_INTR_STATUS		0x100c
#define PVSCSI_REG_OFFSET_INTR_MASK		0x2010
#define PVSCSI_REG_OFFSET_KICK_NON_RW_IO	0x3014
#define PVSCSI_REG_OFFSET_KICK_RW_IO		0x4018

#define PVSCSI_CMD_ADAPTER_RESET		1
#define PVSCSI_CMD_SETUP_RINGS			3

#define PVSCSI_INTR_CMPL_0			(1 << 0)
#define PVSCSI_COMMAND_PROCESSING_SUCCEEDED	0

#define PVSCSI_SETUP_RINGS_MAX_NUM_PAGES	32
#define PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE	(4096 / 128)
#define PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE	(4096 / 32)

#define PVSCSI_FLAG_CMD_DIR_NONE		(1 << 2)
#define PVSCSI_FLAG_CMD_DIR_TOHOST		(1 << 3)
#define PVSCSI_FLAG_CMD_DIR_TODEVICE		(1 << 4)

#define SCSI_INQUIRY		0x12
#define SCSI_TEST_UNIT_READY	0x00
#define SCSI_READ_CAPACITY	0x25
#define SCSI_READ_10		0x28
#define SCSI_WRITE_10		0x2A

#define PVSCSI_NUM_RINGS		1

struct pvscsi_rings_state {
	unsigned int reqProdIdx;
	unsigned int reqConsIdx;
	unsigned int reqNumEntriesLog2;
	unsigned int cmpProdIdx;
	unsigned int cmpConsIdx;
	unsigned int cmpNumEntriesLog2;
	unsigned char pad[104];
	unsigned int msgProdIdx;
	unsigned int msgConsIdx;
	unsigned int msgNumEntriesLog2;
};

struct pvscsi_cmd_desc_setup_rings {
	unsigned int reqRingNumPages;
	unsigned int cmpRingNumPages;
	unsigned long long ringsStatePPN;
	unsigned long long reqRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
	unsigned long long cmpRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
};

struct pvscsi_ring_req_desc {
	unsigned long long context;
	unsigned long long dataAddr;
	unsigned long long dataLen;
	unsigned long long senseAddr;
	unsigned int senseLen;
	unsigned int flags;
	unsigned char cdb[16];
	unsigned char cdbLen;
	unsigned char lun[8];
	unsigned char tag;
	unsigned char bus;
	unsigned char target;
	unsigned char vcpuHint;
	unsigned char unused[59];
};

struct pvscsi_ring_cmp_desc {
	unsigned long long context;
	unsigned long long dataLen;
	unsigned int senseLen;
	unsigned short int hostStatus;
	unsigned short int scsiStatus;
	unsigned char pad[8];
};

struct pvscsi {
	unsigned long mmio;
	struct device *dev;
	struct partition part[NR_PARTITIONS];
	unsigned char *state;		/* rings state page (4096) */
	unsigned char *req_ring;	/* req ring page (4096) */
	unsigned char *cmp_ring;	/* cmp ring page (4096) */
	unsigned int req_prod;
	unsigned int req_mask;
	unsigned int cmp_cons;
	unsigned int cmp_mask;
	unsigned int nr_sects;
	unsigned int sector_size;
	unsigned int context;
} pvscsi;

static struct fs_operations pvscsi_driver_fsop;

static unsigned int pvscsi_reg_r(unsigned long off)
{
	return *(volatile unsigned int *)(pvscsi.mmio + off);
}

static void pvscsi_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(pvscsi.mmio + off) = val;
}

/* write a command with its descriptor dword-by-dword, wait for processing */
static int pvscsi_cmd(unsigned int cmd, void *desc, unsigned int desc_bytes)
{
	unsigned int *p;
	unsigned long spin;
	unsigned int n;

	pvscsi_reg_w(PVSCSI_REG_OFFSET_COMMAND, cmd);
	p = (unsigned int *)desc;
	for(n = 0; n < desc_bytes / 4; n++) {
		pvscsi_reg_w(PVSCSI_REG_OFFSET_COMMAND_DATA, p[n]);
	}
	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		if(pvscsi_reg_r(PVSCSI_REG_OFFSET_COMMAND_STATUS) ==
		   PVSCSI_COMMAND_PROCESSING_SUCCEEDED) {
			return 0;
		}
	}
	return -EAGAIN;
}

static int pvscsi_setup_rings(void)
{
	struct pvscsi_cmd_desc_setup_rings desc;
	int ret;

	memset_b(&desc, 0, sizeof(desc));
	desc.reqRingNumPages = PVSCSI_NUM_RINGS;
	desc.cmpRingNumPages = PVSCSI_NUM_RINGS;
	desc.ringsStatePPN = V2P((addr_t)pvscsi.state) >> 12;
	desc.reqRingPPNs[0] = V2P((addr_t)pvscsi.req_ring) >> 12;
	desc.cmpRingPPNs[0] = V2P((addr_t)pvscsi.cmp_ring) >> 12;

	if((ret = pvscsi_cmd(PVSCSI_CMD_SETUP_RINGS, &desc, sizeof(desc)))) {
		printk("pvscsi: SETUP_RINGS failed (%d)\n", ret);
		return ret;
	}

	pvscsi.req_prod = 0;
	pvscsi.req_mask = PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE - 1;
	pvscsi.cmp_cons = 0;
	pvscsi.cmp_mask = PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE - 1;

	/* reset the state page's producer/consumer indices */
	((volatile struct pvscsi_rings_state *)pvscsi.state)->reqProdIdx = 0;
	((volatile struct pvscsi_rings_state *)pvscsi.state)->cmpProdIdx = 0;

	return 0;
}

/* submit a CDB and wait for its completion. dir: 0=tohost(read),
 * 1=todevice(write), -1=none. */
static int pvscsi_submit(unsigned char *cdb, int cdb_len, int dir,
			 void *data, unsigned int data_len)
{
	volatile struct pvscsi_rings_state *state;
	struct pvscsi_ring_req_desc *req;
	struct pvscsi_ring_cmp_desc *cmp;
	unsigned long spin;
	unsigned int flags;

	state = (volatile struct pvscsi_rings_state *)pvscsi.state;

	/* wait for a free req slot (single-entry: always free after consume) */
	req = (struct pvscsi_ring_req_desc *)
		(pvscsi.req_ring + (pvscsi.req_prod & pvscsi.req_mask) * 128);
	memset_b((void *)req, 0, 128);

	pvscsi.context++;
	if(!pvscsi.context) {
		pvscsi.context = 1;
	}
	req->context = pvscsi.context;
	req->dataAddr = V2P((addr_t)data);
	req->dataLen = data_len;
	req->senseAddr = 0;
	req->senseLen = 0;
	flags = (dir == 0) ? PVSCSI_FLAG_CMD_DIR_TOHOST :
		(dir == 1) ? PVSCSI_FLAG_CMD_DIR_TODEVICE :
		PVSCSI_FLAG_CMD_DIR_NONE;
	req->flags = flags;
	memcpy_b(req->cdb, cdb, cdb_len);
	req->cdbLen = cdb_len;
	req->bus = 0;
	req->target = 0;
	req->vcpuHint = 0;

	/* publish the request and kick */
	state->reqProdIdx = ++pvscsi.req_prod;
	pvscsi_reg_w(PVSCSI_REG_OFFSET_KICK_RW_IO, 0);

	/* wait for the completion */
	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		if(state->cmpProdIdx != pvscsi.cmp_cons) {
			cmp = (struct pvscsi_ring_cmp_desc *)
				(pvscsi.cmp_ring +
				 (pvscsi.cmp_cons & pvscsi.cmp_mask) * 32);
			pvscsi.cmp_cons++;
			state->cmpConsIdx = pvscsi.cmp_cons;
			if(cmp->context != pvscsi.context) {
				return -EIO;
			}
			if(cmp->hostStatus != 0 /* BTSTAT_SUCCESS */) {
				return -EIO;
			}
			return 0;
		}
	}
	return -EAGAIN;
}

/* ---------------- SCSI commands ---------------- */

static int pvscsi_inquiry(void)
{
	unsigned char cdb[16];
	unsigned char *buf;

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_INQUIRY;
	cdb[4] = 36;		/* allocation length */
	buf = (unsigned char *)kmalloc(36);
	if(!buf) {
		return -ENOMEM;
	}
	if(pvscsi_submit(cdb, 6, 0, buf, 36) < 0) {
		kfree((addr_t)buf);
		return -EIO;
	}
	kfree((addr_t)buf);
	return 0;
}

static int pvscsi_test_unit_ready(void)
{
	unsigned char cdb[16];

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_TEST_UNIT_READY;
	return pvscsi_submit(cdb, 6, -1, NULL, 0);
}

static int pvscsi_read_capacity(void)
{
	unsigned char cdb[16];
	unsigned char *buf;

	memset_b(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_READ_CAPACITY;
	buf = (unsigned char *)kmalloc(8);
	if(!buf) {
		return -ENOMEM;
	}
	if(pvscsi_submit(cdb, 10, 0, buf, 8) < 0) {
		kfree((addr_t)buf);
		return -EIO;
	}
	pvscsi.nr_sects =
		((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]) + 1;
	pvscsi.sector_size =
		(buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	kfree((addr_t)buf);
	return 0;
}

static int pvscsi_read10(unsigned int lba, int blocks, unsigned char *data)
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
	return pvscsi_submit(cdb, 10, 0, data, blocks * pvscsi.sector_size);
}

static int pvscsi_write10(unsigned int lba, int blocks, unsigned char *data)
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
	return pvscsi_submit(cdb, 10, 1, data, blocks * pvscsi.sector_size);
}

/* ---------------- block device interface ---------------- */

static int pvscsi_open(struct inode *i, struct fd *f)
{
	return 0;
}

static int pvscsi_close(struct inode *i, struct fd *f)
{
	return 0;
}

static int pvscsi_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	int n;
	int errno;
	struct device *d;

	if(!(d = get_device(BLK_DEV, i->rdev))) {
		return -ENXIO;
	}
	(void)f;

	switch(cmd) {
		case BLKGETSIZE:
			if((errno = check_user_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned int)))) {
				return errno;
			}
			*(int *)arg = (unsigned int)pvscsi.nr_sects;
			break;
		case BLKBSZGET:
			if((errno = check_user_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned int)))) {
				return errno;
			}
			*(int *)arg = ((unsigned int *)d->blksize)[MINOR(i->rdev)];
			break;
		case BLKRRPART:
			if(MINOR(i->rdev)) {
				return -EINVAL;
			}
			for(n = 0; n < NR_PARTITIONS; n++) {
				CLEAR_MINOR(d->minors, n + 1);
			}
			invalidate_buffers(i->rdev);
			if(!read_msdos_partition(i->rdev, pvscsi.part)) {
				for(n = 0; n < NR_PARTITIONS; n++) {
					if(pvscsi.part[n].type) {
						SET_MINOR(d->minors, n + 1);
						((unsigned int *)d->blksize)[n + 1] = BLKSIZE_1K;
						((unsigned int *)d->device_data)[n + 1] =
							pvscsi.part[n].nr_sects / 2;
					}
				}
			}
			break;
		default:
			return -EINVAL;
			break;
	}

	return 0;
}

static __loff_t pvscsi_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

static int pvscsi_read_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned int lba;
	unsigned int count;
	unsigned int offset;

	lba = block * (blksize / pvscsi.sector_size);
	count = blksize / pvscsi.sector_size;

	if(MINOR(dev) && MINOR(dev) <= NR_PARTITIONS) {
		offset = pvscsi.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	if(pvscsi_read10(lba, count, (unsigned char *)buffer)) {
		return -EIO;
	}
	return blksize;
}

static int pvscsi_write_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned int lba;
	unsigned int count;
	unsigned int offset;

	lba = block * (blksize / pvscsi.sector_size);
	count = blksize / pvscsi.sector_size;

	if(MINOR(dev) && MINOR(dev) <= NR_PARTITIONS) {
		offset = pvscsi.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	if(pvscsi_write10(lba, count, (unsigned char *)buffer)) {
		return -EIO;
	}
	return blksize;
}

static struct fs_operations pvscsi_driver_fsop = {
	0,
	0,

	pvscsi_open,
	pvscsi_close,
	NULL,			/* read */
	NULL,			/* write */
	pvscsi_ioctl,
	pvscsi_llseek,
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

	pvscsi_read_block,
	pvscsi_write_block,

	NULL,			/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* stats */
	NULL,			/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

static struct device pvscsi_device = {
	"pvscsi0",
	PVSCSI_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&pvscsi_driver_fsop,
	NULL,
	NULL
};

/* ---------------- probe ---------------- */

extern int map_page64(unsigned long, unsigned long, unsigned long);

int pvscsi_init(void)
{
	struct pci_device *pd;
	struct device *d;
	unsigned long bar;
	int i, found, ret;

	pvscsi.dev = &pvscsi_device;
	found = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == PVSCSI_VENDOR &&
		   pd->device_id == PVSCSI_DEVICE) {
			found = 1;
			break;
		}
		pd = pd->next;
	}
	if(!found) {
		return 0;
	}

	bar = pd->bar[0] & 0xFFFFFFF0;
	if(!bar) {
		return -ENXIO;
	}

	for(i = 0; i < PVSCSI_MMIO_SIZE / 4096; i++) {
		if(map_page64(PVSCSI_MMIO_VA + i * 4096, bar + i * 4096, 0x003)) {
			printk("pvscsi: unable to map BAR0\n");
			return -ENOMEM;
		}
	}
	pvscsi.mmio = PVSCSI_MMIO_VA;
	pci_write_short(pd, 0x04, 0x0006);	/* MEM | MASTER */

	/* adapter reset */
	if((ret = pvscsi_cmd(PVSCSI_CMD_ADAPTER_RESET, NULL, 0))) {
		printk("pvscsi: ADAPTER_RESET failed (%d)\n", ret);
		return ret;
	}

	/* DMA structures */
	if(!(pvscsi.state = (unsigned char *)kmalloc(4096)) ||
	   !(pvscsi.req_ring = (unsigned char *)kmalloc(4096)) ||
	   !(pvscsi.cmp_ring = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	memset_b(pvscsi.state, 0, 4096);
	memset_b(pvscsi.req_ring, 0, 4096);
	memset_b(pvscsi.cmp_ring, 0, 4096);
	pvscsi.context = 0;

	if((ret = pvscsi_setup_rings())) {
		return ret;
	}

	/* INTR_MASK enable CMPL_0 (INTx) */
	pvscsi_reg_w(PVSCSI_REG_OFFSET_INTR_MASK, PVSCSI_INTR_CMPL_0);
	pvscsi_reg_w(PVSCSI_REG_OFFSET_INTR_STATUS, 0xFFFFFFFF);

	/* probe the disk: INQUIRY, TEST UNIT READY, READ CAPACITY */
	if(pvscsi_inquiry() < 0) {
		printk("pvscsi: INQUIRY failed\n");
		return -EIO;
	}
	if(pvscsi_test_unit_ready() < 0) {
		printk("pvscsi: TEST UNIT READY failed\n");
		return -EIO;
	}
	if(pvscsi_read_capacity() < 0) {
		printk("pvscsi: READ CAPACITY failed\n");
		return -EIO;
	}

	/* register the block device (major 8 = /dev/sda) */
	SET_MINOR(pvscsi_device.minors, 0);	/* /dev/sda */
	if(!(d = get_device(BLK_DEV, MKDEV(PVSCSI_MAJOR, 0)))) {
		if(register_device(BLK_DEV, &pvscsi_device)) {
			printk("pvscsi: register_device failed\n");
			return -EINVAL;
		}
		if(!(d = get_device(BLK_DEV, MKDEV(PVSCSI_MAJOR, 0)))) {
			return -EINVAL;
		}
	}
	((unsigned int *)d->device_data)[0] = pvscsi.nr_sects / 2;

	printk("pvscsi: %d sectors of %d bytes (%d MB)\n",
		pvscsi.nr_sects, pvscsi.sector_size,
		pvscsi.nr_sects * pvscsi.sector_size / 1048576);

	/* register the INTx IRQ */
	{
		static struct interrupt irq_config_pvscsi = { 0, "pvscsi", NULL };
		if(pd->irq) {
			register_irq(pd->irq, &irq_config_pvscsi);
		}
	}

	return 0;
}
