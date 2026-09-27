/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSNotificationQueue — the notification family's BUFFER (2026-09-26, plan §62.61).
 *
 * WHY THIS CLASS NEEDED A RUN LOOP CHANGE RATHER THAN ONLY A FILE, which is the honest headline:
 * NSNotification.h had recorded, in as many words, that the styles and the coalescing policies ship and the
 * QUEUE waits — "a queue delivers a notification at a point in the RUN LOOP — idle, or as soon as possible
 * without blocking — and this tree's run loop exposes no phase seam to hang that on yet". §12.6's rule is that a
 * dependency we lack is ADDED rather than refused, so this unit adds the seam (see FNRunLoopQueue.h) and the
 * class stands on it. The three posting styles are therefore all REAL:
 *
 *   NSPostNow      posted synchronously, BEFORE this call returns — after coalescing, which is the one thing it
 *                  shares with the queued styles and the reason it is not just -postNotification:;
 *   NSPostASAP     posted when the CURRENT RUN-LOOP CALLOUT completes, which the loop reaches at the end of a pass;
 *   NSPostWhenIdle posted when the loop is about to WAIT, i.e. when nothing else is left to do.
 *
 * AND COALESCING IS WHAT MAKES IT A QUEUE RATHER THAN A LIST. A queue scans for notifications already waiting that
 * match the new one — by NAME, by SENDER, or by BOTH, as the mask says — and DROPS them, so the newest request
 * stands for all of them. A pending notification is one that has NOT been posted yet: a queued one is only
 * coalescable until the phase that would post it arrives. `NSNotificationNoCoalescing` (0) coalesces nothing.
 *
 * THE MODES LIST GATES DELIVERY, not enqueuing: a notification enqueued `forModes:` is posted only while the loop
 * is running in one of those modes, and a nil or empty list means `NSDefaultRunLoopMode`.
 *
 * WHAT IS OURS AND SAYS SO (§11.6.1 D2): the SENDER comparison in a coalescing match is POINTER IDENTITY
 * ("the same object", which is what a sender is) while the NAME comparison is `-isEqual:` (a name is a string);
 * Apple documents that a match "matches" and not how, and the two spellings are the only ones that make sense
 * for the two kinds of value.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSNotification.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray, NSNotificationCenter, NSString;

/* "A queue for notification centers", with the two features a bare center does not have: ASYNCHRONOUS posting and
 * COALESCING. Every thread has a default queue bound to the default center, and a caller may make its own. */
@interface NSNotificationQueue : NSObject
{
@protected
	NSNotificationCenter *_center;	/* retained: the queue posts through it */
	id _pending;			/* NSMutableArray of FNQueuedPost, in FIFO order */
	id _lock;			/* NSLock: enqueueing happens on any thread, the run loop flushes on one */
}

/* "The default notification queue for the current thread", bound to `+[NSNotificationCenter defaultCenter]`.
 * PER THREAD, like the default center's own state, and reachable from any thread — the run loop that posts from
 * it is the one that thread runs. */
+ (NSNotificationQueue *)defaultQueue;

/* Apple's designated initializer: the queue posts through the center it is given. */
- (instancetype)initWithNotificationCenter:(NSNotificationCenter *)notificationCenter;

/* THE CONVENIENCE FORM, and Apple's own words for what it means: it "coalesces only notifications that match
 * both the notification's name and object", in `NSDefaultRunLoopMode`. */
- (void)enqueueNotification:(NSNotification *)notification
	       postingStyle:(NSPostingStyle)postingStyle;

/* THE DESIGNATED FORM: when to post, what to coalesce against, and which run-loop modes may carry it. A nil
 * `modes` means `NSDefaultRunLoopMode`. */
- (void)enqueueNotification:(NSNotification *)notification
	       postingStyle:(NSPostingStyle)postingStyle
	       coalesceMask:(NSNotificationCoalescing)coalesceMask
		   forModes:(nullable NSArray *)modes;

/* REMOVE WHAT IS STILL WAITING. Every pending notification matching by the mask is dropped and is never posted;
 * Apple: when the mask names BOTH name and sender, a notification must match on BOTH. */
- (void)dequeueNotificationsMatching:(NSNotification *)notification
			coalesceMask:(NSUInteger)coalesceMask;

@end

NS_ASSUME_NONNULL_END
