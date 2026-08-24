/*
 * fnx/kernel/syscalls/readlink.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_readlink(const char *filename, char *buffer, __size_t bufsize)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_readlink(%s, 0x%08x, %d)\n", current->pid, filename, (unsigned int)buffer, bufsize);
#endif /*__DEBUG__ */

	if(bufsize <= 0) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, buffer, bufsize))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, !FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	free_name(tmp_name);

	if(!S_ISLNK(i->i_mode)) {
		iput(i);
		return -EINVAL;
	}

	if(i->fsop && i->fsop->readlink) {
		errno = i->fsop->readlink(i, buffer, bufsize);
		iput(i);
		return errno;
	}
	iput(i);
	return -EINVAL;
}

/* readlinkat(267): musl readlink()/toybox ls -l symlink resolution. */
int sys_readlinkat(int dirfd, const char *filename, char *buffer, __size_t bufsize)
{
	struct inode *i;
	char *tmp_name;
	int errno;

	if(bufsize <= 0) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, buffer, bufsize))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if(dirfd != AT_FDCWD) {
		/* relative against an open directory fd (ls -l resolves each
		 * entry's symlink target via the dir's fd) */
		struct inode *dir;

		CHECK_UFD(dirfd);
		dir = fd_table[current->fd[dirfd]].inode;
		if(!S_ISDIR(dir->i_mode)) {
			free_name(tmp_name);
			return -ENOTDIR;
		}
		errno = parse_namei(tmp_name, dir, &i, NULL, !FOLLOW_LINKS);
	} else {
		errno = namei(tmp_name, &i, NULL, !FOLLOW_LINKS);
	}
	free_name(tmp_name);
	if(errno) {
		return errno;
	}

	if(!S_ISLNK(i->i_mode)) {
		iput(i);
		return -EINVAL;
	}

	if(i->fsop && i->fsop->readlink) {
		errno = i->fsop->readlink(i, buffer, bufsize);
	} else {
		errno = -EINVAL;
	}
	iput(i);
	return errno;
}
