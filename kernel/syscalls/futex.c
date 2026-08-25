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
			if(ticks) {
				SAVE_FLAGS(flags); CLI();
				current->timeout = ticks;
				errno = sleep(uaddr, PROC_INTERRUPTIBLE);
				RESTORE_FLAGS(flags);
				if(errno) {
					return -EINTR;
				}
				if(!current->timeout) {
					/* woken by the timer tick */
					return -ETIMEDOUT;
				}
				current->timeout = 0;
			} else {
				errno = sleep(uaddr, PROC_INTERRUPTIBLE);
				if(errno) {
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
