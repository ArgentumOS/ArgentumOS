/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSIndexSet — an immutable set of unique indexes.
 * docs/design/foundation-plan.md, the dependency queue.
 *
 * Stored as a RANGE LIST, the same representation NSCharacterSet uses, because it
 * is the same shape of thing: a sorted set of unsigned positions. A set built from
 * a range costs one entry however wide the range is, which is the whole reason
 * -indexSetWithIndexesInRange: is cheap in Cocoa and would not be in a list of
 * individual indexes.
 *
 * The canonical order is maintained on mutation: the ranges are sorted and
 * COALESCED, so -count cannot double-count a value added twice.
 *
 * It exists because four NSArray methods are specified in terms of it.
 */

#ifndef FOUNDATION_NSINDEXSET_H
#define FOUNDATION_NSINDEXSET_H

#import <Foundation/NSObject.h>

/* NULLABILITY (F6): NONNULL throughout — every accessor here answers a number, a
 * BOOL or a sentinel, never an object that could be absent. */
NS_ASSUME_NONNULL_BEGIN

@interface NSIndexSet : NSObject <NSCopying>
{
	unsigned long *_ranges;		/* pairs of (location, length) */
	unsigned long _rangeCount;
	unsigned long _capacity;
}

+ (instancetype)indexSet;
+ (instancetype)indexSetWithIndex:(NSUInteger)value;
+ (instancetype)indexSetWithIndexesInRange:(NSRange)range;

- (id)initWithIndex:(NSUInteger)value;
- (id)initWithIndexesInRange:(NSRange)range;

- (BOOL)containsIndex:(NSUInteger)value;
- (BOOL)containsIndexesInRange:(NSRange)range;
- (NSUInteger)count;			/* the NUMBER OF INDEXES, not of ranges */
- (NSUInteger)firstIndex;		/* NSNotFound when empty */
- (NSUInteger)lastIndex;
- (NSUInteger)indexGreaterThanIndex:(NSUInteger)value;	/* NSNotFound when none */
- (NSUInteger)indexLessThanIndex:(NSUInteger)value;
- (void)enumerateIndexesUsingBlock:(void (^)(NSUInteger index, BOOL *stop))block;
- (BOOL)isEqualToIndexSet:(NSIndexSet *)other;


/* THE RANGE-BASED QUERIES (D7's kind (D)). This class's representation IS a range list, so a query
 * over a range of indexes is a walk over ranges rather than a bitmask.
 * NOT here, and named rather than forgotten: -getIndexes:maxCount:inIndexRange: (whose in/out
 * indexRange contract I will not guess), the two block-based -enumerateRanges… forms, and
 * -firstIndexInRange:/-lastIndexInRange:, which the probe's list claims and I could not verify as
 * Apple's API - inventing an API is worse than leaving one refused. */
- (NSUInteger)countOfIndexesInRange:(NSRange)range;
- (NSUInteger)indexGreaterThanOrEqualToIndex:(NSUInteger)index;
- (NSUInteger)indexLessThanOrEqualToIndex:(NSUInteger)index;
@end

@interface NSMutableIndexSet : NSIndexSet

+ (NSMutableIndexSet *)indexSet;

- (void)addIndex:(NSUInteger)value;
- (void)addIndexesInRange:(NSRange)range;
- (void)removeIndex:(NSUInteger)value;
- (void)removeIndexesInRange:(NSRange)range;
- (void)removeAllIndexes;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSINDEXSET_H */
