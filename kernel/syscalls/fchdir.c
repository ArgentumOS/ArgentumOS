/*
 * fnx/kernel/syscalls/fchdir.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/process.h>
#include <fnx/stat.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_fchdir(unsigned int ufd)
{
	struct inode *i;

#ifdef __DEBUG__
	printk("(pid %d) sys_fchdir(%d)\n", current->pid, ufd);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;
	if(!S_ISDIR(i->i_mode)) {
		return -ENOTDIR;
	}
	/* sys_chdir checks execute permission on the target; do the same
	 * here (a fd to a no-exec dir must not become the cwd) */
	if(check_permission(TO_EXEC, i) < 0) {
		return -EACCES;
	}
	iput(current->pwd);
	current->pwd = i;
	current->pwd->count++;
	return 0;
}
