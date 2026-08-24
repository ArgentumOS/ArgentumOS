/*
 * fnx/include/fnx/ata_hd.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_ATA_HD_H
#define _FNX_ATA_HD_H

#include <fnx/types.h>

#define ATA_HD_SECTSIZE		512	/* sector size (in bytes) */

int ata_hd_open(struct inode *, struct fd *);
int ata_hd_close(struct inode *, struct fd *);
int ata_hd_read(__dev_t, __blk_t, char *, int);
int ata_hd_write(__dev_t, __blk_t, char *, int);
int ata_hd_ioctl(struct inode *, struct fd *, int, addr_t);
__loff_t ata_hd_llseek(struct inode *, __loff_t);
int ata_hd_init(struct ide *, struct ata_drv *);

#endif /* _FNX_ATA_HD_H */
