/*
 * fnx/kernel/syscalls/sync.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/buffer.h>
#include <fnx/xbfs.h>
#include <fnx/filesystems.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

void sys_sync(void)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_sync()\n", current->pid);
#endif /*__DEBUG__ */

	sync_superblocks(0);	/* in all devices */
	sync_inodes(0);		/* in all devices */
	xbfs_flush_all();	/* close any group-commit batches the inode
				 * sync re-populated (a full buffer sync
				 * mid-batch would flush their dirty real
				 * blocks ahead of the publish) */
	sync_buffers(0);	/* in all devices */
	return;
}
