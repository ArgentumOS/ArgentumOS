/*
 * fiwix/kernel/syscalls/rt_sigprocmask.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (M6 userland): the modern rt_sigprocmask() syscall (Linux #175),
 * which musl/glibc emit for sigprocmask()/pthread_sigmask(). Like
 * rt_sigaction() it uses a 64-bit signal mask; translate the low 32 bits
 * to Fiwix's __sigset_t and defer to the classic semantics.
 */

#include <fiwix/fs.h>
#include <fiwix/signal.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>
#include <fiwix/string.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

int sys_rt_sigprocmask(int how, const void *set, void *oldset, int sigsetsize)
{
	__sigset_t mask32;
	unsigned long long mask64;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigprocmask(%d, 0x%08x, 0x%08x, %d)\n", current->pid, how, set, oldset, sigsetsize);
#endif /*__DEBUG__ */

	if(sigsetsize != 8) {
		return -EINVAL;
	}

	if(oldset) {
		if((errno = check_user_area(VERIFY_WRITE, oldset, 8))) {
			return errno;
		}
		mask64 = current->sigblocked;
		memcpy_b(oldset, &mask64, 8);
	}
	if(set) {
		if((errno = check_user_area(VERIFY_READ, set, 8))) {
			return errno;
		}
		memcpy_b(&mask64, set, 8);
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
