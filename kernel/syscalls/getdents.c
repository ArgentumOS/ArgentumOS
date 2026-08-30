/*
 * fnx/kernel/syscalls/getdents.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/dirent.h>
#include <fnx/process.h>
#include <fnx/stat.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getdents(unsigned int ufd, struct dirent *dirent, unsigned int count)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_getdents(%d, 0x%08x, %d)", current->pid, ufd, (unsigned int)dirent, count);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	/* the filldir loop writes up to 'count' bytes: verifying only one
	 * dirent let a short user buffer be overrun (kernel write) */
	if((errno = check_user_area(VERIFY_WRITE, dirent, count))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;

	if(!S_ISDIR(i->i_mode)) {
		return -ENOTDIR;
	}

	if(i->fsop && i->fsop->readdir) {
		errno = i->fsop->readdir(i, &fd_table[current->fd[ufd]], dirent, count);
	#ifdef __DEBUG__
		printk(" -> returning %d\n", errno);
	#endif /*__DEBUG__ */
		return errno;
	}
	return -EINVAL;
}
