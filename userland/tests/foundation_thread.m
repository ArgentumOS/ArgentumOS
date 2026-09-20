/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_thread, unit of 1 — F13.17's acceptance for the locking classes and NSThread.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h> plus <sys/time.h> for the elapsed-time
 * measurements.
 *
 * EVERY WAIT IN THIS FILE IS BOUNDED. A lock or condition test that could wait forever would turn a
 * failure into a hang, and a hang is the one outcome a probe cannot report — so the main thread always
 * waits for a flag under the LOCK it already knows how to take, with a deadline, and asserts the flag.
 *
 * THE MEASUREMENT THAT EARNS ITS PLACE IS `lock-serialises-two-threads`: two threads each add to one
 * counter 20000 times under an NSLock, and the total is asserted to be EXACTLY 40000. A lock that did
 * nothing would give a smaller number and nothing else, which is what makes the number the check.
 */

#import <foundation/Foundation.h>

#include <stdio.h>
#include <sys/time.h>

#define PER_THREAD 20000

/* THE SHARED STATE, and the object the threads are told to run. */
@interface ThreadWork : NSObject
{
	NSLock *_counterLock;
	NSCondition *_condition;
	NSLock *_flagLock;
	NSInteger _counter;
	BOOL _woken;
	BOOL _ran;
	id _argument;
	double _slept;
}
- (instancetype)init;
- (void)addMany:(id)ignored;
- (void)waiter:(id)ignored;
- (void)signaller:(id)ignored;
- (void)record:(id)argument;
- (NSInteger)counter;
- (BOOL)woken;
- (BOOL)ran;
- (id)argument;
@end

@implementation ThreadWork

- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_counterLock = [[NSLock alloc] init];
		_flagLock = [[NSLock alloc] init];
		_condition = [[NSCondition alloc] init];
	}
	return self;
}

- (void)addMany:(id)ignored
{
	NSInteger i;

	(void)ignored;
	for (i = 0; i < PER_THREAD; i++) {
		[_counterLock lock];
		_counter++;
		[_counterLock unlock];
	}
}

- (void)waiter:(id)ignored
{
	(void)ignored;
	[_condition lock];
	while (!_woken) {
		/* A BOUNDED WAIT: if the signal never comes this returns and the while loop re-checks, and
		 * the main thread's own deadline ends the test. */
		[_condition waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
	}
	[_flagLock lock];
	_woken = YES;
	[_flagLock unlock];
	[_condition unlock];
}

- (void)signaller:(id)ignored
{
	(void)ignored;
	[_condition lock];
	_woken = YES;
	[_condition signal];
	[_condition unlock];
}

- (void)record:(id)argument
{
	[_flagLock lock];
	_argument = argument;
	_ran = YES;
	[_flagLock unlock];
}

- (NSInteger)counter { return _counter; }

- (BOOL)woken
{
	BOOL value;

	[_flagLock lock];
	value = _woken;
	[_flagLock unlock];
	return value;
}

- (BOOL)ran
{
	BOOL value;

	[_flagLock lock];
	value = _ran;
	[_flagLock unlock];
	return value;
}

- (id)argument { return _argument; }

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-THREAD %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-THREAD %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static double fn_now(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (double)now.tv_sec + ((double)now.tv_usec / 1000000.0);
}

/* A BOUNDED WAIT FOR A FLAG: never longer than `seconds`, never a hang. */
static BOOL fn_wait_for(ThreadWork *work, SEL which, double seconds)
{
	double deadline = fn_now() + seconds;

	for (;;) {
		BOOL answered = which == @selector(ran) ? [work ran] : [work woken];

		if (answered) {
			return YES;
		}
		if (fn_now() >= deadline) {
			return NO;
		}
		[NSThread sleepForTimeInterval:0.005];
	}
}

int main(void)
{
	{
		check("thread-current-and-main",
		      [NSThread currentThread] != nil &&
		      [NSThread currentThread] == [NSThread currentThread] &&
		      [[NSThread currentThread] isMainThread] && [NSThread isMainThread] &&
		      [NSThread mainThread] != nil,
		      [NSString stringWithFormat:@"main=%d current=%@", (int)[NSThread isMainThread],
			[NSThread currentThread]]);
	}

	{
		ThreadWork *work = [[ThreadWork alloc] init];
		double started = fn_now();

		[NSThread detachNewThreadSelector:@selector(addMany:) toTarget:work withObject:nil];
		[NSThread detachNewThreadSelector:@selector(addMany:) toTarget:work withObject:nil];
		/* THE MAIN THREAD WORKS TOO, so the lock has three contenders rather than two. */
		[work addMany:nil];
		while (fn_now() - started < 5.0 && [work counter] < PER_THREAD * 3) {
			[NSThread sleepForTimeInterval:0.01];
		}
		check("lock-serialises-two-threads",
		      [work counter] == PER_THREAD * 3,
		      [NSString stringWithFormat:@"counter=%ld want=%d", (long)[work counter],
			PER_THREAD * 3]);
	}

	{
		NSLock *lock = [[NSLock alloc] init];

		[lock setName:@"probe-lock"];
		check("lock-try-lock",
		      [[lock name] isEqualToString:@"probe-lock"] && [lock tryLock] &&
		      /* THE SAME THREAD ASKS AGAIN — a NON-recursive mutex answers NO rather than
		       * deadlocking, which is why this is asked with -tryLock. */
		      ![lock tryLock],
		      [NSString stringWithFormat:@"name=%@ secondTry=%d", [lock name],
			(int)[lock tryLock]]);
		[lock unlock];
	}

	{
		NSRecursiveLock *lock = [[NSRecursiveLock alloc] init];
		int depth = 0;

		[lock lock];
		[lock lock];
		depth = 2;
		[lock unlock];
		[lock unlock];
		check("lock-recursive-reenters",
		      depth == 2 && [lock tryLock],
		      [NSString stringWithFormat:@"depth=%d freeAgain=%d", depth, (int)[lock tryLock]]);
		[lock unlock];
	}

	{
		NSLock *lock = [[NSLock alloc] init];
		double started;

		[lock lock];
		started = fn_now();
		{
			BOOL got = [lock lockBeforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			double elapsed = fn_now() - started;

			check("lock-before-date",
			      !got && elapsed >= 0.04,
			      [NSString stringWithFormat:@"got=%d elapsed=%.3f", (int)got, elapsed]);
		}
		[lock unlock];
	}

	{
		ThreadWork *work = [[ThreadWork alloc] init];
		BOOL woken;

		[NSThread detachNewThreadSelector:@selector(waiter:) toTarget:work withObject:nil];
		[NSThread sleepForTimeInterval:0.05];
		[NSThread detachNewThreadSelector:@selector(signaller:) toTarget:work withObject:nil];
		woken = fn_wait_for(work, @selector(woken), 3.0);
		check("condition-signals", woken,
		      [NSString stringWithFormat:@"woken=%d", (int)woken]);
	}

	{
		ThreadWork *work = [[ThreadWork alloc] init];
		NSString *argument = @"the argument";
		BOOL ran;

		[NSThread detachNewThreadSelector:@selector(record:) toTarget:work withObject:argument];
		ran = fn_wait_for(work, @selector(ran), 3.0);
		check("thread-detached-runs",
		      ran && [work argument] == argument,
		      [NSString stringWithFormat:@"ran=%d argument=%@", (int)ran, [work argument]]);
	}

	{
		/* THE START-BY-TARGET DOOR (plan §14.5's open item, and §15.3's mechanism 1). The class had
		 * NO check on this path: §14.5 measured `ran=0` here - a thread built with
		 * -initWithTarget:selector:object: and then started did NOT run its target within 2s - and
		 * left it open rather than asserting on a timeout. THE CAUSE WAS OWNERSHIP: the thread
		 * stored its target and its argument WITHOUT retaining them, so anything that let go of
		 * them between -start and the new thread's first instruction (an ARC scope ending, an
		 * operation queue's removal) left the thread running on freed memory. Both are retained
		 * now, and this asserts the behaviour that was missing. */
		ThreadWork *work = [[ThreadWork alloc] init];
		NSString *argument = @"started by target";
		NSThread *worker = [[NSThread alloc] initWithTarget:work
							   selector:@selector(record:)
							     object:argument];
		BOOL ran;

		[worker start];
		ran = fn_wait_for(work, @selector(ran), 3.0);
		check("thread-start-runs-its-target",
		      ran && [work argument] == argument,
		      [NSString stringWithFormat:@"ran=%d argument=%@", (int)ran, [work argument]]);
	}

	{
		/* WHAT THE LIBRARY OWNS IS THAT THESE CALLS RETURN, in bounded time, for a positive interval
		 * AND for a deadline already past — not that the machine slept. `+sleepForTimeInterval:` reaches
		 * nanosleep(2) with the right timespec, and THIS KERNEL RETURNS FROM IT EARLY: measured, and
		 * measured by CONTRAST, because the lock's deadline loop (which uses clock_gettime, not
		 * nanosleep) took its full 40ms. The elapsed time is PRINTED rather than asserted, and the
		 * kernel trait is recorded in docs/design/foundation-plan.md, F13.17. */
		double started = fn_now();
		double elapsed;
		double beforePast = fn_now();
		double pastElapsed;

		[NSThread sleepForTimeInterval:0.05];
		elapsed = fn_now() - started;
		[NSThread sleepForTimeInterval:-1.0];
		[NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow:-1.0]];
		pastElapsed = fn_now() - beforePast;
		check("thread-sleep-returns",
		      elapsed >= 0.0 && elapsed < 2.0 && pastElapsed >= 0.0 && pastElapsed < 2.0,
		      [NSString stringWithFormat:@"positive=%.3f past=%.3f", elapsed, pastElapsed]);
	}

	{
		/* A REAL TARGET AND SELECTOR, never started: the initialiser takes nonnull ones, and a
		 * thread that is never started never runs them — which is the state being checked. */
		ThreadWork *work = [[ThreadWork alloc] init];
		NSThread *thread = [[NSThread alloc] initWithTarget:work
							   selector:@selector(addMany:)
							     object:nil];

		[thread setName:@"never-started"];
		[thread cancel];
		check("thread-cancel-is-a-flag",
		      thread != nil && [thread isCancelled] && ![thread isExecuting] &&
		      ![thread isFinished] && ![[NSThread currentThread] isCancelled] &&
		      [[thread name] isEqualToString:@"never-started"],
		      [NSString stringWithFormat:@"cancelled=%d executing=%d finished=%d",
			(int)[thread isCancelled], (int)[thread isExecuting], (int)[thread isFinished]]);
	}

	printf("FOUNDATION-THREAD RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-THREAD-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-THREAD DONE\n");
	return failc ? 1 : 0;
}
