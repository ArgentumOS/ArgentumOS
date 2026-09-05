/*
 * fnx/kernel/syscalls/fchownat.c
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 *
 * musl implements chown() and lchown() as fchownat(AT_FDCWD, path, ...):
 * without this syscall toybox's `chown -R` (and thus useradd's home
 * ownership pass) fails with ENOSYS. AT_SYMLINK_NOFOLLOW selects the
 * lchown variant; the dirfd forms resolve relative paths like
 * fchmodat does.
 */

#include <fnx/types.h>
#include <fnx/kernel.h>
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

int sys_fchownat(int dirfd, const char *filename, __uid_t owner,
		 __gid_t group, int flags)
{
	struct inode *i, *dir;
	char *tmp_name;
	int errno, follow = FOLLOW_LINKS;

#ifdef __DEBUG__
	printk("(pid %d) sys_fchownat(%d, '%s', %d, %d, %x)\n", current->pid,
		dirfd, filename, owner, group, flags);
#endif /*__DEBUG__ */

	if(flags & AT_SYMLINK_NOFOLLOW) {
		follow = !FOLLOW_LINKS;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}

	/* namei() NULLs both result pointers before parse_namei(); do the
	 * same or the error path iput(*d_res) reads uninitialized stack
	 * garbage (see do_sys_open). */
	i = NULL;
	dir = NULL;
	if(dirfd == AT_FDCWD) {
		if((errno = parse_namei(tmp_name, NULL, &i, &dir, follow))) {
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
		if((errno = parse_namei(tmp_name, base_dir, &i, &dir, follow))) {
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
	if((errno = check_chown_permission(i, owner, group))) {
		iput(i);
		iput(dir);
		free_name(tmp_name);
		return errno;
	}

	if(owner == (__uid_t)-1) {
		owner = i->i_uid;
	}
	if(group == (__gid_t)-1) {
		group = i->i_gid;
	}

	i->i_uid = owner;
	i->i_gid = group;
	i->i_ctime = CURRENT_TIME;	/* chown always updates ctime */
	i->state |= INODE_DIRTY;
	inotify_queue(i, IN_ATTRIB, 0, NULL);
	iput(i);
	iput(dir);
	free_name(tmp_name);
	return 0;
}
