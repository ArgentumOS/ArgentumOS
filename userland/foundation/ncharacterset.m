/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ncharacterset.m — the character set.
 *
 * MANUAL OWNERSHIP: it owns a C buffer and implements no -retain/-release.
 */

#import <foundation/NSCharacterSet.h>
#import <foundation/NSString.h>
#include <stdlib.h>
#include <string.h>

#define FN_MAX_CHARACTER	0xFFFF

@implementation NSCharacterSet

/* ---- the range list, which is the whole representation --------------------- */

static void fn_add_range(NSCharacterSet *set, unsigned int location, unsigned int length)
{
	if (length == 0) {
		return;
	}
	if (set->_rangeCount + 1 > set->_capacity) {
		unsigned long grown = (set->_capacity == 0) ? 8 : set->_capacity * 2;
		unsigned int *fresh = (unsigned int *)realloc(set->_ranges, grown * 2 * sizeof(unsigned int));

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

static BOOL fn_contains(NSCharacterSet *set, unsigned int character)
{
	unsigned long i;

	for (i = 0; i < set->_rangeCount; i++) {
		unsigned int location = set->_ranges[i * 2];
		unsigned int length = set->_ranges[i * 2 + 1];

		if (character >= location && character - location < length) {
			return YES;
		}
	}
	return NO;
}

+ (NSCharacterSet *)characterSetWithCharactersInString:(NSString *)string
{
	return [[self alloc] initWithCharactersInString:string];
}

+ (NSCharacterSet *)characterSetWithRange:(NSRange)range
{
	return [[self alloc] initWithRange:range];
}

- (id)initWithCharactersInString:(NSString *)string
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	{
		size_t i;

		for (i = 0; i < [string length]; i++) {
			unichar c = [string characterAtIndex:i];

			fn_add_range(self, c, 1);
		}
	}
	return self;
}

- (id)initWithRange:(NSRange)range
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* Clamped to the code-unit space: a set cannot hold what a unichar cannot say. */
	if (range.location <= FN_MAX_CHARACTER) {
		unsigned int length = (unsigned int)range.length;

		if (range.location + length > FN_MAX_CHARACTER + 1) {
			length = (unsigned int)(FN_MAX_CHARACTER + 1 - range.location);
		}
		fn_add_range(self, (unsigned int)range.location, length);
	}
	return self;
}

- (void)dealloc
{
	free(_ranges);
}

- (BOOL)characterIsMember:(unichar)character
{
	return fn_contains(self, (unsigned int)character);
}

- (NSCharacterSet *)invertedSet
{
	NSMutableCharacterSet *inverted = [[NSMutableCharacterSet alloc] init];
	unsigned long i;
	unsigned int cursor = 0;

	/* The complement, built from the gaps between the ranges. */
	for (i = 0; i < _rangeCount; i++) {
		unsigned int location = _ranges[i * 2];
		unsigned int length = _ranges[i * 2 + 1];

		if (location > cursor) {
			fn_add_range(inverted, cursor, location - cursor);
		}
		cursor = location + length;
	}
	if (cursor <= FN_MAX_CHARACTER) {
		fn_add_range(inverted, cursor, FN_MAX_CHARACTER + 1 - cursor);
	}
	return inverted;
}

- (BOOL)isSupersetOfSet:(NSCharacterSet *)other
{
	unsigned long i;

	for (i = 0; i < other->_rangeCount; i++) {
		unsigned int location = other->_ranges[i * 2];
		unsigned int length = other->_ranges[i * 2 + 1];
		unsigned int k;

		for (k = 0; k < length; k++) {
			if (!fn_contains(self, location + k)) {
				return NO;
			}
		}
	}
	return YES;
}

- (BOOL)isEqualToCharacterSet:(NSCharacterSet *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	/* Equal when each is a superset of the other, which is true however the
	 * ranges were written down. */
	return [self isSupersetOfSet:other] && [other isSupersetOfSet:self];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSCharacterSet class]]) {
		return NO;
	}
	return [self isEqualToCharacterSet:(NSCharacterSet *)other];
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
	return [NSString stringWithFormat:@"<NSCharacterSet: %lu range(s)>", _rangeCount];
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	NSMutableCharacterSet *copy = [[NSMutableCharacterSet alloc] init];
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		fn_add_range(copy, _ranges[i * 2], _ranges[i * 2 + 1]);
	}
	return copy;
}

/* ---- the built-ins, each one a range list ---------------------------------- */

+ (NSCharacterSet *)whitespaceCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		set = [[NSCharacterSet alloc] initWithRange:NSMakeRange(' ', 1)];
	}
	return set;
}

+ (NSCharacterSet *)whitespaceAndNewlineCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		/* The C0 space controls plus the space itself: 0x09-0x0D and 0x20. */
		[built addCharactersInRange:NSMakeRange(0x09, 5)];
		[built addCharactersInRange:NSMakeRange(0x20, 1)];
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)newlineCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		/* LF, CR, and the Unicode line/paragraph separators. */
		[built addCharactersInRange:NSMakeRange(0x0A, 1)];
		[built addCharactersInRange:NSMakeRange(0x0D, 1)];
		[built addCharactersInRange:NSMakeRange(0x2028, 2)];
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)decimalDigitCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		set = [[NSCharacterSet alloc] initWithRange:NSMakeRange('0', 10)];
	}
	return set;
}

+ (NSCharacterSet *)letterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		/* ASCII letters, and LATIN-1 letters, which is as far as this goes. */
		[built addCharactersInRange:NSMakeRange('A', 26)];
		[built addCharactersInRange:NSMakeRange('a', 26)];
		[built addCharactersInRange:NSMakeRange(0xC0, 0x100 - 0xC0 + 1)];
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)alphanumericCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built formUnionWithCharacterSet:[NSCharacterSet letterCharacterSet]];
		[built formUnionWithCharacterSet:[NSCharacterSet decimalDigitCharacterSet]];
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)punctuationCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built addCharactersInRange:NSMakeRange('!', 15)];	/* ! " # $ % & ' ( ) * + , - . / */
		[built addCharactersInRange:NSMakeRange(':', 7)];	/* : ; < = > ? @ */
		[built addCharactersInRange:NSMakeRange('[', 6)];	/* [ \ ] ^ _ ` */
		[built addCharactersInRange:NSMakeRange('{', 4)];	/* { | } ~ */
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)controlCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built addCharactersInRange:NSMakeRange(0x00, 0x20)];	/* the C0 controls */
		[built addCharactersInRange:NSMakeRange(0x7F, 1)];	/* and DELETE */
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)lowercaseLetterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		set = [[NSCharacterSet alloc] initWithRange:NSMakeRange('a', 26)];
	}
	return set;
}

+ (NSCharacterSet *)uppercaseLetterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		set = [[NSCharacterSet alloc] initWithRange:NSMakeRange('A', 26)];
	}
	return set;
}

@end

@implementation NSMutableCharacterSet

+ (NSMutableCharacterSet *)characterSet
{
	return [[self alloc] init];
}

- (void)addCharactersInString:(NSString *)string
{
	size_t i;

	for (i = 0; i < [string length]; i++) {
		fn_add_range(self, [string characterAtIndex:i], 1);
	}
}

- (void)addCharactersInRange:(NSRange)range
{
	NSCharacterSet *added = [[NSCharacterSet alloc] initWithRange:range];

	[self formUnionWithCharacterSet:added];
}

- (void)removeCharactersInString:(NSString *)string
{
	size_t i;

	for (i = 0; i < [string length]; i++) {
		NSCharacterSet *removed = [[NSCharacterSet alloc]
			initWithRange:NSMakeRange([string characterAtIndex:i], 1)];
		NSMutableCharacterSet *kept = [[NSMutableCharacterSet alloc] init];

		/* Rebuilt from the ranges that do not touch the removed one, which keeps
		 * the representation a plain list rather than a set of holes. */
		{
			unsigned long k;

			for (k = 0; k < _rangeCount; k++) {
				unsigned int location = _ranges[k * 2];
				unsigned int length = _ranges[k * 2 + 1];
				unsigned int cursor = location;
				unsigned int end = location + length;

				while (cursor < end) {
					if ([removed characterIsMember:(unichar)cursor]) {
						cursor++;
						continue;
					}
					{
						unsigned int run = cursor;

						while (run < end && ![removed characterIsMember:(unichar)run]) {
							run++;
						}
						fn_add_range(kept, cursor, run - cursor);
						cursor = run;
					}
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
}

- (void)removeCharactersInRange:(NSRange)range
{
	NSString *removed = [[NSString alloc] init];
	(void)removed;
	{
		unsigned long k;
		NSMutableCharacterSet *kept = [[NSMutableCharacterSet alloc] init];

		for (k = 0; k < _rangeCount; k++) {
			unsigned int location = _ranges[k * 2];
			unsigned int length = _ranges[k * 2 + 1];
			unsigned int start = (unsigned int)range.location;
			unsigned int end = start + (unsigned int)range.length;
			unsigned int cursor = location;
			unsigned int stop = location + length;

			while (cursor < stop) {
				if (cursor >= start && cursor < end) {
					cursor++;
					continue;
				}
				{
					unsigned int run = cursor;

					while (run < stop && !(run >= start && run < end)) {
						run++;
					}
					fn_add_range(kept, cursor, run - cursor);
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
}

- (void)invert
{
	NSCharacterSet *complement = [self invertedSet];

	free(_ranges);
	_ranges = NULL;
	_rangeCount = 0;
	_capacity = 0;
	{
		unsigned long i;

		for (i = 0; i < complement->_rangeCount; i++) {
			fn_add_range(self, complement->_ranges[i * 2], complement->_ranges[i * 2 + 1]);
		}
	}
}

- (void)formUnionWithCharacterSet:(NSCharacterSet *)other
{
	unsigned long i;

	for (i = 0; i < other->_rangeCount; i++) {
		fn_add_range(self, other->_ranges[i * 2], other->_ranges[i * 2 + 1]);
	}
}

- (void)formIntersectionWithCharacterSet:(NSCharacterSet *)other
{
	NSMutableCharacterSet *kept = [[NSMutableCharacterSet alloc] init];
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		unsigned int location = _ranges[i * 2];
		unsigned int length = _ranges[i * 2 + 1];
		unsigned int cursor = location;
		unsigned int stop = location + length;

		while (cursor < stop) {
			if (!fn_contains(other, cursor)) {
				cursor++;
				continue;
			}
			{
				unsigned int run = cursor;

				while (run < stop && fn_contains(other, run)) {
					run++;
				}
				fn_add_range(kept, cursor, run - cursor);
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

@end
