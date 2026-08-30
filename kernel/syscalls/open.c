/*
 * fnx/kernel/syscalls/open.c
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/syscalls.h>
#include <fnx/stat.h>
#include <fnx/types.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

static int do_sys_open(int dirfd, const char *filename, int flags, __mode_t mode);

int sys_open(const char *filename, int flags, __mode_t mode)
{
	return do_sys_open(AT_FDCWD, filename, flags, mode);
}

/* openat(257): like open, but resolve relative paths against the
 * directory referenced by dirfd (musl openat, toybox dirtree).
 * dirfd == AT_FDCWD (-100) means relative to the current working dir. */
int sys_openat(int dirfd, const char *filename, int flags, __mode_t mode)
{
	return do_sys_open(dirfd, filename, flags, mode);
}

static int do_sys_open(int dirfd, const char *filename, int flags, __mode_t mode)
{
	int fd, ufd;
	struct inode *i, *dir;
	char *tmp_name, *basename;
	int errno, follow_links, perms;

	/* open() follows symlinks (O_NOFOLLOW is handled below); this was
	 * uninitialized stack garbage before, which made symlink following
	 * nondeterministic (cat would read the link itself when the stack
	 * slot happened to be zero) */
	follow_links = FOLLOW_LINKS;

#ifdef __DEBUG__
	printk("(pid %d) sys_open('%s', %o, %o)\n", current->pid, filename, flags, mode);
#endif /*__DEBUG__ */

	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}

	basename = get_basename(tmp_name);
	/* namei() NULLs both result pointers before parse_namei(); do the same
	 * or the error path iput(*d_res) reads uninitialized stack garbage. */
	i = NULL;
	dir = NULL;
	if(dirfd == AT_FDCWD) {
		if((errno = parse_namei(tmp_name, NULL, &i, &dir, follow_links))) {
			if(!dir) {
				free_name(tmp_name);
				if(flags & O_CREAT) {
					return -ENOENT;
				}
				return errno;
			}
		}
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
		/* dir (the d_res out-param) must stay NULL here: parse_namei /
		 * do_namei rely on it being NULL on entry, or the first
		 * iteration iputs the fd's reference (double-free). */
		if((errno = parse_namei(tmp_name, base_dir, &i, &dir, follow_links))) {
			if(!dir) {
				free_name(tmp_name);
				if(flags & O_CREAT) {
					return -ENOENT;
				}
				return errno;
			}
		}
	}

#ifdef __DEBUG__
	printk("\t(inode = %d)\n", i ? i->inode : -1);
#endif /*__DEBUG__ */

	if(!errno) {
		if(S_ISLNK(i->i_mode) && (flags & O_NOFOLLOW)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -ELOOP;
		}
		if(!S_ISDIR(i->i_mode) && (flags & O_DIRECTORY)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -ENOTDIR;
		}
	}

	if(flags & O_CREAT) {
		if(!errno && S_ISDIR(i->i_mode)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -EISDIR;
		}
		if(!errno && (flags & O_EXCL)) {
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return -EEXIST;
		}
		if(!i) {
			/*
			 * If the file does not exist, you need enough
			 * permission in the directory to create it.
			 */
			if(check_permission(TO_EXEC | TO_WRITE, dir) < 0) {
				iput(i);
				iput(dir);
				free_name(tmp_name);
				return -EACCES;
			}
		}
		if(IS_RDONLY_FS(dir)) {
			if(!i || S_ISREG(i->i_mode)) {
				if(flags & (O_RDWR | O_WRONLY | O_TRUNC)) {
					iput(i);
					iput(dir);
					free_name(tmp_name);
					return -EROFS;
				}
				flags &= ~(O_RDWR | O_WRONLY | O_TRUNC);
				flags |= O_RDONLY;
			}
		}
		if(errno) {	/* assumes -ENOENT */
			if(dir->fsop && dir->fsop->create) {
				errno = dir->fsop->create(dir, basename, flags, mode, &i);
				if(errno) {
					iput(dir);
					free_name(tmp_name);
					return errno;
				}
				inotify_queue(dir, IN_CREATE, 0, basename);
			} else {
				iput(dir);
				free_name(tmp_name);
				return -EACCES;
			}
		}
	} else {
		if(errno) {
			iput(dir);
			free_name(tmp_name);
			return errno;
		}
		if(flags & (O_RDWR | O_WRONLY | O_TRUNC)) {
			if(S_ISDIR(i->i_mode)) {
				iput(i);
				iput(dir);
				free_name(tmp_name);
				return -EISDIR;
			}
			if(S_ISREG(i->i_mode) && IS_RDONLY_FS(dir)) {
				iput(i);
				iput(dir);
				free_name(tmp_name);
				return -EROFS;
			}
		}
		mode = 0;
	}

	if((flags & O_ACCMODE) == O_RDONLY) {
		perms = TO_READ;
	} else if((flags & O_ACCMODE) == O_WRONLY) {
		perms = TO_WRITE;
	} else {
		perms = TO_READ | TO_WRITE;
	}
	/* O_TRUNC modifies the file: it must require write permission even
	 * when combined with O_RDONLY (a reader must not be able to zero a
	 * file it cannot write) */
	if(flags & O_TRUNC) {
		perms |= TO_WRITE;
	}
	if((errno = check_permission(perms, i))) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return errno;
	}
	if((fd = get_new_fd(i)) < 0) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return fd;
	}
	if((ufd = get_new_user_fd(0)) < 0) {
		release_fd(fd);
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return ufd;
	}

#ifdef __DEBUG__
	printk("\t(ufd = %d)\n", ufd);
#endif /*__DEBUG__ */

	fd_table[fd].flags = flags & ~O_CLOEXEC;
	current->fd[ufd] = fd;
	if(flags & O_CLOEXEC) {
		current->fd_flags[ufd] |= FD_CLOEXEC;
	}
	if(i->fsop && i->fsop->open) {
		inotify_queue(i, IN_OPEN, 0, NULL);
		if((errno = i->fsop->open(i, &fd_table[fd])) < 0) {
			release_fd(fd);
			release_user_fd(ufd);
			iput(i);
			iput(dir);
			free_name(tmp_name);
			return errno;
		}
		iput(dir);
		free_name(tmp_name);
		return ufd;
	}

	printk("WARNING: %s(): file '%s' (inode %d) without the open() method!\n", __FUNCTION__, tmp_name, i->inode);
	release_fd(fd);
	release_user_fd(ufd);
	iput(i);
	iput(dir);
	free_name(tmp_name);
	return -EINVAL;
}
