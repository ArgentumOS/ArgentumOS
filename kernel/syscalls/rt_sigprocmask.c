/*
 * fiwix/kernel/syscalls/rt_sigprocmask.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (pure x86-64 port): SYS_rt_sigprocmask (#14). musl passes a
 * 128-byte sigset_t (sigsetsize = 128); the kernel keeps a 32-bit
 * __sigset_t (NSIG == 32), so translate the low word.
 */

#include <fiwix/fs.h>
#include <fiwix/signal.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_rt_sigprocmask(int how, const void *set, void *oldset, int sigsetsize)
{
	__sigset_t mask32;
	unsigned long long mask64;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigprocmask(%d, 0x%08x, 0x%08x, %d)\n", current->pid, how, (unsigned int)set, (unsigned int)oldset, sigsetsize);
#endif /*__DEBUG__ */

	if(sigsetsize != 8 && sigsetsize != 128) {
		return -EINVAL;
	}

	if(oldset) {
		if((errno = check_user_area(VERIFY_WRITE, oldset, sigsetsize))) {
			return errno;
		}
		mask64 = current->sigblocked;
		memset_b(oldset, 0, sigsetsize);
		memcpy_b(oldset, &mask64, sizeof(mask64));
	}
	if(set) {
		if((errno = check_user_area(VERIFY_READ, set, sigsetsize))) {
			return errno;
		}
		memcpy_b(&mask64, set, sizeof(mask64));
		mask32 = (__sigset_t)(mask64 & 0xFFFFFFFFULL);
		switch(how) {
			case SIG_BLOCK:
				current->sigblocked |= (mask32 & SIG_BLOCKABLE);
				break;
			case SIG_UNBLOCK:
				current->sigblocked &= ~(mask32 & SIG_BLOCKABLE);
				break;
			case SIG_SETMASK:
				current->sigblocked = (mask32 & SIG_BLOCKABLE);
				break;
			default:
				return -EINVAL;
		}
	}
	return 0;
}
