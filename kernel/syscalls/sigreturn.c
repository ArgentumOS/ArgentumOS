/*
 * fnx/kernel/syscalls/sigreturn.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (pure x86-64 port): SYS_rt_sigreturn (#15) is the modern signal
 * return used by musl's __restore_rt trampoline (mov $15,%eax; syscall).
 * psig() saved the interrupted user state in current->sc[signum-1]; the
 * trampoline passes signum as arg1, and we copy the saved 64-bit-native
 * sigcontext back into the current syscall's sigcontext. syscall80_handler
 * then writes it into the iretq frame + gprs (see was_sigreturn), restoring
 * the pre-signal user state exactly.
 */

#include <fnx/process.h>
#include <fnx/sigcontext.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_rt_sigreturn(unsigned int signum, int arg2, int arg3, int arg4, int arg5, struct sigcontext *sc)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_rt_sigreturn(%d)\n", current->pid, signum);
#endif /*__DEBUG__ */

	current->sigblocked &= ~current->sigexecuting;
	current->sigexecuting = 0;
	memcpy_b(sc, &current->sc[signum - 1], sizeof(struct sigcontext));

	/* return the value the interrupted syscall was returning */
	return current->sc[signum - 1].rax;
}
