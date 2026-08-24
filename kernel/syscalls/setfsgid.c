/*
 * fiwix/kernel/syscalls/setfsgid.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: setfsgid(123) - set the filesystem group ID used by
 * check_group() for inode access checks. Linux semantics mirror
 * setfsuid: succeeds if the new value matches the real, effective,
 * saved or current filesystem GID (or the caller is privileged); returns
 * the PREVIOUS fsgid.
 */

#include <fiwix/types.h>
#include <fiwix/errno.h>
#include <fiwix/process.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_setfsgid(__gid_t fsgid)
{
	__gid_t old_fsgid;

#ifdef __DEBUG__
	printk("(pid %d) sys_setfsgid(%d)\n", current->pid, fsgid);
#endif /*__DEBUG__ */

	old_fsgid = current->fsgid;

	if(fsgid == current->gid || fsgid == current->egid ||
	   fsgid == current->sgid || fsgid == current->fsgid) {
		current->fsgid = fsgid;
	} else if(current->egid == 0) {
		current->fsgid = fsgid;
	}

	return old_fsgid;
}
