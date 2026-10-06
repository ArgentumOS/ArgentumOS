/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_thread, unit of 1 — F13.17's acceptance for the locking classes and NSThread.
 * docs/design/foundation-plan.md §10 (and §62.60 for NSConditionLock, which joined the family later).
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus <sys/time.h> for the elapsed-time
 * measurements.
 *
 * EVERY WAIT IN THIS FILE IS BOUNDED. A lock or condition test that could wait forever would turn a
 * failure into a hang, and a hang is the one outcome a probe cannot report — so the main thread always
 * waits for a flag under the LOCK it already knows how to take, with a deadline, and asserts the flag.
 *
 * THE MEASUREMENT THAT EARNS ITS PLACE IS `lock-serialises-two-threads`: two threads each add to one
 * counter 20000 times under an NSLock, and the total is asserted to be EXACTLY 40000. A lock that did
 * nothing would give a smaller number and nothing else, which is what makes the number the check.
 *
 * AND THE NEWEST CHECK IS THE SAME IDEA FOR `NSConditionLock`: one thread BLOCKS in -lockWhenCondition:1
 * and another sets that value, so the handover cannot happen unless the class really does hold the lock
 * across the test and the wait.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <sys/time.h>

#define PER_THREAD 20000

/* THE SHARED STATE, and the object the threads are told to run. */
@interface ThreadWork : NSObject
{
	NSLock *_counterLock;
	NSConditionLock *_conditionLock;
	NSLock *_flagLock;
	NSInteger _counter;
	BOOL _woken;
	BOOL _ran;
	BOOL _handover;
	id _argument;
	double _slept;
}
- (instancetype)init;
- (void)addMany:(id)ignored;
- (void)conditionLockWaiter:(id)ignored;
- (void)conditionLockSignaller:(id)ignored;
- (void)record:(id)argument;
- (NSInteger)counter;
- (BOOL)woken;
- (BOOL)ran;
- (BOOL)handover;
- (id)argument;
@end


/* §63.195: a subclass whose ONLY difference is the body — which is what -main being the hook MEANS. */
@interface FNProbeThread : NSThread
@end

static volatile int fn_probe_main_ran = 0;

@implementation FNProbeThread
- (void)main
{
	fn_probe_main_ran = 1;
}
@end

@implementation ThreadWork

- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_counterLock = [[NSLock alloc] init];
		_flagLock = [[NSLock alloc] init];
		_conditionLock = [[NSConditionLock alloc] initWithCondition:0];
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



/* LOCK AND WAIT AS ONE STEP: this thread blocks inside the lock until the VALUE is 1, and there is no window
 * between the test and the wait for the signaller to slip into — which is the whole reason the class exists. */
- (void)conditionLockWaiter:(id)ignored
{
	(void)ignored;
	[_conditionLock lockWhenCondition:1];
	[_flagLock lock];
	_handover = YES;
	[_flagLock unlock];
	[_conditionLock unlock];
}

/* THE OTHER HALF: set the value and release, which wakes the waiter on the OLD value to look again. */
- (void)conditionLockSignaller:(id)ignored
{
	(void)ignored;
	[NSThread sleepForTimeInterval:0.05];
	[_conditionLock lock];
	[_conditionLock unlockWithCondition:1];
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

- (BOOL)handover
{
	BOOL value;

	[_flagLock lock];
	value = _handover;
	[_flagLock unlock];
	return value;
}

- (id)argument { return _argument; }

@end

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-THREAD %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-THREAD %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

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
	covers("NSThread", "initWithTarget:selector:object:");
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
	covers("NSThread", "sleepUntilDate:");
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

	/* ---- NSConditionLock (§62.60): the value-carrying member of the family -------------------------- */
	{
		/* THE HANDOVER, ACROSS TWO THREADS, WHICH IS THE WHOLE CLASS. The waiter blocks inside
		 * -lockWhenCondition:1 (the value starts at 0), and the signaller sets that value through
		 * -unlockWithCondition:. A class that took the lock, tested and released BEFORE waiting would
		 * lose exactly this handover, so the flag is the assertion. */
		ThreadWork *work = [[ThreadWork alloc] init];
		double deadline = fn_now() + 3.0;

		[NSThread detachNewThreadSelector:@selector(conditionLockWaiter:) toTarget:work withObject:nil];
		[NSThread sleepForTimeInterval:0.05];
		[NSThread detachNewThreadSelector:@selector(conditionLockSignaller:) toTarget:work
				       withObject:nil];
		while (![work handover] && fn_now() < deadline) {
			[NSThread sleepForTimeInterval:0.005];
		}
		check("condition-lock-hands-over-across-threads",
		      [work handover],
		      [NSString stringWithFormat:@"handover=%d", (int)[work handover]]);
	}

	{
		NSConditionLock *lock = [[NSConditionLock alloc] initWithCondition:7];
		BOOL exact;

		check("condition-lock-exposes-its-condition",
		      [lock condition] == 7,
		      [NSString stringWithFormat:@"condition=%ld", (long)[lock condition]]);
		exact = [lock tryLockWhenCondition:7];
		if (exact) {
			[lock unlock];
		}
		{
			/* A WRONG VALUE MUST NOT LEAVE THE LOCK HELD: the class takes it, tests, and releases —
			 * so the door is usable again immediately, which the last clause proves by taking it. */
			BOOL wrong = [lock tryLockWhenCondition:8];
			BOOL freeAgain = [lock tryLock];

			check("condition-lock-try-when-condition",
			      exact && !wrong && freeAgain,
			      [NSString stringWithFormat:@"exact=%d wrong=%d freeAgain=%d",
				(int)exact, (int)wrong, (int)freeAgain]);
			if (freeAgain) {
				[lock unlock];
			}
		}
	}

	{
		NSConditionLock *lock = [[NSConditionLock alloc] initWithCondition:3];

		[lock lock];
		[lock unlockWithCondition:9];
		check("condition-lock-unlock-sets-the-value",
		      [lock condition] == 9,
		      [NSString stringWithFormat:@"condition=%ld", (long)[lock condition]]);
	}

	{
		NSConditionLock *lock = [[NSConditionLock alloc] initWithCondition:1];
		double started;
		BOOL got;
		double elapsed;

		[lock lock];
		started = fn_now();
		got = [lock lockBeforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		elapsed = fn_now() - started;
		check("condition-lock-before-date-times-out",
		      !got && elapsed >= 0.04,
		      [NSString stringWithFormat:@"got=%d elapsed=%.3f", (int)got, elapsed]);
		[lock unlock];
	}

	{
		/* THE CONDITION IS NEVER MET, so the only way out is the deadline — and the lock must NOT be left
		 * held when it gives up (a timed door that leaked its lock would wedge the next caller). */
		NSConditionLock *lock = [[NSConditionLock alloc] initWithCondition:3];
		double started = fn_now();
		BOOL got = [lock lockWhenCondition:4 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		double elapsed = fn_now() - started;
		BOOL free = [lock tryLock];

		check("condition-lock-when-condition-before-date-times-out",
		      !got && elapsed >= 0.04 && free,
		      [NSString stringWithFormat:@"got=%d elapsed=%.3f freeAfter=%d", (int)got, elapsed,
			(int)free]);
		if (free) {
			[lock unlock];
		}
	}

	{
		/* APPLE DECLARES `name` `copy`, AND THIS IS THE CHECK THAT PROVES IT FOR ALL FOUR CLASSES: a
		 * MUTABLE source is handed over and then MUTATED, so a setter that merely assigned would answer
		 * the mutated string. It is also the regression guard for the three siblings, whose setters
		 * assigned until §62.60. */
		NSMutableString *source = [[NSMutableString alloc] initWithString:@"before"];
		NSLock *plain = [[NSLock alloc] init];
		NSRecursiveLock *recursive = [[NSRecursiveLock alloc] init];
		NSConditionLock *conditionLock = [[NSConditionLock alloc] initWithCondition:0];

		[plain setName:source];
		[recursive setName:source];
		[conditionLock setName:source];
		[source appendString:@"-mutated"];
		check("the-name-setter-copies-for-the-whole-family",
		      [[plain name] isEqualToString:@"before"] &&
		      [[recursive name] isEqualToString:@"before"] &&
		      [[conditionLock name] isEqualToString:@"before"],
		      [NSString stringWithFormat:@"lock=%@ recursive=%@ conditionLock=%@",
			[plain name], [recursive name], [conditionLock name]]);
	}


	{
		/* §63.195: A THREAD WHOSE BODY IS A BLOCK, and the flag that says a second thread has RUN. */
		__block int fired = 0;
		int spins = 0;

		[NSThread detachNewThreadWithBlock:^{ fired = 1; }];
		while (fired == 0 && spins < 200) {
			[NSThread sleepForTimeInterval:0.01];
			spins++;
		}
		check("thread-block-body-runs", fired == 1 && [NSThread isMultiThreaded],
		      [NSString stringWithFormat:@"fired=%d spins=%d multi=%d", fired, spins, [NSThread isMultiThreaded]]);
	covers("NSThread", "detachNewThreadWithBlock:");
	}
	{
		/* AND -main IS THE HOOK: a subclass overriding ONLY the body runs. It has no target, no selector and
		 * no block, and that must be LEGAL — which is the case the first pass of -start refused. */
		FNProbeThread *thread = [[FNProbeThread alloc] init];
		int spins = 0;

		fn_probe_main_ran = 0;
		[thread setStackSize:262144];
		[thread start];
		while (fn_probe_main_ran == 0 && spins < 200) {
			[NSThread sleepForTimeInterval:0.01];
			spins++;
		}
		check("thread-main-is-the-overridable-hook", fn_probe_main_ran == 1 && [thread stackSize] == 262144,
		      [NSString stringWithFormat:@"main-ran=%d stackSize=%lu spins=%d", fn_probe_main_ran,
			(unsigned long)[thread stackSize], spins]);
	}
	{
		/* THE VALUES A THREAD CARRIES: priority (0.0-1.0, mapped to nice with a stated reading), service, and
		 * the stack size — asserted as round trips, which is what this tree can promise. */
		NSThread *current = [NSThread currentThread];

		(void)[NSThread setThreadPriority:0.75];
		[current setQualityOfService:NSQualityOfServiceUtility];
		check("thread-priority-and-service-round-trip",
		      [NSThread threadPriority] == 0.75 && [current threadPriority] == 0.75 &&
		      [current qualityOfService] == NSQualityOfServiceUtility,
		      [NSString stringWithFormat:@"class=%g instance=%g qos=%d", [NSThread threadPriority],
			[current threadPriority], (int)[current qualityOfService]]);
	covers("NSThread", "setThreadPriority:");
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
