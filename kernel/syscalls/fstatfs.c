/*
 * fnx/kernel/syscalls/fstatfs.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/statfs.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_fstatfs(unsigned int ufd, struct statfs *statfsbuf)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_fstatfs(%d, 0x%08x)\n", current->pid, ufd, (unsigned int)statfsbuf);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	if((errno = check_user_area(VERIFY_WRITE, statfsbuf, sizeof(struct statfs)))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;
	if(i->sb && i->sb->fsop && i->sb->fsop->statfs) {
		i->sb->fsop->statfs(i->sb, statfsbuf);
		return 0;
	}
	return -ENOSYS;
}
