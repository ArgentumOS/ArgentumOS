/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSIndexSet.m — the index set.
 *
 * MANUAL OWNERSHIP: it owns a C buffer and implements no -retain/-release.
 */

#import <Foundation/NSIndexSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#include <stdlib.h>

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M6): the same shape as the other families. AGIndexSetEmpty is the
 * SHARED empty instance, AGIndexSetItems the general case (the front's own ivars are its storage), and
 * AGIndexSetMutable the mutable one.
 *
 * -init MAY ANSWER THE SINGLETON HERE, unlike the dictionary and data families, and the difference is
 * MEASURED rather than stylistic: this family's constructions are COMPLETE - -initWithIndex: and
 * -initWithIndexesInRange: both start with [super init] and then append, and nothing allocates and fills
 * afterwards - so an -init that answers the shared empty instance cannot have a later store captured into it.
 * The MUTABLE class is unaffected because it is a SIBLING of AGIndexSetItems, not a subclass: its own
 * +indexSet reaches NSObject's -init and answers an empty mutable index set.
 * =================================================================================================== */
@interface AGIndexSetEmpty : NSIndexSet
+ (AGIndexSetEmpty *)emptyIndexSet;
@end

@interface AGIndexSetItems : NSIndexSet
@end

@interface AGIndexSetMutable : NSMutableIndexSet
@end

@implementation NSIndexSet

/* THE DOOR (§C.3 item 1), routed exactly once at the front. */
+ (id)alloc
{
	if (self != [NSIndexSet class]) {
		return [super alloc];
	}
	return [AGIndexSetItems alloc];
}

/* §C.3 item 4: an archiver asks for THIS, never for -class. NSMutableIndexSet answers ITSELF below, being a
 * public subclass - the shape NSDecimalNumber, NSMutableString, NSMutableOrderedSet and NSMutableData all
 * need. */
- (Class)classForCoder
{
	return [NSIndexSet class];
}

/* ---- the range list, and its canonical order -------------------------------- */

static int fn_compare_ranges(const void *a, const void *b)
{
	const unsigned long *left = (const unsigned long *)a;
	const unsigned long *right = (const unsigned long *)b;

	if (left[0] < right[0]) {
		return -1;
	}
	if (left[0] > right[0]) {
		return 1;
	}
	return 0;
}

/* Sort, then merge anything that touches: this is what makes -count honest when
 * the same index is added twice, and it is why the representation never grows a
 * second entry for a value it already holds. */
static void fn_normalise(NSIndexSet *set)
{
	unsigned long read, write;

	if (set->_rangeCount == 0) {
		return;
	}
	qsort(set->_ranges, set->_rangeCount, 2 * sizeof(unsigned long), fn_compare_ranges);
	write = 0;
	for (read = 1; read < set->_rangeCount; read++) {
		unsigned long *held = &set->_ranges[write * 2];
		unsigned long *next = &set->_ranges[read * 2];

		/* Overlapping OR adjacent (held ends where next begins) merge. */
		if (next[0] <= held[0] + held[1]) {
			unsigned long stop = held[0] + held[1];

			if (next[0] + next[1] > stop) {
				stop = next[0] + next[1];
			}
			held[1] = stop - held[0];
		} else {
			write++;
			set->_ranges[write * 2] = next[0];
			set->_ranges[write * 2 + 1] = next[1];
		}
	}
	set->_rangeCount = write + 1;
}

static void fn_append(NSIndexSet *set, unsigned long location, unsigned long length)
{
	if (length == 0) {
		return;
	}
	if (set->_rangeCount + 1 > set->_capacity) {
		unsigned long grown = (set->_capacity == 0) ? 8 : set->_capacity * 2;
		unsigned long *fresh = (unsigned long *)realloc(set->_ranges, grown * 2 * sizeof(unsigned long));

		if (fresh == NULL) {
			return;
		}
		set->_ranges = fresh;
		set->_capacity = grown;
	}
	set->_ranges[set->_rangeCount * 2] = location;
	set->_ranges[set->_rangeCount * 2 + 1] = length;
	set->_rangeCount++;
}

+ (instancetype)indexSet
{
	return [[self alloc] init];
}

+ (instancetype)indexSetWithIndex:(NSUInteger)value
{
	return [[self alloc] initWithIndex:value];
}

+ (instancetype)indexSetWithIndexesInRange:(NSRange)range
{
	return [[self alloc] initWithIndexesInRange:range];
}

- (id)initWithIndex:(NSUInteger)value
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	fn_append(self, value, 1);
	return self;
}

- (id)initWithIndexesInRange:(NSRange)range
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	fn_append(self, range.location, range.length);
	return self;
}

- (id)initWithIndexSet:(NSIndexSet *)other
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE OTHER SET'S OWN RANGES ARE ALREADY CANONICAL (sorted, merged), so appending them in order
	 * leaves this set canonical too and no normalise is needed. */
	if (other != nil) {
		[other enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
			fn_append(self, mine.location, mine.length);
			(void)stop;
		}];
	}
	return self;
}

- (void)dealloc
{
	free(_ranges);
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

- (BOOL)containsIndex:(NSUInteger)value
{
	return [self containsIndexesInRange:NSMakeRange(value, 1)];
}

- (BOOL)containsIndexesInRange:(NSRange)range
{
	__block BOOL found = NO;

	if (range.length == 0) {
		return YES;
	}
	/* OVER THE RANGE PRIMITIVE (§C.3 item 5) - and -containsIndex: comes free, since it delegates here. */
	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		if (range.location >= mine.location &&
		    range.location + range.length <= mine.location + mine.length) {
			found = YES;
			*stop = YES;
		}
	}];
	return found;
}

/* ALL OF THE OTHER SET, decided range by range: every one of other's ranges must be FULLY covered by
 * this set, and `-countOfIndexesInRange:` is the question asked once per range. An empty argument is
 * vacuously contained, which is what "all of its indexes" means. */
- (BOOL)containsIndexes:(NSIndexSet *)other
{
	__block BOOL all = YES;

	if (other == nil) {
		return NO;
	}
	[other enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		if ((unsigned long)[self countOfIndexesInRange:mine] != (unsigned long)mine.length) {
			all = NO;
			*stop = YES;
		}
	}];
	return all;
}

/* ANY index in the range at all - the same count, asked for its sign rather than its value. */
- (BOOL)intersectsIndexesInRange:(NSRange)range
{
	return [self countOfIndexesInRange:range] > 0;
}

- (NSUInteger)count
{
	unsigned long total = 0;
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		total += _ranges[i * 2 + 1];
	}
	return total;
}

- (NSUInteger)firstIndex
{
	if (_rangeCount == 0) {
		return NSNotFound;
	}
	return _ranges[0];
}

- (NSUInteger)lastIndex
{
	NSUInteger walk = [self firstIndex];

	/* OVER THE PRIMITIVES (§C.3 item 5): the LAST index is found by walking to the end. */
	if (walk == NSNotFound) {
		return NSNotFound;
	}
	for (;;) {
		NSUInteger next = [self indexGreaterThanIndex:walk];

		if (next == NSNotFound) {
			return walk;
		}
		walk = next;
	}
}

- (NSUInteger)indexGreaterThanIndex:(NSUInteger)value
{
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		unsigned long location = _ranges[i * 2];
		unsigned long length = _ranges[i * 2 + 1];
		unsigned long stop = location + length;

		if (value + 1 >= location && value + 1 < stop) {
			return value + 1;
		}
		if (value < location) {
			return location;
		}
	}
	return NSNotFound;
}

- (NSUInteger)indexLessThanIndex:(NSUInteger)value
{
	NSUInteger walk = [self firstIndex];
	NSUInteger previous = NSNotFound;

	/* OVER THE PRIMITIVES: walk up while the indexes stay below the argument, remembering the last. */
	while (walk != NSNotFound && walk < value) {
		previous = walk;
		walk = [self indexGreaterThanIndex:walk];
	}
	return previous;
}

- (void)enumerateIndexesUsingBlock:(void (^)(NSUInteger index, BOOL *stop))block
{
	/* OVER THE RANGE PRIMITIVE (§C.3 item 5): the same iteration, one range at a time. The audit that M3
	 * taught caught THIS one - a public door still reading the ivar, which for a class with a different
	 * layout answers "no indexes" instead of crashing. */
	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		NSUInteger k;

		for (k = 0; k < mine.length && !*stop; k++) {
			block(mine.location + k, stop);
		}
	}];
}

/*
 * THE TEST SCAN AND THE INDEX ENUMERATORS. All of them are the range primitive (§C.3 item 5) with a
 * clip, walked ascending or descending - the arithmetic the range enumerators above already use, and
 * the reason the options are read in exactly one place per door.
 *
 * THE PREDICATE'S OUT-PARAMETER IS THE SCAN'S STOP, which is why the loops below test it rather than
 * keeping a flag of their own: a caller that stops early and one that runs to the end differ only in
 * how many times the walk asks.
 */
- (void)enumerateIndexesInRange:(NSRange)range
			options:(NSEnumerationOptions)options
		     usingBlock:(void (^)(NSUInteger index, BOOL *stop))block
{
	BOOL reverse = (options & NSEnumerationReverse) != 0;
	NSRange clip = range;

	if (block == nil) {
		return;
	}
	[self fnEnumerateRanges:&clip reverse:reverse usingBlock:^(NSRange mine, BOOL *stop) {
		NSUInteger k;

		for (k = 0; k < mine.length && !*stop; k++) {
			NSUInteger index = reverse ? (mine.location + mine.length - 1 - k)
						   : (mine.location + k);

			block(index, stop);
		}
	}];
}

- (void)enumerateIndexesWithOptions:(NSEnumerationOptions)options
			 usingBlock:(void (^)(NSUInteger index, BOOL *stop))block
{
	[self enumerateIndexesInRange:NSMakeRange(0, NSUIntegerMax) options:options usingBlock:block];
}

/* THE FIRST (or, reversed, the LAST) INDEX THE PREDICATE ACCEPTS - NSNotFound when there is none, which
 * is the same sentinel -firstIndex answers for an empty set. The predicate's own `stop` ends the scan
 * exactly as a match does: both mean "no further index can change this answer". */
- (NSUInteger)indexInRange:(NSRange)range
		   options:(NSEnumerationOptions)options
	       passingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	__block NSUInteger found = NSNotFound;
	BOOL reverse = (options & NSEnumerationReverse) != 0;
	NSRange clip = range;

	if (predicate == nil) {
		return NSNotFound;
	}
	[self fnEnumerateRanges:&clip reverse:reverse usingBlock:^(NSRange mine, BOOL *stop) {
		NSUInteger k;

		for (k = 0; k < mine.length; k++) {
			NSUInteger index = reverse ? (mine.location + mine.length - 1 - k)
						   : (mine.location + k);
			BOOL stopHere = NO;

			if (predicate(index, &stopHere)) {
				found = index;
				*stop = YES;
				break;
			}
			if (stopHere) {
				*stop = YES;
				break;
			}
		}
	}];
	return found;
}

- (NSUInteger)indexWithOptions:(NSEnumerationOptions)options
		   passingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	return [self indexInRange:NSMakeRange(0, NSUIntegerMax) options:options passingTest:predicate];
}

- (NSUInteger)indexPassingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	return [self indexInRange:NSMakeRange(0, NSUIntegerMax) options:0 passingTest:predicate];
}

/* THE SAME SCAN, COLLECTED: a mutable set accumulates while the walk runs, and the ANSWER IS AN
 * IMMUTABLE COPY - -copy on this family is a retain (an immutable needs no copy), so a genuinely
 * frozen result has to be built through -initWithIndexSet: rather than asked for. */
- (NSIndexSet *)indexesInRange:(NSRange)range
		       options:(NSEnumerationOptions)options
		   passingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	NSMutableIndexSet *picked = [[NSMutableIndexSet alloc] init];
	NSIndexSet *answer;
	BOOL reverse = (options & NSEnumerationReverse) != 0;
	NSRange clip = range;

	if (predicate != nil) {
		[self fnEnumerateRanges:&clip reverse:reverse usingBlock:^(NSRange mine, BOOL *stop) {
			NSUInteger k;

			for (k = 0; k < mine.length; k++) {
				NSUInteger index = reverse ? (mine.location + mine.length - 1 - k)
							   : (mine.location + k);
				BOOL stopHere = NO;

				if (predicate(index, &stopHere)) {
					[picked addIndex:index];
				}
				if (stopHere) {
					*stop = YES;
					break;
				}
			}
		}];
	}
	answer = [[NSIndexSet alloc] initWithIndexSet:picked];
	[picked release];
	return [answer autorelease];
}

- (NSIndexSet *)indexesWithOptions:(NSEnumerationOptions)options
		       passingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	return [self indexesInRange:NSMakeRange(0, NSUIntegerMax) options:options passingTest:predicate];
}

- (NSIndexSet *)indexesPassingTest:(BOOL (^)(NSUInteger index, BOOL *stop))predicate
{
	return [self indexesInRange:NSMakeRange(0, NSUIntegerMax) options:0 passingTest:predicate];
}

- (BOOL)isEqualToIndexSet:(NSIndexSet *)other
{
	__block unsigned long covered = 0;

	/*
	 * OVER THE RANGE PRIMITIVE, WITHOUT A PAIRED WALK: equal index counts plus "every one of MY ranges is
	 * covered by the other" is equality - the counts are compared first, so a range the other lacks cannot be
	 * compensated by one it has twice. O(ranges of self x ranges of other), which is small by construction.
	 */
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other count] != [self count]) {
		return NO;
	}
	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		covered += (unsigned long)[other countOfIndexesInRange:mine];
		(void)stop;
	}];
	return covered == (unsigned long)[self count];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSIndexSet class]]) {
		return NO;
	}
	return [self isEqualToIndexSet:(NSIndexSet *)other];
}

- (NSUInteger)hash
{
	__block unsigned long h = 2166136261UL;

	/*
	 * THE SAME FOLD, over the ranges the primitive reports - so the VALUE is unchanged, which is what keeps
	 * -hash consistent with -isEqualToIndexSet: for index sets built in different orders. It stays O(ranges)
	 * rather than O(indexes): hashing {0..1000000} must not visit a million indexes.
	 */
	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		h ^= (unsigned long)mine.location;
		h *= 16777619UL;
		h ^= (unsigned long)mine.length;
		h *= 16777619UL;
		(void)stop;
	}];
	return (NSUInteger)h;
}

- (NSString *)description
{
	__block unsigned long ranges = 0;

	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		(void)mine;
		(void)stop;
		ranges++;
	}];
	return [NSString stringWithFormat:@"<NSIndexSet: %lu index(es) in %lu range(s)>",
					  [self count], ranges];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

- (id)mutableCopy
{
	NSMutableIndexSet *copy = [[NSMutableIndexSet alloc] init];

	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		[copy addIndexesInRange:mine];
		(void)stop;
	}];
	return copy;
}


/*
 * THE THREE RANGE-BASED QUERIES (D7's kind (D)). The representation IS a range list - (location,
 * length) pairs, sorted and merged by fn_normalise - so each of these walks ranges and is exact
 * rather than approximate. A range that does not intersect contributes nothing, and an empty set
 * answers NSNotFound, which is what the header says -firstIndex and -lastIndex answer too.
 */
/* THE BUFFER FORM (Apple's page, with its own worked example: for the contiguous indexes 1-100, asking
 * with range (1,100) and a buffer of 20 copies 1-20 and leaves the range as (21,80)). THE REMAINDER IS
 * COMPUTED FROM THE LAST INDEX COPIED rather than from the count: with a sparse set those differ, and
 * the page's wording is "the range is updated to the indexes NOT COPIED". */
- (NSUInteger)getIndexes:(NSUInteger *)indexBuffer
		maxCount:(NSUInteger)bufferSize
	    inIndexRange:(NSRangePointer)range
{
	__block NSUInteger written = 0;
	NSUInteger low = 0;
	NSUInteger high = NSUIntegerMax;

	if (indexBuffer == NULL || bufferSize == 0) {
		return 0;
	}
	if (range != NULL) {
		low = range->location;
		high = (range->length > NSUIntegerMax - low) ? NSUIntegerMax : low + range->length;
	}
	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		NSUInteger j;

		for (j = 0; j < mine.length && written < bufferSize; j++) {
			NSUInteger index = mine.location + j;

			if (index < low || index >= high) {
				continue;
			}
			indexBuffer[written++] = index;
		}
		if (written >= bufferSize) {
			*stop = YES;
		}
	}];
	if (range != NULL) {
		if (written == 0) {
			/* NOTHING WAS COPIED, so nothing was consumed and the range stands as asked. */
			range->length = (high == NSUIntegerMax) ? 0 : (high - low);
		} else {
			NSUInteger next = indexBuffer[written - 1] + 1;

			range->location = next;
			range->length = (high > next) ? (high - next) : 0;
		}
	}
	return written;
}

/* ONE WALK OF THE RANGE LIST, which is what this class IS: the enumerators differ only in direction and
 * in whether the block sees the range or its intersection with one. */
- (void)fnEnumerateRanges:(NSRange *)clip reverse:(BOOL)reverse usingBlock:(void (^)(NSRange, BOOL *))block
{
	BOOL stop = NO;
	NSInteger count = (NSInteger)_rangeCount;
	NSInteger i;

	if (block == nil) {
		return;
	}
	for (i = reverse ? count - 1 : 0; reverse ? (i >= 0) : (i < count); i += reverse ? -1 : 1) {
		NSRange mine = NSMakeRange((NSUInteger)_ranges[i * 2], (NSUInteger)_ranges[i * 2 + 1]);
		NSRange out = mine;

		if (clip != NULL) {
			NSUInteger low = clip->location;
			NSUInteger high = clip->location + clip->length;
			NSUInteger end = mine.location + mine.length;
			NSUInteger start = mine.location > low ? mine.location : low;

			if (end > high) {
				end = high;
			}
			if (start >= end) {
				continue;		/* no overlap, and Apple says the block sees INTERSECTIONS */
			}
			out = NSMakeRange(start, end - start);
		}
		block(out, &stop);
		if (stop) {
			return;
		}
	}
}

- (void)enumerateRangesUsingBlock:(void (^)(NSRange range, BOOL *stop))block
{
	[self fnEnumerateRanges:NULL reverse:NO usingBlock:block];
}

- (void)enumerateRangesWithOptions:(NSEnumerationOptions)options usingBlock:(void (^)(NSRange range, BOOL *stop))block
{
	[self fnEnumerateRanges:NULL reverse:(options & NSEnumerationReverse) != 0 usingBlock:block];
}

- (void)enumerateRangesInRange:(NSRange)range options:(NSEnumerationOptions)options usingBlock:(void (^)(NSRange range, BOOL *stop))block
{
	NSRange clip = range;

	[self fnEnumerateRanges:&clip reverse:(options & NSEnumerationReverse) != 0 usingBlock:block];
}

- (NSUInteger)countOfIndexesInRange:(NSRange)range
{
	__block unsigned long total = 0;
	unsigned long otherEnd = range.location + range.length;

	[self enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		unsigned long loc = mine.location;
		unsigned long end = loc + mine.length;
		unsigned long from = (loc > range.location) ? loc : range.location;
		unsigned long to = (end < otherEnd) ? end : otherEnd;

		(void)stop;
		if (to > from) {
			total += (to - from);
		}
	}];
	return (NSUInteger)total;
}

- (NSUInteger)indexGreaterThanOrEqualToIndex:(NSUInteger)index
{
	NSUInteger walk = [self firstIndex];

	while (walk != NSNotFound && walk < index) {
		walk = [self indexGreaterThanIndex:walk];
	}
	return walk;
}

- (NSUInteger)indexLessThanOrEqualToIndex:(NSUInteger)index
{
	NSUInteger walk = [self firstIndex];
	NSUInteger previous = NSNotFound;

	while (walk != NSNotFound && walk <= index) {
		previous = walk;
		walk = [self indexGreaterThanIndex:walk];
	}
	return previous;
}
@end

@implementation NSMutableIndexSet

+ (id)alloc
{
	if (self != [NSMutableIndexSet class]) {
		return [super alloc];
	}
	return [AGIndexSetMutable alloc];
}

- (Class)classForCoder
{
	return [NSMutableIndexSet class];
}

+ (NSMutableIndexSet *)indexSet
{
	return [[self alloc] init];
}

- (void)addIndex:(NSUInteger)value
{
	fn_append(self, value, 1);
	fn_normalise(self);
}

- (void)addIndexesInRange:(NSRange)range
{
	fn_append(self, range.location, range.length);
	fn_normalise(self);
}

- (void)removeIndex:(NSUInteger)value
{
	[self removeIndexesInRange:NSMakeRange(value, 1)];
}

- (void)removeIndexesInRange:(NSRange)range
{
	unsigned long i;
	NSMutableIndexSet *kept = [[NSMutableIndexSet alloc] init];

	if (range.length == 0) {
		return;
	}
	for (i = 0; i < _rangeCount; i++) {
		unsigned long location = _ranges[i * 2];
		unsigned long length = _ranges[i * 2 + 1];
		unsigned long stop = location + length;
		unsigned long start = range.location;
		unsigned long end = range.location + range.length;
		unsigned long cursor = location;

		while (cursor < stop) {
			if (cursor >= start && cursor < end) {
				cursor++;
				continue;
			}
			{
				unsigned long run = cursor;

				while (run < stop && !(run >= start && run < end)) {
					run++;
				}
				fn_append(kept, cursor, run - cursor);
				cursor = run;
			}
		}
	}
	free(_ranges);
	_ranges = kept->_ranges;
	_rangeCount = kept->_rangeCount;
	_capacity = kept->_capacity;
	kept->_ranges = NULL;
	kept->_rangeCount = 0;
	kept->_capacity = 0;
}

- (void)removeAllIndexes
{
	free(_ranges);
	_ranges = NULL;
	_rangeCount = 0;
	_capacity = 0;
}

/* THE OTHER SET'S RANGES ARE APPENDED AND NORMALISED ONCE, rather than one -addIndexesInRange: call
 * each (which would normalise per range). An alias is a no-op: [m addIndexes:m] asks for what m already
 * holds, and walking the buffer while appending to it is the one thing this form must not do. */
- (void)addIndexes:(NSIndexSet *)indexSet
{
	__block BOOL appended = NO;

	if (indexSet == nil || indexSet == self) {
		return;
	}
	[indexSet enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		fn_append(self, mine.location, mine.length);
		appended = YES;
		(void)stop;
	}];
	if (appended) {
		fn_normalise(self);
	}
}

/* EACH OF THE OTHER SET'S RANGES COMES OUT, through the range form below - which rebuilds the storage
 * rather than editing it, so the walk over `indexSet` never sees a buffer this method is freeing. */
- (void)removeIndexes:(NSIndexSet *)indexSet
{
	if (indexSet == nil) {
		return;
	}
	if (indexSet == self) {
		[self removeAllIndexes];
		return;
	}
	[indexSet enumerateRangesUsingBlock:^(NSRange mine, BOOL *stop) {
		[self removeIndexesInRange:mine];
		(void)stop;
	}];
}

/* THE SHIFT. Every index at or after `startIndex` moves by `delta`; a range that STRADDLES startIndex is
 * cut in two, the lower part staying where it is. The rebuilt list is normalised, which is what makes a
 * downward shift merge with the indexes it lands on instead of double-counting them. Apple's one error
 * case is honoured rather than clamped: an index that would land below zero is an NSRangeException. */
- (void)shiftIndexesStartingAtIndex:(NSUInteger)startIndex by:(NSInteger)delta
{
	NSMutableIndexSet *shifted;
	unsigned long i;

	if (delta == 0) {
		return;
	}
	if (delta < 0) {
		/* THE FIRST INDEX THAT WOULD ACTUALLY MOVE IS WHAT DECIDES THIS: a start below every index is
		 * not an error (nothing moves), and an empty set cannot place anything below zero. */
		NSUInteger first = [self indexGreaterThanOrEqualToIndex:startIndex];

		if (first != NSNotFound && (NSInteger)first + delta < 0) {
			[NSException raise:NSRangeException
				    format:@"*** -[NSMutableIndexSet shiftIndexesStartingAtIndex:by:]: "
					   @"shift of %ld from %lu would place index %lu below zero",
					   (long)delta, (unsigned long)startIndex, (unsigned long)first];
		}
	}
	shifted = [[NSMutableIndexSet alloc] init];
	for (i = 0; i < _rangeCount; i++) {
		unsigned long location = _ranges[i * 2];
		unsigned long length = _ranges[i * 2 + 1];
		unsigned long end = location + length;

		if (end <= startIndex) {
			fn_append(shifted, location, length);
		} else if (location >= startIndex) {
			fn_append(shifted, (unsigned long)((NSInteger)location + delta), length);
		} else {
			fn_append(shifted, location, startIndex - location);
			fn_append(shifted, (unsigned long)((NSInteger)startIndex + delta),
				  end - startIndex);
		}
	}
	fn_normalise(shifted);
	free(_ranges);
	_ranges = shifted->_ranges;
	_rangeCount = shifted->_rangeCount;
	_capacity = shifted->_capacity;
	shifted->_ranges = NULL;
	shifted->_rangeCount = 0;
	shifted->_capacity = 0;
}

@end


/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8).
 * =================================================================================================== */

@implementation AGIndexSetItems

/* [[NSIndexSet alloc] init] IS A LEGITIMATE THING TO WRITE (§C.3 item 1) AND IT IS THE EMPTY CASE. */
- (id)init
{
	[self release];	/* never initialized: the storage was never built */
	return (id)[AGIndexSetEmpty emptyIndexSet];
}

@end

@implementation AGIndexSetEmpty

+ (AGIndexSetEmpty *)emptyIndexSet
{
	static AGIndexSetEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGIndexSetEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, the price of a singleton in a library with no `+allocWithZone:` and no collector. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* ITS STORAGE IS ALREADY THE EMPTY ANSWER: zero ranges, so the front's own reads answer "no indexes"
 * without a line of code here - which is why this class needs no primitives of its own yet. */

@end

@implementation AGIndexSetMutable

/* NOTHING TO IMPLEMENT: NSMutableIndexSet's implementation IS the mutable storage implementation, and what
 * a caller gains is the NAME that -class answers. */

@end
