/*
 * fnx/kernel/syscalls/close.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/syscalls.h>
#include <fnx/fd.h>
#include <fnx/locks.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

int sys_close(unsigned int ufd)
{
	unsigned int fd;
	struct inode *i;

#ifdef __DEBUG__
	printk("(pid %d) sys_close(%d)\n", current->pid, ufd);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	fd = current->fd[ufd];
	release_user_fd(ufd);

	if(--fd_table[fd].count) {
		return 0;
	}
	i = fd_table[fd].inode;
	flock_release_inode(i);
	if(i->fsop && i->fsop->close) {
		if(fd_table[fd].flags & (O_WRONLY | O_RDWR)) {
			inotify_queue(i, IN_CLOSE_WRITE, 0, NULL);
		} else {
			inotify_queue(i, IN_CLOSE_NOWRITE, 0, NULL);
		}
		i->fsop->close(i, &fd_table[fd]);
		release_fd(fd);
		iput(i);
		return 0;
	}
	printk("WARNING: %s(): ufd %d without the close() method!\n", __FUNCTION__, ufd);
	return -EINVAL;
}
