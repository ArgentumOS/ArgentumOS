/*
 * fnx/kernel/syscalls/setitimer.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/time.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_setitimer(%d, 0x%08x, 0x%08x) -> \n", current->pid, which, (unsigned int)new_value, (unsigned int)old_value);
#endif /*__DEBUG__ */

	if((addr_t)old_value) {
		if((errno = check_user_area(VERIFY_WRITE, old_value, sizeof(struct itimerval)))) {
			return errno;
		}
	}
	if((addr_t)new_value) {
		if((errno = check_user_area(VERIFY_READ, new_value, sizeof(struct itimerval)))) {
			return errno;
		}
		return setitimer(which, new_value, old_value);
	}

	/* new_value == NULL cancels the timer (Linux semantics): kernel
	 * setitimer() derefs the struct, so pass a zeroed one */
	{
		struct itimerval cancel;
		cancel.it_interval.tv_sec = 0;
		cancel.it_interval.tv_usec = 0;
		cancel.it_value.tv_sec = 0;
		cancel.it_value.tv_usec = 0;
		return setitimer(which, &cancel, old_value);
	}
}
