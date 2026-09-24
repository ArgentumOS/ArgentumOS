/*
 * fnx/include/fnx/timer.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_TIMER_H
#define _FNX_TIMER_H

#include <fnx/types.h>
#include <fnx/sigcontext.h>

#define TIMER_IRQ	0
#define HZ		100	/* kernel's Hertz rate (100 = 10ms) */
#define TICK		(1000000 / HZ)

#define UNIX_EPOCH	1970

#define LEAP_YEAR(y)	((y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0))
#define DAYS_PER_YEAR(y)	((LEAP_YEAR(y)) ? 366 : 365)

#define SECS_PER_MIN	60
#define SECS_PER_HOUR	(SECS_PER_MIN * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)

#define INFINITE_WAIT	0xFFFFFFFF

/* THE CALLBACK'S ARGUMENT IS AN ADDRESS, NOT AN `unsigned int` - A BUG FIX, NOT A STYLE CHOICE
 * (foundation-plan.md §58.1c). It was `unsigned int` in here AND in do_callouts_bh(), while tty_read's
 * VTIME sites store a POINTER in it:
 *
 *     creq.arg = (addr_t)&tty->read_q;
 *
 * so the address was truncated to 32 bits AT THE ASSIGNMENT: wait_vtime_off() then called
 * wakeup((void *)(uint32_t)address), which hashes the wrong bucket of sleep_hash_table and matches no
 * sleeper - so the VTIME callout could not wake anything on this 64-bit kernel, whichever channel it
 * named. MEASURED, not deduced: kernel_pty_read's VMIN=0/VTIME=1 read never returned, and correcting
 * only the channel it named changed nothing. SEVEN OF THE EIGHT callout users pass a small integer
 * (a console, a drive number), where 32 bits is plenty - which is exactly why the truncation went
 * unnoticed; wait_vtime_off is the only one handed a pointer, and it is the only one that was silent. */
struct callout {
	int expires;
	void (*fn)(addr_t);
	addr_t arg;
	struct callout *next;
};

struct callout_req {
	void (*fn)(addr_t);
	addr_t arg;
};

void add_callout(struct callout_req *, unsigned int);
void del_callout(struct callout_req *);
void irq_timer(int, struct sigcontext *);
void irq_timer_bh(struct sigcontext *);
void do_callouts_bh(struct sigcontext *);
void get_system_time(void);
void set_system_time(__time_t);
int gettimeoffset(void);
void timer_init(void);

#endif /* _FNX_TIMER_H */
