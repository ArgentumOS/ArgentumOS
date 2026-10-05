/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBlockOperation — an operation made of BLOCKS rather than of an overridden `-main`. §62.22 of
 * docs/design/foundation-plan.md, and the first half of the App Support / Operations family.
 *
 * APPLE'S OWN SENTENCE IS THE CONTRACT, AND IT IS ABOUT FINISHING RATHER THAN ABOUT ORDERING: "when executing more
 * than one block, the operation itself is considered finished only when all blocks have finished executing". Every
 * block added is a unit of the SAME operation, so a caller that waits on the operation has waited for all of them,
 * and a queue that reads `-isFinished` sees the truth. That is the property this class exists to give, and it is
 * what the probe asserts.
 *
 * THE ONE DEVIATION, AND IT IS A CONSEQUENCE OF THIS LIBRARY'S OWN RECORDED BOUNDARY: Apple dispatches the blocks
 * CONCURRENTLY onto a work queue, and `NSOperation.h` here says outright that the only kind of operation this
 * library has is NON-CONCURRENT ("for a NON-concurrent operation, which is the only kind here, this runs `-main` to
 * completion before returning"). So the blocks run ONE AFTER ANOTHER, inside `-main`, on the caller's thread — and
 * the part of Apple's description that survives is the FINISHING rule above, while the part that does not is the
 * "concurrent" of its abstract. Nothing about Argentum prevents a work queue; this is the family's existing
 * boundary showing through, and it is stated here rather than left for a caller to discover.
 *
 * CANCELLATION IS COOPERATIVE AND IS MADE GRANULAR HERE: `NSOperation.h` records that a cancelled operation is one
 * that will not run (its `-start` marks it finished), and a block operation that is cancelled WHILE RUNNING has
 * blocks it has not reached yet. This class checks `-isCancelled` BEFORE EACH BLOCK and skips the rest — a choice
 * Apple publishes no rule for, stated where it happens, and the one that makes `-cancel` mean something in the
 * middle of a chain. A block that is already running is never interrupted; no thread is killed here.
 *
 * THE BLOCKS IN THE ARRAY ARE COPIES (Apple: "the blocks in this array are copies of those originally added"), so
 * a caller that adds a block built over a stack variable keeps working after that variable's frame is gone.
 */

#ifndef FOUNDATION_NSBLOCKOPERATION_H
#define FOUNDATION_NSBLOCKOPERATION_H

#import <Foundation/NSOperation.h>

@class NSArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSBlockOperation : NSOperation
{
	NSMutableArray *_blocks;
}

/* A NEW OPERATION WITH ONE BLOCK ALREADY ADDED — Apple's convenience, and the usual way this class is built. */
+ (instancetype)blockOperationWithBlock:(void (^)(void))block;

/* ADDS A UNIT OF WORK. APPLE'S DOCUMENTED REFUSAL: "calling this method while the receiver is executing or has
 * already finished causes an `NSInvalidArgumentException` exception to be thrown" — because blocks added after the
 * run started would either be ignored (a silent loss) or change what "finished" meant after someone had waited. */
- (void)addExecutionBlock:(void (^)(void))block;

/* THE BLOCKS, as an immutable snapshot in the order they were added — and, because they are copies, safe to hold. */
@property (readonly, copy) NSArray *executionBlocks;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBLOCKOPERATION_H */
