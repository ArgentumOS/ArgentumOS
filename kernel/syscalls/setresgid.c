/*
 * fiwix/kernel/syscalls/setresgid.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: setresgid(119) - set real, effective and saved group IDs.
 * Any argument may be -1 to leave that ID unchanged (mirror of
 * sys_setresuid with group IDs).
 */

#include <fiwix/types.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_setresgid(__gid_t rgid, __gid_t egid, __gid_t sgid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_setresgid(%d, %d, %d)\n", current->pid, rgid, egid, sgid);
#endif /*__DEBUG__ */

	if(!IS_SUPERUSER) {
		if(rgid != (__gid_t)-1 && rgid != current->gid && rgid != current->egid && rgid != current->sgid) {
			return -EPERM;
		}
		if(egid != (__gid_t)-1 && egid != current->gid && egid != current->egid && egid != current->sgid) {
			return -EPERM;
		}
		if(sgid != (__gid_t)-1 && sgid != current->gid && sgid != current->egid && sgid != current->sgid) {
			return -EPERM;
		}
	}

	if(rgid != (__gid_t)-1) {
		current->gid = rgid;
	}
	if(egid != (__gid_t)-1) {
		current->egid = egid;
	}
	if(sgid != (__gid_t)-1) {
		current->sgid = sgid;
	}
	return 0;
}
