/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSOrderedCollectionDifference / NSOrderedCollectionChange — the implementation, the DIFF ALGORITHM, and the
 * eight doors the two collection classes carry. See the header for the direction, the two index spaces and the
 * choices that are ours (§11.6.1 D2).
 *
 * WHY THE DOORS ARE IMPLEMENTED HERE AS CATEGORIES. `-differenceFromArray:` and its neighbours are declared in
 * NSArray.h and NSOrderedSet.h (they are the classes' own API), and DEFINED here, beside the differ they call.
 * That is this tree's existing shape for a convenience a class carries but does not own: the plist
 * `+arrayWithContentsOfFile:` family lives as categories in NSPropertyListSerialization.m for the same reason,
 * and it keeps the algorithm in one place rather than three.
 */

#import <Foundation/NSOrderedCollectionDifference.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSOrderedSet.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import <Foundation/NSIndexSet.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================================================
 * NSOrderedCollectionChange
 * ==================================================================================================== */

@implementation NSOrderedCollectionChange

+ (instancetype)changeWithObject:(nullable id)anObject
			    type:(NSCollectionChangeType)type
			   index:(NSUInteger)index
{
	return [[[self alloc] initWithObject:anObject type:type index:index] autorelease];
}

+ (instancetype)changeWithObject:(nullable id)anObject
			    type:(NSCollectionChangeType)type
			   index:(NSUInteger)index
		 associatedIndex:(NSUInteger)associatedIndex
{
	return [[[self alloc] initWithObject:anObject type:type index:index
			      associatedIndex:associatedIndex] autorelease];
}

- (instancetype)initWithObject:(nullable id)anObject
			  type:(NSCollectionChangeType)type
			 index:(NSUInteger)index
{
	return [self initWithObject:anObject type:type index:index
		    associatedIndex:(NSUInteger)NSNotFound];
}

- (instancetype)initWithObject:(nullable id)anObject
			  type:(NSCollectionChangeType)type
			 index:(NSUInteger)index
	       associatedIndex:(NSUInteger)associatedIndex
{
	self = [super init];
	if (self != nil) {
		_object = [anObject retain];
		_changeType = type;
		_index = index;
		_associatedIndex = associatedIndex;
	}
	return self;
}

- (void)dealloc
{
	[_object release];
	[super dealloc];
}

- (nullable id)object
{
	return _object;
}

- (NSCollectionChangeType)changeType
{
	return _changeType;
}

- (NSUInteger)index
{
	return _index;
}

- (NSUInteger)associatedIndex
{
	return _associatedIndex;
}

/* OURS (§11.6.1 D2): Apple documents no -description for this class, and a change that cannot be printed is a
 * change nobody can debug from a log. */
- (NSString *)description
{
	if (_associatedIndex == (NSUInteger)NSNotFound) {
		return [NSString stringWithFormat:@"<%@: %@ %@ index %lu>",
			[self class],
			_changeType == NSCollectionChangeInsert ? @"insert" : @"remove",
			_object != nil ? _object : @"(no object)",
			(unsigned long)_index];
	}
	return [NSString stringWithFormat:@"<%@: %@ %@ index %lu associatedIndex %lu>",
		[self class],
		_changeType == NSCollectionChangeInsert ? @"insert" : @"remove",
		_object != nil ? _object : @"(no object)",
		(unsigned long)_index,
		(unsigned long)_associatedIndex];
}

@end

/* ====================================================================================================
 * THE DIFF ALGORITHM — Myers' greedy O(ND) shortest edit script (§12.6's "diff algorithm" dependency)
 *
 * The CONTRACT is what Apple publishes and all that is asserted anywhere: applying the difference to the
 * SOURCE produces the DESTINATION. The algorithm itself is ours, which is legitimate because Apple documents
 * no algorithm — its own -differenceFromArray:withOptions: page says a legitimate difference for
 * `@[A,B,C]` -> `@[C,B]` may remove index 0 and MOVE index 1 to index 1, which is exactly the freedom a
 * non-unique edit script has.
 *
 * A NAMED LIMIT, WITH ITS GROUND: the trace below is O(D * (N+M)) memory, so the middle is only run through
 * Myers while N+M is at most FN_DIFF_MAX_NODES; beyond that the script is the COARSE one (every source element
 * removed, every destination element inserted), which still satisfies the contract but is not minimal and
 * infers no moves. The linear-space refinement of the same algorithm is the fix when something needs it; a
 * probe of that size would be a strange thing to write, and silently allocating hundreds of megabytes would
 * be worse than saying so here.
 * ==================================================================================================== */

#define FN_DIFF_MAX_NODES 1024

static BOOL fn_diff_equal(id a, id b, BOOL (^test)(id, id))
{
	if (a == nil || b == nil) {
		return a == b;
	}
	if (test != NULL) {
		return test(a, b);
	}
	return a == b || [a isEqual:b];
}

static void fn_diff_emit(NSMutableArray *list, NSUInteger index)
{
	[list addObject:[NSNumber numberWithUnsignedInteger:index]];
}

/* THE COARSE SCRIPT: everything removed, everything inserted. Correct, never minimal. */
static void fn_diff_coarse(NSUInteger n, NSUInteger m, NSUInteger baseSrc, NSUInteger baseDst,
			   NSMutableArray *removals, NSMutableArray *insertions)
{
	NSUInteger i;

	for (i = 0; i < n; i++) {
		fn_diff_emit(removals, baseSrc + i);
	}
	for (i = 0; i < m; i++) {
		fn_diff_emit(insertions, baseDst + i);
	}
}

/* MYERS OVER THE TRIMMED MIDDLE. The rows are snapshots of V taken BEFORE each d's updates, which is the form
 * the backtrack needs; unset cells are a SENTINEL and not 0, because the recursion compares two of them. */
static void fn_diff_myers(const id *a, NSUInteger n, const id *b, NSUInteger m,
			  BOOL (^test)(id, id), NSUInteger baseSrc, NSUInteger baseDst,
			  NSMutableArray *removals, NSMutableArray *insertions)
{
	NSUInteger maxd = n + m;
	NSUInteger vsize = 2 * maxd + 1;
	long *v = NULL;
	long *trace = NULL;
	NSUInteger d, k;
	long x = 0, y = 0, doneD = 0;
	BOOL found = NO;

	if ((NSUInteger)(n + m) > FN_DIFF_MAX_NODES) {
		fn_diff_coarse(n, m, baseSrc, baseDst, removals, insertions);
		return;
	}
	v = (long *)malloc(vsize * sizeof(long));
	trace = (long *)malloc((maxd + 1) * vsize * sizeof(long));
	if (v == NULL || trace == NULL) {
		free(v);
		free(trace);
		fn_diff_coarse(n, m, baseSrc, baseDst, removals, insertions);
		return;
	}
	for (k = 0; k < vsize; k++) {
		v[k] = LONG_MIN / 4;
	}
	v[maxd + 1] = 0;			/* V[1] = 0: the search starts at (0,0) */

	for (d = 0; d <= maxd; d++) {
		memcpy(trace + d * vsize, v, vsize * sizeof(long));
		/* STEP 2, WHICH IS NOT COSMETIC: the recurrence reads the two NEIGHBOURING diagonals (kk-1 and
		 * kk+1), and only every OTHER k is populated at step d — that parity alternation is what makes the
		 * search O(ND) rather than O(N^2), and a step of 1 here reads cells that were never written (the
		 * sentinel), which both gives a wrong script and leaves `found` false. The `found` bail-out below is
		 * the belt to that braces: a run that somehow never reaches (n,m) takes the COARSE script rather than
		 * driving the backtrack walk off the end of the trace. */
		for (k = 0; k <= 2 * d; k += 2) {
			long kk = (long)d - (long)k;	/* kk runs d, d-2, ... -d */
			long px, py;

			if (kk == -(long)d ||
			    (kk != (long)d && v[maxd + kk - 1] < v[maxd + kk + 1])) {
				px = v[maxd + kk + 1];
			} else {
				px = v[maxd + kk - 1] + 1;
			}
			py = px - kk;
			while (px < (long)n && py < (long)m &&
			       fn_diff_equal(a[px], b[py], test)) {
				px++;
				py++;
			}
			v[maxd + kk] = px;
			if (px >= (long)n && py >= (long)m) {
				found = YES;
				doneD = d;
				break;
			}
		}
		if (found) {
			break;
		}
	}
	if (!found) {
		free(v);
		free(trace);
		fn_diff_coarse(n, m, baseSrc, baseDst, removals, insertions);
		return;
	}

	/* BACKTRACK from (n,m): walk the snake, then take the one non-diagonal step the recursion chose. */
	x = (long)n;
	y = (long)m;
	for (d = doneD + 1; d-- > 0; ) {
		long *row = trace + d * vsize;
		long kk = x - y;
		long prevk, prevx, prevy;

		if (kk == -(long)d || (kk != (long)d && row[maxd + kk - 1] < row[maxd + kk + 1])) {
			prevk = kk + 1;
		} else {
			prevk = kk - 1;
		}
		prevx = row[maxd + prevk];
		prevy = prevx - prevk;
		while (x > prevx && y > prevy) {
			x--;
			y--;
		}
		if (d > 0) {
			if (x == prevx) {
				fn_diff_emit(insertions, baseDst + (NSUInteger)(y - 1));
			} else {
				fn_diff_emit(removals, baseSrc + (NSUInteger)(x - 1));
			}
		}
		x = prevx;
		y = prevy;
	}
	free(v);
	free(trace);
}

/* THE WHOLE SCRIPT: trim the common prefix and suffix (which are matches by construction) and hand the middle
 * to Myers. Trimming first is not an optimisation of the algorithm, it is the difference between a trace that
 * fits and one that does not for the common case of a collection that changed in one place. */
static void fn_diff_script(NSArray *source, NSArray *destination, BOOL (^test)(id, id),
			   NSMutableArray *removals, NSMutableArray *insertions)
{
	NSUInteger n = [source count], m = [destination count];
	NSUInteger prefix = 0, suffix = 0, i;
	id *a = NULL, *b = NULL;

	while (prefix < n && prefix < m &&
	       fn_diff_equal([source objectAtIndex:prefix], [destination objectAtIndex:prefix], test)) {
		prefix++;
	}
	while (suffix < (n - prefix) && suffix < (m - prefix) &&
	       fn_diff_equal([source objectAtIndex:n - 1 - suffix],
			     [destination objectAtIndex:m - 1 - suffix], test)) {
		suffix++;
	}
	n -= prefix + suffix;
	m -= prefix + suffix;
	if (n == 0 && m == 0) {
		return;
	}
	a = (id *)malloc((n > 0 ? n : 1) * sizeof(id));
	b = (id *)malloc((m > 0 ? m : 1) * sizeof(id));
	if (a == NULL || b == NULL) {
		free(a);
		free(b);
		fn_diff_coarse(n, m, prefix, prefix, removals, insertions);
		return;
	}
	for (i = 0; i < n; i++) {
		a[i] = [source objectAtIndex:prefix + i];
	}
	for (i = 0; i < m; i++) {
		b[i] = [destination objectAtIndex:prefix + i];
	}
	fn_diff_myers(a, n, b, m, test, prefix, prefix, removals, insertions);
	free(a);
	free(b);
}

/* ====================================================================================================
 * NSOrderedCollectionDifference
 * ==================================================================================================== */

/* BUILD FROM A MEMBER ARRAY, WITH THE TWO CHECKS APPLE STATES. Every element must be a change, and every move
 * association must be REFLEXIVE — a change whose associatedIndex is set must have a counterpart of the
 * OPPOSITE type at that index pointing back. Apple's own words: "initializing a NSOrderedCollectionDifference
 * with broken associations (or associations that aren't reflexive) will generate an exception". */
static void fn_difference_partition(NSArray *changes, NSArray **insertions, NSArray **removals)
{
	NSMutableArray *ins = [NSMutableArray array];
	NSMutableArray *rem = [NSMutableArray array];
	NSUInteger i, j;

	for (i = 0; i < [changes count]; i++) {
		id change = [changes objectAtIndex:i];

		if (![change isKindOfClass:[NSOrderedCollectionChange class]]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSOrderedCollectionDifference: element %lu of the change list is a %@, "
					   @"not an NSOrderedCollectionChange",
				   (unsigned long)i, [change class]];
		}
		[(NSOrderedCollectionChange *)change changeType] == NSCollectionChangeInsert
			? [ins addObject:change] : [rem addObject:change];
	}
	for (i = 0; i < [changes count]; i++) {
		NSOrderedCollectionChange *change = [changes objectAtIndex:i];
		NSUInteger associated = [change associatedIndex];
		BOOL matched = NO;

		if (associated == (NSUInteger)NSNotFound) {
			continue;
		}
		for (j = 0; j < [changes count]; j++) {
			NSOrderedCollectionChange *other = [changes objectAtIndex:j];

			if ([other changeType] == [change changeType]) {
				continue;
			}
			if ([other index] == associated &&
			    [other associatedIndex] == [change index]) {
				matched = YES;
				break;
			}
		}
		if (!matched) {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSOrderedCollectionDifference: the change at index %lu is associated with "
					   @"index %lu, but no change of the opposite type points back (Apple: broken or "
					   @"non-reflexive associations raise)",
				   (unsigned long)[change index], (unsigned long)associated];
		}
	}
	*insertions = ins;
	*removals = rem;
}

@implementation NSOrderedCollectionDifference

- (instancetype)initWithChanges:(NSArray *)changes
{
	self = [super init];
	if (self != nil) {
		NSArray *ins = nil, *rem = nil;

		fn_difference_partition(changes != nil ? changes : [NSArray array], &ins, &rem);
		_insertions = [ins copy];
		_removals = [rem copy];
		_changes = [changes != nil ? changes : [NSArray array] copy];
	}
	return self;
}

/* THE INDEX-SET FORM. The object arrays pair with the indexes in ASCENDING ORDER, and a count that does not
 * match raises rather than dropping a side silently; nil object arrays are allowed and leave every -object
 * nil. */
- (instancetype)initWithInsertIndexes:(NSIndexSet *)inserts
		      insertedObjects:(nullable NSArray *)insertedObjects
			removeIndexes:(NSIndexSet *)removes
			removedObjects:(nullable NSArray *)removedObjects
{
	return [self initWithInsertIndexes:inserts
			   insertedObjects:insertedObjects
			     removeIndexes:removes
			   removedObjects:removedObjects
			additionalChanges:[NSArray array]];
}

- (instancetype)initWithInsertIndexes:(NSIndexSet *)inserts
		      insertedObjects:(nullable NSArray *)insertedObjects
			removeIndexes:(NSIndexSet *)removes
			removedObjects:(nullable NSArray *)removedObjects
		     additionalChanges:(NSArray *)changes
{
	NSMutableArray *all = [NSMutableArray array];
	__block NSUInteger slot = 0;

	if (insertedObjects != nil && [insertedObjects count] != [inserts count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSOrderedCollectionDifference: %lu insert indexes but %lu inserted objects",
				   (unsigned long)[inserts count], (unsigned long)[insertedObjects count]];
	}
	if (removedObjects != nil && [removedObjects count] != [removes count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSOrderedCollectionDifference: %lu remove indexes but %lu removed objects",
				   (unsigned long)[removes count], (unsigned long)[removedObjects count]];
	}
	slot = 0;
	[inserts enumerateIndexesUsingBlock:^(NSUInteger index, BOOL *stop) {
		(void)stop;
		[all addObject:[NSOrderedCollectionChange
				   changeWithObject:insertedObjects != nil
					   ? [insertedObjects objectAtIndex:slot] : nil
						   type:NSCollectionChangeInsert
						  index:index]];
		slot++;
	}];
	slot = 0;
	[removes enumerateIndexesUsingBlock:^(NSUInteger index, BOOL *stop) {
		(void)stop;
		[all addObject:[NSOrderedCollectionChange
				   changeWithObject:removedObjects != nil
					   ? [removedObjects objectAtIndex:slot] : nil
						   type:NSCollectionChangeRemove
						  index:index]];
		slot++;
	}];
	[all addObjectsFromArray:changes != nil ? changes : [NSArray array]];
	return [self initWithChanges:all];
}

- (void)dealloc
{
	[_insertions release];
	[_removals release];
	[_changes release];
	[super dealloc];
}

- (BOOL)hasChanges
{
	return [_changes count] > 0;
}

- (NSArray *)insertions
{
	return _insertions;
}

- (NSArray *)removals
{
	return _removals;
}

/* "A copy of the receiver with all removals changed to insertions (and vice versa)" — and the two halves of a
 * move keep their pairing by SWAPPING the indexes, which is what makes applying a difference and then its
 * inverse return the original collection (Apple's example on the -inverseDifference page). */
- (NSOrderedCollectionDifference *)inverseDifference
{
	NSMutableArray *flipped = [NSMutableArray arrayWithCapacity:[_changes count]];
	NSUInteger i;

	for (i = 0; i < [_changes count]; i++) {
		NSOrderedCollectionChange *change = [_changes objectAtIndex:i];

		[flipped addObject:[NSOrderedCollectionChange
				       changeWithObject:[change object]
						   type:[change changeType] == NSCollectionChangeInsert
							   ? NSCollectionChangeRemove
							   : NSCollectionChangeInsert
						  index:[change associatedIndex] != (NSUInteger)NSNotFound
							   ? [change associatedIndex]
							   : [change index]
					associatedIndex:[change associatedIndex] != (NSUInteger)NSNotFound
							   ? [change index]
							   : (NSUInteger)NSNotFound]];
	}
	return [[[NSOrderedCollectionDifference alloc] initWithChanges:flipped] autorelease];
}

- (NSOrderedCollectionDifference *)differenceByTransformingChangesWithBlock:
	(NSOrderedCollectionChange * (^)(NSOrderedCollectionChange *change))block
{
	NSMutableArray *mapped = [NSMutableArray arrayWithCapacity:[_changes count]];
	NSUInteger i;

	for (i = 0; i < [_changes count]; i++) {
		NSOrderedCollectionChange *change = [_changes objectAtIndex:i];
		NSOrderedCollectionChange *updated = block != NULL ? block(change) : change;

		[mapped addObject:updated != nil ? updated : change];
	}
	return [[[NSOrderedCollectionDifference alloc] initWithChanges:mapped] autorelease];
}

/* NSFastEnumeration over the changes, in the order the difference was built. Apple publishes no
 * -enumerateChanges… door, so this conformance IS the enumeration API; the batch is taken from the backing
 * array, which is immutable and therefore needs none of the copying a mutable collection's does. */
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id __unsafe_unretained *)buffer
				    count:(NSUInteger)len
{
	return [_changes countByEnumeratingWithState:state objects:buffer count:len];
}

/* OURS (§11.6.1 D2), like the change's. */
- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %lu change(s) (%lu insertion(s), %lu removal(s))>",
		[self class],
		(unsigned long)[_changes count],
		(unsigned long)[_insertions count],
		(unsigned long)[_removals count]];
}

@end

/* ====================================================================================================
 * THE DOORS ON NSArray AND NSOrderedSet
 *
 * BOTH TAKE OPTS AND A COUNT OF CHANGES; the two `…withOptions:usingEquivalenceTest:` forms differ in that
 * Apple says NOT to ask for moves there — "don't use the option inferMoves when providing a block for the
 * equivalence test. The changes returned in the difference object don't include valid values for
 * associatedIndex" — so a move option with a block is IGNORED here and the associated indexes stay NSNotFound,
 * which is the behaviour that page describes rather than a rule invented on top of it.
 * ==================================================================================================== */

/* SORT AN INDEX LIST ASCENDING. The script walks BACKWARDS from the end, so it emits BOTH lists in descending
 * order; every consumer here wants ascending, and the lists are short enough that an insertion sort keeps the
 * ordering rule in one readable place. */
static void fn_sort_ascending(NSMutableArray *indexes)
{
	NSUInteger i, j;

	for (i = 1; i < [indexes count]; i++) {
		id key = [indexes objectAtIndex:i];
		NSUInteger keyValue = [key unsignedIntegerValue];

		for (j = i; j > 0 && [[indexes objectAtIndex:j - 1] unsignedIntegerValue] > keyValue; j--) {
			[indexes replaceObjectAtIndex:j withObject:[indexes objectAtIndex:j - 1]];
		}
		[indexes replaceObjectAtIndex:j withObject:key];
	}
}

/* THE SHARED DOOR BODY. `dest` is the DESTINATION (the receiver of -differenceFromArray:) and `src` the SOURCE
 * (its argument) — see the header's opening comment for why that direction is the one two of Apple's examples
 * agree on. */
static NSOrderedCollectionDifference *fn_difference_between(NSArray *dest, NSArray *src,
							    NSOrderedCollectionDifferenceCalculationOptions options,
							    BOOL (^test)(id, id))
{
	NSMutableArray *removals = [NSMutableArray array];
	NSMutableArray *insertions = [NSMutableArray array];
	NSMutableArray *changes = [NSMutableArray array];
	NSUInteger *remAssociated = NULL;
	NSUInteger *insAssociated = NULL;
	NSUInteger i, j;

	fn_diff_script(src != nil ? src : [NSArray array], dest != nil ? dest : [NSArray array],
		       test, removals, insertions);
	fn_sort_ascending(removals);
	fn_sort_ascending(insertions);

	remAssociated = (NSUInteger *)malloc(([removals count] + 1) * sizeof(NSUInteger));
	insAssociated = (NSUInteger *)malloc(([insertions count] + 1) * sizeof(NSUInteger));
	if (remAssociated == NULL || insAssociated == NULL) {
		free(remAssociated);
		free(insAssociated);
		remAssociated = NULL;
		insAssociated = NULL;
	}
	for (i = 0; remAssociated != NULL && i < [removals count]; i++) {
		remAssociated[i] = (NSUInteger)NSNotFound;
	}
	for (i = 0; insAssociated != NULL && i < [insertions count]; i++) {
		insAssociated[i] = (NSUInteger)NSNotFound;
	}

	/* THE MOVE PAIRING, AND IT IS THE ONE PART OF THE DIFFERENCE THAT IS A CHOICE. A removal and an insertion
	 * of equal objects are two halves of ONE move: each gets the other's index in -associatedIndex, and each
	 * change is paired at most once. The comparison is the SAME test the script used, so an equivalence block
	 * pairs what it equated. A move may even keep its index — Apple: "don't ignore a move when the indexes of
	 * its changes are the same" — and this pairs those too. */
	if ((options & NSOrderedCollectionDifferenceCalculationInferMoves) != 0 && test == NULL &&
	    remAssociated != NULL && insAssociated != NULL) {
		for (i = 0; i < [removals count]; i++) {
			NSUInteger srcIndex = [[removals objectAtIndex:i] unsignedIntegerValue];
			id object = [src objectAtIndex:srcIndex];

			for (j = 0; j < [insertions count]; j++) {
				NSUInteger dstIndex;

				if (insAssociated[j] != (NSUInteger)NSNotFound) {
					continue;
				}
				dstIndex = [[insertions objectAtIndex:j] unsignedIntegerValue];
				if (fn_diff_equal(object, [dest objectAtIndex:dstIndex], NULL)) {
					remAssociated[i] = dstIndex;
					insAssociated[j] = srcIndex;
					break;
				}
			}
		}
	}

	/* INSERTIONS FIRST, THEN REMOVALS — each list already ascending — so the enumeration order and the two
	 * accessors agree with one another (the order is ours; see the header). */
	for (i = 0; i < [insertions count]; i++) {
		NSUInteger index = [[insertions objectAtIndex:i] unsignedIntegerValue];

		[changes addObject:[NSOrderedCollectionChange
				       changeWithObject:(options & NSOrderedCollectionDifferenceCalculationOmitInsertedObjects)
					       ? nil : [dest objectAtIndex:index]
						   type:NSCollectionChangeInsert
						  index:index
					associatedIndex:insAssociated != NULL ? insAssociated[i]
							      : (NSUInteger)NSNotFound]];
	}
	for (i = 0; i < [removals count]; i++) {
		NSUInteger index = [[removals objectAtIndex:i] unsignedIntegerValue];

		[changes addObject:[NSOrderedCollectionChange
				       changeWithObject:(options & NSOrderedCollectionDifferenceCalculationOmitRemovedObjects)
					       ? nil : [src objectAtIndex:index]
						   type:NSCollectionChangeRemove
						  index:index
					associatedIndex:remAssociated != NULL ? remAssociated[i]
							      : (NSUInteger)NSNotFound]];
	}
	free(remAssociated);
	free(insAssociated);
	return [[[NSOrderedCollectionDifference alloc] initWithChanges:changes] autorelease];
}

/* APPLYING: removals first, DESCENDING by index (so each earlier removal does not shift a later one), then
 * insertions ASCENDING by index into the result. That is the definition of the two index spaces: a removal's
 * index is in the receiver, an insertion's is in the array being built. */
static id fn_apply_difference(NSArray *source, NSOrderedCollectionDifference *difference)
{
	NSMutableArray *result = [NSMutableArray array];
	NSMutableArray *removals = [NSMutableArray arrayWithArray:[difference removals]];
	NSMutableArray *insertions = [NSMutableArray arrayWithArray:[difference insertions]];
	NSUInteger i, j;

	[result addObjectsFromArray:source];
	/* A SMALL INSERTION SORT, not -sortedArrayUsingFunction: (which has no comparator to hand here): the two
	 * lists are short in the cases that matter and this keeps the ordering rule in one readable place. */
	for (i = 1; i < [removals count]; i++) {
		id key = [removals objectAtIndex:i];
		NSUInteger keyIndex = [(NSOrderedCollectionChange *)key index];

		for (j = i; j > 0 && [(NSOrderedCollectionChange *)[removals objectAtIndex:j - 1] index] < keyIndex; j--) {
			[removals replaceObjectAtIndex:j withObject:[removals objectAtIndex:j - 1]];
		}
		[removals replaceObjectAtIndex:j withObject:key];
	}
	for (i = 0; i < [removals count]; i++) {
		[result removeObjectAtIndex:[(NSOrderedCollectionChange *)[removals objectAtIndex:i] index]];
	}
	for (i = 1; i < [insertions count]; i++) {
		id key = [insertions objectAtIndex:i];
		NSUInteger keyIndex = [(NSOrderedCollectionChange *)key index];

		for (j = i; j > 0 && [(NSOrderedCollectionChange *)[insertions objectAtIndex:j - 1] index] > keyIndex; j--) {
			[insertions replaceObjectAtIndex:j withObject:[insertions objectAtIndex:j - 1]];
		}
		[insertions replaceObjectAtIndex:j withObject:key];
	}
	for (i = 0; i < [insertions count]; i++) {
		NSOrderedCollectionChange *change = [insertions objectAtIndex:i];

		[result insertObject:[change object] atIndex:[change index]];
	}
	return result;
}

@implementation NSArray (NSOrderedCollectionDifferenceAdditions)

- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray *)other
{
	/* TO THE HELPER DIRECTLY, not through the equivalence-test door: that door's block parameter is NONNULL (as
	 * Apple's is), so reaching it with NULL here would be the very null-to-nonnull conversion the guest's
	 * `-Werror=nullable-to-nonnull-conversion` exists to catch. */
	return fn_difference_between(self, other, 0, NULL);
}

- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray *)other
					  withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
{
	return fn_difference_between(self, other, options, NULL);
}

- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray *)other
					  withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
				  usingEquivalenceTest:(BOOL (^)(id, id))block
{
	return fn_difference_between(self, other, options, block);
}

- (NSArray *)arrayByApplyingDifference:(NSOrderedCollectionDifference *)difference
{
	return fn_apply_difference(self, difference);
}

@end

@implementation NSOrderedSet (NSOrderedCollectionDifferenceAdditions)

/* `-array` ON BOTH SIDES, because an NSOrderedSet is NOT an NSArray in this library (it wraps one): the helper
 * compares and indexes the ORDER, which is the only part a difference is about. */
- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet *)other
{
	return fn_difference_between([self array], [other array], 0, NULL);
}

- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet *)other
						withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
{
	return fn_difference_between([self array], [other array], options, NULL);
}

- (NSOrderedCollectionDifference *)differenceFromOrderedSet:(NSOrderedSet *)other
						withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
					usingEquivalenceTest:(BOOL (^)(id, id))block
{
	return fn_difference_between([self array], [other array], options, block);
}

/* THE ORDERED SET IS REBUILT FROM THE APPLIED ARRAY rather than through a mutable ordered set: this library has
 * no NSMutableOrderedSet, and one is not needed to answer this door. */
- (NSOrderedSet *)orderedSetByApplyingDifference:(NSOrderedCollectionDifference *)difference
{
	return [NSOrderedSet orderedSetWithArray:fn_apply_difference([self array], difference)];
}

@end
