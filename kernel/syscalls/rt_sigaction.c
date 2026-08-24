/*
 * fnx/kernel/syscalls/rt_sigaction.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (pure x86-64 port): SYS_rt_sigaction (#13) is what musl's
 * sigaction() emits. The user struct is the x86-64 ABI layout
 * (include/signal.h: union {sa_handler,sa_sigaction}, sigset_t sa_mask
 * (128 bytes), int sa_flags, void *sa_restorer - 152 bytes total) and
 * sigsetsize is sizeof(sigset_t) = 128. The kernel keeps a 32-bit
 * __sigset_t (NSIG == 32): translate the mask and the pointer widths.
 */

#include <fnx/fs.h>
#include <fnx/signal.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

/* x86-64 ABI struct sigaction (musl): handler, mask, flags, restorer */
struct sigaction64 {
	unsigned long long sa_handler;
	unsigned long long sa_mask[16];	/* sigset_t = 128 bytes */
	int sa_flags;
	int __pad;
	unsigned long long sa_restorer;
};

int sys_rt_sigaction(__sigset_t signum, const void *act, void *oldact, int sigsetsize)
{
	const struct sigaction64 *a64;
	struct sigaction64 *o64;
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
	if(sigsetsize != 8 && sigsetsize != 128) {
		return -EINVAL;
	}

	sa = &current->sigaction[signum - 1];
	a64 = act;
	o64 = oldact;

	if(o64) {
		if((errno = check_user_area(VERIFY_WRITE, o64, sizeof(struct sigaction64)))) {
			return errno;
		}
		o64->sa_handler = (unsigned long long)(unsigned long)sa->sa_handler;
		o64->sa_mask[0] = (unsigned long long)sa->sa_mask;
		o64->sa_flags = sa->sa_flags;
		o64->sa_restorer = (unsigned long long)(unsigned long)sa->sa_restorer;
	}
	if(a64) {
		if((errno = check_user_area(VERIFY_READ, a64, sizeof(struct sigaction64)))) {
			return errno;
		}
		/* only the low 32 signals exist in Fiwix (NSIG == 32) */
		sa->sa_handler = (void *)(unsigned long)a64->sa_handler;
		sa->sa_mask = (__sigset_t)(a64->sa_mask[0] & 0xFFFFFFFFULL);
		sa->sa_flags = a64->sa_flags;
		sa->sa_restorer = (void *)(unsigned long)a64->sa_restorer;

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
