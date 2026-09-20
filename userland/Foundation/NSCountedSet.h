/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCountedSet — a set that remembers HOW MANY of each. docs/design/foundation-plan.md §10,
 * F13.8d.
 *
 * THE ONE THING THAT MAKES IT A DIFFERENT COLLECTION: `-count` answers the number of DISTINCT
 * members, while `-countForObject:` answers how many times one of them was added. So adding the same
 * string three times leaves a set of ONE member with a count of three — which is what a tally needs
 * and what a plain NSSet cannot express.
 *
 * IT IS A SUBCLASS OF NSMutableSet, and the counts are a SECOND, INDEX-ALIGNED ARRAY beside the
 * member array. Only the operations that reach the members past the public doors have to be
 * overridden: NSSet's own -addObjectsFromArray:, -unionSet: and -minusSet: all route through
 * -addObject: and -removeObject:, which land here — while -intersectSet:, -filterUsingPredicate: and
 * -removeAllObjects: replace the member array directly and would desynchronise the counts, so those
 * three are overridden rather than inherited.
 *
 * THE ENUMERATION ANSWERS EACH DISTINCT MEMBER ONCE, matching -allObjects. Named because it is the
 * one place a reader might expect the counts to show up, and this is the reading that agrees with
 * -count and -allObjects rather than contradicting them.
 */

#ifndef FOUNDATION_NSCOUNTEDSET_H
#define FOUNDATION_NSCOUNTEDSET_H

#import <Foundation/NSSet.h>

/* FORWARD-DECLARED, because the ivar only needs the name: the implementation imports NSArray.h,
 * where NSMutableArray is declared. Declaring a concrete superclass here would be a lie about what
 * this class needs to expose. */
@class NSMutableArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSCountedSet : NSMutableSet
{
	NSMutableArray *_counts;	/* index-aligned with the inherited member array */
}

+ (instancetype)setWithCapacity:(NSUInteger)capacity;
- (instancetype)initWithCapacity:(NSUInteger)capacity;

- (void)addObject:(id)object;
- (void)removeObject:(id)object;
- (void)removeAllObjects;
- (void)intersectSet:(NSSet *)other;
- (void)filterUsingPredicate:(NSPredicate *)predicate;

- (NSUInteger)countForObject:(id)object;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCOUNTEDSET_H */
