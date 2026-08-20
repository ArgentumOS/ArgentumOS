/*
 * fiwix/kernel/signal.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/asm.h>
#include <fiwix/kernel.h>
#include <fiwix/errno.h>
#include <fiwix/process.h>
#include <fiwix/signal.h>
#include <fiwix/sigcontext.h>
#include <fiwix/sleep.h>
#include <fiwix/sched.h>
#include <fiwix/syscalls.h>
#include <fiwix/mm.h>
#include <fiwix/stdio.h>
#include <fiwix/string.h>

/* can process 'current' send a signal to process 'p'? */
int can_signal(struct proc *p)
{
	if(!IS_SUPERUSER && current->euid != p->euid && current->sid != p->sid) {
		return 0;
	}

	return 1;
}

int send_sig(struct proc *p, __sigset_t signum)
{
	if(signum > NSIG || !p) {
		return -EINVAL;
	}

	/* kernel processes can't receive signals */
	if(p->flags & PF_KPROC) {
		return 0;
	}

	switch(signum) {
		case 0:
			return 0;
		case SIGKILL:
		case SIGCONT:
			if(p->state == PROC_STOPPED) {
				runnable(p);
				need_resched = 1;
			}
			/* discard all pending stop signals */
			p->sigpending &= SIG_MASK(SIGSTOP);
			p->sigpending &= SIG_MASK(SIGTSTP);
			p->sigpending &= SIG_MASK(SIGTTIN);
			p->sigpending &= SIG_MASK(SIGTTOU);
			break;
		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			/* discard all pending SIGCONT signals */
			p->sigpending &= SIG_MASK(SIGCONT);
			break;
		default:
			break;
	}

	/*
	 * Force some signals, that a process cannot ignore, by changing its
	 * signal disposition to SIG_DFL.
	 */
	switch(signum) {
		case SIGFPE:
		case SIGSEGV:
			if(p->sigaction[signum - 1].sa_handler == SIG_IGN) {
				p->sigaction[signum - 1].sa_handler = SIG_DFL;
			}
			break;
	}

	if(p->sigaction[signum - 1].sa_handler == SIG_DFL) {
		/*
		 * INIT process is special, it only gets signals that have the
		 * signal handler installed. This avoids to bring down the
		 * system accidentally.
		 */
		if(p->pid == INIT) {
			return 0;
		}

		/* these signals with SIG_DFL are ignored */
		switch(signum) {
			case SIGCONT:
			case SIGWINCH:
			case SIGCHLD:
			case SIGURG:
				return 0;
		}
	}

	if(p->sigaction[signum - 1].sa_handler == SIG_IGN) {
		/* if SIGCHLD is ignored reap its children (prevent zombies) */
		if(signum == SIGCHLD) {
			while(sys_waitpid(-1, NULL, WNOHANG) > 0) {
				continue;
			}
		}
		return 0;
	}

	p->sigpending |= 1 << (signum - 1);
	p->usage.ru_nsignals++;

	/* wake up the process only if the signal is not blocked */
	if(!(p->sigblocked & (1 << (signum - 1)))) {
		wakeup_proc(p);
	}

	return 0;
}

int issig(void)
{
	__sigset_t signum;
	unsigned int mask;
	struct proc *p;

	if(!(current->sigpending & ~current->sigblocked)) {
		return 0;
	}

	for(signum = 1, mask = 1; signum < NSIG; signum++, mask <<= 1) {
		if(current->sigpending & mask) {
			if(signum == SIGCHLD) {
				if(current->sigaction[signum - 1].sa_handler == SIG_IGN) {
					/* this process ignores SIGCHLD */
					while((p = get_next_zombie(current))) {
						remove_zombie(p);
					}
				} else {
					if(current->sigaction[signum - 1].sa_handler != SIG_DFL) {
						return signum;
					}
				}
			} else {
				if(current->sigaction[signum - 1].sa_handler != SIG_IGN) {
					return signum;
				}
			}
			current->sigpending &= ~mask;
		}
	}
	return 0;
}

#ifdef __x86_64__
/* Fiwix64 (M6-A): the 32-bit compat signal trampoline, defined in this
 * object so psig()'s references resolve locally. (References to the
 * external switch64.S labels from PATCH_PIC-patched objects get stale
 * RIP-relative displacements and would copy garbage/zeros.) Sequence:
 * pushl %eax; pushl %eax; call *%ecx; addl $4,%esp; popl %ebx;
 * movl $119,%eax; int $0x80; movl $1,%eax; int $0x80; ret. */
static unsigned char fiwix64_trampoline[] = {
	0x50, 0x50, 0xff, 0xd1, 0x83, 0xc4, 0x04, 0x5b,
	0xb8, 119, 0, 0, 0, 0xcd, 0x80,
	0xb8, 1, 0, 0, 0, 0xcd, 0x80, 0xc3
};
#endif /* __x86_64__ */

void psig(struct sigcontext *sc)
{
	int len;
	__sigset_t signum;
	unsigned int mask;
	struct proc *p;

	for(signum = 1, mask = 1; signum < NSIG; signum++, mask <<= 1) {
		if(current->sigpending & mask) {
			current->sigpending &= ~mask;

			if((addr_t)current->sigaction[signum - 1].sa_handler) {

				/*
				 * page_not_present() may have raised a SIGSEGV if it
				 * detected that this process doesn't have an stack
				 * region in vma_table.
				 *
				 * So, this check is needed to make sure to terminate the
				 * process now, otherwise the kernel would panic when using
				 * 'sc->oldesp' during the 'memcpy_b' below.
				 */
				if(!find_vma_region(sc->oldesp)) {
					printk("WARNING: %s(): no stack region in vma table for process %d. Terminated.\n", __FUNCTION__, current->pid);
					do_exit(signum);
				}

				current->sigexecuting = mask;
				if(!(current->sigaction[signum - 1].sa_flags & SA_NODEFER)) {
					current->sigblocked |= mask;
				}

				/* save the current sigcontext */
				memcpy_b(&current->sc[signum - 1], sc, sizeof(struct sigcontext));
				/* setup the jump to the user signal handler */
#ifdef __x86_64__
				/* Fiwix64 (M6-A): use the local trampoline (see above); the
				 * extern-function-address subtraction would also codegen a
				 * memory-load sub under gcc -O2. */
				len = sizeof(fiwix64_trampoline);
#else
				len = ((addr_t)end_sighandler_trampoline - (addr_t)sighandler_trampoline);
#endif /* __x86_64__ */
				sc->oldesp -= len;
				sc->oldesp -= 4;
				sc->oldesp &= ~3;	/* round up */
#ifdef __x86_64__
				/* Fiwix64 (M6-A): demand-map the user stack page U/S to a
				 * REAL RAM page BEFORE copying the trampoline. The raw
				 * identity 2MB page at this address (the process stack sits
				 * at ~2GB) is the PCI MMIO hole - a CPL0 write lands in
				 * MMIO and a CPL3 fetch reads zeros. fiwix64_fault_user_pages
				 * maps it via the stack vma (fresh zeroed page) and skips
				 * pages already mapped U/S. */
				{
					extern int fiwix64_fault_user_pages(addr_t, unsigned int);
					fiwix64_fault_user_pages((addr_t)sc->oldesp, len);
				}
#endif /* __x86_64__ */
#ifdef __x86_64__
				memcpy_b((void *)sc->oldesp, fiwix64_trampoline, len);
#else
				memcpy_b((void *)sc->oldesp, sighandler_trampoline, len);
#endif /* __x86_64__ */
				sc->ecx = (addr_t)current->sigaction[signum - 1].sa_handler;
				sc->eax= signum;
				sc->eip = sc->oldesp;

				if(current->sigaction[signum - 1].sa_flags & SA_RESETHAND) {
					current->sigaction[signum - 1].sa_handler = SIG_DFL;
				}
				return;
			}
			if(current->sigaction[signum - 1].sa_handler == SIG_DFL) {
				switch(signum) {
					case SIGCONT:
						runnable(current);
						need_resched = 1;
						break;
					case SIGSTOP:
					case SIGTSTP:
					case SIGTTIN:
					case SIGTTOU:
						current->exit_code = signum;
						not_runnable(current, PROC_STOPPED);
						if(!(current->sigaction[signum - 1].sa_flags & SA_NOCLDSTOP)) {
							p = current->ppid;
							send_sig(p, SIGCHLD);
							/* needed for job control */
							wakeup(&sys_wait4);
						}
						need_resched = 1;
						break;
					case SIGCHLD:
						break;
					default:
						do_exit(signum);
				}
			}
		}
	}

	/* coming from a system call that needs to be restarted */
	if(sc->err > 0) {
		if(sc->eax == -ERESTART) {
			sc->eax = sc->err;	/* syscall was saved in 'err' */
			sc->eip -= 2;		/* point again to 'int 0x80' */
		}
	}
}

int kill_pid(__pid_t pid, __sigset_t signum, int sender)
{
	struct proc *p;

	FOR_EACH_PROCESS(p) {
		if(p->pid == pid && p->state != PROC_ZOMBIE) {
			if(sender == USER) {
				if(!can_signal(p)) {
					return -EPERM;
				}
			}
			return send_sig(p, signum);
		}
		p = p->next;
	}
	return -ESRCH;
}

int kill_pgrp(__pid_t pgid, __sigset_t signum, int sender)
{
	struct proc *p;
	int found;

	found = 0;
	FOR_EACH_PROCESS(p) {
		if(p->pgid == pgid && p->state != PROC_ZOMBIE) {
			if(sender == USER) {
				if(!can_signal(p)) {
					p = p->next;
					continue;
				}
			}
			send_sig(p, signum);
			found = 1;
		}
		p = p->next;
	}

	if(!found) {
		return -ESRCH;
	}
	return 0;
}
