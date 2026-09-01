/*
 * fnx/kernel/syscalls/unlink.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/syscalls.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

/*
 * unlinkat(dirfd, pathname, flags): like unlink, but resolves relative
 * paths against the directory referenced by dirfd (musl/toybox rm use
 * this). flags != 0 (AT_REMOVEDIR) is handled by sys_rmdir when the
 * target is a directory.
 */
int sys_unlinkat(int dirfd, const char *filename, int flags)
{
	struct inode *i, *dir;
	char *tmp_name, *basename;
	int errno;

	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	i = NULL;
	dir = NULL;
	if(dirfd == AT_FDCWD) {
		errno = parse_namei(tmp_name, NULL, &i, &dir, !FOLLOW_LINKS);
	} else {
		struct inode *base_dir;

		if(dirfd < 0) {
			free_name(tmp_name);
			return -EBADF;
		}
		CHECK_UFD(dirfd);
		base_dir = fd_table[current->fd[dirfd]].inode;
		if(!S_ISDIR(base_dir->i_mode)) {
			free_name(tmp_name);
			return -ENOTDIR;
		}
		if((errno = parse_namei(tmp_name, base_dir, &i, &dir, !FOLLOW_LINKS))) {
			if(dir) {
				iput(dir);
			}
			free_name(tmp_name);
			return errno;
		}
	}
	if(errno) {
		if(dir) {
			iput(dir);
		}
		free_name(tmp_name);
		return errno;
	}

	if(S_ISDIR(i->i_mode)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EPERM;	/* Linux returns -EISDIR; sys_rmdir is the dir path */
	}
	if(flags && !(flags & AT_REMOVEDIR)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EINVAL;
	}

	if(IS_RDONLY_FS(i)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EROFS;
	}
	if(check_permission(TO_EXEC | TO_WRITE, dir) < 0) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EACCES;
	}

	/* check sticky permission bit (missing here let any user in a
	 * sticky directory unlink other users' files) */
	if(dir->i_mode & S_ISVTX) {
		if(check_user_permission(i)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -EPERM;
		}
	}

	basename = get_basename(tmp_name);
	if(dir->fsop && dir->fsop->unlink) {
		errno = dir->fsop->unlink(dir, i, basename);
	} else {
		errno = -EPERM;
	}
	if(!errno) {
		inotify_queue(dir, IN_DELETE, 0, basename);
		inotify_queue(i, IN_DELETE_SELF, 0, NULL);
	}
	iput(i);
	iput(dir);
	free_name(tmp_name);
	return errno;
}

int sys_unlink(const char *filename)
{
	struct inode *i, *dir;
	char *tmp_name, *basename;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_unlink('%s')\n", current->pid, filename);
#endif /*__DEBUG__ */

	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, &dir, !FOLLOW_LINKS))) {
		if(dir) {
			iput(dir);
		}
		free_name(tmp_name);
		return errno;
	}
	if(S_ISDIR(i->i_mode)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EPERM;	/* Linux returns -EISDIR */
	}
	if(IS_RDONLY_FS(i)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EROFS;
	}
	if(check_permission(TO_EXEC | TO_WRITE, dir) < 0) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EACCES;
	}

	/* check sticky permission bit */
	if(dir->i_mode & S_ISVTX) {
		if(check_user_permission(i)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -EPERM;
		}
	}

	basename = get_basename(filename);
	if(dir->fsop && dir->fsop->unlink) {
		errno = dir->fsop->unlink(dir, i, basename);
	} else {
		errno = -EPERM;
	}
	if(!errno) {
		inotify_queue(dir, IN_DELETE, 0, basename);
		inotify_queue(i, IN_DELETE_SELF, 0, NULL);
	}
	iput(i);
	iput(dir);
	free_name(tmp_name);
	return errno;
}
