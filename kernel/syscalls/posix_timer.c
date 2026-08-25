/*
 * fnx/kernel/syscalls/posix_timer.c
 *
 * FNX: POSIX per-process timers - timer_create(222), timer_settime(223),
 * timer_gettime(224), timer_getoverrun(225), timer_delete(226).
 *
 * Timers live on current->ptimers, driven from the timer tick
 * (kernel/timer.c calls posix_timer_tick()). Each timer delivers
 * sigev_signo (default SIGALRM) on expiry; if it is periodic
 * (interval != 0) it reloads and counts overruns.
 */

#include <fnx/posix_timer.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/timer.h>
#include <fnx/time.h>
#include <fnx/signal.h>
#include <fnx/errno.h>
#include <fnx/kernel.h>
#include <fnx/mm.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#define NR_POSIX_TIMERS	32
#define CLOCK_REALTIME	0
#define CLOCK_MONOTONIC	1
#define TIMER_ABSTIME	1

#define SIGEV_SIGNAL	0
#define SIGEV_NONE	1

/* user ABI (musl <signal.h>/<time.h>) */
struct sigevent_abi {
	int sigev_notify;
	int sigev_signo;
	union {
		int sigev_value_int;
		void *sigev_value_ptr;
	} sigev_value;
	void (*sigev_notify_function)(void *);
	void *sigev_notify_attributes;
};

/* user ABI: struct itimerspec = two timespecs (16 bytes each) */
struct itimerspec_abi {
	struct timespec it_interval;
	struct timespec it_value;
};

static unsigned int ts2ticks(const struct timespec *ts)
{
	unsigned int nsec = ts->tv_nsec;

	if(nsec < 10000000L) {
		nsec *= 10;	/* min 10ms granularity (HZ=100) */
	}
	return (ts->tv_sec * HZ) + (nsec * HZ / 1000000000L);
}

static void ticks2ts(int ticks, struct timespec *ts)
{
	ts->tv_sec = ticks / HZ;
	ts->tv_nsec = (ticks % HZ) * (1000000000 / HZ);
}

/* keep a private list so the tick hook has no header deps */
struct posix_timer *posix_timer_list(struct proc *p)
{
	return p->ptimers;
}

void posix_timer_tick(struct proc *p)
{
	struct posix_timer *t;
	int sig;

	for(t = p->ptimers; t; t = t->next) {
		if(t->value > 0) {
			t->value--;
			if(!t->value) {
				if(t->interval) {
					t->value = t->interval;
				}
				sig = t->sigev_signo ? t->sigev_signo : SIGALRM;
				send_sig(p, sig);
			}
		}
	}
}

static struct posix_timer *find_timer(struct proc *p, int id)
{
	struct posix_timer *t;

	for(t = p->ptimers; t; t = t->next) {
		if(t->id == id) {
			return t;
		}
	}
	return NULL;
}

static int next_timer_id(struct proc *p)
{
	int id;
	struct posix_timer *t;

	for(id = 0; id < NR_POSIX_TIMERS; id++) {
		if(!find_timer(p, id)) {
			return id;
		}
	}
	return -1;
}

int sys_timer_create(int clock_id, const struct sigevent_abi *evp, int *timer_id)
{
	struct posix_timer *t;
	struct posix_timer **tail;
	int errno, id;

#ifdef __DEBUG__
	printk("(pid %d) sys_timer_create(%d, 0x%x, 0x%x)\n", current->pid, (int)clock_id, (unsigned int)evp, (unsigned int)timer_id);
#endif /*__DEBUG__ */

	if(clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, timer_id, sizeof(int)))) {
		return errno;
	}
	if((id = next_timer_id(current)) < 0) {
		return -EAGAIN;
	}
	if(!(t = (struct posix_timer *)kmalloc(sizeof(struct posix_timer)))) {
		return -ENOMEM;
	}
	memset_b(t, 0, sizeof(struct posix_timer));
	t->id = id;
	t->clock_id = clock_id;
	t->sigev_signo = SIGALRM;
	if(evp) {
		if((errno = check_user_area(VERIFY_READ, evp, sizeof(struct sigevent_abi)))) {
			kfree((addr_t)t);
			return errno;
		}
		if(evp->sigev_notify == SIGEV_NONE) {
			t->sigev_signo = 0;
		} else if(evp->sigev_notify == SIGEV_SIGNAL) {
			if(evp->sigev_signo > 0 && evp->sigev_signo <= NSIG) {
				t->sigev_signo = evp->sigev_signo;
			}
		}
	}

	/* append to the process's timer list */
	tail = &current->ptimers;
	while(*tail) {
		tail = &(*tail)->next;
	}
	*tail = t;

	*timer_id = id;
	return 0;
}

int sys_timer_settime(int timer_id, int flags, const struct itimerspec_abi *new_value, struct itimerspec_abi *old_value)
{
	struct posix_timer *t;
	struct itimerspec_abi nv;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_timer_settime(%d, %d, 0x%x, 0x%x)\n", current->pid, timer_id, flags, (unsigned int)new_value, (unsigned int)old_value);
#endif /*__DEBUG__ */

	if(!(t = find_timer(current, timer_id))) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_READ, new_value, sizeof(struct itimerspec_abi)))) {
		return errno;
	}
	memcpy_b(&nv, new_value, sizeof(struct itimerspec_abi));
	if(nv.it_value.tv_sec < 0 || nv.it_value.tv_nsec < 0 || nv.it_value.tv_nsec >= 1000000000L ||
	   nv.it_interval.tv_sec < 0 || nv.it_interval.tv_nsec < 0 || nv.it_interval.tv_nsec >= 1000000000L) {
		return -EINVAL;
	}
	if(old_value) {
		struct itimerspec_abi ov;

		if((errno = check_user_area(VERIFY_WRITE, old_value, sizeof(struct itimerspec_abi)))) {
			return errno;
		}
		ticks2ts(t->value, &ov.it_value);
		ticks2ts(t->interval, &ov.it_interval);
		memcpy_b(old_value, &ov, sizeof(struct itimerspec_abi));
	}

	if(flags & TIMER_ABSTIME) {
		/* absolute: relative = abs - now */
		unsigned int now = CURRENT_TICKS;
		unsigned int target = ts2ticks(&nv.it_value);
		t->value = (target > now) ? (target - now) : 0;
	} else {
		t->value = ts2ticks(&nv.it_value);
	}
	t->interval = ts2ticks(&nv.it_interval);
	t->overrun = 0;
	return 0;
}

int sys_timer_gettime(int timer_id, struct itimerspec_abi *curr_value)
{
	struct posix_timer *t;
	struct itimerspec_abi cv;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_timer_gettime(%d, 0x%x)\n", current->pid, timer_id, (unsigned int)curr_value);
#endif /*__DEBUG__ */

	if(!(t = find_timer(current, timer_id))) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, curr_value, sizeof(struct itimerspec_abi)))) {
		return errno;
	}
	ticks2ts(t->value, &cv.it_value);
	ticks2ts(t->interval, &cv.it_interval);
	memcpy_b(curr_value, &cv, sizeof(struct itimerspec_abi));
	return 0;
}

int sys_timer_getoverrun(int timer_id)
{
	struct posix_timer *t;

#ifdef __DEBUG__
	printk("(pid %d) sys_timer_getoverrun(%d)\n", current->pid, timer_id);
#endif /*__DEBUG__ */

	if(!(t = find_timer(current, timer_id))) {
		return -EINVAL;
	}
	return t->overrun;
}

int sys_timer_delete(int timer_id)
{
	struct posix_timer *t, **tail;

#ifdef __DEBUG__
	printk("(pid %d) sys_timer_delete(%d)\n", current->pid, timer_id);
#endif /*__DEBUG__ */

	tail = &current->ptimers;
	while(*tail) {
		if((*tail)->id == timer_id) {
			t = *tail;
			*tail = t->next;
			kfree((addr_t)t);
			return 0;
		}
		tail = &(*tail)->next;
	}
	return -EINVAL;
}
