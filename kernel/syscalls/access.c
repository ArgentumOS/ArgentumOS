/*
 * fiwix/kernel/syscalls/access.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/types.h>
#include <fiwix/fs.h>
#include <fiwix/fcntl.h>
#include <fiwix/stat.h>
#include <fiwix/errno.h>
#include <fiwix/string.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#include <fiwix/process.h>
#endif /*__DEBUG__ */

int sys_access(const char *filename, __mode_t mode)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_access('%s', %d)", current->pid, filename, mode);
#endif /*__DEBUG__ */

	if((mode & S_IRWXO) != mode) {
		return -EINVAL;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	current->flags |= PF_USEREAL;
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		current->flags &= ~PF_USEREAL;
		free_name(tmp_name);
		return errno;
	}
	if(mode & TO_WRITE) {
		if(S_ISREG(i->i_mode) || S_ISDIR(i->i_mode) || S_ISLNK(i->i_mode)) {
			if(IS_RDONLY_FS(i)) {
				current->flags &= ~PF_USEREAL;
				iput(i);
				free_name(tmp_name);
				return -EROFS;
			}
		}
	}
	errno = check_permission(mode, i);

#ifdef __DEBUG__
	printk(" -> returning %d\n", errno);
#endif /*__DEBUG__ */

	current->flags &= ~PF_USEREAL;
	iput(i);
	free_name(tmp_name);
	return errno;
}

/* faccessat(269): musl's faccessat()/eaccess() — dash's `test -x/-r/-w`
 * uses it with AT_EACCESS. dirfd is AT_FDCWD for path-based calls; flags
 * AT_EACCESS means use the effective uid/gid (the plain access() syscall
 * uses the real uid via PF_USEREAL). */
int sys_faccessat(int dirfd, const char *filename, __mode_t mode, int flags)
{
	struct inode *i;

	char *tmp_name;
	int errno;

	if((mode & S_IRWXO) != mode) {
		return -EINVAL;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if(dirfd != AT_FDCWD) {
		struct inode *dir;

		if(dirfd < 0) {		/* any negative dirfd other than AT_FDCWD */
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
	/* AT_EACCESS → effective uid (no PF_USEREAL); plain faccessat without
	 * the flag behaves like access() and uses the real uid. */
	if(!(flags & AT_EACCESS)) {
		current->flags |= PF_USEREAL;
	}
	if(mode & TO_WRITE) {
		if(S_ISREG(i->i_mode) || S_ISDIR(i->i_mode) || S_ISLNK(i->i_mode)) {
			if(IS_RDONLY_FS(i)) {
				current->flags &= ~PF_USEREAL;
				iput(i);
				return -EROFS;
			}
		}
	}
	errno = check_permission(mode, i);
	current->flags &= ~PF_USEREAL;
	iput(i);
	return errno;
}
