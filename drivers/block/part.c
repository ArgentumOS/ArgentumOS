/*
 * fnx/drivers/block/part.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/ata.h>
#include <fnx/ata_hd.h>
#include <fnx/fs.h>
#include <fnx/part.h>
#include <fnx/buffer.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

int read_msdos_partition(__dev_t dev, struct partition *part)
{
	struct buffer *buf;
	struct device *d;
	int blksize;

	if(!(d = get_device(BLK_DEV, dev))) {
		return -ENXIO;
	}

	blksize = ((unsigned int *)d->blksize)[MINOR(dev)];
	if(!(buf = bread(dev, PARTITION_BLOCK, blksize))) {
		printk("WARNING: %s(): unable to read partition block in device %d,%d.\n", __FUNCTION__, MAJOR(dev), MINOR(dev));
		return -EIO;
	}

	memcpy_b(part, (void *)(buf->data + MBR_CODE_SIZE), sizeof(struct partition) * NR_PARTITIONS);
	brelse(buf);
	return 0;
}

/*
 * read_partitions(): kernel convenience over the pure parser — reads
 * 512-byte sectors from a whole-disk block device (minor 0, so no
 * partition offset applies) through the buffer layer.
 */
struct part_dev_ctx {
	__dev_t dev;
};

#define SECTOR_OFF_ODD 512

static int part_dev_read_sector(void *vctx, unsigned long long lba,
				unsigned char *sector)
{
	struct part_dev_ctx *ctx = (struct part_dev_ctx *)vctx;
	struct buffer *buf;
	__blk_t blk;

	if(lba > (unsigned long long)0xFFFFFFFF) {
		return -EIO;
	}
	blk = (__blk_t)(lba / 2);	/* one 1K buffer holds two sectors */
	if(!(buf = bread(ctx->dev, blk, BLKSIZE_1K))) {
		return -EIO;
	}
	if(lba & 1) {
		memcpy_b(sector, buf->data + SECTOR_OFF_ODD, 512);
	} else {
		memcpy_b(sector, buf->data, 512);
	}
	brelse(buf);
	return 0;
}

int read_partitions(__dev_t dev, struct partition *out, int max)
{
	struct part_dev_ctx ctx;

	ctx.dev = dev;
	return partition_parse(&ctx, part_dev_read_sector, out, max);
}
