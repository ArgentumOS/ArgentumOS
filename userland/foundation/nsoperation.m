/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsoperation.m — the operation and the queue that schedules it (F13.19). One file, because the queue
 * reads the operation's state and the operation's state is what the queue acts on.
 *
 * THE SCHEDULER IS ONE LOOP, CALLED FROM TWO PLACES, and both of them matter: an operation is added,
 * and an operation finishes. The second is the one a scheduler can get wrong — finishing is what makes
 * the NEXT operation ready, so a queue that only examined its list when work arrived would stall a
 * dependency graph at its second step. `-fnSchedule` is therefore called on the completion path too,
 * with the same lock held.
 *
 * A CANCELLED OPERATION IS REMOVED RATHER THAN RUN: it is never asked to start, so it cannot be left in
 * the list for `-waitUntilAllOperationsAreFinished` to wait on forever.
 *
 * THE LOCK IS ONE NSCondition PER QUEUE, and `-waitUntilAllOperationsAreFinished` waits on it, so the
 * completion of the last operation IS the signal — there is no polling anywhere in this file.
 */

#import <foundation/NSOperation.h>
#import <foundation/NSOperationQueue.h>
#import <foundation/NSArray.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>
#import <foundation/NSLock.h>
#import <foundation/NSThread.h>
#include <pthread.h>

static pthread_key_t fn_queue_key;
static pthread_once_t fn_queue_key_once = PTHREAD_ONCE_INIT;
static BOOL fn_queue_key_ready = NO;

@interface NSOperationQueue (FNPrivate)
- (void)fnSchedule;
- (void)fnRunOnQueue:(NSOperation *)operation;
@end

static void fn_make_queue_key(void)
{
	if (pthread_key_create(&fn_queue_key, NULL) == 0) {
		fn_queue_key_ready = YES;
	}
}

static void fn_set_current_queue(NSOperationQueue *queue)
{
	pthread_once(&fn_queue_key_once, fn_make_queue_key);
	if (fn_queue_key_ready) {
		pthread_setspecific(fn_queue_key, (void *)queue);
	}
}

@implementation NSOperation

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_dependencies = [[NSMutableArray alloc] init];
	_condition = [[NSCondition alloc] init];
	return self;
}

- (void)main
{
	[NSException raise:NSInvalidArgumentException
		    format:@"NSOperation: -main was not overridden by %@ — a unit of work that does "
			   "nothing is a failure a caller cannot see", [self class]];
}

- (void)start
{
	[_condition lock];
	if (_started || _finished) {
		[_condition unlock];
		return;
	}
	if (_cancelled) {
		/* CANCELLED IS FINISHED: the operation will not run, and anyone waiting on it must stop
		 * waiting. */
		_finished = YES;
		[_condition broadcast];
		[_condition unlock];
		return;
	}
	_started = YES;
	_executing = YES;
	[_condition unlock];

	[self main];

	[_condition lock];
	_executing = NO;
	_finished = YES;
	[_condition broadcast];
	[_condition unlock];
}

- (void)cancel
{
	[_condition lock];
	_cancelled = YES;
	[_condition broadcast];
	[_condition unlock];
}

- (BOOL)isCancelled
{
	return _cancelled;
}

- (BOOL)isExecuting
{
	return _executing;
}

- (BOOL)isFinished
{
	return _finished;
}

- (BOOL)isReady
{
	NSUInteger i;

	for (i = 0; i < [_dependencies count]; i++) {
		if (![(NSOperation *)[_dependencies objectAtIndex:i] isFinished]) {
			return NO;
		}
	}
	return YES;
}

- (void)addDependency:(NSOperation *)operation
{
	if (operation == nil || operation == self) {
		return;
	}
	[_condition lock];
	[_dependencies addObject:operation];
	[_condition unlock];
}

- (void)removeDependency:(NSOperation *)operation
{
	[_condition lock];
	[_dependencies removeObjectIdenticalTo:operation];
	[_condition unlock];
}

- (NSArray *)dependencies
{
	return [_dependencies copy];
}

- (void)waitUntilFinished
{
	[_condition lock];
	while (!_finished) {
		[_condition wait];
	}
	[_condition unlock];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@%@%@>", [self class], self,
				  _executing ? @" executing" : @"",
				  _finished ? @" finished" : @"",
				  _cancelled ? @" cancelled" : @""];
}

@end

@implementation NSOperationQueue

+ (NSOperationQueue *)mainQueue
{
	static NSOperationQueue *queue = nil;

	if (queue == nil) {
		queue = [[self alloc] init];
		[queue setName:@"NSOperationQueue.mainQueue"];
		/* SERIAL, like Cocoa's — but its operations run on WORKER THREADS here, because this
		 * library's run loop has timers and no sources. The header says so. */
		[queue setMaxConcurrentOperationCount:1];
	}
	return queue;
}

+ (nullable NSOperationQueue *)currentQueue
{
	pthread_once(&fn_queue_key_once, fn_make_queue_key);
	return fn_queue_key_ready ? (NSOperationQueue *)pthread_getspecific(fn_queue_key) : nil;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_operations = [[NSMutableArray alloc] init];
	_condition = [[NSCondition alloc] init];
	/* NEGATIVE MEANS UNLIMITED, which is Cocoa's spelling of "no limit" and therefore the default. */
	_maxConcurrent = -1;
	return self;
}

- (void)addOperation:(NSOperation *)operation
{
	if (operation == nil) {
		return;
	}
	[_condition lock];
	[_operations addObject:operation];
	[self fnSchedule];
	[_condition unlock];
}

/* CALLED WITH THE LOCK HELD: start what is ready, while there is room and something to start. */
- (void)fnSchedule
{
	for (;;) {
		NSOperation *next = nil;
		NSUInteger i;

		if (_suspended) {
			return;
		}
		if (_maxConcurrent >= 0 && _running >= (NSUInteger)_maxConcurrent) {
			return;
		}
		for (i = 0; i < [_operations count]; i++) {
			NSOperation *candidate = [_operations objectAtIndex:i];

			if ([candidate isCancelled]) {
				/* REMOVED RATHER THAN STARTED, so that a queue waiting for everything to finish
				 * is not waiting on work that will never run. */
				[_operations removeObjectAtIndex:i];
				[_condition broadcast];
				i--;
				continue;
			}
			if (![candidate isExecuting] && ![candidate isFinished] && [candidate isReady]) {
				next = candidate;
				break;
			}
		}
		if (next == nil) {
			return;
		}
		_running++;
		[NSThread detachNewThreadSelector:@selector(fnRunOnQueue:) toTarget:self withObject:next];
	}
}

- (void)fnRunOnQueue:(NSOperation *)operation
{
	fn_set_current_queue(self);
	[operation start];
	[_condition lock];
	_running--;
	[_operations removeObjectIdenticalTo:operation];
	[_condition broadcast];
	/* THE COMPLETION PATH RE-SCHEDULES: finishing is what makes the next one ready. */
	[self fnSchedule];
	[_condition unlock];
}

- (NSArray *)operations
{
	NSArray *snapshot;

	[_condition lock];
	snapshot = [_operations copy];
	[_condition unlock];
	return snapshot;
}

- (NSUInteger)operationCount
{
	NSUInteger count;

	[_condition lock];
	count = [_operations count];
	[_condition unlock];
	return count;
}

- (NSInteger)maxConcurrentOperationCount
{
	return _maxConcurrent;
}

- (void)setMaxConcurrentOperationCount:(NSInteger)count
{
	if (count == 0) {
		count = 1;		/* a queue that can run nothing would never finish anything */
	}
	[_condition lock];
	_maxConcurrent = count;
	[self fnSchedule];
	[_condition unlock];
}

- (void)cancelAllOperations
{
	NSUInteger i;

	[_condition lock];
	for (i = 0; i < [_operations count]; i++) {
		[(NSOperation *)[_operations objectAtIndex:i] cancel];
	}
	[self fnSchedule];
	[_condition unlock];
}

- (void)waitUntilAllOperationsAreFinished
{
	[_condition lock];
	while ([_operations count] > 0) {
		[_condition wait];
	}
	[_condition unlock];
}

- (BOOL)isSuspended
{
	return _suspended;
}

- (void)setSuspended:(BOOL)suspended
{
	[_condition lock];
	_suspended = suspended;
	if (!suspended) {
		[self fnSchedule];
	}
	[_condition unlock];
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	_name = name;
}

@end
