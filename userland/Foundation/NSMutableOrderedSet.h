/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMutableOrderedSet — the ordered set you can change. F13.8e.
 *
 * WHAT IS NOT HERE, named: `-moveObjectsAtIndexes:toIndex:`, `-replaceObjectsAtIndexes:withObjects:`,
 * `-replaceObjectsInRange:withObjectsFromArray:`, `-addObjects:count:`, `-setOrderedSet:` and the
 * comparator-taking sorts. Each is an index- or block-taking form whose absence is visible, rather
 * than a half-built version of it.
 */

#ifndef FOUNDATION_NSMUTABLEORDEREDSET_H
#define FOUNDATION_NSMUTABLEORDEREDSET_H

#import <Foundation/NSOrderedSet.h>

@class NSSet;

NS_ASSUME_NONNULL_BEGIN

@interface NSMutableOrderedSet<ObjectType> : NSOrderedSet<ObjectType>

+ (instancetype)orderedSetWithCapacity:(NSUInteger)capacity;
- (instancetype)initWithCapacity:(NSUInteger)capacity;
/* APPLE DECLARES THIS ON THE MUTABLE CLASS TOO, so it is redeclared here rather than left to inheritance: a
 * source-compatible caller reading THIS header must find it, and the ledger's shipped test is a declaration
 * in the owner's own block. The implementation is not inherited either — it lives in this class's own
 * `@implementation`, where `[super initWithCoder:]` reaches the front's and still answers a MUTABLE set,
 * because the class-choosing rule only sends the IMMUTABLE concrete class to the shared empty instance. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

- (void)addObject:(ObjectType)object;
- (void)addObjectsFromArray:(NSArray<ObjectType> *)array;
- (void)insertObject:(ObjectType)object atIndex:(NSUInteger)index;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(ObjectType)object;
- (void)setObject:(ObjectType)object atIndexedSubscript:(NSUInteger)index;

/* THE INDEX-SET AND COUNT MUTATORS (§63.9). Apple declares the position forms in its
 * `NSExtendedMutableOrderedSet` category and the count forms beside the array ones; they are declared here in
 * one block because they share TWO RULES — an index or range outside the receiver RAISES, before anything is
 * touched, and the SET rule holds throughout (a member appears in the result once). */
- (void)addObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (void)insertObjects:(NSArray<ObjectType> *)objects atIndexes:(NSIndexSet *)indexes;
- (void)moveObjectsAtIndexes:(NSIndexSet *)indexes toIndex:(NSUInteger)index;
- (void)setObject:(ObjectType)object atIndex:(NSUInteger)index;
- (void)replaceObjectsInRange:(NSRange)range
		  withObjects:(const ObjectType _Nonnull * _Nullable)objects
			count:(NSUInteger)count;
- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray<ObjectType> *)objects;
- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes;
- (void)removeObjectsInArray:(NSArray<ObjectType> *)array;

- (void)removeObject:(ObjectType)object;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeObjectsInRange:(NSRange)range;
- (void)removeAllObjects;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;

- (void)unionOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (void)unionSet:(NSSet<ObjectType> *)other;
- (void)minusOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (void)minusSet:(NSSet<ObjectType> *)other;
- (void)intersectOrderedSet:(NSOrderedSet<ObjectType> *)other;
- (void)intersectSet:(NSSet<ObjectType> *)other;

- (void)sortUsingDescriptors:(NSArray *)descriptors;
/* THE COMPARATOR SORTS (§63.8), Apple's three: the whole set, the whole set with options, and a RANGE. Each
 * replaces the members through the one private door every mutation uses, so the for-in consistency token
 * moves with the storage exactly as it does for `-addObject:`. */
- (void)sortUsingComparator:(NSComparator)comparator;
- (void)sortWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator;
- (void)sortRange:(NSRange)range options:(NSSortOptions)options usingComparator:(NSComparator)comparator;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMUTABLEORDEREDSET_H */
