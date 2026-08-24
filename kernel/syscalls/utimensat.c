/*
 * fiwix/kernel/syscalls/utimensat.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: utimensat(280) - change a file's access and modification
 * timestamps. musl's utime()/utimensat() (used by toybox touch) call this.
 * x86-64 ABI: the times argument is a packed array of 4 longs
 * {atime_sec, atime_nsec, mtime_sec, mtime_nsec}; the nsec slots may be
 * UTIME_NOW (0x3fffffff, use current time) or UTIME_OMIT (0x3ffffffe,
 * leave unchanged). times == NULL means set both to the current time.
 */

#include <fiwix/config.h>
#include <fiwix/types.h>
#include <fiwix/errno.h>
#include <fiwix/fs.h>
#include <fiwix/fcntl.h>
#include <fiwix/stat.h>
#include <fiwix/string.h>
#include <fiwix/process.h>
#include <fiwix/kernel.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

#define UTIME_NOW	0x3fffffff
#define UTIME_OMIT	0x3ffffffe

int sys_utimensat(int dirfd, const char *filename, const long *times, int flags)
{
	struct inode *i;
	struct inode *dir;
	char *tmp_name;
	long t[4];
	__time_t atime, mtime;
	int atime_set, mtime_set;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_utimensat('%s', %p, %x)\n", current->pid, filename, (void *)times, flags);
#endif /*__DEBUG__ */

	if(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) {
		return -EINVAL;
	}

	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}

	if(dirfd != AT_FDCWD) {
		if(dirfd < 0) {
			free_name(tmp_name);
			return -EBADF;
		}
		CHECK_UFD(dirfd);
		dir = fd_table[current->fd[dirfd]].inode;
		if(!S_ISDIR(dir->i_mode)) {
			free_name(tmp_name);
			return -ENOTDIR;
		}
		errno = parse_namei(tmp_name, dir, &i, NULL,
			(flags & AT_SYMLINK_NOFOLLOW) ? !FOLLOW_LINKS : FOLLOW_LINKS);
	} else {
		errno = namei(tmp_name, &i, NULL,
			(flags & AT_SYMLINK_NOFOLLOW) ? !FOLLOW_LINKS : FOLLOW_LINKS);
	}
	free_name(tmp_name);
	if(errno) {
		return errno;
	}

	if(IS_RDONLY_FS(i)) {
		iput(i);
		return -EROFS;
	}

	atime_set = mtime_set = 1;
	atime = mtime = CURRENT_TIME;
	if(times) {
		if((errno = check_user_area(VERIFY_READ, times, 4 * sizeof(long)))) {
			iput(i);
			return errno;
		}
		memcpy_b(t, times, 4 * sizeof(long));
		if(t[1] == UTIME_NOW) {
			atime = CURRENT_TIME;
		} else if(t[1] == UTIME_OMIT) {
			atime_set = 0;
		} else {
			if(t[1] < 0 || t[1] >= 1000000000L) {
				iput(i);
				return -EINVAL;
			}
			atime = (__time_t)t[0];
		}
		if(t[3] == UTIME_NOW) {
			mtime = CURRENT_TIME;
		} else if(t[3] == UTIME_OMIT) {
			mtime_set = 0;
		} else {
			if(t[3] < 0 || t[3] >= 1000000000L) {
				iput(i);
				return -EINVAL;
			}
			mtime = (__time_t)t[2];
		}
	}

	if(check_user_permission(i)) {
		iput(i);
		return -EACCES;
	}
	if(atime_set) {
		i->i_atime = atime;
	}
	if(mtime_set) {
		i->i_mtime = mtime;
	}
	iput(i);
	return 0;
}
