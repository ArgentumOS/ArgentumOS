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
/* FOR `NSEnumerationOptions` AND THE INDEX SET `-objectsAtIndexes:` TAKES: the option type lives in
 * NSIndexSet.h (with Apple's published values), so the enumeration doors cannot be declared without it.
 * Apple's own header imports the same. */
#import <Foundation/NSIndexSet.h>

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
/* THE FIFTH VIEW: a SUBSET by position, and the REVERSAL — Apple declares the second as a property, and so
 * does this header, whose getter is implemented by hand (as NSAttributedString's `string` is). */
- (NSArray<ObjectType> *)objectsAtIndexes:(NSIndexSet *)indexes;
@property (readonly, copy) NSOrderedSet<ObjectType> *reversedOrderedSet;

- (NSEnumerator<ObjectType> *)objectEnumerator;
- (NSEnumerator<ObjectType> *)reverseObjectEnumerator;
- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)objects range:(NSRange)range;
/* THE ENUMERATION DOORS, AND THE FIRST ONE'S SIGNATURE WAS WRONG UNTIL §63.7. Apple's block takes THREE
 * parameters — the object, its INDEX and the stop flag — and this tree declared two, so a caller written
 * against Apple's header could not compile here. Measured before the change: ZERO callers in the tree used
 * the two-parameter form (the suite's `-enumerateObjectsUsingBlock:` sites are NSSet's, which IS
 * two-parameter in Apple's header, and NSArray's, which was already three), so the correction costs no
 * call site. `NSEnumerationConcurrent` is a hint this library does not take (NSIndexSet.h says why). */
- (void)enumerateObjectsUsingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)options
			 usingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;
- (void)enumerateObjectsAtIndexes:(NSIndexSet *)indexes
			  options:(NSEnumerationOptions)options
		       usingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;

- (BOOL)isEqualToOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (BOOL)intersectsOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (BOOL)isSubsetOfOrderedSet:(NSOrderedSet<ObjectType> *)other;
/* THE SAME TWO QUESTIONS ASKED OF AN NSSet, which is the shape a caller holding one of each needs. Each is one
 * line over the `set` view above — the ordered-ness is not part of either question. */
- (BOOL)intersectsSet:(NSSet<ObjectType> *)set;
- (BOOL)isSubsetOfSet:(NSSet<ObjectType> *)set;

- (instancetype)filteredOrderedSetUsingPredicate:(NSPredicate *)predicate;
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors;
/* THE PREDICATE AND COMPARATOR DOORS (§63.8). The two SORTS DELEGATE to the array view, exactly as
 * `-sortedArrayUsingDescriptors:` above does and for the same reason: the array view IS the same sequence —
 * same members, same order, same indexes — so one implementation cannot drift from itself. The two
 * PREDICATE searches walk this class's own primitives, because the walk is the contract. */
- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSArray<ObjectType> *)sortedArrayUsingComparator:(NSComparator)comparator;
- (NSArray<ObjectType> *)sortedArrayWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator;
/* The BINARY SEARCH BY COMPARATOR, which is the one door whose answer is an INDEX — and an index into this
 * set and into its array view are the same number, so it delegates too. */
- (NSUInteger)indexOfObject:(ObjectType)object
	      inSortedRange:(NSRange)range
		  options:(NSBinarySearchingOptions)options
	      usingComparator:(NSComparator)comparator;

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
