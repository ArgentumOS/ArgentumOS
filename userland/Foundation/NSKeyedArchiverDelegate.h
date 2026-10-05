/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyedArchiverDelegate — the five doors an archiver offers while it writes.
 * docs/design/foundation-plan.md §12.3 W9.
 *
 * EVERY MEMBER IS OPTIONAL, and that is the protocol's whole shape rather than a convenience: a delegate is
 * an OBSERVER that may also intervene, and a caller who wants to watch one thing should not have to implement
 * five. So the archiver asks `-respondsToSelector:` before each door, and this header declares them rather
 * than describing a class.
 *
 * TWO OF THE FIVE CAN CHANGE THE ARCHIVE, and that is the difference worth knowing:
 *   * `-archiver:willEncodeObject:` ANSWERS THE OBJECT TO ENCODE — returning a different object substitutes
 *     it, which is how a delegate replaces an instance with its class-cluster twin;
 *   * `-archiver:willReplaceObject:withObject:` is the archiver TELLING the delegate that it is doing that
 *     substitution itself.
 * The other three — `didEncodeObject:`, `willFinish:` and `didFinish:` — are pure observation, which is why
 * the last two are split around the finish: a delegate that wants to add one more entry has a window before
 * the archive is closed and not after.
 */

#ifndef FOUNDATION_NSKEYEDARCHIVERDELEGATE_H
#define FOUNDATION_NSKEYEDARCHIVERDELEGATE_H

#import <Foundation/NSObject.h>

@class NSKeyedArchiver;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@protocol NSKeyedArchiverDelegate <NSObject>

@optional

/* Informs the delegate that `object` is about to be encoded. THE ANSWER IS WHAT GETS ENCODED, so a delegate
 * may substitute an object here — and answering nil writes the empty slot, which is the same thing the
 * archiver does for a nil. */
- (nullable id)archiver:(NSKeyedArchiver *)archiver willEncodeObject:(id)object;

/* Informs the delegate that a given object has been encoded. RETURNED — but the archiver cannot use it: the
 * entry exists by now, so an answer here is a value the delegate computes and the archive ignores. It is
 * declared `nullable id` because Apple's is, and the honest note is that ours never reads it. */
- (nullable id)archiver:(NSKeyedArchiver *)archiver didEncodeObject:(nullable id)object;

/* Informs the delegate that one object is being substituted for another BY THE ARCHIVER (not by the
 * delegate's own substitution above). */
- (void)archiver:(NSKeyedArchiver *)archiver willReplaceObject:(id)object withObject:(id)newObject;

/* The two halves of the end: before the archive is closed, and after. */
- (void)archiverWillFinish:(NSKeyedArchiver *)archiver;
- (void)archiverDidFinish:(NSKeyedArchiver *)archiver;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYEDARCHIVERDELEGATE_H */
