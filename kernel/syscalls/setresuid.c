/*
 * fiwix/kernel/syscalls/setresuid.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: setresuid(117) - set real, effective and saved user IDs.
 * Any argument may be -1 to leave that ID unchanged. Linux semantics:
 * only superuser may set arbitrary IDs; a non-root process may only set
 * each ID to the current real/effective/saved value.
 */

#include <fiwix/types.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_setresuid(__uid_t ruid, __uid_t euid, __uid_t suid)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_setresuid(%d, %d, %d)\n", current->pid, ruid, euid, suid);
#endif /*__DEBUG__ */

	if(!IS_SUPERUSER) {
		if(ruid != (__uid_t)-1 && ruid != current->uid && ruid != current->euid && ruid != current->suid) {
			return -EPERM;
		}
		if(euid != (__uid_t)-1 && euid != current->uid && euid != current->euid && euid != current->suid) {
			return -EPERM;
		}
		if(suid != (__uid_t)-1 && suid != current->uid && suid != current->euid && suid != current->suid) {
			return -EPERM;
		}
	}

	if(ruid != (__uid_t)-1) {
		current->uid = ruid;
	}
	if(euid != (__uid_t)-1) {
		current->euid = euid;
		current->fsuid = current->euid;
	}
	if(suid != (__uid_t)-1) {
		current->suid = suid;
	}
	return 0;
}
