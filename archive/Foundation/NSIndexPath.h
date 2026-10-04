/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSIndexPath — an immutable path of indexes.
 * docs/design/foundation-plan.md, the dependency queue (stage D).
 *
 * A path is an ORDERED sequence of positions, and that order IS its identity:
 * [1, 2] is not [2, 1]. It is stored as one flat array of NSUInteger plus its
 * length, which is what makes -indexAtPosition: O(1) and the two derived forms —
 * -indexPathByAddingIndex: and -indexPathByRemovingLastIndex — copies rather than
 * changes: Cocoa's NSIndexPath is immutable, so a table view can hold one while a
 * model walks its hierarchy.
 *
 * WHERE IT IS USED: a table or collection view addresses a cell by (row, section)
 * or (item, section), and that layer is the toolkit, not the array surface. The
 * four NSArray `…AtIndexes:`/`indexesOf…` methods the plan once paired with this
 * class take an NSIndexSet and landed in stage B — see the plan's work queue.
 *
 * -indexAtPosition: RAISES NSRangeException past the end, matching
 * -objectsAtIndexes: (which raises for an index outside the array) rather than
 * answering NSNotFound: the caller has -length to check against, and a silent
 * sentinel here would hide the bug instead of naming it.
 *
 * Deliberate deviations, stated rather than discovered later:
 *   - -indexPathByRemovingLastIndex on an EMPTY path raises NSRangeException
 *     (Cocoa leaves it undefined; refusing is the house rule) and -compare: with
 *     nil raises NSInvalidArgumentException;
 *   - -description is one line of our own shape ("<NSIndexPath: 2 position(s)
 *     3-1>"), like every other class here — Cocoa's is not a contract;
 *   - -hash is ours (FNV over the positions) and documented as such: Cocoa does
 *     not specify one, and only -isEqual:/-hash agreement matters.
 */

#ifndef FOUNDATION_NSINDEXPATH_H
#define FOUNDATION_NSINDEXPATH_H

#import <Foundation/NSObject.h>

/* NULLABILITY (F6): NONNULL throughout — an index path is a value, and the
 * operations that could not answer (a position past -length, trimming an empty
 * path) RAISE rather than returning an absent thing. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSIndexPath : NSObject <NSCopying, NSCoding>
{
	NSUInteger *_indexes;	/* _length positions, kept in order */
	NSUInteger _length;
}

+ (instancetype)indexPathWithIndex:(NSUInteger)index;
+ (instancetype)indexPathWithIndexes:(const NSUInteger [_Nonnull])indexes
			      length:(NSUInteger)length;

- (id)initWithIndex:(NSUInteger)index;
- (id)initWithIndexes:(const NSUInteger [_Nonnull])indexes length:(NSUInteger)length;

- (NSIndexPath *)indexPathByAddingIndex:(NSUInteger)index;
- (NSIndexPath *)indexPathByRemovingLastIndex;	/* empty path: NSRangeException */

- (NSUInteger)indexAtPosition:(NSUInteger)position;	/* past -length: NSRangeException */
- (NSUInteger)length;
- (void)getIndexes:(NSUInteger *)indexes;		/* all _length positions */
- (void)getIndexes:(NSUInteger *)indexes range:(NSRange)positionRange;

- (NSComparisonResult)compare:(NSIndexPath *)otherObject;	/* nil: NSInvalidArgumentException */


NS_ASSUME_NONNULL_END


@end

#endif /* FOUNDATION_NSINDEXPATH_H */
