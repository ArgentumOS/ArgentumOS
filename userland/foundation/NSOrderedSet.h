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

#import <foundation/NSObject.h>
#import <foundation/NSFastEnumeration.h>

@class NSArray;
@class NSEnumerator;
@class NSSet;
@class NSPredicate;

NS_ASSUME_NONNULL_BEGIN

@interface NSOrderedSet : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
{
	NSArray *_members;		/* insertion order, each value once; REPLACED by every mutation */
	unsigned long _mutations;	/* the for-in consistency token */
}

+ (instancetype)orderedSet;
+ (instancetype)orderedSetWithObject:(id)object;
+ (instancetype)orderedSetWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count;
+ (instancetype)orderedSetWithArray:(NSArray *)array;
+ (instancetype)orderedSetWithOrderedSet:(NSOrderedSet *)set;

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (instancetype)initWithArray:(NSArray *)array;
- (instancetype)initWithOrderedSet:(NSOrderedSet *)set;

- (NSUInteger)count;
- (nullable id)objectAtIndex:(NSUInteger)index;
- (nullable id)objectAtIndexedSubscript:(NSUInteger)index;
- (NSUInteger)indexOfObject:(id)object;
- (BOOL)containsObject:(id)object;
- (nullable id)firstObject;
- (nullable id)lastObject;

/* The two views: the ORDER (an array) and the MEMBERSHIP (a set). */
- (NSArray *)array;
- (NSSet *)set;

- (NSEnumerator *)objectEnumerator;
- (NSEnumerator *)reverseObjectEnumerator;
- (void)enumerateObjectsUsingBlock:(void (^)(id object, BOOL *stop))block;
/* THE HOUSE'S OWN SPELLING for an out-parameter C array — the same one NSArray.h and NSDictionary.h
 * use, because a bare `__unsafe_unretained *` inside an NS_ASSUME_NONNULL region is a
 * nullability-completeness error rather than a warning. */
- (void)getObjects:(id __unsafe_unretained _Nonnull * _Nonnull)objects range:(NSRange)range;

- (BOOL)isEqualToOrderedSet:(NSOrderedSet *)other;
- (BOOL)intersectsOrderedSet:(NSOrderedSet *)other;
- (BOOL)isSubsetOfOrderedSet:(NSOrderedSet *)other;

- (instancetype)filteredOrderedSetUsingPredicate:(NSPredicate *)predicate;
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSORDEREDSET_H */
