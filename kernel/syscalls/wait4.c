/*
 * fnx/kernel/syscalls/wait4.c
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/resource.h>
#include <fnx/signal.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_wait4(__pid_t pid, int *status, int options, struct rusage *ru)
{
	struct proc *p;
	struct proc *owner;
	int flag, seen, signum, errno;

	/* FNX: a wait is a PROCESS's wait, not a thread's. Children are created and counted per PROCESS, so
	 * a wait issued from a thread (a different struct proc) saw current->children == 0 and returned
	 * immediately - and even with a count it matched children by the forking TASK's pointer. POSIX: any
	 * thread may reap the process's children. Work on the THREAD GROUP LEADER, whose tgid is the group. */
	{
		extern struct proc *get_proc_by_pid(__pid_t);

		owner = (current->tgid && current->tgid != current->pid) ?
			get_proc_by_pid(current->tgid) : current;
		if(!owner) {
			owner = current;
		}
	}

#ifdef __DEBUG__
	printk("(pid %d) sys_wait4(%d, status, %d)\n", current->pid, pid, options);
#endif /*__DEBUG__ */

	if(ru) {
		if((errno = check_user_area(VERIFY_WRITE, ru, sizeof(struct rusage)))) {
			return errno;
		}
	}
	while(owner->children) {
		flag = 0;
		/* `seen` IS THE SCAN'S OWN MEMORY, AND THE TWO VARIABLES ARE NOT INTERCHANGEABLE: `flag` is
		 * cleared at the end of every iteration because the pid SELECTORS below set it fresh per process,
		 * so by the time the scan ends it only ever answers "did the LAST process match". `seen` is set
		 * when any process matches and is cleared once per SCAN, which is what the WNOHANG decision after
		 * the loop actually needs - and it must be made after the WHOLE scan, not inside it, or a live
		 * child found early would preempt the zombie that comes later in the table (which is exactly the
		 * defect the first version of this fix introduced, and kernel_pipe_dup2's threaded variants caught
		 * it: a reaper polling waitpid(-1, WNOHANG) was answered 0 while a zombie waited to be reaped).
		 * foundation-plan.md §58.1c. */
		seen = 0;
		FOR_EACH_PROCESS(p) {
			if(!p->ppid || p->ppid->tgid != owner->tgid) {
				p = p->next;
				continue;
			}
			if(pid > 0) {
				if(p->pid == pid) {
					flag = 1;
				}
			}
			if(!pid) {
				if(p->pgid == current->pgid) {
					flag = 1;
				}
			}
			if(pid < -1) {
				if(p->pgid == -pid) {
					flag = 1;
				}
			}
			if(pid == -1) {
				flag = 1;
			}
			if(flag) {
				if(p->state == PROC_STOPPED) {
					/* POSIX: a STOPPED child is reported ONLY to a caller that ASKED for it with
					 * WUNTRACED - which is what that flag's own comment in fnx/signal.h says it is
					 * for. Reporting a stopped child to a plain waitpid(pid, 0) tells the caller it
					 * has FINISHED, and that is exactly what it did to NSTask's reaper: on SUSPEND
					 * it marked the task exited, so -resume then refused and -terminationStatus
					 * answered 0 (a 0x7F low byte is neither an exit nor a signal). Keep scanning -
					 * and if nothing else matches, keep waiting, because the child's real exit still
					 * wakes this call. */
					if(!(options & WUNTRACED)) {
						p = p->next;
						continue;
					}
					if(!p->exit_code) {
						p = p->next;
						continue;
					}
					if(status) {
						*status = (p->exit_code << 8) | 0x7F;
					}
					p->exit_code = 0;
					if(ru) {
						get_rusage(p, ru);
					}
					return p->pid;
				}
				if(p->state == PROC_ZOMBIE) {
					add_rusage(p);
					if(status) {
						*status = p->exit_code;
					}
					if(ru) {
						get_rusage(p, ru);
					}
					return remove_zombie(p);
				}
				/* A MATCHING CHILD: REMEMBER IT FOR THE SCAN'S ANSWER (see `seen`'s comment above). The
				 * stopped and zombie cases just above have already had their say, so scanning on skips
				 * nothing - and NOT returning here is the whole point: a live child found early must not
				 * preempt the zombie that comes later in the table, which is the defect the first version
				 * of this fix introduced and kernel_pipe_dup2's threaded variants caught. The WNOHANG
				 * decision is made ONCE, after the whole scan. */
				seen = 1;
			}
			p = p->next;
			flag = 0;
		}
		if(options & WNOHANG) {
			if(seen) {
				return 0;
			}
			break;
		}
		if((signum = sleep(&sys_wait4, PROC_INTERRUPTIBLE))) {
			/* A pending SIGCHLD is the normal reason this sleep was
			 * interrupted (a child exited); consume it so the wait4
			 * retry doesn't busy-loop on issig() returning the same
			 * SIGCHLD forever (dash's waitpid retries on EINTR).
			 *
			 * The return value must be -EINTR and NOT the signal
			 * number: on the Linux ABI wait4 returns a pid or a
			 * negative errno, so a positive signal number reads as
			 * "child <signum> was reaped" to the caller. A shell that
			 * installs a SIGCHLD handler (dash does) gets the signal
			 * *delivered*, i.e. this path, and then matched the
			 * returned number - SIGCHLD's 17 - against its job table
			 * and stored an uninitialized wait status for that pid:
			 * an intermittent bogus "Stack fault"/"Unknown signal"
			 * report and $? (root-caused 2026-09-12; the same bug
			 * made the devpts case's log show `Unknown signal`). */
			current->sigpending &= ~SIG_MASK(SIGCHLD);
			return -EINTR;
		}
		current->sigpending &= ~SIG_MASK(SIGCHLD);
	}
	return -ECHILD;
}
