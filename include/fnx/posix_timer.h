/*
 * fnx/include/fnx/posix_timer.h
 *
 * FNX: POSIX per-process timers (timer_create/settime/gettime/
 * getoverrun/delete, syscalls 222-226).
 */

#ifndef _FNX_POSIX_TIMER_H
#define _FNX_POSIX_TIMER_H

#include <fnx/types.h>

struct proc;

struct posix_timer {
	int id;			/* timer id (0..NR_POSIX_TIMERS-1) */
	int clock_id;	/* CLOCK_REALTIME (0) / CLOCK_MONOTONIC (1) */
	unsigned int interval;	/* periodic interval in ticks (0 = one-shot) */
	unsigned int value;	/* ticks remaining until expiry */
	int overrun;		/* expirations missed while blocked */
	int sigev_signo;	/* signal to deliver (0 = none) */
	struct posix_timer *next;
};

extern void posix_timer_tick(struct proc *);

#endif /* _FNX_POSIX_TIMER_H */
