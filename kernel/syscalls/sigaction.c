/*
 * fiwix/kernel/syscalls/sigaction.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/fs.h>
#include <fiwix/signal.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#include <fiwix/process.h>
#endif /*__DEBUG__ */

#ifdef __x86_64__
/* Fiwix64 (M6-A): the user's act/oldact are 32-bit compat structs (4-byte
 * pointers); the kernel's struct sigaction is the 64-bit layout (8-byte
 * pointers, sa_mask/sa_flags at different offsets). Translate in place so
 * sa_flags (SA_RESETHAND/SA_NODEFER/SA_NOCLDSTOP) and the SIG_IGN/SIG_DFL
 * comparisons actually see the user's values. */
struct sigaction32 {
	unsigned int sa_handler;
	__sigset_t sa_mask;
	int sa_flags;
	unsigned int sa_restorer;
};
#endif /* __x86_64__ */

int sys_sigaction(__sigset_t signum, const struct sigaction *newaction, struct sigaction *oldaction)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_sigaction(%d, 0x%08x, 0x%08x)\n", current->pid, signum, (unsigned int)newaction, (unsigned int)oldaction);
#endif /*__DEBUG__ */

	if(signum < 1 || signum > NSIG) {
		return -EINVAL;
	}
	if(signum == SIGKILL || signum == SIGSTOP) {
		return -EINVAL;
	}
	if(oldaction) {
#ifdef __x86_64__
		if((errno = check_user_area(VERIFY_WRITE, oldaction, sizeof(struct sigaction32)))) {
			return errno;
		}
		{
			struct sigaction32 *a32 = (struct sigaction32 *)oldaction;
			struct sigaction *sa = &current->sigaction[signum - 1];

			a32->sa_handler = (unsigned int)(unsigned long)sa->sa_handler;
			a32->sa_mask = sa->sa_mask;
			a32->sa_flags = sa->sa_flags;
			a32->sa_restorer = (unsigned int)(unsigned long)sa->sa_restorer;
		}
#else
		if((errno = check_user_area(VERIFY_WRITE, oldaction, sizeof(struct sigaction)))) {
			return errno;
		}
		*oldaction = current->sigaction[signum - 1];
#endif /* __x86_64__ */
	}
	if(newaction) {
#ifdef __x86_64__
		if((errno = check_user_area(VERIFY_READ, newaction, sizeof(struct sigaction32)))) {
			return errno;
		}
		{
			struct sigaction32 *a32 = (struct sigaction32 *)newaction;
			struct sigaction *sa = &current->sigaction[signum - 1];

			sa->sa_handler = (void *)(unsigned long)a32->sa_handler;
			sa->sa_mask = a32->sa_mask;
			sa->sa_flags = a32->sa_flags;
			sa->sa_restorer = (void *)(unsigned long)a32->sa_restorer;
		}
#else
		if((errno = check_user_area(VERIFY_READ, newaction, sizeof(struct sigaction)))) {
			return errno;
		}
		current->sigaction[signum - 1] = *newaction;
#endif /* __x86_64__ */
		if(current->sigaction[signum - 1].sa_handler == SIG_IGN) {
			if(signum != SIGCHLD) {
				current->sigpending &= SIG_MASK(signum);
			}
		}
		if(current->sigaction[signum - 1].sa_handler == SIG_DFL) {
			if(signum != SIGCHLD) {
				current->sigpending &= SIG_MASK(signum);
			}
		}
	}
	return 0;
}
