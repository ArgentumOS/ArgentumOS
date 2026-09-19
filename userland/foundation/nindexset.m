/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nindexset.m — the index set.
 *
 * MANUAL OWNERSHIP: it owns a C buffer and implements no -retain/-release.
 */

#import <foundation/NSIndexSet.h>
#import <foundation/NSString.h>
#include <stdlib.h>

@implementation NSIndexSet

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

- (void)dealloc
{
	free(_ranges);
}

- (BOOL)containsIndex:(NSUInteger)value
{
	return [self containsIndexesInRange:NSMakeRange(value, 1)];
}

- (BOOL)containsIndexesInRange:(NSRange)range
{
	unsigned long i;

	if (range.length == 0) {
		return YES;
	}
	for (i = 0; i < _rangeCount; i++) {
		unsigned long location = _ranges[i * 2];
		unsigned long length = _ranges[i * 2 + 1];

		if (range.location >= location && range.location + range.length <= location + length) {
			return YES;
		}
	}
	return NO;
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
	if (_rangeCount == 0) {
		return NSNotFound;
	}
	return _ranges[(_rangeCount - 1) * 2] + _ranges[(_rangeCount - 1) * 2 + 1] - 1;
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
	unsigned long i;

	if (value == 0) {
		return NSNotFound;
	}
	for (i = _rangeCount; i > 0; i--) {
		unsigned long location = _ranges[(i - 1) * 2];
		unsigned long length = _ranges[(i - 1) * 2 + 1];

		if (value - 1 >= location && value - 1 < location + length) {
			return value - 1;
		}
		if (value > location + length) {
			return location + length - 1;
		}
	}
	return NSNotFound;
}

- (void)enumerateIndexesUsingBlock:(void (^)(NSUInteger index, BOOL *stop))block
{
	unsigned long i;
	BOOL stop = NO;

	for (i = 0; i < _rangeCount && !stop; i++) {
		unsigned long location = _ranges[i * 2];
		unsigned long length = _ranges[i * 2 + 1];
		unsigned long k;

		for (k = 0; k < length && !stop; k++) {
			block(location + k, &stop);
		}
	}
}

- (BOOL)isEqualToIndexSet:(NSIndexSet *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if (_rangeCount != other->_rangeCount) {
		return NO;
	}
	{
		unsigned long i;

		for (i = 0; i < _rangeCount; i++) {
			if (_ranges[i * 2] != other->_ranges[i * 2] ||
			    _ranges[i * 2 + 1] != other->_ranges[i * 2 + 1]) {
				return NO;
			}
		}
	}
	return YES;
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
	unsigned long h = 2166136261UL;
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		h ^= _ranges[i * 2];
		h *= 16777619UL;
		h ^= _ranges[i * 2 + 1];
		h *= 16777619UL;
	}
	return h;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSIndexSet: %lu index(es) in %lu range(s)>",
					  [self count], _rangeCount];
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	NSMutableIndexSet *copy = [[NSMutableIndexSet alloc] init];
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		fn_append(copy, _ranges[i * 2], _ranges[i * 2 + 1]);
	}
	return copy;
}

@end

@implementation NSMutableIndexSet

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

@end
