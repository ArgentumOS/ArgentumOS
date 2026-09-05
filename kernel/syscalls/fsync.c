/*
 * fnx/kernel/syscalls/fsync.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/xbfs.h>
#include <fnx/filesystems.h>
#include <fnx/process.h>
#include <fnx/stat.h>
#include <fnx/buffer.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_fsync(unsigned int ufd)
{
	struct inode *i;

#ifdef __DEBUG__
	printk("(pid %d) sys_fsync(%d)\n", current->pid, ufd);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;
	if(!S_ISREG(i->i_mode)) {
		return -EINVAL;
	}
	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	sync_superblocks(i->dev);
	sync_inodes(i->dev);
	xbfs_flush_all();
	sync_buffers(i->dev);
	return 0;
}
