/*
 * fnx/include/fnx/statfs.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_STATFS_H
#define _FNX_STATFS_H

#include <fnx/types.h>

typedef struct {
	int val[2];
} fsid_t;

struct statfs {
	int f_type;
	int f_bsize;
	int f_blocks;
	int f_bfree;
	int f_bavail;
	int f_files;
	int f_ffree;
	fsid_t f_fsid;
	int f_namelen;
	int f_spare[6];
};

/* x86-64 statfs ABI: 64-bit fields, 120 bytes total. The fsop->statfs()
 * callbacks fill the 32-bit-era struct above; sys_statfs64/fstatfs64
 * expand into this layout for native x86_64 userland. */
struct fnx_statfs64 {
	__s64 f_type;
	__s64 f_bsize;
	__u64 f_blocks;
	__u64 f_bfree;
	__u64 f_bavail;
	__u64 f_files;
	__u64 f_ffree;
	__s32 f_fsid[2];
	__s64 f_namelen;
	__s64 f_frsize;
	__s64 f_flags;
	__s64 f_spare[4];
};

#endif /* _FNX_STATFS_H */
