/*
 * fnx/kernel/syscalls/getpgid.c
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getpgid(__pid_t pid)
{
	struct proc *p;

#ifdef __DEBUG__
	printk("(pid %d) sys_getpgid(%d)\n", current->pid, pid);
#endif /*__DEBUG__ */

	if(pid < 0) {
		return -EINVAL;
	}
	if(!pid) {
		return current->pgid;
	}
	FOR_EACH_PROCESS(p) {
		if(p->pid == pid) {
			return p->pgid;
		}
		p = p->next;
	}
	return -ESRCH;
}
