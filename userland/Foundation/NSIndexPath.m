/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSIndexPath.m — the index path.
 *
 * MANUAL OWNERSHIP: it owns a C buffer and implements no -retain/-release.
 */

#import <Foundation/NSIndexPath.h>
#import <Foundation/NSCoder.h>	/* the coder forms below */
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#include <stdlib.h>
#include <string.h>

@implementation NSIndexPath

/* One place that takes ownership of a position list, so "the buffer and the length
 * agree" is stated once instead of at every constructor and both derived forms.
 * A failed allocation leaves the empty path, the same posture NSIndexSet's buffer
 * growth takes. */
static void fn_hold(NSIndexPath *path, const NSUInteger *indexes, NSUInteger length)
{
	if (length == 0) {
		return;
	}
	path->_indexes = (NSUInteger *)malloc(length * sizeof(NSUInteger));
	if (path->_indexes == NULL) {
		return;
	}
	memcpy(path->_indexes, indexes, length * sizeof(NSUInteger));
	path->_length = length;
}

+ (instancetype)indexPathWithIndex:(NSUInteger)index
{
	return [[self alloc] initWithIndex:index];
}

+ (instancetype)indexPathWithIndexes:(const NSUInteger [])indexes
			      length:(NSUInteger)length
{
	return [[self alloc] initWithIndexes:indexes length:length];
}

- (id)initWithIndex:(NSUInteger)index
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	fn_hold(self, &index, 1);
	return self;
}

- (id)initWithIndexes:(const NSUInteger [])indexes length:(NSUInteger)length
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	fn_hold(self, indexes, length);
	return self;
}

- (void)dealloc
{
	free(_indexes);
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

- (NSUInteger)length
{
	return _length;
}

- (NSUInteger)indexAtPosition:(NSUInteger)position
{
	if (position >= _length) {
		[NSException raise:NSRangeException
			    format:@"NSIndexPath position %lu is out of range (length %lu)",
				   (unsigned long)position, (unsigned long)_length];
	}
	return _indexes[position];
}

- (void)getIndexes:(NSUInteger *)indexes
{
	if (indexes == NULL || _length == 0) {
		return;
	}
	memcpy(indexes, _indexes, _length * sizeof(NSUInteger));
}

- (void)getIndexes:(NSUInteger *)indexes range:(NSRange)positionRange
{
	if (indexes == NULL || positionRange.length == 0) {
		return;
	}
	if (positionRange.location + positionRange.length > _length) {
		[NSException raise:NSRangeException
			    format:@"NSIndexPath range {%lu, %lu} is out of range (length %lu)",
				   (unsigned long)positionRange.location,
				   (unsigned long)positionRange.length,
				   (unsigned long)_length];
	}
	memcpy(indexes, &_indexes[positionRange.location],
	       positionRange.length * sizeof(NSUInteger));
}

- (NSIndexPath *)indexPathByAddingIndex:(NSUInteger)index
{
	NSUInteger *grown = (NSUInteger *)malloc((_length + 1) * sizeof(NSUInteger));
	NSIndexPath *path;

	if (grown == NULL) {
		return nil;
	}
	if (_length > 0) {
		memcpy(grown, _indexes, _length * sizeof(NSUInteger));
	}
	grown[_length] = index;
	path = [[NSIndexPath alloc] initWithIndexes:grown length:_length + 1];
	free(grown);
	return path;
}

- (NSIndexPath *)indexPathByRemovingLastIndex
{
	if (_length == 0) {
		[NSException raise:NSRangeException
			    format:@"-indexPathByRemovingLastIndex on an empty NSIndexPath"];
	}
	return [[NSIndexPath alloc] initWithIndexes:_indexes length:_length - 1];
}

- (NSComparisonResult)compare:(NSIndexPath *)otherObject
{
	NSUInteger shared = _length;
	NSUInteger i;

	if (otherObject == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-compare: needs an NSIndexPath, not nil"];
	}
	if (otherObject->_length < shared) {
		shared = otherObject->_length;
	}
	for (i = 0; i < shared; i++) {
		if (_indexes[i] < otherObject->_indexes[i]) {
			return NSOrderedAscending;
		}
		if (_indexes[i] > otherObject->_indexes[i]) {
			return NSOrderedDescending;
		}
	}
	/* A prefix compares less than the path that extends it. */
	if (_length < otherObject->_length) {
		return NSOrderedAscending;
	}
	if (_length > otherObject->_length) {
		return NSOrderedDescending;
	}
	return NSOrderedSame;
}

- (BOOL)isEqual:(id)other
{
	NSIndexPath *path;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSIndexPath class]]) {
		return NO;
	}
	path = (NSIndexPath *)other;
	if (path->_length != _length) {
		return NO;
	}
	if (_length == 0) {
		return YES;
	}
	return memcmp(_indexes, path->_indexes, _length * sizeof(NSUInteger)) == 0;
}

- (NSUInteger)hash
{
	unsigned long h = 2166136261UL;
	NSUInteger i;

	for (i = 0; i < _length; i++) {
		h ^= _indexes[i];
		h *= 16777619UL;
	}
	return h;
}

- (NSString *)description
{
	NSMutableString *shown = [[NSMutableString alloc] initWithUTF8String:""];
	NSUInteger i;

	for (i = 0; i < _length; i++) {
		if (i > 0) {
			[shown appendUTF8String:"-"];
		}
		[shown appendFormat:@"%lu", (unsigned long)_indexes[i]];
	}
	return [NSString stringWithFormat:@"<NSIndexPath: %lu position(s) %@>",
					  (unsigned long)_length, shown];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}


/*
 * NSCoding FOR AN INDEX PATH (D7's kind (D)): the positions go out as ONE BYTE BLOB rather than as
 * an array of objects, which is Apple's own trick for this class and also what keeps it from
 * depending on NSArray conforming first. The two keys are spelled by this library - they are
 * internal to our own archive format and a program never sees them.
 */
- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInteger:(NSInteger)_length forKey:@"NS.length"];
	if (_length > 0) {
		[coder encodeBytes:(const void *)_indexes
			    length:_length * sizeof(NSUInteger)
			    forKey:@"NS.indexes"];
	}
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	NSUInteger length = (NSUInteger)[coder decodeIntegerForKey:@"NS.length"];
	NSUInteger stored = 0;
	const void *bytes = NULL;

	if (length > 0) {
		bytes = [coder decodeBytesForKey:@"NS.indexes" returnedLength:&stored];
		length = stored / sizeof(NSUInteger);
	}
	return [self initWithIndexes:(const NSUInteger *)bytes length:length];
}






@end
