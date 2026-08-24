/*
 * fnx/kernel/syscalls/creat.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/syscalls.h>
#include <fnx/fcntl.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_creat(const char *filename, __mode_t mode)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_creat('%s', %d)\n", current->pid, filename, mode);
#endif /*__DEBUG__ */
	return sys_open(filename, O_CREAT | O_WRONLY | O_TRUNC, mode);
}
