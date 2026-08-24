/*
 * fnx/kernel/syscalls/rt_sigpending.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (M6 userland): modern rt_sigpending() (Linux #176) - like
 * rt_sigprocmask() it uses a 64-bit signal mask; report the pending
 * set in the low 32 bits.
 */

#include <fnx/fs.h>
#include <fnx/signal.h>
#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_rt_sigpending(void *set, int sigsetsize)
{
	unsigned long long mask64;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigpending(0x%08x, %d)\n", current->pid, set, sigsetsize);
#endif /*__DEBUG__ */

	if(sigsetsize != 8) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, set, 8))) {
		return errno;
	}
	mask64 = current->sigpending;
	memcpy_b(set, &mask64, 8);
	return 0;
}
