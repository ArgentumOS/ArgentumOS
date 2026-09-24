/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOperation.m — the operation and the queue that schedules it (F13.19). One file, because the queue
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

#import <Foundation/NSOperation.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSArray.h>
#include <Block.h>	/* Block_copy/Block_release: a block is not owned with a message */
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSThread.h>
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

/* THE BLOCK DOOR'S OPERATION, PRIVATE TO THIS FILE. THE BLOCK IS Block_copy'd AND Block_release'd, NOT
 * SENT -copy/-release: -copy is a message send, the runtime reads the block's isa, and that is where the URL
 * session's crash lived. tools/foundation-gate.py refuses the message form for a block-typed name. */
@interface FNBlockOperation : NSOperation
{
	void (^_block)(void);
}
- (instancetype)initWithBlock:(void (^)(void))block;
@end

@implementation FNBlockOperation

- (instancetype)initWithBlock:(void (^)(void))block
{
	self = [super init];
	if (self != nil) {
		_block = Block_copy(block);
	}
	return self;
}

- (void)main
{
	if (_block != NULL) {
		_block();
	}
}

- (void)dealloc
{
	if (_block != NULL) {
		Block_release(_block);
	}
	[super dealloc];
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
	_pending = [[NSMutableArray alloc] init];
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
				/* A PENDING OPERATION IS ITS WORKER'S TO REMOVE: dropping it from _operations here
				 * releases this array's reference while that worker is about to send it `-start`,
				 * which is the freed-operation fault this set exists to prevent. */
				if ([_pending indexOfObjectIdenticalTo:candidate] != NSNotFound) {
					continue;
				}
				/* REMOVED RATHER THAN STARTED, so that a queue waiting for everything to finish
				 * is not waiting on work that will never run. */
				[_operations removeObjectAtIndex:i];
				[_condition broadcast];
				i--;
				continue;
			}
			/* AND NOT ALREADY HANDED TO A WORKER: see _pending. Without this test the same
			 * operation is dispatched once per pass of this loop, because nothing has started it
			 * yet on the worker side. */
			if (![candidate isExecuting] && ![candidate isFinished] && [candidate isReady] &&
			    [_pending indexOfObjectIdenticalTo:candidate] == NSNotFound) {
				next = candidate;
				break;
			}
		}
		if (next == nil) {
			return;
		}
		_running++;
		/* BEFORE THE DETACH, NOT AFTER: the worker marks the operation started only when its
		 * thread runs, so the next iteration of this loop - and the next caller - must not still
		 * see it as ready. */
		[_pending addObject:next];
		[NSThread detachNewThreadSelector:@selector(fnRunOnQueue:) toTarget:self withObject:next];
	}
}

- (void)fnRunOnQueue:(NSOperation *)operation
{
	fn_set_current_queue(self);
	[operation start];
	[_condition lock];
	_running--;
	[_pending removeObjectIdenticalTo:operation];
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

/* TEMPORARY INSTRUMENT (foundation-plan.md §58.2h), AND IT IS DELIBERATELY SILENT: it reads what the SCHEDULER
 * BELIEVES - `_running`, `_pending`, `_operations` - in ONE lock scope and returns the three numbers; the caller
 * prints them once, after a burst. §58.2g's lesson applied to this class: an instrument that writes per event
 * changes the thing it is measuring, and this one never writes at all.
 *
 * IT IS NOT DECLARED IN THE PRIVATE CATEGORY ON PURPOSE: a declaration here is a declaration the probe cannot see,
 * and the probe declares the same selector on its side instead - Objective-C resolves it at runtime, so the two
 * sides only have to agree on the name. */
- (void)fnSchedulerCountsRunning:(NSUInteger *)running pending:(NSUInteger *)pending operations:(NSUInteger *)ops
{
	[_condition lock];
	if (running != NULL) {
		*running = _running;
	}
	if (pending != NULL) {
		*pending = [_pending count];
	}
	if (ops != NULL) {
		*ops = [_operations count];
	}
	[_condition unlock];
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


- (void)addOperationWithBlock:(void (^)(void))block
{
	FNBlockOperation *operation;

	if (block == NULL) {
		return;
	}
	operation = [[FNBlockOperation alloc] initWithBlock:block];
	[self addOperation:operation];
	[operation release];
}

@end
