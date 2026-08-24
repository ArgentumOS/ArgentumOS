/*
 * fnx/include/fnx/statbuf.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_STATBUF_H
#define _FNX_STATBUF_H

struct new_stat {
	__u64 st_dev;
	__u64 st_ino;
	__u64 st_nlink;
	__u32 st_mode;
	__u32 st_uid;
	__u32 st_gid;
	__u32 __pad0;
	__u64 st_rdev;
	__s64 st_size;
	__s64 st_blksize;
	__s64 st_blocks;
	__s64 st_atime;		/* struct timespec: tv_sec */
	__s64 st_atime_nsec;	/* tv_nsec */
	__s64 st_mtime;
	__s64 st_mtime_nsec;
	__s64 st_ctime;
	__s64 st_ctime_nsec;
	__s64 __unused[3];
};

#endif /* _FNX_STATBUF_H */
