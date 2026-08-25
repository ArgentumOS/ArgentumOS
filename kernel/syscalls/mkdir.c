/*
 * fnx/kernel/syscalls/mkdir.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_mkdir(const char *dirname, __mode_t mode)
{
	struct inode *i, *dir;
	char *tmp_dirname, *basename;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_mkdir('%s', %o)\n", current->pid, dirname, mode);
#endif /*__DEBUG__ */

	if((errno = malloc_name(dirname, &tmp_dirname)) < 0) {
		return errno;
	}
	basename = remove_trailing_slash(tmp_dirname);
	if((errno = namei(basename, &i, &dir, !FOLLOW_LINKS))) {
		if(!dir) {
			free_name(tmp_dirname);
			return errno;
		}
	}
	if(!errno) {
		iput(i);
		iput(dir);
		free_name(tmp_dirname);
		return -EEXIST;
	}
	if(IS_RDONLY_FS(dir)) {
		iput(dir);
		free_name(tmp_dirname);
		return -EROFS;
	}

	if(check_permission(TO_EXEC | TO_WRITE, dir) < 0) {
		iput(dir);
		free_name(tmp_dirname);
		return -EACCES;
	}

	basename = get_basename(basename);
	if(dir->fsop && dir->fsop->mkdir) {
		errno = dir->fsop->mkdir(dir, basename, mode);
	} else {
		errno = -EPERM;
	}
	if(!errno) {
		inotify_queue(dir, IN_CREATE | IN_ISDIR, 0, basename);
	}
	iput(dir);
	free_name(tmp_dirname);
	return errno;
}

/* mkdirat(258): create a directory relative to dirfd (AT_FDCWD = cwd) */
int sys_mkdirat(int dirfd, const char *dirname, __mode_t mode)
{
	struct inode *i, *dir;
	char *tmp_dirname, *basename;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_mkdirat(%d, '%s', %o)\n", current->pid, dirfd, dirname, mode);
#endif /*__DEBUG__ */

	if((errno = malloc_name(dirname, &tmp_dirname)) < 0) {
		return errno;
	}
	basename = remove_trailing_slash(tmp_dirname);
	if(dirfd != AT_FDCWD) {
		if(dirfd < 0) {
			free_name(tmp_dirname);
			return -EBADF;
		}
		CHECK_UFD(dirfd);
		dir = fd_table[current->fd[dirfd]].inode;
		if(!S_ISDIR(dir->i_mode)) {
			free_name(tmp_dirname);
			return -ENOTDIR;
		}
		errno = parse_namei(basename, dir, &i, &dir, !FOLLOW_LINKS);
		if(errno) {
			if(!dir) {
				free_name(tmp_dirname);
				return errno;
			}
		}
	} else {
		if((errno = namei(basename, &i, &dir, !FOLLOW_LINKS))) {
			if(!dir) {
				free_name(tmp_dirname);
				return errno;
			}
		}
	}
	if(!errno) {
		iput(i);
		iput(dir);
		free_name(tmp_dirname);
		return -EEXIST;
	}
	if(IS_RDONLY_FS(dir)) {
		iput(dir);
		free_name(tmp_dirname);
		return -EROFS;
	}
	if(check_permission(TO_EXEC | TO_WRITE, dir) < 0) {
		iput(dir);
		free_name(tmp_dirname);
		return -EACCES;
	}
	basename = get_basename(basename);
	if(dir->fsop && dir->fsop->mkdir) {
		errno = dir->fsop->mkdir(dir, basename, mode);
	} else {
		errno = -EPERM;
	}
	if(!errno) {
		inotify_queue(dir, IN_CREATE | IN_ISDIR, 0, basename);
	}
	iput(dir);
	free_name(tmp_dirname);
	return errno;
}
