/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDiscardableContent — the protocol a class adopts when ITS CONTENT CAN BE THROWN AWAY AND BUILT AGAIN.
 * docs/design/foundation-plan.md §12.3 W13c.
 *
 * THE FOUR MEMBERS ARE ONE CONVERSATION ABOUT ONE PIECE OF MEMORY, and the interesting half is the ACCESS
 * COUNT rather than the discarding:
 *
 *   * `-beginContentAccess` says "I am about to read this" AND answers whether there is anything to read. A
 *     FALSE answer means THE CONTENT WAS DISCARDED — the caller is being told to build it again, not that the
 *     call failed, which is why the method both reports and reserves;
 *   * `-endContentAccess` is the matching release of that reservation;
 *   * `-discardContentIfPossible` is the OWNER's side of it — a cache freeing memory — and it must do NOTHING
 *     while an access is outstanding, which is the whole point of the count;
 *   * `-isContentDiscarded` asks whether it is gone right now.
 *
 * SO THE CONTRACT IS A HANDSHAKE, not a set of independent calls, and a class that answers
 * `-discardContentIfPossible` without honouring `-beginContentAccess` has implemented neither.
 *
 * THE COUNT IS NOT REFERENCE COUNTING and must not be confused with it: it counts ACCESSES, not owners, and a
 * content with many owners and no access is still discardable.
 */

#ifndef FOUNDATION_NSDISCARDABLECONTENT_H
#define FOUNDATION_NSDISCARDABLECONTENT_H

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@protocol NSDiscardableContent <NSObject>

/* RESERVE THE CONTENT AND REPORT WHETHER THERE IS ANY: YES means the content is there and will stay there
 * until the matching `-endContentAccess`; NO means it was discarded and the caller should recreate it. */
- (BOOL)beginContentAccess;

/* Release the reservation. Every YES from `-beginContentAccess` owes exactly one of these. */
- (void)endContentAccess;

/* Discard the content IF NOBODY IS ACCESSING IT. The owner calls this; a caller mid-access is what the
 * access count protects. */
- (void)discardContentIfPossible;

/* Is the content gone right now? */
- (BOOL)isContentDiscarded;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDISCARDABLECONTENT_H */
