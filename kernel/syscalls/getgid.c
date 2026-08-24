/*
 * fnx/kernel/syscalls/getgid.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/process.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getgid(void)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_getgid() -> %d\n", current->pid, current->gid);
#endif /*__DEBUG__ */
	return current->gid;
}
