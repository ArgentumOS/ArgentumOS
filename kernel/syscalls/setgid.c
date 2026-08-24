/*
 * fnx/kernel/syscalls/setgid.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_setgid(__gid_t gid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_setgid(%d)\n", current->pid, gid);
#endif /*__DEBUG__ */

	if(IS_SUPERUSER) {
		current->gid = current->egid = current->sgid = gid;
		current->fsgid = current->egid;
	} else {
		if((current->gid == gid) || (current->sgid == gid)) {
			current->egid = gid;
			current->fsgid = current->egid;
		} else {
			return -EPERM;
		}
	}
	return 0;
}
