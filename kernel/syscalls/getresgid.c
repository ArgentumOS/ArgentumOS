/*
 * fnx/kernel/syscalls/getresgid.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: getresgid(120) - return the real, effective and saved group
 * IDs. The three pointers may be NULL (Linux ignores NULL pointers).
 */

#include <fnx/types.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getresgid(__gid_t *rgid, __gid_t *egid, __gid_t *sgid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_getresgid()\n", current->pid);
#endif /*__DEBUG__ */

	if(rgid) {
		if(check_user_area(VERIFY_WRITE, rgid, sizeof(__gid_t))) {
			return -EFAULT;
		}
		*rgid = current->gid;
	}
	if(egid) {
		if(check_user_area(VERIFY_WRITE, egid, sizeof(__gid_t))) {
			return -EFAULT;
		}
		*egid = current->egid;
	}
	if(sgid) {
		if(check_user_area(VERIFY_WRITE, sgid, sizeof(__gid_t))) {
			return -EFAULT;
		}
		*sgid = current->sgid;
	}
	return 0;
}
