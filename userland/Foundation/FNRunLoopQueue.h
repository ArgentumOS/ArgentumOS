/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNRunLoopQueue.h — THE RUN LOOP'S SEAM FOR THE NOTIFICATION QUEUE (2026-09-26, plan §62.61).
 *
 * INTERNAL: this is not Apple's API and it is NOT in Foundation.h. It exists because NSNotificationQueue's two
 * queued posting styles are DEFINED as points in a run loop — "as soon as possible" is "when the current callout
 * completes" and "when idle" is "when the loop is about to wait" — and NSNotification.h recorded that this tree's
 * run loop exposed no such point, which is why the class was absent while its vocabulary shipped. These are the
 * three questions the LOOP can answer and the QUEUE cannot, so the loop asks them:
 *
 *   * `-fnPostPendingWhenIdle:mode:` — the loop has reached a phase; post what belongs to it.
 *   * `-fnHasPendingWorkForMode:`    — is there work the loop would otherwise not know it has? A queued
 *     notification IS work: without this, `-runUntilDate:` would see no live timer and no live source, decide the
 *     loop had nothing to do, and return without ever reaching the phase that would post it.
 *
 * ALL THREE ARE PER-THREAD QUESTIONS ABOUT THE CALLING THREAD, because that is what a run loop and a default
 * notification queue both are.
 */

#ifndef FOUNDATION_FNRUNLOOPQUEUE_H
#define FOUNDATION_FNRUNLOOPQUEUE_H

#import <Foundation/NSObject.h>
/* THE CLASS ITSELF, because this header declares a CATEGORY on it: a category without its class's interface is
 * "cannot find interface declaration", which is exactly what the first build of this seam said. */
#import <Foundation/NSNotificationQueue.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSNotificationQueue (FNRunLoopSeam)

/* `idle` says WHICH PHASE the loop has reached: NO is the end of a callout (so NSPostASAP notifications are due)
 * and YES is "about to wait" (so NSPostWhenIdle ones are). Everything this thread has queued for that phase, and
 * whose modes include `mode`, is posted through its center — in FIFO order, and outside this lock. */
+ (void)fnPostPendingWhenIdle:(BOOL)idle mode:(NSString *)mode;

/* Whether this thread has anything queued that `mode` could carry. */
+ (BOOL)fnHasPendingWorkForMode:(NSString *)mode;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNRUNLOOPQUEUE_H */
