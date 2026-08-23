/*
 * fiwix/kernel/syscalls/newfstat.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/fs.h>
#include <fiwix/statbuf.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_newfstat(unsigned int ufd, struct new_stat *statbuf)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_newfstat(%d, 0x%08x) -> returning structure\n", current->pid, ufd, (unsigned int )statbuf);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	if((errno = check_user_area(VERIFY_WRITE, statbuf, sizeof(struct new_stat)))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;
		fill_new_stat(i, statbuf);
	return 0;
}
