/*
 * fnx/kernel/syscalls/rt_sched.c
 *
 * FNX: real-time scheduling syscalls - sched_setscheduler(144),
 * sched_getscheduler(145), sched_get_priority_max(146),
 * sched_get_priority_min(147), sched_rr_get_interval(148).
 *
 * Policies: SCHED_OTHER (0, nice-scaled round-robin), SCHED_FIFO (1,
 * runs to completion), SCHED_RR (2, round-robin within its priority).
 * RT priorities are 1..99 (SCHED_RT_MAX_PRIO); the scheduler picks the
 * runnable RT process with the highest rt_priority ahead of SCHED_OTHER.
 * Only the superuser may switch to an RT policy (Linux requires CAP_SYS_NICE).
 */

#include <fnx/sched.h>
#include <fnx/process.h>
#include <fnx/timer.h>
#include <fnx/time.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_sched_setscheduler(int pid, int policy, const struct sched_param *param)
{
	struct proc *p;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_sched_setscheduler(%d, %d, 0x%x)\n", current->pid, pid, policy, (unsigned int)param);
#endif /*__DEBUG__ */

	if(policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_READ, param, sizeof(struct sched_param)))) {
		return errno;
	}
	if(policy != SCHED_OTHER) {
		if(!IS_SUPERUSER) {
			return -EPERM;
		}
		if(param->sched_priority < 1 || param->sched_priority > SCHED_RT_MAX_PRIO) {
			return -EINVAL;
		}
	}

	if(pid == 0) {
		p = current;
	} else {
		p = NULL;
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
			p = p->next;
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
	}

	p->policy = policy;
	if(policy == SCHED_OTHER) {
		p->rt_priority = 0;
		p->priority = DEF_PRIORITY;
	} else {
		p->rt_priority = param->sched_priority;
		p->priority = 1;	/* RT quantum is 1 tick (10ms); FIFO never exhausts it */
		p->cpu_count = 1;
	}
	return 0;
}

int sys_sched_getscheduler(int pid)
{
	struct proc *p;

#ifdef __DEBUG__
	printk("(pid %d) sys_sched_getscheduler(%d)\n", current->pid, pid);
#endif /*__DEBUG__ */

	if(pid == 0) {
		p = current;
	} else {
		p = NULL;
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
			p = p->next;
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
	}
	return p->policy;
}

int sys_sched_get_priority_max(int policy)
{
	if(policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR) {
		return -EINVAL;
	}
	if(policy == SCHED_OTHER) {
		return 0;
	}
	return SCHED_RT_MAX_PRIO;
}

int sys_sched_get_priority_min(int policy)
{
	if(policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR) {
		return -EINVAL;
	}
	if(policy == SCHED_OTHER) {
		return 0;
	}
	return 1;
}

int sys_sched_rr_get_interval(int pid, struct timespec *tp)
{
	struct proc *p;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_sched_rr_get_interval(%d, 0x%x)\n", current->pid, pid, (unsigned int)tp);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, tp, sizeof(struct timespec)))) {
		return errno;
	}
	if(pid == 0) {
		p = current;
	} else {
		p = NULL;
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
			p = p->next;
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
	}
	/* RR quantum is 1 tick (10ms) for RT processes, DEF_PRIORITY otherwise */
	tp->tv_sec = 0;
	tp->tv_nsec = (p->policy == SCHED_RR) ? (1000000000 / HZ) : (p->priority * 1000000000 / HZ);
	return 0;
}

/* sched_setparam(142): set just the priority of the current policy */
int sys_sched_setparam(int pid, const struct sched_param *param)
{
	struct proc *p;
	int errno;

	if((errno = check_user_area(VERIFY_READ, param, sizeof(struct sched_param)))) {
		return errno;
	}
	if(param->sched_priority < 1 || param->sched_priority > SCHED_RT_MAX_PRIO) {
		return -EINVAL;
	}
	if(pid == 0) {
		p = current;
	} else {
		p = NULL;
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
			p = p->next;
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
	}
	if(p->policy == SCHED_OTHER) {
		return -EINVAL;	/* SCHED_OTHER has no RT priority */
	}
	if(!IS_SUPERUSER) {
		return -EPERM;
	}
	p->rt_priority = param->sched_priority;
	return 0;
}

/* sched_getparam(143): return the current priority */
int sys_sched_getparam(int pid, struct sched_param *param)
{
	struct proc *p;
	int errno;

	if((errno = check_user_area(VERIFY_WRITE, param, sizeof(struct sched_param)))) {
		return errno;
	}
	if(pid == 0) {
		p = current;
	} else {
		p = NULL;
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
			p = p->next;
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
	}
	param->sched_priority = p->rt_priority;
	return 0;
}
