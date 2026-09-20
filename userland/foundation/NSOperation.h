/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOperation — a unit of work with STATE. F13.19, docs/design/foundation-plan.md §10.
 *
 * WHAT MAKES IT AN OBJECT RATHER THAN A FUNCTION CALL: it is executing, or finished, or cancelled, or
 * waiting for something else to finish first — and those are questions a caller asks, not flags a
 * caller maintains. `-isReady` is the one with the most content: an operation is ready when every
 * operation it DEPENDS ON has finished, which is how a graph of work is expressed without a scheduler
 * of your own.
 *
 * `-main` IS THE SUBCLASS HOOK, and the base class RAISES rather than doing nothing: a subclass that
 * forgot to override it would otherwise "succeed" at doing nothing at all, which is the failure a
 * caller cannot see.
 *
 * WHAT IS NOT HERE, named: `-completionBlock` and `-addOperationWithBlock:` (this library has no
 * blocks in its public headers), `-queuePriority`/`-qualityOfService`, `-asynchronous` operations that
 * manage their own completion, and `NSOperation`'s KVO announcements for its own state — the state is
 * readable, but changing it does not notify (F13.9's registry exists; wiring it here is its own step).
 */

#ifndef FOUNDATION_NSOPERATION_H
#define FOUNDATION_NSOPERATION_H

#import <foundation/NSObject.h>

@class NSArray;
@class NSMutableArray;		/* the IVAR needs the name, and NSArray is not NSMutableArray */
@class NSCondition;

/* THE LEGACY QUALITY-OF-SERVICE SPELLINGS (2026-09-20). Apple declares these as plain
 * macros that ALIAS the modern NSQualityOfService names - the older spelling kept
 * working alongside the newer one, which is the whole reason the family exists - and
 * the whole family is five names, so this is the entire set rather than a selection.
 *
 * A MACRO IS THE API HERE, not an implementation choice: these are #defines by
 * declaration, so a program that spells the old name gets the new one, and our values
 * for NSQualityOfService (§11.6.1 D2) are what they resolve to.
 */
#define NSOperationQualityOfService		NSQualityOfService
#define NSOperationQualityOfServiceUserInteractive	NSQualityOfServiceUserInteractive
#define NSOperationQualityOfServiceUserInitiated	NSQualityOfServiceUserInitiated
#define NSOperationQualityOfServiceUtility		NSQualityOfServiceUtility
#define NSOperationQualityOfServiceBackground		NSQualityOfServiceBackground

NS_ASSUME_NONNULL_BEGIN
@interface NSOperation : NSObject
{
	NSMutableArray *_dependencies;
	NSCondition *_condition;
	BOOL _cancelled;
	BOOL _executing;
	BOOL _finished;
	BOOL _started;
}

/* THE HOOK. The base class raises, on purpose. */
- (void)main;
/* START IT, whichever thread you are on — and for a NON-concurrent operation, which is the only kind
 * here, this runs -main to completion before returning. */
- (void)start;

- (void)cancel;
- (BOOL)isCancelled;
- (BOOL)isExecuting;
- (BOOL)isFinished;
- (BOOL)isReady;

- (void)addDependency:(NSOperation *)operation;
- (void)removeDependency:(NSOperation *)operation;
- (NSArray *)dependencies;
- (void)waitUntilFinished;

- (NSString *)description;

@end

/* HOW URGENT THIS OPERATION IS (2026-09-20). -queuePriority is in this header's
 * refusal list above, so the type is here ahead of its user — as it is in Cocoa's
 * own header. Names from Apple's documentation index; values are ours (§11.6.1 D2,
 * see NSFileManager.h), and what is NOT ours is the ORDER: VeryLow < Low < Normal <
 * High < VeryHigh is the semantic the enum exists to express, and it is preserved. */
typedef enum {
	NSOperationQueuePriorityVeryLow = 0,
	NSOperationQueuePriorityLow = 1,
	NSOperationQueuePriorityNormal = 2,
	NSOperationQueuePriorityHigh = 3,
	NSOperationQueuePriorityVeryHigh = 4
} NSOperationQueuePriority;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSOPERATION_H */
