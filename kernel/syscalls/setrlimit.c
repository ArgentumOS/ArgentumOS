/*
 * fnx/kernel/syscalls/setrlimit.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/resource.h>
#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_setrlimit(int resource, const struct rlimit *rlim)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_setrlimit(%d, 0x%08x)\n", current->pid, resource, (unsigned int)rlim);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_READ, rlim, sizeof(struct rlimit)))) {
		return errno;
	}
	if(resource < 0 || resource >= RLIM_NLIMITS) {
		return -EINVAL;
	}
	if(rlim->rlim_cur > rlim->rlim_max) {
		return -EINVAL;
	}
	if(!IS_SUPERUSER) {
		if(rlim->rlim_max > current->rlim[resource].rlim_max) {
			return -EPERM;
		}
	}
	/* copy the whole struct through the fault-recovering helper: the
	 * value is re-read below, so a TOCTOU against the check above could
	 * smuggle a larger hard limit past a non-root caller */
	{
		struct rlimit new_rlim;
		if((errno = copy_from_user(&new_rlim, rlim, sizeof(struct rlimit))) < 0) {
			return errno;
		}
		if(new_rlim.rlim_cur > new_rlim.rlim_max) {
			return -EINVAL;
		}
		if(!IS_SUPERUSER) {
			if(new_rlim.rlim_max > current->rlim[resource].rlim_max) {
				return -EPERM;
			}
		}
		memcpy_b(&current->rlim[resource], &new_rlim, sizeof(struct rlimit));
	}
	return 0;
}
