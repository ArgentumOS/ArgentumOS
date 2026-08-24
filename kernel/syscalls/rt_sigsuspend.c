/*
 * fnx/kernel/syscalls/rt_sigsuspend.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (M6 userland): modern rt_sigsuspend() (Linux #179) - 64-bit
 * signal mask, translated to Fiwix's __sigset_t and then the classic
 * sigsuspend() semantics (temporarily replace the mask and pause).
 */

#include <fnx/fs.h>
#include <fnx/syscalls.h>
#include <fnx/signal.h>
#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_rt_sigsuspend(const void *mask, int sigsetsize)
{
	__sigset_t old_mask;
	unsigned long long mask64;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigsuspend(0x%08x, %d)\n", current->pid, mask, sigsetsize);
#endif /*__DEBUG__ */

	if(sigsetsize != 8) {
		return -EINVAL;
	}

	mask64 = 0;
	if(mask) {
		if((errno = check_user_area(VERIFY_READ, mask, 8))) {
			return errno;
		}
		memcpy_b(&mask64, mask, 8);
	}

	old_mask = current->sigblocked;
	current->sigblocked = (int)((__sigset_t)(mask64 & 0xFFFFFFFFULL)) & SIG_BLOCKABLE;
	sys_pause();
	current->sigblocked = old_mask;
	return -EINTR;
}
