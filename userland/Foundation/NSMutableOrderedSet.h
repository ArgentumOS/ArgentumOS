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

@interface NSMutableOrderedSet : NSOrderedSet

+ (instancetype)orderedSetWithCapacity:(NSUInteger)capacity;
- (instancetype)initWithCapacity:(NSUInteger)capacity;

- (void)addObject:(id)object;
- (void)addObjectsFromArray:(NSArray *)array;
- (void)insertObject:(id)object atIndex:(NSUInteger)index;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(id)object;
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index;

- (void)removeObject:(id)object;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeObjectsInRange:(NSRange)range;
- (void)removeAllObjects;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;

- (void)unionOrderedSet:(NSOrderedSet *)other;
- (void)unionSet:(NSSet *)other;
- (void)minusOrderedSet:(NSOrderedSet *)other;
- (void)minusSet:(NSSet *)other;
- (void)intersectOrderedSet:(NSOrderedSet *)other;
- (void)intersectSet:(NSSet *)other;

- (void)sortUsingDescriptors:(NSArray *)descriptors;
- (void)filterUsingPredicate:(NSPredicate *)predicate;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMUTABLEORDEREDSET_H */
