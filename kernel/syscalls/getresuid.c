/*
 * fiwix/kernel/syscalls/getresuid.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: getresuid(118) - return the real, effective and saved user
 * IDs. The three pointers may be NULL (Linux ignores NULL pointers).
 */

#include <fiwix/types.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_getresuid(__uid_t *ruid, __uid_t *euid, __uid_t *suid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_getresuid()\n", current->pid);
#endif /*__DEBUG__ */

	if(ruid) {
		if(check_user_area(VERIFY_WRITE, ruid, sizeof(__uid_t))) {
			return -EFAULT;
		}
		*ruid = current->uid;
	}
	if(euid) {
		if(check_user_area(VERIFY_WRITE, euid, sizeof(__uid_t))) {
			return -EFAULT;
		}
		*euid = current->euid;
	}
	if(suid) {
		if(check_user_area(VERIFY_WRITE, suid, sizeof(__uid_t))) {
			return -EFAULT;
		}
		*suid = current->suid;
	}
	return 0;
}
