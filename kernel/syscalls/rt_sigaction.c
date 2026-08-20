/*
 * fiwix/kernel/syscalls/rt_sigaction.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (M6 userland): the modern rt_sigaction() syscall (Linux #174),
 * which musl/glibc emit for sigaction(). Its user struct uses a 64-bit
 * signal mask (realtime signals), unlike the classic sigaction() (#67)
 * 32-bit mask that Fiwix implements internally; translate in place.
 */

#include <fiwix/fs.h>
#include <fiwix/signal.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

/* i386 rt_sigaction() user struct: handler, flags, restorer, 64-bit mask */
struct rt_sigaction32 {
	unsigned int sa_handler;
	unsigned int sa_flags;
	unsigned int sa_restorer;
	unsigned long long sa_mask;
};

int sys_rt_sigaction(__sigset_t signum, const void *act, void *oldact, int sigsetsize)
{
	const struct rt_sigaction32 *a32;
	struct rt_sigaction32 *o32;
	struct sigaction *sa;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigaction(%d, 0x%08x, 0x%08x, %d)\n", current->pid, signum, (unsigned int)act, (unsigned int)oldact, sigsetsize);
#endif /*__DEBUG__ */

	if(signum < 1 || signum > NSIG) {
		return -EINVAL;
	}
	if(signum == SIGKILL || signum == SIGSTOP) {
		return -EINVAL;
	}
	if(sigsetsize != 8) {
		return -EINVAL;
	}

	sa = &current->sigaction[signum - 1];
	a32 = act;
	o32 = oldact;

	if(o32) {
		if((errno = check_user_area(VERIFY_WRITE, o32, sizeof(struct rt_sigaction32)))) {
			return errno;
		}
		o32->sa_handler = (unsigned int)(unsigned long)sa->sa_handler;
		o32->sa_flags = sa->sa_flags;
		o32->sa_restorer = (unsigned int)(unsigned long)sa->sa_restorer;
		o32->sa_mask = (unsigned long long)sa->sa_mask;
	}
	if(a32) {
		if((errno = check_user_area(VERIFY_READ, a32, sizeof(struct rt_sigaction32)))) {
			return errno;
		}
		/* only the low 32 signals exist in Fiwix (NSIG == 32) */
		sa->sa_handler = (void *)(unsigned long)a32->sa_handler;
		sa->sa_mask = (__sigset_t)(a32->sa_mask & 0xFFFFFFFFULL);
		sa->sa_flags = a32->sa_flags;
		sa->sa_restorer = (void *)(unsigned long)a32->sa_restorer;

		if(sa->sa_handler == SIG_IGN) {
			if(signum != SIGCHLD) {
				current->sigpending &= SIG_MASK(signum);
			}
		}
		if(sa->sa_handler == SIG_DFL) {
			if(signum != SIGCHLD) {
				current->sigpending &= SIG_MASK(signum);
			}
		}
	}
	return 0;
}
