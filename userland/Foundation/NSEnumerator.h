/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSEnumerator — a cursor over a collection.
 * docs/design/foundation-plan.md, the dependency queue.
 *
 * A SNAPSHOT, and that is a decision rather than an accident. The enumerator
 * holds its own copy of the sequence, so mutating the collection it came from
 * cannot invalidate the walk — and nothing in this class has to detect a
 * mutation, which is why its fast-enumeration mutations pointer can be its own
 * (never-changing) counter. Cocoa's cursor is live and raises on a concurrent
 * modification; this one answers the collection as it was when the enumerator
 * was made, which is stated here because it is observable.
 *
 * It exists because NSArray and NSDictionary are specified in terms of it
 * (-objectEnumerator, -reverseObjectEnumerator, -keyEnumerator).
 */

#ifndef FOUNDATION_NSENUMERATOR_H
#define FOUNDATION_NSENUMERATOR_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>

@class NSArray;

/* NULLABILITY (F6): NONNULL by default, and the ONE exception is -nextObject,
 * which answers nil once the cursor is exhausted. */
NS_ASSUME_NONNULL_BEGIN

@interface NSEnumerator : NSObject <NSFastEnumeration>
{
	NSArray *_sequence;		/* the snapshot this cursor walks */
	unsigned long _index;
	unsigned long _mutations;	/* a stable address for fast enumeration */
	BOOL _reverse;
}

- (nullable id)nextObject;
- (NSArray *)allObjects;		/* what is LEFT, and the cursor is exhausted */

/* Ours, not Cocoa's: the collections are the ones that construct enumerators. */
- (id)initWithSequence:(NSArray *)sequence reverse:(BOOL)reverse;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSENUMERATOR_H */
