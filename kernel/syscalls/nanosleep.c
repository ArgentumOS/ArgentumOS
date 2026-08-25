/*
 * fnx/kernel/syscalls/nanosleep.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/fs.h>
#include <fnx/time.h>
#include <fnx/timer.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/errno.h>
#include <fnx/kernel.h>

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME	1
#endif

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
	int errno, nsec;
	unsigned int timeout, flags;

#ifdef __DEBUG__
	printk("(pid %d) sys_nanosleep(0x%08x, 0x%08x)\n", current->pid, (unsigned int)req, (unsigned int)rem);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_READ, req, sizeof(struct timespec)))) {
		return errno;
	}
	if(req->tv_sec < 0 || req->tv_nsec >= 1000000000L || req->tv_nsec < 0) {
		return -EINVAL;
	}

	/*
	 * Since the current maximum precision of the kernel is only 10ms, we
	 * need to convert any lower request to a minimum of 10ms, even knowing
	 * that this might increase the sleep a bit more than the requested.
	 */
	nsec = req->tv_nsec;
	if(nsec < 10000000L) {
		nsec *= 10;
	}

	/*
	 * Interrupts must be disabled before setting current->timeout in order
	 * to avoid a race condition. Otherwise it might occur that timeout is
	 * so small that it would reach zero before the call to sleep(). In this
	 * case, the process would miss the wakeup() and would stay in the sleep
	 * queue forever.
	 */
	timeout = (req->tv_sec * HZ) + (nsec * HZ / 1000000000L);
	if(timeout) {
		SAVE_FLAGS(flags); CLI();
		current->timeout = timeout;
		sleep(&sys_nanosleep, PROC_INTERRUPTIBLE);
		RESTORE_FLAGS(flags);
		if(current->timeout) {
			if(rem) {
				if((errno = check_user_area(VERIFY_WRITE, rem, sizeof(struct timespec)))) {
					return errno;
				}
				rem->tv_sec = current->timeout / HZ;
				rem->tv_nsec = (current->timeout % HZ) * 1000000000L / HZ;
			}
			return -EINTR;
		}
	}
	return 0;
}

/* clock_nanosleep(230): sleep with a clock id + TIMER_ABSTIME support */
int sys_clock_nanosleep(int clock_id, int flags, const struct timespec *req, struct timespec *rem)
{
	struct timespec r, abst;
	int errno, nsec;
	unsigned int timeout, uflags;

	if((errno = check_user_area(VERIFY_READ, req, sizeof(struct timespec)))) {
		return errno;
	}
	if(req->tv_sec < 0 || req->tv_nsec >= 1000000000L || req->tv_nsec < 0) {
		return -EINVAL;
	}
	if(flags & ~TIMER_ABSTIME) {
		return -EINVAL;
	}

	if(flags & TIMER_ABSTIME) {
		/* relative = abs - now (only CLOCK_REALTIME/MONOTONIC) */
		unsigned int now;

		now = (clock_id == 1) ? CURRENT_TICKS : CURRENT_TIME * HZ;
		if(clock_id == 0) {
			now = CURRENT_TIME * HZ + (CURRENT_TICKS % HZ);
		}
		abst = *req;
		{
			unsigned int ns = abst.tv_nsec;
			if(ns < 10000000L) {
				ns *= 10;
			}
			timeout = (abst.tv_sec * HZ) + (ns * HZ / 1000000000L);
		}
		timeout = (timeout > now) ? (timeout - now) : 0;
	} else {
		nsec = req->tv_nsec;
		if(nsec < 10000000L) {
			nsec *= 10;
		}
		timeout = (req->tv_sec * HZ) + (nsec * HZ / 1000000000L);
	}

	if(timeout) {
		SAVE_FLAGS(uflags); CLI();
		current->timeout = timeout;
		sleep(&sys_nanosleep, PROC_INTERRUPTIBLE);
		RESTORE_FLAGS(uflags);
		if(current->timeout) {
			if(rem) {
				if((errno = check_user_area(VERIFY_WRITE, rem, sizeof(struct timespec)))) {
					return errno;
				}
				rem->tv_sec = current->timeout / HZ;
				rem->tv_nsec = (current->timeout % HZ) * 1000000000L / HZ;
			}
			return -EINTR;
		}
	}
	return 0;
}
