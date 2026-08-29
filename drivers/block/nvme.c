/*
 * fnx/drivers/block/nvme.c
 *
 * NVMe (NVM Express) PCIe SSD driver.
 *
 * QEMU: -drive file=X,if=none,id=d0 -device nvme,serial=fnx,drive=d0
 * (nvme = Intel 8086:5845 or RedHat 1B36:0010, class 0x0108, BAR0 64-bit)
 *
 * Implemented subset: a single admin queue pair (SQ0/CQ0) plus a single
 * I/O queue pair (SQ1/CQ1), polled completion (no MSI-X / no IRQ-driven
 * block path; the block layer calls read/write_block with interrupts
 * disabled, so completions are spin-polled via the CQ phase tag).
 *
 * Copyright 2018-2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
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

#define NVME_VENDOR_INTEL	0x8086
#define NVME_DEVICE_INTEL	0x5845
#define NVME_VENDOR_REDHAT	0x1B36
#define NVME_DEVICE_REDHAT	0x0010
#define NVME_MAJOR		9
#define NVME_MINOR_DISK		0	/* /dev/nvme0n1 */

#define NVME_MMIO_VA		0xFFFFBD8000000000UL	/* pml4[379] */
#define NVME_MMIO_SIZE		0x4000

/* BAR0 registers */
#define NVME_REG_CAP		0x0000	/* 64-bit */
#define NVME_REG_VS		0x0008
#define NVME_REG_INTMS		0x000C
#define NVME_REG_INTMC		0x0010
#define NVME_REG_CC		0x0014
#define NVME_REG_CSTS		0x001C
#define NVME_REG_NSSR		0x0020
#define NVME_REG_AQA		0x0024
#define NVME_REG_ASQ		0x0028	/* 64-bit */
#define NVME_REG_ACQ		0x0030	/* 64-bit */

#define NVME_DBS			0x1000	/* doorbell base */

/* CAP bits */
#define NVME_CAP_MQES_MASK	0x000000000000FFFF
#define NVME_CAP_DSTRD_SHIFT	32
#define NVME_CAP_DSTRD_MASK	0x0000000F00000000
#define NVME_CAP_TO_SHIFT	24
#define NVME_CAP_TO_MASK	0x000000FF000000
#define NVME_CAP_MPSMIN_SHIFT	48

/* CC bits */
#define NVME_CC_EN		0x00000001
#define NVME_CC_CSS_SHIFT	4
#define NVME_CC_MPS_SHIFT	7
#define NVME_CC_IOSQES_SHIFT	16	/* log2(SQ entry size) */
#define NVME_CC_IOCQES_SHIFT	20	/* log2(CQ entry size) */

/* CSTS bits */
#define NVME_CSTS_RDY		0x00000001
#define NVME_CSTS_CFS		0x00000002

/* AQA bits */
#define NVME_AQA_ASQS_SHIFT	0
#define NVME_AQA_ACQS_SHIFT	16

/* opcodes */
#define NVME_OP_WRITE		0x01
#define NVME_OP_READ		0x02
#define NVME_OP_CREATE_SQ	0x01	/* admin */
#define NVME_OP_CREATE_CQ	0x05	/* admin */
#define NVME_OP_IDENTIFY	0x06	/* admin */

/* CQ flags */
#define NVME_CQ_PC		0x1
#define NVME_CQ_IEN		0x2
/* SQ flags */
#define NVME_SQ_PC		0x1

#define NVME_ADMIN_Q_ENTRIES	8	/* qsize (power of 2) */
#define NVME_IO_Q_ENTRIES	128

#define NVME_ADM_CQ_HEAD_PHASE	1	/* initial expected phase */

struct nvme_sqe {
	unsigned char opcode;
	unsigned char flags;
	unsigned short int cid;
	unsigned int nsid;
	unsigned int cdw2;
	unsigned int cdw3;
	unsigned long long mptr;
	unsigned long long prp1;
	unsigned long long prp2;
	unsigned int cdw10;
	unsigned int cdw11;
	unsigned int cdw12;
	unsigned int cdw13;
	unsigned int cdw14;
	unsigned int cdw15;
};

struct nvme_cqe {
	unsigned int result;
	unsigned int dw1;		/* reserved (QEMU NvmeCqe.dw1) */
	unsigned short int sq_head;
	unsigned short int sq_id;
	unsigned short int cid;
	unsigned short int status;	/* bit0 = phase */
};

struct nvme {
	unsigned long mmio;
	struct device *dev;
	struct partition part[NR_PARTITIONS];
	unsigned char *adm_sq;		/* admin SQ, phys-aligned */
	unsigned char *adm_cq;		/* admin CQ, phys-aligned */
	unsigned char *io_sq;		/* I/O SQ */
	unsigned char *io_cq;		/* I/O CQ */
	unsigned char *dmabuf;		/* page-aligned DMA bounce */
	unsigned char *ident;		/* 4096B IDENTIFY buffer */
	unsigned int adm_sq_tail;
	unsigned int adm_cq_head;
	unsigned int adm_cq_phase;
	unsigned int io_sq_tail;
	unsigned int io_cq_head;
	unsigned int io_cq_phase;
	unsigned int nr_sects;
	unsigned int sector_size;
	unsigned int cid;
	unsigned int doorbell_stride;
} nvme;

static struct fs_operations nvme_driver_fsop;

static unsigned int nvme_reg_r(unsigned long off)
{
	return *(volatile unsigned int *)(nvme.mmio + off);
}

static void nvme_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(nvme.mmio + off) = val;
}

static unsigned long long nvme_reg_r64(unsigned long off)
{
	return *(volatile unsigned long long *)(nvme.mmio + off);
}

static void nvme_reg_w64(unsigned long off, unsigned long long val)
{
	*(volatile unsigned long long *)(nvme.mmio + off) = val;
}

static unsigned long nvme_db_sq_tail(unsigned int qid)
{
	return NVME_DBS + (2 * qid) * nvme.doorbell_stride;
}

static unsigned long nvme_db_cq_head(unsigned int qid)
{
	return NVME_DBS + (2 * qid + 1) * nvme.doorbell_stride;
}

/* submit one admin command (sq0) with a single PRP buffer, poll cq0 */
static int nvme_admin_cmd(unsigned char opcode, unsigned int nsid,
			  unsigned int cdw10, unsigned int cdw11,
			  unsigned int cdw12, unsigned int cdw13,
			  void *buf, unsigned int buflen)
{
	struct nvme_sqe *sqe;
	struct nvme_cqe *cqe;
	unsigned int tail, phase;

	tail = nvme.adm_sq_tail & (NVME_ADMIN_Q_ENTRIES - 1);
	sqe = (struct nvme_sqe *)(nvme.adm_sq + tail * 64);
	memset_b(sqe, 0, 64);
	nvme.cid++;
	sqe->opcode = opcode;
	sqe->flags = 0;
	sqe->cid = nvme.cid;
	sqe->nsid = nsid;
	sqe->prp1 = buf ? V2P((addr_t)buf) : 0;
	sqe->prp2 = 0;
	sqe->cdw10 = cdw10;
	sqe->cdw11 = cdw11;
	sqe->cdw12 = cdw12;
	sqe->cdw13 = cdw13;

	/* the SQE must be visible before the doorbell MMIO write */
	__asm__ __volatile__("" ::: "memory");
	/* ring the doorbell */
	nvme.adm_sq_tail++;
	nvme_reg_w(nvme_db_sq_tail(0), nvme.adm_sq_tail & (NVME_ADMIN_Q_ENTRIES - 1));

	/* poll for completion (volatile: the CQ is written by DMA) */
	phase = nvme.adm_cq_phase;
	/* unbounded poll, same rationale as nvme_io_cmd: the admin
	 * completion will arrive (synchronous driver) */
	for(;;) {
		volatile struct nvme_cqe *vcqe =
			(volatile struct nvme_cqe *)(nvme.adm_cq + nvme.adm_cq_head * 16);
		if((vcqe->status & 1) == phase) {
			/* status is bit 15:1 */
			if((vcqe->status >> 1) & 0x7FFF) {
				return -EIO;
			}
			nvme.adm_cq_head++;
			if(nvme.adm_cq_head == NVME_ADMIN_Q_ENTRIES) {
				nvme.adm_cq_head = 0;
				nvme.adm_cq_phase ^= 1;
			}
			nvme_reg_w(nvme_db_cq_head(0), nvme.adm_cq_head);
			return 0;
		}
	}
}

/* create an I/O CQ (qid 1) and SQ (qid 1) */
static int nvme_setup_io_queues(void)
{
	int ret;

/* CREATE_CQ: cdw10 = cqid | (qsize << 16), cdw11 = cq_flags | (irq_vector << 16),
 * prp1 = phys(cq). NOTE: IEN deliberately NOT set - the driver polls the
 * CQ (nvme_io_cmd/nvme_admin_cmd spin on the phase bit), so a completion
 * interrupt would just fire the kernel's spurious MSI-X path ("Unknown
 * MSI-X vector %d received!") with no handler and wedge the CPU. */
if((ret = nvme_admin_cmd(NVME_OP_CREATE_CQ, 0,
		1 | ((NVME_IO_Q_ENTRIES - 1) << 16),	/* cqid=1, qsize-1 */
		(NVME_CQ_PC) | (0 << 16),	/* cq_flags, irq_vector */
		0, 0,
		nvme.io_cq, 0))) {
	printk("nvme: CREATE_CQ failed (%d)\n", ret);
	return ret;
}
/* CREATE_SQ: cdw10 = sqid | (qsize << 16), cdw11 = sq_flags | (cqid << 16),
 * prp1 = phys(sq) */
if((ret = nvme_admin_cmd(NVME_OP_CREATE_SQ, 0,
		1 | ((NVME_IO_Q_ENTRIES - 1) << 16),	/* sqid=1, qsize-1 */
		NVME_SQ_PC | (1 << 16),	/* sq_flags, cqid=1 */
		0, 0,
		nvme.io_sq, 0))) {
	printk("nvme: CREATE_SQ failed (%d)\n", ret);
	return ret;
}
	nvme.io_sq_tail = 0;
	nvme.io_cq_head = 0;
	nvme.io_cq_phase = 1;
	return 0;
}

/* submit one I/O command (sq1), poll cq1 */
static int nvme_io_cmd(unsigned char opcode, unsigned long long slba,
		       unsigned int nlb, void *buf)
{
	struct nvme_sqe *sqe;
	struct nvme_cqe *cqe;
	unsigned int tail, phase;

	tail = nvme.io_sq_tail & (NVME_IO_Q_ENTRIES - 1);
	sqe = (struct nvme_sqe *)(nvme.io_sq + tail * 64);
	memset_b(sqe, 0, 64);
	nvme.cid++;
	sqe->opcode = opcode;
	sqe->flags = 0;
	sqe->cid = nvme.cid;
	sqe->nsid = 1;
	sqe->prp1 = V2P((addr_t)buf);
	sqe->prp2 = 0;
	sqe->cdw10 = (unsigned int)(slba & 0xFFFFFFFF);
	sqe->cdw11 = (unsigned int)(slba >> 32);
	sqe->cdw12 = nlb;	/* 0-based */

	/* the SQE must be visible before the doorbell MMIO write */
	__asm__ __volatile__("" ::: "memory");

	nvme.io_sq_tail++;
	/* QEMU's doorbell handler takes `new_tail = val & 0xffff` and
	 * REJECTS new_tail >= queue size, so the tail doorbell MUST be the
	 * masked index (0..31). nvme_process_sq then walks head..tail as
	 * raw ring offsets. This matches the NVMe spec's requirement that
	 * the doorbell value equal the queue index (not a wrap counter). */
	nvme_reg_w(nvme_db_sq_tail(1), nvme.io_sq_tail & (NVME_IO_Q_ENTRIES - 1));

	phase = nvme.io_cq_phase;
	/* Synchronous driver: the completion MUST arrive (QEMU processes
	 * the SQ one command at a time and only re-fills its req_list after
	 * the guest advances the CQ head doorbell). A bounded spin that
	 * gives up (-EAGAIN) leaves the CQE unconsumed, QEMU's req_list
	 * fills up and nvme_process_sq stops - every later command hangs.
	 * Poll without a bound; the command is in flight and will complete.
	 */
	for(;;) {
		volatile struct nvme_cqe *vcqe =
			(volatile struct nvme_cqe *)(nvme.io_cq + nvme.io_cq_head * 16);
		if((vcqe->status & 1) == phase) {
			if((vcqe->status >> 1) & 0x7FFF) {
				return -EIO;
			}
			nvme.io_cq_head++;
			if(nvme.io_cq_head == NVME_IO_Q_ENTRIES) {
				nvme.io_cq_head = 0;
				nvme.io_cq_phase ^= 1;
			}
			nvme_reg_w(nvme_db_cq_head(1), nvme.io_cq_head);
			return 0;
		}
	}
}

static int nvme_identify_namespace(void)
{
	unsigned char *id = nvme.ident;
	int ret;

	/* CNS=0: namespace identify for nsid 1 */
	if((ret = nvme_admin_cmd(NVME_OP_IDENTIFY, 1, 0, 0, 0, 0, id, 4096))) {
		return ret;
	}
	/* nsze (u64) at +0; lbaf[0].ds (log2) at offset 0x80+2 */
	nvme.nr_sects =
		(id[3] << 24) | (id[2] << 16) | (id[1] << 8) | id[0];
	nvme.sector_size = 1 << id[0x82];
	return 0;
}

/* ---------------- block layer integration ---------------- */

static int nvme_open(struct inode *i, struct fd *f)
{
	return 0;
}

static int nvme_close(struct inode *i, struct fd *f)
{
	return 0;
}

static int nvme_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
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
			*(int *)arg = (unsigned int)nvme.nr_sects;
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
			if(!read_msdos_partition(i->rdev, nvme.part)) {
				for(n = 0; n < NR_PARTITIONS; n++) {
					if(nvme.part[n].type) {
						SET_MINOR(d->minors, n + 1);
						((unsigned int *)d->blksize)[n + 1] = BLKSIZE_1K;
						((unsigned int *)d->device_data)[n + 1] =
							nvme.part[n].nr_sects / 2;
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

static __loff_t nvme_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

/* PRP1 must be page-aligned; bounce through the page-aligned dmabuf */
static int nvme_read_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned long long lba;
	unsigned int count, offset;

	lba = (unsigned long long)block * (blksize / nvme.sector_size);
	count = blksize / nvme.sector_size;

	if(MINOR(dev) && MINOR(dev) <= NR_PARTITIONS) {
		offset = nvme.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	if(nvme_io_cmd(NVME_OP_READ, lba, count - 1, nvme.dmabuf)) {
		return -EIO;
	}
	memcpy_b(buffer, nvme.dmabuf, blksize);
	return blksize;
}

static int nvme_write_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned long long lba;
	unsigned int count, offset;

	lba = (unsigned long long)block * (blksize / nvme.sector_size);
	count = blksize / nvme.sector_size;

	if(MINOR(dev) && MINOR(dev) <= NR_PARTITIONS) {
		offset = nvme.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	memcpy_b(nvme.dmabuf, buffer, blksize);
	if(nvme_io_cmd(NVME_OP_WRITE, lba, count - 1, nvme.dmabuf)) {
		return -EIO;
	}
	return blksize;
}

static struct fs_operations nvme_driver_fsop = {
	0,
	0,

	nvme_open,
	nvme_close,
	NULL,			/* read */
	NULL,			/* write */
	nvme_ioctl,
	nvme_llseek,
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

	nvme_read_block,
	nvme_write_block,

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

static struct device nvme_device = {
	"nvme0",
	NVME_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&nvme_driver_fsop,
	NULL,
	NULL
};

/* ---------------- probe ---------------- */

extern int map_page64(unsigned long, unsigned long, unsigned long);

int nvme_init(void)
{
	struct pci_device *pd;
	struct device *d;
	unsigned long long bar;
	unsigned int cap, cc, aqa, csts;
	unsigned long spin;
	int i, found, ret;

	nvme.dev = &nvme_device;
	found = 0;

	pd = pci_device_table;
	while(pd) {
		if((pd->vendor_id == NVME_VENDOR_INTEL &&
		    pd->device_id == NVME_DEVICE_INTEL) ||
		   (pd->vendor_id == NVME_VENDOR_REDHAT &&
		    pd->device_id == NVME_DEVICE_REDHAT)) {
			found = 1;
			break;
		}
		pd = pd->next;
	}
	if(!found) {
		return 0;
	}

	bar = pd->bar[0] | ((unsigned long long)pd->bar[1] << 32);
	bar &= 0xFFFFFFFFFFFFFFF0;
	if(!bar) {
		return -ENXIO;
	}

	for(i = 0; i < NVME_MMIO_SIZE / 4096; i++) {
		if(map_page64(NVME_MMIO_VA + i * 4096, (unsigned long)bar + i * 4096, 0x003)) {
			printk("nvme: unable to map BAR0\n");
			return -ENOMEM;
		}
	}
	nvme.mmio = NVME_MMIO_VA;
	pci_write_short(pd, 0x04, 0x0006);	/* MEM | MASTER */

	cap = (unsigned int)nvme_reg_r64(NVME_REG_CAP);
	nvme.doorbell_stride = 4 << ((cap & NVME_CAP_DSTRD_MASK) >> NVME_CAP_DSTRD_SHIFT);

	/* disable controller (CC.EN = 0), wait for CSTS.RDY == 0 */
	nvme_reg_w(NVME_REG_CC, 0);
	for(spin = 0; spin < 0x100000; spin++) {
		if(!(nvme_reg_r(NVME_REG_CSTS) & NVME_CSTS_RDY)) {
			break;
		}
	}

	/* DMA structures (page-aligned). The I/O SQ ring is 128 x 64B = 8192B
	 * (NVME_IO_Q_ENTRIES SQEs) - allocating only 4096 (a single kmalloc
	 * page) made SQEs 64..127 spill into the adjacent page (often the I/O
	 * CQ ring): the overflow SQE bytes sit in CQ slots with phase bit 0,
	 * so after the CQ phase wraps to 0 the poll loop consumed them as
	 * FAKE completions and every read returned the previous command's
	 * data (the intermittent shell text corruption on NVMe root).
	 * The CQ ring is 128 x 16B = 2048B. */
	extern unsigned long alloc_pages64(int);
	{
		unsigned long sq_phys = alloc_pages64(2);	/* 2 pages = 8192B */
		if(!sq_phys) {
			return -ENOMEM;
		}
		nvme.io_sq = (unsigned char *)P2V(sq_phys);
	}
	if(!(nvme.adm_sq = (unsigned char *)kmalloc(4096)) ||
	   !(nvme.adm_cq = (unsigned char *)kmalloc(4096)) ||
	   !(nvme.io_cq = (unsigned char *)kmalloc(4096)) ||
	   !(nvme.dmabuf = (unsigned char *)kmalloc(4096)) ||
	   !(nvme.ident = (unsigned char *)kmalloc(4096))) {
		return -ENOMEM;
	}
	memset_b(nvme.adm_sq, 0, 4096);
	memset_b(nvme.adm_cq, 0, 4096);
	memset_b(nvme.io_sq, 0, 8192);
	memset_b(nvme.io_cq, 0, 4096);

	/* AQA: admin SQ/CQ size-1; ASQ/ACQ: physical addresses */
	aqa = ((NVME_ADMIN_Q_ENTRIES - 1) << NVME_AQA_ASQS_SHIFT) |
	      ((NVME_ADMIN_Q_ENTRIES - 1) << NVME_AQA_ACQS_SHIFT);
	nvme_reg_w(NVME_REG_AQA, aqa);
	nvme_reg_w64(NVME_REG_ASQ, V2P((addr_t)nvme.adm_sq));
	nvme_reg_w64(NVME_REG_ACQ, V2P((addr_t)nvme.adm_cq));

	/* CC: EN | IOSQES(6) | IOCQES(4) | CSS(0) | MPS(0) */
	cc = NVME_CC_EN | (6 << NVME_CC_IOSQES_SHIFT) |
	     (4 << NVME_CC_IOCQES_SHIFT);
	nvme_reg_w(NVME_REG_CC, cc);
	for(spin = 0; spin < 0x100000; spin++) {
		csts = nvme_reg_r(NVME_REG_CSTS);
		if(csts & NVME_CSTS_RDY) {
			break;
		}
		if(csts & NVME_CSTS_CFS) {
			printk("nvme: controller failed (CFS)\n");
			return -EIO;
		}
	}
	if(!(nvme_reg_r(NVME_REG_CSTS) & NVME_CSTS_RDY)) {
		printk("nvme: controller not ready\n");
		return -EAGAIN;
	}

	nvme.adm_sq_tail = 0;
	nvme.adm_cq_head = 0;
	nvme.adm_cq_phase = NVME_ADM_CQ_HEAD_PHASE;
	nvme.cid = 0;

	if((ret = nvme_setup_io_queues())) {
		return ret;
	}
	if((ret = nvme_identify_namespace())) {
		printk("nvme: IDENTIFY namespace failed (%d)\n", ret);
		return ret;
	}

	/* register the block device (major 9 = /dev/nvme0n1) */
	SET_MINOR(nvme_device.minors, NVME_MINOR_DISK);
	if(!(d = get_device(BLK_DEV, MKDEV(NVME_MAJOR, NVME_MINOR_DISK)))) {
		if(register_device(BLK_DEV, &nvme_device)) {
			printk("nvme: register_device failed\n");
			return -EINVAL;
		}
		if(!(d = get_device(BLK_DEV, MKDEV(NVME_MAJOR, NVME_MINOR_DISK)))) {
			return -EINVAL;
		}
	}
	((unsigned int *)d->device_data)[NVME_MINOR_DISK] = nvme.nr_sects / 2;
	devfs_make_node("nvme0n1", MKDEV(NVME_MAJOR, NVME_MINOR_DISK), S_IFBLK | S_IRUSR | S_IWUSR);

	printk("nvme: %d sectors of %d bytes (%d MB)\n",
		nvme.nr_sects, nvme.sector_size,
		nvme.nr_sects * nvme.sector_size / 1048576);

	/* register the INTx IRQ */
	{
		static struct interrupt irq_config_nvme = { 0, "nvme", NULL };
		if(pd->irq) {
			register_irq(pd->irq, &irq_config_nvme);
		}
	}

	return 0;
}
