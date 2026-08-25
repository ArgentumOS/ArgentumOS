/*
 * fnx/kernel/syscalls/getpid.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/process.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getpid(void)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_getpid() -> %d\n", current->pid, current->tgid);
#endif /*__DEBUG__ */
	return current->tgid;
}

int sys_gettid(void)
{
	return current->pid;
}
