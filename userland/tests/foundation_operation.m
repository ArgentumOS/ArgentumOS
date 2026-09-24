/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_operation, unit of 1 — F13.19's acceptance for NSOperation and NSOperationQueue.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE MEASUREMENTS THAT EARN THEIR PLACE:
 *   operation-dependency-order  an operation that DEPENDS on another runs AFTER it although both were
 *                               added to the queue in the other order — the graph is what decides;
 *   queue-serial-order          with a limit of ONE, the START ORDER is the ADDITION ORDER, which is
 *                               what makes the limit observable at all;
 *   queue-cancel-all            suspending first makes cancellation DETERMINISTIC: nothing has begun,
 *                               so "none of them ran" is a fact rather than a race;
 *   and-every-burst-leaves-the-queue-empty
 *                               the THREADED producer's burst is repeated ten times, and the scheduler
 *                               must drain to 0/0/0 after every one (§58.2q — the assertion §58.2p owed).
 *
 * AND ONE CHECK IS ABOUT A REFUSAL THAT MUST BE LOUD: `-main` is a hook, and the base class RAISES,
 * because a subclass that forgot to override it would otherwise succeed at doing nothing.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

/* THE PROBE'S SIDE OF §58.2h'S INSTRUMENT: the same selector NSOperation.m defines, declared here because a
 * declaration inside that file's private category is one this probe could not see, and Objective-C resolves the
 * call at runtime anyway. It reads the scheduler's own three numbers without writing anything. */
@interface NSOperationQueue (FNSchedulerCounts)
- (void)fnSchedulerCountsRunning:(NSUInteger *)running pending:(NSUInteger *)pending operations:(NSUInteger *)ops;
@end

/* THE LOG AND THE OPERATIONS SHARE IT, under a lock, because operations run on other threads. */
@interface OpLog : NSObject
{
	NSLock *_lock;
	NSMutableArray *_entries;
	NSOperationQueue *_seenQueue;
}
- (void)append:(NSString *)entry;
- (NSArray *)entries;
- (NSUInteger)count;
- (void)noteQueue:(NSOperationQueue *)queue;
- (NSOperationQueue *)seenQueue;
@end

@implementation OpLog

- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_lock = [[NSLock alloc] init];
		_entries = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)append:(NSString *)entry
{
	[_lock lock];
	[_entries addObject:entry];
	[_lock unlock];
}

- (NSArray *)entries
{
	NSArray *snapshot;

	[_lock lock];
	snapshot = [_entries copy];
	[_lock unlock];
	return snapshot;
}

- (NSUInteger)count
{
	NSUInteger count;

	[_lock lock];
	count = [_entries count];
	[_lock unlock];
	return count;
}

- (void)noteQueue:(NSOperationQueue *)queue
{
	[_lock lock];
	_seenQueue = queue;
	[_lock unlock];
}

- (NSOperationQueue *)seenQueue { return _seenQueue; }

@end

/* THE OPERATION: it records its name when it runs, and it can hold a dependency. */
@interface WorkOperation : NSOperation
{
	OpLog *_log;
	NSString *_label;
}
- (instancetype)initWithLabel:(NSString *)label log:(OpLog *)log;
@end

@implementation WorkOperation

- (instancetype)initWithLabel:(NSString *)label log:(OpLog *)log
{
	if ((self = [super init]) != nil) {
		_label = label;
		_log = log;
	}
	return self;
}

- (void)main
{
	[_log append:_label];
	[_log noteQueue:[NSOperationQueue currentQueue]];
}

@end

/* THE PRODUCER THAT IS NOT THE MAIN THREAD (foundation-plan.md §58.2e). It exists because every other check in
 * this probe adds operations from main(), while the stream task adds them from its own WORKER thread - a SHAPE
 * nothing else in this tree covered. The reading that first made this leg eligible ("its TLS leg loses HALF its
 * deliveries inside `[queue addOperationWithBlock:]`, §58.2d) was WITHDRAWN by §58.2p - that probe had stopped
 * waiting - so the JUSTIFICATION here is the shape and not that number: the leg is what then FOUND the
 * scheduler's own leak, once per burst, and §58.2q measures that leak as an assertion. */
@interface ProducerThread : NSObject
{
@public
	NSOperationQueue *queue;
	NSLock *lock;
	int *ran;
	int count;
}
- (void)fnProduce;
@end

@implementation ProducerThread

- (void)fnProduce
{
	/* LOCALS, NOT `self`: a block that captures `self` from a detached thread's selector is how a probe earns
	 * a use-after-free, and there is no reason for one here. */
	NSOperationQueue *q = queue;
	NSLock *l = lock;
	int *counter = ran;
	int total = count;
	int i;

	for (i = 0; i < total; i++) {
		[q addOperationWithBlock:^{
			[l lock];
			(*counter)++;
			[l unlock];
		}];
	}
}

@end

/* A TARGET WHOSE SELECTOR MARKS ITS OWN ENTRY AND EXIT, IN MEMORY (foundation-plan.md §58.2g). THE POINT IS THAT
 * IT DOES NOT WRITE: the last instrument died of its own perturbation - a raw write between the steps of the
 * queue's worker made an intermittent leak vanish (61 of 61 clean) - so this one counts with two lock-protected
 * increments and the CALLER prints the totals once, after the fact. */
@interface ThreadEntryTarget : NSObject
{
@public
	NSLock *lock;
	int *entries;
	int *exits;
}
- (void)fnEnterAndExit;
@end

@implementation ThreadEntryTarget

- (void)fnEnterAndExit
{
	NSLock *l = lock;
	int *e = entries;
	int *x = exits;

	[l lock];
	(*e)++;
	[l unlock];
	[l lock];
	(*x)++;
	[l unlock];
}

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-OPERATION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-OPERATION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		OpLog *log = [[OpLog alloc] init];
		WorkOperation *operation = [[WorkOperation alloc] initWithLabel:@"one" log:log];

		[operation start];
		check("operation-subclass-runs",
		      [log count] == 1 && [[[log entries] objectAtIndex:0] isEqualToString:@"one"] &&
		      [operation isFinished] && ![operation isExecuting] && ![operation isCancelled],
		      [NSString stringWithFormat:@"log=%@ finished=%d", [log entries],
			(int)[operation isFinished]]);
	}

	{
		/* THE BASE CLASS'S -main RAISES, which is the point of it. */
		NSOperation *plain = [[NSOperation alloc] init];
		BOOL raised = NO;

		@try {
			[plain main];
		} @catch (NSException *e) {
			(void)e;
			raised = YES;
		}
		check("operation-base-main-raises", plain != nil && raised,
		      raised ? @"raised" : @"the base -main did nothing at all");
	}

	{
		/* DEPENDENCIES DECIDE THE ORDER, NOT THE ADDITION ORDER: "second" is added FIRST and
		 * depends on "first", so it must run after it. */
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		WorkOperation *first = [[WorkOperation alloc] initWithLabel:@"first" log:log];
		WorkOperation *second = [[WorkOperation alloc] initWithLabel:@"second" log:log];

		[second addDependency:first];
		[queue addOperation:second];
		[queue addOperation:first];
		[queue waitUntilAllOperationsAreFinished];
		check("operation-dependency-order",
		      [log count] == 2 &&
		      [[[log entries] objectAtIndex:0] isEqualToString:@"first"] &&
		      [[[log entries] objectAtIndex:1] isEqualToString:@"second"] &&
		      [first isFinished] && [second isFinished],
		      [NSString stringWithFormat:@"order=%@", [log entries]]);
	}

	{
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];

		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"a" log:log]];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"b" log:log]];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"c" log:log]];
		[queue waitUntilAllOperationsAreFinished];
		check("queue-runs-and-drains",
		      [log count] == 3 && [queue operationCount] == 0 &&
		      [[queue operations] count] == 0,
		      [NSString stringWithFormat:@"log=%@ remaining=%lu", [log entries],
			(unsigned long)[queue operationCount]]);
	}

	{
		/* ONE AT A TIME, SO THE START ORDER IS THE ADDITION ORDER. */
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];

		[queue setMaxConcurrentOperationCount:1];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"1" log:log]];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"2" log:log]];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"3" log:log]];
		[queue waitUntilAllOperationsAreFinished];
		check("queue-serial-order",
		      [queue maxConcurrentOperationCount] == 1 &&
		      [[log entries] count] == 3 &&
		      [[[log entries] objectAtIndex:0] isEqualToString:@"1"] &&
		      [[[log entries] objectAtIndex:1] isEqualToString:@"2"] &&
		      [[[log entries] objectAtIndex:2] isEqualToString:@"3"],
		      [NSString stringWithFormat:@"max=%ld order=%@",
			(long)[queue maxConcurrentOperationCount], [log entries]]);
	}

	{
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		NSUInteger whileSuspended;

		[queue setSuspended:YES];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"held" log:log]];
		whileSuspended = [log count];
		check("queue-suspend-holds-work",
		      whileSuspended == 0 && [queue isSuspended] && [queue operationCount] == 1,
		      [NSString stringWithFormat:@"ranWhileSuspended=%lu count=%lu",
			(unsigned long)whileSuspended, (unsigned long)[queue operationCount]]);
		[queue setSuspended:NO];
		[queue waitUntilAllOperationsAreFinished];
		check("queue-resume-runs",
		      [log count] == 1 && [queue operationCount] == 0,
		      [NSString stringWithFormat:@"log=%@", [log entries]]);
	}

	{
		/* SUSPENDED FIRST, SO CANCELLATION IS DETERMINISTIC: nothing has begun. */
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];

		[queue setSuspended:YES];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"x" log:log]];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"y" log:log]];
		[queue cancelAllOperations];
		[queue setSuspended:NO];
		[queue waitUntilAllOperationsAreFinished];
		check("queue-cancel-all",
		      [log count] == 0 && [queue operationCount] == 0,
		      [NSString stringWithFormat:@"ran=%lu remaining=%lu", (unsigned long)[log count],
			(unsigned long)[queue operationCount]]);
	}

	{
		OpLog *log = [[OpLog alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];

		[queue setName:@"probe-queue"];
		[queue addOperation:[[WorkOperation alloc] initWithLabel:@"q" log:log]];
		[queue waitUntilAllOperationsAreFinished];
		check("queue-current-inside-operation",
		      [log seenQueue] == queue && [[queue name] isEqualToString:@"probe-queue"] &&
		      [NSOperationQueue mainQueue] != nil,
		      [NSString stringWithFormat:@"seen=%d name=%@", (int)([log seenQueue] == queue),
			[queue name]]);
	}

	{
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		__block int ran = 0;

		[queue setMaxConcurrentOperationCount:1];
		[queue addOperationWithBlock:^{ ran = 1; }];
		[queue waitUntilAllOperationsAreFinished];
		check("add-operation-with-block-runs-the-block", ran == 1,
		      @"-addOperationWithBlock: runs its block, and the queue's own wait sees it");
	}

	/* ---- A PRODUCER THAT IS NOT THE MAIN THREAD (§58.2e), REPEATED (§58.2p's OPEN ITEM, CLOSED) -----
	 *
	 * THE ELIGIBILITY OF THIS LEG IS THE POINT, NOT THE QUEUE ITSELF: every check above adds operations from
	 * main(), and the stream task adds them from its own WORKER thread - the shape nothing in this tree
	 * exercised. So the question is exactly: does a queue run every operation when the caller is not the main
	 * thread, AND DOES IT THEN GO QUIET?
	 *
	 * THE COUNT IS THE INSTRUMENT (the blocks run on N threads, so line order would prove nothing), the wait is
	 * BOUNDED (a queue that loses an operation never drains, and this probe must report rather than hang), and
	 * the counter is under a lock so a torn increment cannot fake a pass.
	 *
	 * AND ONE BURST IS REPEATED, WHICH IS WHAT MAKES THE SECOND CHECK POSSIBLE. The defect this leg found was
	 * INTERMITTENT: `29 operation(s) still in the queue` on its first run, 0 on the next three (§58.2f), and a
	 * worker given a raw write between its steps never leaked at all (61 of 61). §58.2h then read the
	 * SCHEDULER's own three numbers after a burst, in memory, once - and caught it twice in eight runs as
	 * `running=27 pending=27 operations=27`: workers parked in the completion path, ~half of them. §58.2i's
	 * `FUTEX_WAIT` fix (ARM BEFORE YOU LOOK) measured that state gone, 2 of 8 runs before and 0 of 8 after.
	 *
	 * BUT EIGHT CLEAN RUNS ARE NOT AN ASSERTION, and that is the whole reason for the loop: at the pre-fix rate
	 * (~2 bursts in 8) a clean eight happens ~10% of the time BY LUCK, so the DIAG stayed a DIAG and §58.2p
	 * could only leave the item open. One probe run now performs K bursts and counts the bursts whose
	 * bookkeeping never QUIESCES - one number with a stated chance of being a lucky one rather than eight
	 * readings of another.
	 *
	 * THE WAIT IS FOR QUIESCENCE, NOT A SLEEP, AND THAT IS THE INSTRUMENT'S CORRECTNESS: `ran == wanted` means
	 * the last block's BODY has run, and its worker is still inside `-fnRunOnQueue`'s cleanup at that instant -
	 * so an IMMEDIATE read of a perfectly healthy queue is expected to show `running=1 ops=1`, and §58.2h's
	 * single immediate read could not tell that from a leak. Here a burst counts as leaked only if the three
	 * numbers never reach 0/0/0 within 2s, which a healthy queue does in microseconds. */
	{
		int bursts = 10;
		int wanted = 50;
		int produced = 0;
		int leaked = 0;
		int which = 0;			/* bit i set: burst i never quiesced */
		int b;

		for (b = 0; b < bursts; b++) {
			NSOperationQueue *queue = [[NSOperationQueue alloc] init];
			ProducerThread *producer = [[ProducerThread alloc] init];
			NSLock *lock = [[NSLock alloc] init];
			NSUInteger running = 0, pending = 0, ops = 0;
			int ran = 0;
			int waited = 0;
			int quiet = 0;
			int r;

			producer->queue = queue;
			producer->lock = lock;
			producer->ran = &ran;
			producer->count = wanted;
			[queue setName:@"probe-threaded-queue"];
			[NSThread detachNewThreadSelector:@selector(fnProduce) toTarget:producer withObject:nil];
			while (ran < wanted && waited < 3000) {	/* 30s at 10ms: bounded, never a hang */
				[NSThread sleepForTimeInterval:0.01];
				waited++;
			}
			produced += ran;
			for (r = 0; r < 200; r++) {		/* 2s: a healthy queue is quiet on the first or second read */
				[queue fnSchedulerCountsRunning:&running pending:&pending operations:&ops];
				if (running == 0 && pending == 0 && ops == 0) {
					quiet = 1;
					break;
				}
				[NSThread sleepForTimeInterval:0.01];
			}
			if (!quiet) {
				leaked++;
				which |= 1 << b;
			}
		}
		/* ONE WRITE, AFTER THE MEASUREMENT - §58.2g's lesson is about writes DURING it. */
		printf("FOUNDATION-OPERATION-DIAG threaded producer: %d bursts of %d, ran=%d, leaked bursts=%d (mask=0x%x)\n",
		       bursts, wanted, produced, leaked, which);
		check("every-block-added-from-a-worker-thread-runs", produced == wanted * bursts,
		      [NSString stringWithFormat:@"ran=%d of %d - a queue that ACCEPTS an operation must RUN it",
		       produced, wanted * bursts]);
		/* THE ASSERTION §58.2p LEFT OWED, MADE POSSIBLE BY THE REPETITION RATHER THAN BY HOPE. The claim is
		 * absence, so it is stated as one: every burst drains to `running=0 pending=0 operations=0`, and the
		 * mask in the failure detail names the bursts that did not. */
		check("and-every-burst-leaves-the-queue-empty", leaked == 0,
		      [NSString stringWithFormat:@"%d of %d bursts left the scheduler non-empty (mask=0x%x) - a "
			"worker parked in the completion path", leaked, bursts, which]);
	}

	/* ---- DO DETACHED THREADS RELIABLY ENTER THEIR SELECTOR? (§58.2g) -------------------------------
	 *
	 * THE QUESTION THE WHOLE TRAIL REDUCES TO. §58.2d's delivery loss (14 hops handed to the queue, 7 ever ran),
	 * §58.2f's intermittent leak (29 of 50 operations left in `_operations` once, 0 on the next three runs) and
	 * this session's `wait4` WNOHANG answer all have the same shape: a thread, or a just-created thread's first
	 * step, that never proceeds. So this leg detaches N threads whose selector marks ENTRY and EXIT with two
	 * lock-protected increments and nothing else - the caller prints the totals once, at the end, because a
	 * per-event write is what made the previous instrument lose the very race it was measuring. */
	{
		NSLock *lock = [[NSLock alloc] init];
		ThreadEntryTarget *target = [[ThreadEntryTarget alloc] init];
		int entries = 0;
		int exits = 0;
		int waited = 0;
		int threads = 20;
		int i;

		target->lock = lock;
		target->entries = &entries;
		target->exits = &exits;
		for (i = 0; i < threads; i++) {
			[NSThread detachNewThreadSelector:@selector(fnEnterAndExit) toTarget:target withObject:nil];
		}
		while (exits < threads && waited < 3000) {		/* 30s at 10ms: bounded, never a hang */
			[NSThread sleepForTimeInterval:0.01];
			waited++;
		}
		printf("FOUNDATION-OPERATION-DIAG detached threads: entered=%d exited=%d of %d, waited=%d\n",
		       entries, exits, threads, waited);
		check("every-detached-thread-enters-its-selector", entries == threads,
		      [NSString stringWithFormat:@"%d of %d threads ever entered their selector", entries, threads]);
		check("and-every-detached-thread-finishes", exits == threads,
		      [NSString stringWithFormat:@"%d of %d threads finished their selector", exits, threads]);
	}

	printf("FOUNDATION-OPERATION RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-OPERATION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-OPERATION DONE\n");
	return failc ? 1 : 0;
}
