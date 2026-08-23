/*
 * fiwix/kernel64/sched64.c
 *
 * Fiwix64 M3 (phase B): minimal round-robin scheduler for kernel threads.
 *
 * Thread 0 is the idle (main) thread; worker threads are created with
 * thread_create64(). Every SCHED_QUANTUM ticks the timer IRQ calls
 * sched64_tick(), which hands the CPU to the next runnable thread via
 * switch_to64() (kernel64/switch64.S). All threads run at CPL 0; the
 * switch happens inside the IRQ handler, so a preempted thread resumes
 * inside its own interrupted context and iretq's back to itself.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fiwix/efi.h>
#include "serial64.h"

#define MAX_THREADS64	4
#define THREAD_STACK64	16384
#define SCHED_QUANTUM	10	/* ticks per round-robin quantum (100 ms) */

#define THREAD_RUNNABLE	0
#define THREAD_EXITED	1

struct thread64 {
	unsigned long id;
	unsigned long rsp;	/* saved kernel stack pointer (switch_to64) */
	char stack[THREAD_STACK64] __attribute__((aligned(16)));
	int state;
};

static struct thread64 threads[MAX_THREADS64];
static int nr_threads = 1;	/* thread 0 = idle (main) */
static int cur_idx;		/* index of the currently running thread */
static unsigned long idle_rsp;	/* thread 0's saved stack pointer */

struct thread64 *current64;

extern void switch_to64(unsigned long *old_rsp, unsigned long new_rsp);
/* hidden: taking the address of an extern fn must emit lea, not a
 * GOT-style load (ld -m i386pep resolves those by loading the code) */
extern void thread_trampoline64(void) __attribute__((visibility("hidden")));
extern unsigned long get_ticks64(void);

static void schedule64(void)
{
	struct thread64 *old, *next;
	unsigned long *old_rsp;
	int i, idx;

	/* no other runnable thread yet (idle-only): nothing to switch to.
	 * thread 0 has no prepared frame - its rsp lives in idle_rsp. */
	if(nr_threads <= 1) {
		return;
	}
	old = &threads[cur_idx];
	for(i = 1; i <= nr_threads; i++) {
		idx = (cur_idx + i) % nr_threads;
		if(idx == 0 && nr_threads > 1) {
			continue;	/* idle only runs when nothing else can */
		}
		if(threads[idx].state == THREAD_RUNNABLE) {
			next = &threads[idx];
			if(cur_idx == 0) {
				old_rsp = &idle_rsp;
			} else {
				old_rsp = &old->rsp;
			}
			current64 = next;
			cur_idx = idx;
			switch_to64(old_rsp, next->rsp);
			__asm__ __volatile__("" ::: "memory");
			/* resumed here when switched back to */
			return;
		}
	}
}

unsigned long thread_id64(void)
{
	return current64 ? current64->id : 0;
}

void sched64_tick(void)
{
	if((get_ticks64() % SCHED_QUANTUM) == 0) {
		schedule64();
	}
}

void thread_create64(void (*fn)(void))
{
	unsigned long *sp;

	if(nr_threads >= MAX_THREADS64) {
		return;
	}
	threads[nr_threads].id = nr_threads;
	threads[nr_threads].state = THREAD_RUNNABLE;

	/* prepare the initial context. switch_to64 pops r15..rbx and rets
	 * into thread_trampoline64, which sti's and jumps to fn (on top). */
	sp = (unsigned long *)(threads[nr_threads].stack + THREAD_STACK64);
	*--sp = (unsigned long)fn;
	*--sp = (unsigned long)thread_trampoline64;
	*--sp = 0;	/* rbx */
	*--sp = 0;	/* rbp */
	*--sp = 0;	/* r12 */
	*--sp = 0;	/* r13 */
	*--sp = 0;	/* r14 */
	*--sp = 0;	/* r15 */
	threads[nr_threads].rsp = (unsigned long)sp;

	nr_threads++;
}

