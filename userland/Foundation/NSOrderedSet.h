/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOrderedSet — a set that REMEMBERS THE ORDER its members arrived in. F13.8e,
 * docs/design/foundation-plan.md §10.
 *
 * THE ONE THING THAT MAKES IT NEITHER AN NSSet NOR AN NSArray: like a set it holds each value once,
 * and like an array the order is part of the VALUE — so two ordered sets with the same members in
 * different orders are NOT equal, and `-objectAtIndex:` is a real question with a stable answer. It
 * is a separate class rather than an NSSet subclass precisely because of that: an ordered set that
 * inherited unordered equality would be lying about what it is.
 *
 * THE MEMBER-NOT-A-KEY RULE, THE SAME AS NSSet's: members are RETAINED (not copied, so a member that
 * cannot be copied is fine) and lookups are LINEAR by -hash/-isEqual:, comparing BY VALUE. The first
 * occurrence of a duplicate is the one that stays, as in Cocoa.
 *
 * WHAT IS NOT HERE, named: the `…range:copyItems:` constructors, `-indexOfObject:inSortedRange:options:usingComparator:`
 * (a binary search), the `…UsingComparator:` and `NSIndexSet`-taking mutation forms, and
 * `-addObjects:count:`. Out-of-range access RAISES NSRangeException, as in Cocoa.
 */

#ifndef FOUNDATION_NSORDEREDSET_H
#define FOUNDATION_NSORDEREDSET_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSOrderedCollectionDifference.h>

@class NSArray<ObjectType>, NSEnumerator<ObjectType>, NSSet<ObjectType>;
@class NSPredicate;

NS_ASSUME_NONNULL_BEGIN

@interface NSOrderedSet<__covariant ObjectType> : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
{
	NSArray *_members;		/* insertion order, each value once; REPLACED by every mutation */
	unsigned long _mutations;	/* the for-in consistency token */
}

+ (instancetype)orderedSet;
+ (instancetype)orderedSetWithObject:(ObjectType)object;
+ (instancetype)orderedSetWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
/* THE NIL-TERMINATED VARIADIC FORM, and the two that take a SOURCE, a RANGE and a COPY flag. Apple declares
 * all of them; this tree shipped the count and whole-source forms only, and the ledger carried the rest as
 * open rows until §63.6. */
+ (instancetype)orderedSetWithObjects:(ObjectType)firstObject, ... NS_REQUIRES_NIL_TERMINATION;
+ (instancetype)orderedSetWithArray:(NSArray<ObjectType> *)array;
+ (instancetype)orderedSetWithArray:(NSArray<ObjectType> *)array
			      range:(NSRange)range
			  copyItems:(BOOL)flag;
+ (instancetype)orderedSetWithOrderedSet:(NSOrderedSet<ObjectType> *)set;
+ (instancetype)orderedSetWithOrderedSet:(NSOrderedSet<ObjectType> *)set
				   range:(NSRange)range
			       copyItems:(BOOL)flag;
+ (instancetype)orderedSetWithSet:(NSSet<ObjectType> *)set;
+ (instancetype)orderedSetWithSet:(NSSet<ObjectType> *)set copyItems:(BOOL)flag;

- (instancetype)initWithObject:(ObjectType)object;
- (instancetype)initWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (instancetype)initWithObjects:(ObjectType)firstObject, ... NS_REQUIRES_NIL_TERMINATION;
- (instancetype)initWithArray:(NSArray<ObjectType> *)array;
- (instancetype)initWithArray:(NSArray<ObjectType> *)array copyItems:(BOOL)flag;
- (instancetype)initWithArray:(NSArray<ObjectType> *)array range:(NSRange)range copyItems:(BOOL)flag;
- (instancetype)initWithOrderedSet:(NSOrderedSet<ObjectType> *)set;
- (instancetype)initWithOrderedSet:(NSOrderedSet<ObjectType> *)set copyItems:(BOOL)flag;
- (instancetype)initWithOrderedSet:(NSOrderedSet<ObjectType> *)set
			     range:(NSRange)range
			 copyItems:(BOOL)flag;
- (instancetype)initWithSet:(NSSet<ObjectType> *)set;
- (instancetype)initWithSet:(NSSet<ObjectType> *)set copyItems:(BOOL)flag;

- (NSUInteger)count;
- (nullable ObjectType)objectAtIndex:(NSUInteger)index;
- (nullable ObjectType)objectAtIndexedSubscript:(NSUInteger)index;
- (NSUInteger)indexOfObject:(ObjectType)object;
- (BOOL)containsObject:(ObjectType)object;
- (nullable ObjectType)firstObject;
- (nullable ObjectType)lastObject;

/* The two views: the ORDER (an array) and the MEMBERSHIP (a set). */
- (NSArray<ObjectType> *)array;
- (NSSet<ObjectType> *)set;

- (NSEnumerator<ObjectType> *)objectEnumerator;
- (NSEnumerator<ObjectType> *)reverseObjectEnumerator;
- (void)enumerateObjectsUsingBlock:(void (^)(ObjectType object, BOOL *stop))block;
/* THE HOUSE'S OWN SPELLING for an out-parameter C array — the same one NSArray.h and NSDictionary.h
 * use, because a bare `__unsafe_unretained *` inside an NS_ASSUME_NONNULL region is a
 * nullability-completeness error rather than a warning. */
- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)objects range:(NSRange)range;

- (BOOL)isEqualToOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (BOOL)intersectsOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (BOOL)isSubsetOfOrderedSet:(NSOrderedSet<ObjectType> *)other;

- (instancetype)filteredOrderedSetUsingPredicate:(NSPredicate *)predicate;
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

/* ---- THE DIFFERENCE DOORS (2026-09-26), the NSOrderedSet spelling of NSArray's four ------------------
 *
 * A CATEGORY FOR THE SAME MECHANICAL REASON AS NSArray's: they are implemented beside the differ, in
 * NSOrderedCollectionDifference.m, and a method in a class's own @interface implemented in another translation
 * unit warns `-Wincomplete-implementation` in the class's own file. The call is unchanged for a caller.
 *
 * The direction is the same as the array's: the receiver is the DESTINATION, so
 * `[b orderedSetByApplyingDifference:[a differenceFromOrderedSet:b]]` answers `a`. The
 * `NSOrderedCollectionDifferenceCalculationOptions` the middle two take is declared in
 * NSOrderedCollectionDifference.h, which this header imports — Apple files it under BOTH classes' pages. */
@interface NSOrderedSet<ObjectType> (NSOrderedCollectionDifferenceAdditions)

- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet<ObjectType> *)other
						withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options;
- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet<ObjectType> *)other
						withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
					usingEquivalenceTest:(BOOL (^)(id obj1, id obj2))block;
/* "Creates a new ordered set by applying a difference object to an existing ordered set." The receiver is the
 * SOURCE. */
- (NSOrderedSet<ObjectType> *)orderedSetByApplyingDifference:(NSOrderedCollectionDifference *)difference;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSORDEREDSET_H */
