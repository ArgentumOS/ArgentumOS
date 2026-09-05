/*
 * fnx/drivers/block/ahci.c
 *
 * AHCI (Advanced Host Controller Interface) SATA driver.
 *
 * QEMU: -device ich9-ahci,id=ahci -drive file=X,if=none,id=d0 \
 *           -device ide-hd,drive=d0,bus=ahci.0
 * (ich9-ahci = Intel ICH9 8086:2922, class 0x0106 prog-if 0x01, ABAR on BAR5)
 *
 * Only the non-NCQ DMA path is implemented (READ/WRITE DMA EXT + IDENTIFY
 * PIO through the same PRDT mechanism, which QEMU honors). Completion is
 * signalled by the D2H Register FIS (PxIS.DHRS) after the port receives it
 * (QEMU requires PORT_CMD.FRE + a FIS buffer for that), errors by TFES.
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
#include <fnx/ata.h>
#include <fnx/ata_hd.h>
#include <fnx/part.h>
#include <fnx/ioctl.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/pci.h>
#include <fnx/irq.h>
#include <fnx/timer.h>
#include <fnx/sleep.h>

#define AHCI_VENDOR		0x8086
#define AHCI_DEVICE		0x2922
#define AHCI_MAJOR		8

#define AHCI_MMIO_VA		0xFFFFBE0000000000UL	/* pml4[381] */
#define AHCI_MMIO_SIZE		0x1000

#define AHCI_MAX_PORTS		6
#define AHCI_CMD_SLOTS		32
#define AHCI_CMD_LIST_SZ	(AHCI_CMD_SLOTS * 32)	/* 1024 */
#define AHCI_FIS_SZ		256

/* HBA registers (ABAR) */
#define CAP			0x00
#define GHC			0x04
#define IS			0x08
#define PI			0x0C
#define VS			0x10

#define GHC_HR			0x00000001	/* HBA reset */
#define GHC_IE			0x00000002	/* interrupt enable */
#define GHC_AE			0x80000000	/* AHCI enable */

/* port registers (ABAR + 0x100 + n * 0x80) */
#define PXCLB			0x00
#define PXCLBU			0x04
#define PXFB			0x08
#define PXFBU			0x0C
#define PXIS			0x10
#define PXIE			0x14
#define PXCMD			0x18
#define PXTFD			0x20
#define PXSIG			0x24
#define PXSSTS			0x28
#define PXSCTL			0x2C
#define PXSERR			0x30
#define PXSACT			0x34
#define PXCI			0x38

#define PXCMD_ST		0x00000001	/* start */
#define PXCMD_CR		0x00008000	/* cmd list running */
#define PXCMD_FR		0x00004000	/* FIS rx running */
#define PXCMD_FRE		0x00000010	/* FIS rx enable */

#define PXIS_DHRS		0x00000001	/* D2H register FIS */
#define PXIS_TFES		0x40000000	/* task file error status */

#define PXSSTS_DET_MASK		0x0000000F
#define PXSSTS_DET_DEV		3		/* device present */
#define PXSSTS_IPM_MASK		0x00000F00
#define PXSSTS_IPM_ACTIVE	0x00000100

#define PXTFD_BSY		0x00000080
#define PXTFD_DRQ		0x00000008
#define PXTFD_ERR		0x00000001

#define ATA_IDENTIFY		0xEC
#define ATA_READ_DMA_EXT	0x25
#define ATA_WRITE_DMA_EXT	0x35
#define ATA_DSM			0x06	/* DATA SET MANAGEMENT */
#define ATA_DSM_TRIM		0x01	/* feature: TRIM (non-queued) */

/* command header opts */
#define AHCI_CMD_C		0x8000	/* clear busy on error */
#define AHCI_CMD_P		0x4000	/* prefetch */
#define AHCI_CMD_R		0x2000	/* reset */
#define AHCI_CMD_W		0x0040	/* write */
#define AHCI_CMD_A		0x0020	/* ATAPI */

struct ahci_cmd_hdr {
	unsigned short int opts;
	unsigned short int prdtl;
	unsigned int status;
	unsigned int tbl_addr;
	unsigned int tbl_addr_hi;
	unsigned int rsvd[4];
};

struct ahci_prd {
	unsigned int addr;
	unsigned int addr_hi;
	unsigned int rsvd;
	unsigned int flags_size;	/* bit31 DBC, bits 30:0 = size - 1 */
};

struct ahci {
	unsigned long mmio;
	unsigned int pi;
	struct device *dev;
	struct partition part[MAX_PARTITIONS];
	unsigned char *cmd_list;	/* phys-aligned 1024B */
	unsigned char *fis;		/* phys-aligned 256B */
	unsigned char *cmd_table;	/* phys-aligned 128B+ */
	unsigned char *dsm_list;	/* phys-aligned range-list buffer */
	unsigned int nr_sects;
	unsigned int sector_size;
	int port;
} ahci;

static struct fs_operations ahci_driver_fsop;
static unsigned int ahci_reg(unsigned long off)
{
	return *(volatile unsigned int *)(ahci.mmio + off);
}

static void ahci_reg_w(unsigned long off, unsigned int val)
{
	*(volatile unsigned int *)(ahci.mmio + off) = val;
}

static unsigned int ahci_port_reg(unsigned long off)
{
	return ahci_reg(0x100 + ahci.port * 0x80 + off);
}

static void ahci_port_reg_w(unsigned long off, unsigned int val)
{
	ahci_reg_w(0x100 + ahci.port * 0x80 + off, val);
}

/* wait for a port condition; returns 0 on success */
static int ahci_wait_port_ready(void)
{
	unsigned long spin;
	unsigned int ssts;

	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		ssts = ahci_port_reg(PXSSTS);
		if((ssts & PXSSTS_DET_MASK) == PXSSTS_DET_DEV &&
		   (ssts & PXSSTS_IPM_MASK) == PXSSTS_IPM_ACTIVE) {
			return 0;
		}
		if(ahci_port_reg(PXTFD) & (PXTFD_BSY | PXTFD_DRQ)) {
			continue;
		}
	}
	return -EIO;
}

/* wait for command completion (PxIS.DHRS or TFES), returns 0 on success */
static int ahci_wait_cmd_done(void)
{
	unsigned long spin;
	unsigned int is;

	/* the poll is bounded: a lost DHRS (seen intermittently during the
	 * boot-time probes) must surface as an error within a couple of
	 * seconds so the caller (a read_superblock probe, the buffer
	 * layer) can fail and the boot move on - an unbounded spin wedges
	 * the boot for ~30 minutes instead. */
	for(spin = 0; spin < 0x1000000; spin++) {
		is = ahci_port_reg(PXIS);
		if(is & PXIS_DHRS) {
			ahci_port_reg_w(PXIS, PXIS_DHRS);
			if(ahci_port_reg(PXTFD) & PXTFD_ERR) {
				return -EIO;
			}
			return 0;
		}
		if(is & PXIS_TFES) {
			ahci_port_reg_w(PXIS, PXIS_TFES);
			return -EIO;
		}
	}
	printk("AHCI-TMO: cmd done never seen: PXIS %x PXCI %x PXTFD %x (port %d)\n",
		ahci_port_reg(PXIS), ahci_port_reg(PXCI),
		ahci_port_reg(PXTFD), ahci.port);
	return -EAGAIN;
}
/* issue a single non-NCQ command. dir_in: 1 = device->host (read/identify).
 * lba/count are LBA48 (count = sectors). buffer is the DMA buffer. */
static int ahci_cmd(unsigned char cmd, unsigned char features, int dir_in,
		    unsigned long long lba, unsigned int count, void *buffer,
		    unsigned int bufsize)
{
	struct ahci_cmd_hdr *hdr;
	struct ahci_prd *prd;
	unsigned char *cfis;
	unsigned int i;

	/* wait for the port to be idle before touching the rings */
	for(i = 0; i < 0x7FFFFFFF; i++) {
		if(!(ahci_port_reg(PXTFD) & (PXTFD_BSY | PXTFD_DRQ)) &&
		   !ahci_port_reg(PXCI)) {
			break;
		}
	}
	if(i == 0x7FFFFFFF) {
		return -EAGAIN;
	}

	/* command header slot 0 */
	memset_b(ahci.cmd_list, 0, 32);
	hdr = (struct ahci_cmd_hdr *)ahci.cmd_list;
	hdr->opts = AHCI_CMD_C | AHCI_CMD_P;
	if(!dir_in) {
		hdr->opts |= AHCI_CMD_W;
	}
	hdr->prdtl = 1;
	hdr->tbl_addr = V2P((addr_t)ahci.cmd_table);
	hdr->tbl_addr_hi = 0;

	/* command table: H2D FIS at +0, PRDT at +0x80 */
	memset_b(ahci.cmd_table, 0, 0x80 + 16);
	cfis = ahci.cmd_table;
	cfis[0] = 0x27;			/* FIS type H2D */
	cfis[1] = 0x80;			/* C bit: command */
	cfis[2] = cmd;
	cfis[3] = features;				/* features 7:0 */
	cfis[4] = (unsigned char)(lba & 0xFF);		/* LBA 7:0 */
	cfis[5] = (unsigned char)((lba >> 8) & 0xFF);	/* LBA 15:8 */
	cfis[6] = (unsigned char)((lba >> 16) & 0xFF);	/* LBA 23:16 */
	cfis[7] = 0x40 | 0x00;				/* LBA + master */
	cfis[8] = (unsigned char)((lba >> 24) & 0xFF);	/* LBA 31:24 */
	cfis[9] = (unsigned char)((lba >> 32) & 0xFF);	/* LBA 39:32 */
	cfis[10] = (unsigned char)((lba >> 40) & 0xFF);	/* LBA 47:40 */
	cfis[12] = (unsigned char)(count & 0xFF);	/* sector count lo */
	cfis[13] = (unsigned char)((count >> 8) & 0xFF);

	prd = (struct ahci_prd *)(ahci.cmd_table + 0x80);
	prd->addr = V2P((addr_t)buffer);
	prd->addr_hi = 0;
	prd->flags_size = (bufsize - 1) | 0x80000000;	/* DBC | size-1 */

	/* issue */
	ahci_port_reg_w(PXCI, 0x1);
	return ahci_wait_cmd_done();
}

static int ahci_identify(unsigned char *ident)
{
	int ret;

	if((ret = ahci_cmd(ATA_IDENTIFY, 0, 1, 0, 0, ident, 512))) {
		return ret;
	}
	return 0;
}

static int ahci_read_sectors(unsigned long long lba, unsigned int count,
			     void *buffer)
{
	return ahci_cmd(ATA_READ_DMA_EXT, 0, 1, lba, count, buffer,
			count * ahci.sector_size);
}

static int ahci_write_sectors(unsigned long long lba, unsigned int count,
			      void *buffer)
{
	return ahci_cmd(ATA_WRITE_DMA_EXT, 0, 0, lba, count, buffer,
			count * ahci.sector_size);
}

/* ---------------- block layer integration ---------------- */

static int ahci_open(struct inode *i, struct fd *f)
{
	return 0;
}

static int ahci_close(struct inode *i, struct fd *f)
{
	return 0;
}

/* scan the partition table on the whole disk and publish minors + nodes */
static void ahci_scan_partitions(struct device *d)
{
	int n, np;

	for(n = 1; n < MAX_PARTITIONS; n++) {
		if(TEST_MINOR(d->minors, n)) {
			devfs_remove_node(MKDEV(AHCI_MAJOR, n));
		}
		CLEAR_MINOR(d->minors, n);
	}
	np = read_partitions(MKDEV(AHCI_MAJOR, 0), ahci.part, MAX_PARTITIONS);
	for(n = 1; n <= np; n++) {
		if(!ahci.part[n - 1].type) {
			continue;
		}
		SET_MINOR(d->minors, n);
		((unsigned int *)d->blksize)[n] = BLKSIZE_1K;
		((unsigned int *)d->device_data)[n] = ahci.part[n - 1].nr_sects / 2;
		devfs_partition_node("AHCI", 0, n, MKDEV(AHCI_MAJOR, n));
	}
}

static int ahci_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
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
			*(int *)arg = (unsigned int)ahci.nr_sects;
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
			/* re-read the partition table */
			invalidate_buffers(i->rdev);
			ahci_scan_partitions(d);
			break;
		default:
			return -EINVAL;
			break;
	}

	return 0;
}

static __loff_t ahci_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

/* block numbers are in blksize units; blksize >= sector_size */
static int ahci_read_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned long long lba;
	unsigned int count;
	unsigned int offset;

	lba = (unsigned long long)block * (blksize / ahci.sector_size);
	count = blksize / ahci.sector_size;

	/* partition offset (minor >= 1) */
	if(MINOR(dev) && MINOR(dev) < MAX_PARTITIONS) {
		offset = ahci.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	if(ahci_read_sectors(lba, count, buffer)) {
		return -EIO;
	}
	return blksize;
}

static int ahci_write_block(__dev_t dev, __blk_t block, char *buffer, int blksize)
{
	unsigned long long lba;
	unsigned int count;
	unsigned int offset;

	lba = (unsigned long long)block * (blksize / ahci.sector_size);
	count = blksize / ahci.sector_size;

	if(MINOR(dev) && MINOR(dev) < MAX_PARTITIONS) {
		offset = ahci.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}

	if(ahci_write_sectors(lba, count, buffer)) {
		return -EIO;
	}
	return blksize;
}

/*
 * Trim a contiguous range of 512-byte sectors (LBA48) via DATA SET
 * MANAGEMENT. The range list (one 8-byte entry: LBA48 + count) is DMA'd
 * out to the device; the H2D sector count is 0 for DSM and the transfer
 * length comes from the PRD. Polled, like every other ahci command.
 */
static int ahci_dsm(unsigned long long lba, unsigned int count)
{
	unsigned long long *entry;
	int ret;

	if(!ahci.dsm_list) {
		return -ENODEV;
	}
	if(!count) {
		return 0;
	}
	/* QEMU's IDE/ahci emulation sizes the DSM DMA from the H2D sector
	 * count (nsector * 512) - unlike real hardware, where the length
	 * comes from the PRDT and count is 0. Send the range list padded
	 * to a full 512-byte sector with count = 1 (extra entries decode
	 * as count 0 and are skipped); this matches what QEMU's own IDE
	 * trim test does and works on real hardware too. */
	memset_b(ahci.dsm_list, 0, 512);
	entry = (unsigned long long *)ahci.dsm_list;
	*entry = lba | ((unsigned long long)count << 48);
	return ahci_cmd(ATA_DSM, ATA_DSM_TRIM, 0, 1, 0,
		       ahci.dsm_list, 512);
}

static int ahci_discard_blocks(__dev_t dev, __blk_t block, __blk_t count,
			       int blksize)
{
	unsigned long long lba;
	unsigned int offset;

	lba = (unsigned long long)block * (blksize / ahci.sector_size);
	if(MINOR(dev) && MINOR(dev) < MAX_PARTITIONS) {
		offset = ahci.part[MINOR(dev) - 1].startsect;
		lba += offset;
	}
	return ahci_dsm(lba, (unsigned int)count * (blksize / ahci.sector_size));
}

static struct fs_operations ahci_driver_fsop = {
	.flags = 0,
	.fsdev = 0,
	.open = ahci_open,
	.close = ahci_close,
	.ioctl = ahci_ioctl,
	.llseek = ahci_llseek,
	.read_block = ahci_read_block,
	.write_block = ahci_write_block,
	.discard_blocks = ahci_discard_blocks,
};

static struct device ahci_device = {
	"ahci0",
	AHCI_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&ahci_driver_fsop,
	NULL,
	NULL
};

/* ---------------- probe ---------------- */

extern int map_page64(unsigned long, unsigned long, unsigned long);

static int ahci_port_init(void)
{
	unsigned long spin;

	/* stop the port (ST + FRE off) and wait for the engines to halt */
	ahci_port_reg_w(PXCMD, 0);
	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		if(!(ahci_port_reg(PXCMD) & (PXCMD_CR | PXCMD_FR))) {
			break;
		}
	}

	/* clear SERR, set the FIS + command list bases */
	ahci_port_reg_w(PXSERR, 0xFFFFFFFF);
	ahci_port_reg_w(PXCLB, V2P((addr_t)ahci.cmd_list));
	ahci_port_reg_w(PXCLBU, 0);
	ahci_port_reg_w(PXFB, V2P((addr_t)ahci.fis));
	ahci_port_reg_w(PXFBU, 0);

	/* enable FIS receive, wait for the engine, then start the port */
	ahci_port_reg_w(PXCMD, PXCMD_FRE);
	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		if(ahci_port_reg(PXCMD) & PXCMD_FR) {
			break;
		}
	}
	ahci_port_reg_w(PXCMD, PXCMD_FRE | PXCMD_ST);
	for(spin = 0; spin < 0x7FFFFFFF; spin++) {
		if(ahci_port_reg(PXCMD) & PXCMD_CR) {
			break;
		}
	}

	return ahci_wait_port_ready();
}

static void ahci_irq_handler(int num, struct sigcontext *sc)
{
	/* polled driver: the device IRQ (if any) is not serviced here, but
	 * the handler must exist so a shared IRQ line (e.g. AC97's IRQ 11)
	 * never dispatches a NULL pointer */
}

int ahci_init(void)
{
	struct pci_device *pd;
	struct device *d;
	unsigned char *ident;
	unsigned long bar;
	unsigned int pi;
	int i, ports, found, port;

	ahci.dev = &ahci_device;
	found = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == AHCI_VENDOR &&
		   pd->device_id == AHCI_DEVICE) {
			found = 1;
			break;
		}
		pd = pd->next;
	}
	if(!found) {
		return 0;
	}

	bar = pd->bar[5] & 0xFFFFFFF0;
	if(!bar) {
		return -ENXIO;
	}

	/* map the ABAR (BAR5, 0x1000) at a fixed kernel VA */
	for(i = 0; i < AHCI_MMIO_SIZE / 4096; i++) {
		if(map_page64(AHCI_MMIO_VA + i * 4096, bar + i * 4096, 0x003)) {
			printk("ahci: unable to map BAR5\n");
			return -ENOMEM;
		}
	}
	ahci.mmio = AHCI_MMIO_VA;
	pci_write_short(pd, 0x04, 0x0006);	/* MEM | MASTER */

	/* HBA reset */
	ahci_reg_w(GHC, GHC_HR);
	for(i = 0; i < 0x7FFFFFFF; i++) {
		if(!(ahci_reg(GHC) & GHC_HR)) {
			break;
		}
	}
	ahci_reg_w(GHC, GHC_AE);	/* AHCI enable */

	/* allocate the DMA structures (kmalloc is page-aligned) */
	if(!(ahci.cmd_list = (unsigned char *)kmalloc(1024))) {
		return -ENOMEM;
	}
	memset_b(ahci.cmd_list, 0, 1024);
	if(!(ahci.fis = (unsigned char *)kmalloc(256))) {
		return -ENOMEM;
	}
	memset_b(ahci.fis, 0, 256);
	if(!(ahci.cmd_table = (unsigned char *)kmalloc(0x80 + 16))) {
		return -ENOMEM;
	}
	/* DSM range-list scratch: one 8-byte entry per TRIM command */
	if(!(ahci.dsm_list = (unsigned char *)kmalloc(512))) {
		return -ENOMEM;
	}
	memset_b(ahci.dsm_list, 0, 512);

	/* find the first implemented port with a device attached */
	pi = ahci_reg(PI);
	ports = (ahci_reg(CAP) & 0x1F);
	if(!ports) {
		ports = AHCI_MAX_PORTS;
	}
	for(port = 0; port < ports; port++) {
		if(!(pi & (1 << port))) {
			continue;
		}
		ahci.port = port;
		if(ahci_port_init() < 0) {
			continue;
		}
		/* clear port interrupts, enable DHRS + TFES */
		ahci_port_reg_w(PXIS, 0xFFFFFFFF);
		ahci_port_reg_w(PXIE, PXIS_DHRS | PXIS_TFES);

		/* IDENTIFY: 512B into a DMA buffer */
		if(!(ident = (unsigned char *)kmalloc(512))) {
			return -ENOMEM;
		}
		if(ahci_identify(ident) == 0) {
			/* word 83 bit 10 = LBA48 supported, bit 9 = valid;
			 * LBA48 sectors in words 100-103 (bytes 200-207 LE) */
			if((ident[166] & 0x02) && (ident[166] & 0x04)) {
				ahci.nr_sects =
					((unsigned int)ident[200] << 0) |
					((unsigned int)ident[201] << 8) |
					((unsigned int)ident[202] << 16) |
					((unsigned int)ident[203] << 24);
			} else {
				/* words 60-61 (bytes 120-123 LE) */
				ahci.nr_sects =
					((unsigned int)ident[120] << 0) |
					((unsigned int)ident[121] << 8) |
					((unsigned int)ident[122] << 16) |
					((unsigned int)ident[123] << 24);
			}
			ahci.sector_size = 512;
			kfree((addr_t)ident);
			break;
		}
		kfree((addr_t)ident);
	}
	if(port == ports) {
		printk("ahci: no disk found on any port\n");
		return -EIO;
	}

	/* register the block device (major 8 = /dev/sda) */
	/* register the node first so the device-table fallback (which
	 * cannot know the bus) does not pre-empt it */
	devfs_block_node("AHCI", 0, "sda", MKDEV(AHCI_MAJOR, 0));
	SET_MINOR(ahci_device.minors, 0);	/* Disk/AHCI/Disk0 */
	if(!(d = get_device(BLK_DEV, MKDEV(AHCI_MAJOR, 0)))) {
		if(register_device(BLK_DEV, &ahci_device)) {
			printk("ahci: register_device failed\n");
			return -EINVAL;
		}
		if(!(d = get_device(BLK_DEV, MKDEV(AHCI_MAJOR, 0)))) {
			return -EINVAL;
		}
	}
	((unsigned int *)d->device_data)[0] = ahci.nr_sects / 2;
	ahci_scan_partitions(d);

	printk("ahci: %d sectors of %d bytes (%d MB) on port %d\n",
		ahci.nr_sects, ahci.sector_size,
		ahci.nr_sects * ahci.sector_size / 1048576, ahci.port);

	/* register the INTx IRQ */
	{
		static struct interrupt irq_config_ahci = { 0, "ahci", &ahci_irq_handler };
		if(pd->irq) {
			register_irq(pd->irq, &irq_config_ahci);
		}
	}

	return 0;
}
