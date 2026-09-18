/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNull — the object that stands for "nothing", so a collection can hold a hole. F13.8c.
 *
 * IT EXISTS BECAUSE THE COLLECTIONS CANNOT HOLD nil: an NSArray, an NSDictionary and an NSSet all
 * treat nil as a terminator or a mistake, so a payload that needs to say "this slot is empty" needs
 * an OBJECT to say it with. That is the whole class, and it is why this is not a curiosity — it is
 * what a decoded JSON document, or a table row with a missing column, actually contains.
 *
 * IT IS A SINGLETON, and the singleton is enforced at ALLOCATION rather than only in +null: asking
 * for a new one answers the same instance, so `+null == +null` and `[[NSNull alloc] init] == +null`.
 * Equality follows from that, and -description is Cocoa's `"<null>"`.
 *
 * WHAT IS NOT HERE, named: NSNull's coder conformance (F13.8's coders) — this is the object and the
 * placeholder, not the archiving.
 */

#ifndef FOUNDATION_NSNULL_H
#define FOUNDATION_NSNULL_H

#import <foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSNull : NSObject <NSCopying>

+ (NSNull *)null;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNULL_H */
