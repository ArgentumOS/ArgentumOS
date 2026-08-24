/*
 * fnx/kernel/syscalls/setfsuid.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: setfsuid(122) - set the filesystem user ID used by
 * check_permission() for inode access checks. Linux semantics: the call
 * succeeds if the new value matches the real, effective, saved or current
 * filesystem UID (or the caller is privileged); it returns the PREVIOUS
 * fsuid (callers use the return to detect failure). Unlike setuid(), a
 * non-privileged caller can set fsuid back to the real/saved/effective
 * uid even if the effective uid changed.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/process.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_setfsuid(__uid_t fsuid)
{
	__uid_t old_fsuid;

#ifdef __DEBUG__
	printk("(pid %d) sys_setfsuid(%d)\n", current->pid, fsuid);
#endif /*__DEBUG__ */

	old_fsuid = current->fsuid;

	if(fsuid == current->uid || fsuid == current->euid ||
	   fsuid == current->suid || fsuid == current->fsuid) {
		current->fsuid = fsuid;
	} else if(current->euid == 0) {
		current->fsuid = fsuid;
	}

	return old_fsuid;
}
