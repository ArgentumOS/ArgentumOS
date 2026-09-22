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
 *                               so "none of them ran" is a fact rather than a race.
 *
 * AND ONE CHECK IS ABOUT A REFUSAL THAT MUST BE LOUD: `-main` is a hook, and the base class RAISES,
 * because a subclass that forgot to override it would otherwise succeed at doing nothing.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

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

	printf("FOUNDATION-OPERATION RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-OPERATION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-OPERATION DONE\n");
	return failc ? 1 : 0;
}
