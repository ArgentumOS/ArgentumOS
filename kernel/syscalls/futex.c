/*
 * fnx/kernel/syscalls/futex.c
 *
 * FNX: futex(202) - fast user-space mutex.
 *
 * Minimal but correct-for-musl implementation:
 *   FUTEX_WAIT(addr, val, timeout): if *addr != val return -EAGAIN,
 *     else sleep until FUTEX_WAKE (or timeout/signal).
 *   FUTEX_WAKE(addr, n): wake up to n sleepers on addr.
 *   FUTEX_REQUEUE / FUTEX_CMP_REQUEUE: musl's pthread_cond uses these to
 *     move waiters from the condvar futex to the mutex futex; we realize
 *     them as "wake them all" (waking extra waiters is always safe for
 *     futex users - they re-check the predicate).
 *
 * The kernel's sleep()/wakeup() hash the wait address, so the user-space
 * futex word address works directly as the wait key.
 */

#include <fnx/syscalls.h>
#include <fnx/asm.h>
#include <fnx/sleep.h>
#include <fnx/timer.h>
#include <fnx/time.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_REQUEUE		3
#define FUTEX_CMP_REQUEUE	4
#define FUTEX_PRIVATE		128
#define FUTEX_CLOCK_REALTIME	256

int sys_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3)
{
	int nsec;
	unsigned int ticks, flags;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_futex(0x%x, %d, %d)\n", current->pid, (unsigned int)uaddr, op, val);
#endif /*__DEBUG__ */

	if(!uaddr) {
		return -EINVAL;
	}
	/* strip the PRIVATE / CLOCK_REALTIME modifiers */
	op &= ~(FUTEX_PRIVATE | FUTEX_CLOCK_REALTIME);

	switch(op) {
		case FUTEX_WAIT:
			if((errno = check_user_area(VERIFY_READ, uaddr, sizeof(int)))) {
				return errno;
			}
			if(*uaddr != val) {
				return -EAGAIN;
			}
			if(timeout) {
				if((errno = check_user_area(VERIFY_READ, timeout, sizeof(struct timespec)))) {
					return errno;
				}
				if(timeout->tv_sec < 0 || timeout->tv_nsec >= 1000000000L || timeout->tv_nsec < 0) {
					return -EINVAL;
				}
				nsec = timeout->tv_nsec;
				if(nsec < 10000000L) {
					nsec *= 10;	/* 10ms granularity */
				}
				ticks = (timeout->tv_sec * HZ) + (nsec * HZ / 1000000000L);
			} else {
				ticks = 0;
			}
			/* ARM BEFORE THE LOOK - §58.1's cure, AND THIS IS THE SITE WHERE IT MATTERS MOST. A FUTEX_WAIT's
			 * contract is "if *uaddr != val return -EAGAIN, else sleep", and the check and the registration
			 * are NOT atomic together. `pthread_mutex_lock` reads the word, sees it held, and calls
			 * FUTEX_WAIT; the kernel then checks the word (still held) and REGISTERS - and if the holder
			 * unlocks and calls FUTEX_WAKE inside that gap, the wake finds no waiter, and the waiter sleeps
			 * FOR EVER.
			 *
			 * THAT IS MEASURED, NOT DEDUCED (foundation-plan.md §58.2h): in an NSOperationQueue burst of 50
			 * blocks, ~half the worker threads park exactly here, with the scheduler's own three counters
			 * all agreeing `_running == _pending == _operations == 26/27` - workers that ran their block and
			 * never got back through the cleanup that re-takes the queue's mutex.
			 *
			 * ARMED FIRST, the wake either finds us or has already happened - and the re-look below SEES it,
			 * because a mutex unlock STORES the word and THEN wakes (musl's a_store then __wake), so the
			 * state change is always visible to a look that follows the arm. */
			if(ticks) {
				SAVE_FLAGS(flags); CLI();
				current->timeout = ticks;
				sleep_arm(uaddr);
				if(*uaddr != val) {
					/* THE RE-LOOK: the word changed while we armed, so the answer is the one the
					 * check above would have given - and the timeout must not be left armed for the
					 * next syscall to inherit. */
					current->timeout = 0;
					sleep_disarm();
					RESTORE_FLAGS(flags);
					return -EAGAIN;
				}
				errno = sleep_commit(uaddr, PROC_INTERRUPTIBLE);
				RESTORE_FLAGS(flags);
				if(errno) {
					sleep_disarm();
					return -EINTR;
				}
				if(!current->timeout) {
					/* woken by the timer tick */
					return -ETIMEDOUT;
				}
				current->timeout = 0;
			} else {
				/* THE DEADLINE-LESS ARM NEEDS IT MORE, NOT LESS: there is no timer to end this wait, so a
				 * wake lost in the window would park the thread for the life of the process. */
				sleep_arm(uaddr);
				if(*uaddr != val) {
					sleep_disarm();
					return -EAGAIN;
				}
				errno = sleep_commit(uaddr, PROC_INTERRUPTIBLE);
				if(errno) {
					sleep_disarm();
					return -EINTR;
				}
			}
			return 0;
		case FUTEX_WAKE:
			if(val <= 0) {
				return 0;
			}
			wakeup(uaddr);
			return val;	/* over-approximation: wakeup() wakes all */
		case FUTEX_REQUEUE:
		case FUTEX_CMP_REQUEUE:
			wakeup(uaddr);
			return val;	/* wake all waiters (safe over-approx) */
		default:
			return -ENOSYS;
	}
}
