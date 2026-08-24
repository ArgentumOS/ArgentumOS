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
