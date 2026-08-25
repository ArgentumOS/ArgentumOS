/*
 * fnx/kernel/syscalls/priority.c
 *
 * FNX: setpriority(141)/getpriority(140)/nice(34)/sched_yield(24).
 *
 * The scheduler's p->priority is a time-slice quantum (ticks); the nice
 * value (-20..19) scales it: nice 0 -> DEF_PRIORITY, nice 19 -> 1/20th,
 * nice -20 -> 2x. getpriority returns 20 - nice (kernel convention; musl
 * maps it back with `return 20 - ret`). sched_yield re-queues the caller
 * by zeroing its quantum and invoking the scheduler.
 */

#include <fnx/sched.h>
#include <fnx/process.h>
#include <fnx/timer.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#define NZERO		20	/* default nice value offset */
#define NICE_MIN	(-20)
#define NICE_MAX	19

/* map a nice value to a time-slice quantum (p->priority) */
static int nice_to_priority(int nice)
{
	unsigned int prio;

	prio = (unsigned int)DEF_PRIORITY * (20 - nice) / 20;
	if(prio < 1) {
		prio = 1;
	}
	return (int)prio;
}

static int can_set_nice(struct proc *p, int new_nice)
{
	/* a process may only raise its own nice (make itself less
	 * important); lowering it (or changing others) needs superuser */
	if(!IS_SUPERUSER && (new_nice < p->nice || p != current)) {
		return 0;
	}
	return 1;
}

int sys_getpriority(int which, int who)
{
	struct proc *p;
	int n;

	switch(which) {
		case PRIO_PROCESS:
			if(who == 0) {
				return 20 - current->nice;
			}
			FOR_EACH_PROCESS(p) {
				if(p->pid == who) {
					if(p->state != PROC_ZOMBIE) {
						return 20 - p->nice;
					}
					return -ESRCH;
				}
				p = p->next;
			}
			return -ESRCH;
		case PRIO_PGRP:
			n = -1;
			if(who == 0) {
				who = current->pgid;
			}
			FOR_EACH_PROCESS(p) {
				if(p->pgid == who && p->state != PROC_ZOMBIE) {
					if(p->nice < n || n == -1) {
						n = p->nice;
					}
				}
				p = p->next;
			}
			if(n == -1) {
				return -ESRCH;
			}
			return 20 - n;
		case PRIO_USER:
			n = -1;
			if(who == 0) {
				who = current->uid;
			}
			FOR_EACH_PROCESS(p) {
				if(p->uid == who && p->state != PROC_ZOMBIE) {
					if(p->nice < n || n == -1) {
						n = p->nice;
					}
				}
				p = p->next;
			}
			if(n == -1) {
				return -ESRCH;
			}
			return 20 - n;
		default:
			return -EINVAL;
	}
}

int sys_setpriority(int which, int who, int prio)
{
	struct proc *p;
	int n;

	if(prio < NICE_MIN) {
		prio = NICE_MIN;
	}
	if(prio > NICE_MAX) {
		prio = NICE_MAX;
	}

	switch(which) {
		case PRIO_PROCESS:
			if(who == 0) {
				who = current->pid;
			}
			FOR_EACH_PROCESS(p) {
				if(p->pid == who) {
					if(p->state == PROC_ZOMBIE) {
						return -ESRCH;
					}
					if(!can_set_nice(p, prio)) {
						return -EPERM;
					}
					p->nice = prio;
					p->priority = nice_to_priority(prio);
					return 0;
				}
				p = p->next;
			}
			return -ESRCH;
		case PRIO_PGRP:
			if(who == 0) {
				who = current->pgid;
			}
			n = 0;
			FOR_EACH_PROCESS(p) {
				if(p->pgid == who && p->state != PROC_ZOMBIE) {
					if(!can_set_nice(p, prio)) {
						return -EPERM;
					}
					p->nice = prio;
					p->priority = nice_to_priority(prio);
					n++;
				}
				p = p->next;
			}
			return n ? 0 : -ESRCH;
		case PRIO_USER:
			if(who == 0) {
				who = current->uid;
			}
			n = 0;
			FOR_EACH_PROCESS(p) {
				if(p->uid == who && p->state != PROC_ZOMBIE) {
					if(!can_set_nice(p, prio)) {
						return -EPERM;
					}
					p->nice = prio;
					p->priority = nice_to_priority(prio);
					n++;
				}
				p = p->next;
			}
			return n ? 0 : -ESRCH;
		default:
			return -EINVAL;
	}
}

/* sched_yield(24): voluntarily give up the CPU */
int sys_sched_yield(void)
{
	current->cpu_count = 0;
	need_resched = 1;
	do_sched();
	return 0;
}
