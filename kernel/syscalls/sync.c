/*
 * fnx/kernel/syscalls/sync.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/buffer.h>
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
	sync_buffers(0);	/* in all devices */
	return;
}
