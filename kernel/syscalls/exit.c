/*
 * fnx/kernel/syscalls/exit.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/syscalls.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/mman.h>
#include <fnx/sleep.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/buffer.h>
#include <fnx/filesystems.h>
#ifdef CONFIG_SYSVIPC
#include <fnx/sem.h>
#endif /* CONFIG_SYSVIPC */

void do_exit(int exit_code)
{
	int n;
	struct proc *p, *init;

#ifdef __DEBUG__
	printk("\n");
	printk("sys_exit(pid %d, ppid %d)\n", current->pid, current->ppid->pid);
	printk("------------------------------\n");
#endif /*__DEBUG__ */

#ifdef CONFIG_SYSVIPC
	if(current->semundo) {
		semexit();
	}
#endif /* CONFIG_SYSVIPC */

	/* FNX: CLONE_CHILD_CLEARTID - a thread exiting clears its tid in
	 * the shared user space and futex-wakes it (musl pthread_join
	 * waits on this address). Threads also skip the address-space
	 * teardown below: the address space is shared with the parent. */
	if(current->flags & PF_THREAD) {
		if(current->set_child_tid) {
			if(!check_user_area(VERIFY_WRITE, current->set_child_tid, sizeof(int))) {
				*(int *)current->set_child_tid = 0;
			}
			wakeup(current->set_child_tid);
		}
		for(n = 0; n < OPEN_MAX; n++) {
			if(current->fd[n]) {
				sys_close(n);
			}
		}
		current->exit_code = exit_code;
		/* FNX: CLONE_THREAD threads are auto-reaped (Linux semantics):
		 * no zombie, no SIGCHLD to the creating thread. Release the
		 * slot now; the scheduler switches away and never returns. */
		not_runnable(current, PROC_ZOMBIE);
		release_proc(current);
		do_sched();
		return;	/* never reached */
	}

	release_binary();
	current->argv = NULL;
	current->envp = NULL;

	init = &proc_table[INIT];
	FOR_EACH_PROCESS(p) {
		if(SESS_LEADER(current)) {
			if(p->sid == current->sid && p->state != PROC_ZOMBIE) {
				p->pgid = 0;
				p->sid = 0;
				p->ctty = NULL;
				send_sig(p, SIGHUP);
				send_sig(p, SIGCONT);
			}
		}

		/* make INIT inherit the children of this exiting process */
		if(p->ppid == current) {
			p->ppid = init;
			init->children++;
			current->children--;
			if(p->state == PROC_ZOMBIE) {
				send_sig(init, SIGCHLD);
				if(init->sleep_address == (void *)SLEEP_ADDR(&sys_wait4)) {
					wakeup_proc(init);
				}
			}
		}
		p = p->next;
	}

	if(SESS_LEADER(current)) {
		disassociate_ctty(current->ctty);
	}

	for(n = 0; n < OPEN_MAX; n++) {
		if(current->fd[n]) {
			sys_close(n);
		}
	}

	iput(current->root);
	current->root = NULL;
	iput(current->pwd);
	current->pwd = NULL;
	current->exit_code = exit_code;
	if(!--nr_processes) {
		printk("\n");
		printk("WARNING: the last user process has exited. The kernel will stop itself.\n");
		sync_superblocks(0);    /* in all devices */
		sync_inodes(0);         /* in all devices */
		sync_buffers(0);        /* in all devices */
		stop_kernel();
	}

	/* become a zombie FIRST, then notify the parent: a parent blocked
	 * in wait4() is woken by the address-match wakeup below and
	 * re-scans the child list immediately. If it runs before the
	 * PROC_ZOMBIE transition it finds nothing, re-sleeps, and is never
	 * re-woken (send_sig() drops SIGCHLD when the parent has it at
	 * SIG_DFL) -> waitpid() hangs forever. */
	not_runnable(current, PROC_ZOMBIE);

	current->sigpending = 0;
	current->sigblocked = 0;
	current->sigexecuting = 0;
	for(n = 0; n < NSIG; n++) {
		current->sigaction[n].sa_mask = 0;
		current->sigaction[n].sa_flags = 0;
		current->sigaction[n].sa_handler = SIG_IGN;
	}

	/* notify the parent about the child's death */
	p = current->ppid;
	send_sig(p, SIGCHLD);
	if(p->sleep_address == (void *)SLEEP_ADDR(&sys_wait4)) {
		wakeup_proc(p);
	}

	do_sched();
}

int sys_exit(int exit_code)
{
#ifdef __DEBUG__
	printk("(pid %d) sys_exit()\n", current->pid);
#endif /*__DEBUG__ */

	/* exit code in the second byte.
	 *  15                8 7                 0
	 * +-------------------+-------------------+
	 * | exit code (0-255) |         0         |
	 * +-------------------+-------------------+
	 */
	do_exit((exit_code & 0xFF) << 8);
	return 0;
}
