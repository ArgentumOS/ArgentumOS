/*
 * fnx/kernel/syscalls/pause.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/syscalls.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_pause(void)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_pause()\n", current->pid);
#endif /*__DEBUG__ */

	for(;;) {
		if(sleep(&sys_pause, PROC_INTERRUPTIBLE)) {
			break;
		}
	}
	return -EINTR;
}
