/*
 * fiwix/include/fiwix/statbuf.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_STATBUF_H
#define _FIWIX_STATBUF_H

struct old_stat {
	__dev_t st_dev;
	unsigned short int st_ino;
	__mode_t st_mode;
	__nlink_t st_nlink;
	__uid_t st_uid;
	__gid_t st_gid;
	__dev_t st_rdev;
	unsigned int st_size;
	__time_t st_atime;
	__time_t st_mtime;
	__time_t st_ctime;
};

/* Fiwix64: the native 64-bit syscall table (stat=4, fstat=5, lstat=6)
 * must fill the x86-64 ABI 'struct stat' (musl arch/x86_64/bits/stat.h).
 * The old i386 'new_stat' layout (16-bit dev/mode/nlink, 32-bit size)
 * made userspace read garbage for st_mode (offset 24) etc., so every
 * stat()/execvp() PATH probe failed with EACCES. */
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

struct stat64 {
	unsigned long long int st_dev;
	int __st_dev_padding;
	int __st_ino_truncated;
	unsigned int st_mode;
	unsigned int st_nlink;
	unsigned int st_uid;
	unsigned int st_gid;
	unsigned long long int st_rdev;
	int __st_rdev_padding;
	long long int st_size;
	int st_blksize;
	long long int st_blocks;
	int st_atime;
	int st_atime_nsec;
	int st_mtime;
	int st_mtime_nsec;
	int st_ctime;
	int st_ctime_nsec;
	unsigned long long int st_ino;
};

#endif /* _FIWIX_STATBUF_H */
