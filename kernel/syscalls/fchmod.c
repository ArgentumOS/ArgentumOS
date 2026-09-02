/*
 * fnx/kernel/syscalls/fchmod.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/acl.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_fchmod(unsigned int ufd, __mode_t mode)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_fchmod(%d, %d)\n", current->pid, ufd, mode);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;

	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	if(check_user_permission(i)) {
		return -EPERM;
	}

	/* the access ACL (if any) is the permissions model: chmod edits
	 * its owner/other/mask entries, not just the mode projection */
	if((errno = acl_chmod(i, mode))) {
		return errno;	/* the ACL rewrite failed; do not change the mode */
	}

	i->i_mode &= S_IFMT;
	i->i_mode |= mode & ~S_IFMT;
	i->i_ctime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	return 0;
}
