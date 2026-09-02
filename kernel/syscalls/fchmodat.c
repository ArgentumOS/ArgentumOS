/*
 * fnx/kernel/syscalls/fchmodat.c
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/string.h>
#include <fnx/acl.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_fchmodat(int dirfd, const char *filename, __mode_t mode)
{
	struct inode *i, *dir;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_fchmodat(%d, '%s', %o)\n", current->pid,
		dirfd, filename, mode);
#endif /*__DEBUG__ */

	/* note: musl's fchmodat() passes only 3 syscall args (dirfd, path,
	 * mode); AT_SYMLINK_NOFOLLOW is resolved inside libc, never here.
	 * A 4th declared parameter would read a garbage register. */
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}

	/* namei() NULLs both result pointers before parse_namei(); do the
	 * same or the error path iput(*d_res) reads uninitialized stack
	 * garbage (see do_sys_open). */
	i = NULL;
	dir = NULL;
	if(dirfd == AT_FDCWD) {
		if((errno = parse_namei(tmp_name, NULL, &i, &dir, FOLLOW_LINKS))) {
			if(!dir) {
				free_name(tmp_name);
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
		/* dir must stay NULL on entry (see do_sys_open) */
		if((errno = parse_namei(tmp_name, base_dir, &i, &dir, FOLLOW_LINKS))) {
			if(!dir) {
				free_name(tmp_name);
				return errno;
			}
		}
	}
	if(errno) {
		/* a component denied search: parse_namei left dir set */
		iput(dir);
		free_name(tmp_name);
		return errno;
	}
	if(!i) {
		iput(dir);
		free_name(tmp_name);
		return -ENOENT;
	}

	if(IS_RDONLY_FS(i)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EROFS;
	}
	if(check_user_permission(i)) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return -EPERM;
	}

	/* the access ACL (if any) is the permissions model: chmod edits
	 * its owner/other/mask entries, not just the mode projection */
	if((errno = acl_chmod(i, mode))) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return errno;	/* the ACL rewrite failed; do not change the mode */
	}

	i->i_mode &= S_IFMT;
	i->i_mode |= mode & ~S_IFMT;
	i->i_ctime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	inotify_queue(i, IN_ATTRIB, 0, NULL);
	iput(i);
	iput(dir);
	free_name(tmp_name);
	return 0;
}
