/*
 * fnx/kernel/syscalls/getrusage.c
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

int sys_getrusage(int who, struct rusage *usage)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_getrusage(%d, 0x%08x)\n", current->pid, who, (unsigned int)usage);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, usage, sizeof(struct rusage)))) {
		return errno;
	}
	switch(who) {
		case RUSAGE_SELF:
			memcpy_b(usage, &current->usage, sizeof(struct rusage));
			break;
		case RUSAGE_CHILDREN:
			memcpy_b(usage, &current->cusage, sizeof(struct rusage));
			break;
		default:
			return -EINVAL;
	}
	return 0;
}
