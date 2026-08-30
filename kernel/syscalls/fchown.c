/*
 * fnx/kernel/syscalls/fchown.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_fchown(unsigned int ufd, __uid_t owner, __gid_t group)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_fchown(%d, %d, %d)\n", current->pid, ufd, owner, group);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;

	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	if((errno = check_chown_permission(i, owner, group))) {
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
	i->i_ctime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	return 0;
}
