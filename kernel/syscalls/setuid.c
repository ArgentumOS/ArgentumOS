/*
 * fnx/kernel/syscalls/setuid.c
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

int sys_setuid(__uid_t uid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_setuid(%d)\n", current->pid, uid);
#endif /*__DEBUG__ */

	if(IS_SUPERUSER) {
		current->uid = current->suid = uid;
	} else {
		if((current->uid != uid) && (current->suid != uid)) {
			return -EPERM;
		}
	}
	current->euid = uid;
	current->fsuid = current->euid;
	return 0;
}
