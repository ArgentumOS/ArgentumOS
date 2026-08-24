/*
 * fnx/include/fnx/dirent.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_DIRENT_H
#define _FNX_DIRENT_H

#include <fnx/types.h>
#include <fnx/limits.h>

struct dirent {
#ifdef __x86_64__
	/* FNX: this struct crosses the 32-bit user ABI via sys_getdents.
	 * The kernel's native __ino_t/__off_t are 8 bytes on x86-64, but the
	 * Linux i386 `struct linux_dirent` uses 32-bit ino/off, so pin them. */
	unsigned int d_ino;			/* inode number */
	unsigned int d_off;			/* offset to next dirent */
#else
	__ino_t d_ino;			/* inode number */
	__off_t d_off;			/* offset to next dirent */
#endif /* __x86_64__ */
	unsigned short int d_reclen;	/* length of this dirent */
	char d_name[NAME_MAX + 1];	/* file name (null-terminated) */
};

struct dirent64 {
	__ino64_t d_ino;		/* inode number */
	__loff_t d_off;			/* offset to next dirent */
	unsigned short d_reclen;	/* length of this dirent */
	unsigned char d_type;		/* file type */
	char d_name[];			/* file name (null-terminated) */
};

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#endif /* _FNX_DIRENT_H */
