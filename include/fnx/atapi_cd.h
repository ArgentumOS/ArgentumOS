/*
 * fnx/include/fnx/atapi_cd.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_ATAPI_CD_H
#define _FNX_ATAPI_CD_H

#include <fnx/fs.h>

#define ATAPI_CD_SECTSIZE	BLKSIZE_2K	/* sector size (in bytes) */

int atapi_cd_open(struct inode *, struct fd *);
int atapi_cd_close(struct inode *, struct fd *);
int atapi_cd_read(__dev_t, __blk_t, char *, int);
int atapi_cd_ioctl(struct inode *, struct fd *, int, addr_t);
__loff_t atapi_cd_llseek(struct inode *, __loff_t);
int atapi_cd_init(struct ide *, struct ata_drv *);

#endif /* _FNX_ATAPI_CD_H */
