/*
 * fnx/include/fnx/ramdisk.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_RAMDISK_H
#define _FNX_RAMDISK_H

#include <fnx/fs.h>

#define RAMDISK_MAJOR	1	/* ramdisk device major number */
#define RAMDISK_TOTAL	10	/* total number of ramdisk drives */

struct ramdisk {
	char *addr;		/* ramdisk memory address */
	int size;		/* in KB */
};

extern int ramdisk_minors;	/* initrd + RAMDISK_DRIVES + kexec */
extern struct ramdisk ramdisk_table[RAMDISK_TOTAL];

int ramdisk_open(struct inode *, struct fd *);
int ramdisk_close(struct inode *, struct fd *);
int ramdisk_read(__dev_t, __blk_t, char *, int);
int ramdisk_write(__dev_t, __blk_t, char *, int);
int ramdisk_ioctl(struct inode *, struct fd *, int, addr_t);
__loff_t ramdisk_llseek(struct inode *, __loff_t);

void ramdisk_init(void);

#endif /* _FNX_RAMDISK_H */
