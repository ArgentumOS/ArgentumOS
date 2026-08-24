/*
 * fnx/kernel/syscalls/kill.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/process.h>
#include <fnx/signal.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_kill(__pid_t pid, __sigset_t signum)
{
	int count;
	struct proc *p;

#ifdef __DEBUG__
	printk("(pid %d) sys_kill(%d, %d)\n", current->pid, pid, signum);
#endif /*__DEBUG__ */

	if(signum > NSIG) {
		return -EINVAL;
	}
	if(pid == -1) {
		count = 0;
		FOR_EACH_PROCESS(p) {
			if(p->pid == INIT || p == current || !can_signal(p)) {
				p = p->next;
				continue;
			}
			count++;
			send_sig(p, signum);
			p = p->next;
		}
		return count ? 0 : -ESRCH;
	}
	if(!pid) {
		return kill_pgrp(current->pgid, signum, USER);
	}
	if(pid < 1) {
		return kill_pgrp(-pid, signum, USER);
	}

	return kill_pid(pid, signum, USER);
}

/* tkill(200): signal a single thread (process) by tid == pid.
 * tgkill(234): same, with a tgid check (ignored: single-threaded). */
int sys_tkill(int tid, __sigset_t signum)
{
	if(signum > NSIG) {
		return -EINVAL;
	}
	return kill_pid(tid, signum, USER);
}

int sys_tgkill(int tgid, int tid, __sigset_t signum)
{
	if(signum > NSIG) {
		return -EINVAL;
	}
	if(tgid != tid) {
		return -EINVAL;
	}
	return kill_pid(tid, signum, USER);
}
