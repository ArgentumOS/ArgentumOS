/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOperationQueue — the thing that decides WHEN. F13.19.
 *
 * A QUEUE RUNS OPERATIONS ON ITS OWN THREADS, one per operation while it is under its limit, and
 * RE-EXAMINES WHAT IS READY every time one finishes — because finishing an operation is what makes the
 * next one ready, and a queue that only looked when work was ADDED would stall a graph at its second
 * step. That re-examination is the whole of the scheduler, and it is why the completion path takes the
 * same lock the addition path does.
 *
 * `-maxConcurrentOperationCount` IS HONOURED AND NEGATIVE MEANS UNLIMITED, which is Cocoa's own
 * spelling of "no limit" — so a program that sets it to a small number gets a serial queue, and in a
 * serial queue the START ORDER IS THE ADDITION ORDER, which is a thing a probe can measure.
 *
 * WHAT IS NOT HERE, named: `NSOperationQueuePriority` ordering (operations run in the order they became
 * ready), `-qualityOfService`, and THE MAIN QUEUE RUNS ON THE MAIN THREAD — this library's run loop has
 * timers and no sources, so `+mainQueue` is a SERIAL queue whose operations run on worker threads like
 * any other. That is a real difference from Cocoa and it is stated rather than implied.
 */

#ifndef FOUNDATION_NSOPERATIONQUEUE_H
#define FOUNDATION_NSOPERATIONQUEUE_H

#import <foundation/NSObject.h>

@class NSArray;
@class NSCondition;
@class NSOperation;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSOperationQueue : NSObject
{
	NSMutableArray *_operations;
	NSCondition *_condition;
	NSInteger _maxConcurrent;
	NSUInteger _running;
	BOOL _suspended;
	NSString *_name;
}

/* THE QUEUE AN OPERATION IS RUNNING IN, from inside it, and the process's serial main queue. */
+ (nullable NSOperationQueue *)currentQueue;
+ (NSOperationQueue *)mainQueue;

- (void)addOperation:(NSOperation *)operation;
- (NSArray *)operations;
- (NSUInteger)operationCount;

- (NSInteger)maxConcurrentOperationCount;
- (void)setMaxConcurrentOperationCount:(NSInteger)count;

- (void)cancelAllOperations;
- (void)waitUntilAllOperationsAreFinished;

- (BOOL)isSuspended;
- (void)setSuspended:(BOOL)suspended;

- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSOPERATIONQUEUE_H */
