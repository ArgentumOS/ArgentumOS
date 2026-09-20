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
 *
 * WHAT IS NO LONGER ABSENT (§23): -getIndexes:maxCount:inIndexRange: - whose in/out contract turned out
 * to be documented, with a worked example - and the three block-based -enumerateRanges… forms.
 * AND THE TWO THE PROBE'S LIST USED TO CLAIM ARE NOT APPLE'S API AT ALL: -firstIndexInRange: and
 * -lastIndexInRange: appear on no documented NSIndexSet page; the documented neighbours are -firstIndex
 * / -lastIndex and -indexInRange:options:passingTest:. Refusing to invent them was right — the CLAIM was
 * what was wrong, and it has been removed from the probe's list. */
- (NSUInteger)countOfIndexesInRange:(NSRange)range;
- (NSUInteger)indexGreaterThanOrEqualToIndex:(NSUInteger)index;
- (NSUInteger)indexLessThanOrEqualToIndex:(NSUInteger)index;

/* THE OPTIONS THE ENUMERATORS TAKE, WITH APPLE'S OWN PUBLISHED VALUES (1 and 2 — unlike the opaque bit
 * positions elsewhere in this library). NSEnumerationConcurrent is a HINT Apple's page says a caller
 * must not rely on and an implementation may ignore; this one ignores it, so an enumeration is always
 * serial and synchronous, which is what the docs promise for these methods anyway. */
typedef enum {
	NSEnumerationConcurrent = 1,
	NSEnumerationReverse = 2
} NSEnumerationOptions;

/* THE BUFFER FORM AND ITS IN/OUT RANGE: the buffer receives at most `maxCount` of the receiver's indexes
 * that lie in *range, the return value is how many were written, and *range is UPDATED to the indexes
 * NOT COPIED. `range` may be NULL, which means every index. */
- (NSUInteger)getIndexes:(NSUInteger *)indexBuffer
		maxCount:(NSUInteger)bufferSize
	    inIndexRange:(nullable NSRangePointer)range;

/* THE RANGE ENUMERATORS: the block receives each of the receiver's ranges — the INTERSECTION with
 * `range` for the third form — ascending unless NSEnumerationReverse, and may stop by writing YES
 * through `stop`. */
- (void)enumerateRangesUsingBlock:(void (^)(NSRange range, BOOL *stop))block;
- (void)enumerateRangesWithOptions:(NSEnumerationOptions)options
			usingBlock:(void (^)(NSRange range, BOOL *stop))block;
- (void)enumerateRangesInRange:(NSRange)range
		       options:(NSEnumerationOptions)options
		    usingBlock:(void (^)(NSRange range, BOOL *stop))block;
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
