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
#include <fnx/mm.h>
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
		/* A THREAD CLOSES NOTHING HERE (§45-V). The descriptor table belongs to the PROCESS and a thread
		 * SHARES the lead task's pointer, so the loop that used to stand here - which closed the THREAD's
		 * own copy of every open descriptor - closed the PROCESS's descriptors instead. MEASURED: a thread
		 * exiting took the leader's stdout with it, so the leader's own last printf went to a CLOSED fd
		 * and its output vanished while the process still exited 0. The cleartid wakeup above is the part
		 * of this block a thread needs; the fds are not. */
		/* FNX: the LAST user of a shared address space frees it. A thread
		 * that outlives its creator is the one that has to, or the tables
		 * are never released (see pml4_has_other_user). */
		{
			extern int pml4_has_other_user(unsigned long);
			extern void free_pml4_64(unsigned long);
			extern unsigned long paging64_pml4_phys(void);
			unsigned long cr3 = current->cr3_64;

			current->cr3_64 = 0;
			if(cr3 && cr3 != paging64_pml4_phys() && !pml4_has_other_user(cr3)) {
				free_pml4_64(cr3);
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
	/* AND THE TABLE IS FREED ONLY FOR ITS LAST USER (§45-V), because a live thread may still be sharing
	 * it - the same rule the pml4 teardown above states for the address space. */
	{
		struct proc *q = proc_table_head;
		int last = 1;

		while(q) {
			if(q != current && q->fd == current->fd) {
				last = 0;
				break;
			}
			q = q->next;
		}
		if(last) {
			kfree((addr_t)current->fd);
			kfree((addr_t)current->fd_flags);
			current->fd = NULL;
			current->fd_flags = NULL;
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

	/* NOTIFY EVERY WAITER, NOT ONLY THE TASK THAT FORKED. `current->ppid` is the task that CALLED
	 * fork(2); a wait may legally be issued by ANOTHER thread of the same process (POSIX: any thread may
	 * reap the process's children - sys_wait4 already matches by TGID), and that thread is a different
	 * struct proc. wakeup_proc(p) reaches p ALONE, and send_sig(p, SIGCHLD) sets sigpending on p alone,
	 * so a threaded waiter sat in sleep(&sys_wait4, PROC_INTERRUPTIBLE) FOREVER. MEASURED: NSTask's
	 * reaper thread never returned from waitpid(2) while the main thread waited on its condition, and the
	 * whole process hung; the parent, being asleep on a futex, did not even satisfy
	 * `p->sleep_address == SLEEP_ADDR(&sys_wait4)`, so nothing was woken at all.
	 *
	 * wakeup() is ADDRESS-WIDE - it wakes every proc whose sleep_address matches - which is exactly what
	 * the job-control path in signal.c already does for SIGSTOP/SIGCONT. A woken waiter re-scans the
	 * child list, finds the zombie, and reaps it. */
	p = current->ppid;
	send_sig(p, SIGCHLD);
	wakeup(&sys_wait4);

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
