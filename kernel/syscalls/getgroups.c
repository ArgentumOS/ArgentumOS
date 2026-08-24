/*
 * fnx/kernel/syscalls/getgroups.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_getgroups(__ssize_t size, __gid_t *list)
{
	int n, ngroups, errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_getgroups(%d, 0x%08x)\n", current->pid, size, (unsigned int)list);
#endif /*__DEBUG__ */

	/* count the current supplementary groups first */
	for(ngroups = 0; ngroups < NGROUPS_MAX; ngroups++) {
		if(current->groups[ngroups] == -1) {
			break;
		}
	}

	/*
	 * If size is 0, sys_getgroups() shall return the number of group IDs
	 * that it would otherwise return without modifying the array pointed
	 * to by list.
	 */
	if(!size) {
		return ngroups;
	}

	if(size < 0) {
		return -EINVAL;
	}
	if(size < ngroups) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, list, ngroups * sizeof(__gid_t)))) {
		return errno;
	}
	for(n = 0; n < ngroups; n++) {
		list[n] = (__gid_t)current->groups[n];
	}
	return ngroups;
}
