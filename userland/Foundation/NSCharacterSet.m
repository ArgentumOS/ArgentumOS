/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCharacterSet.m — the character set.
 *
 * MANUAL OWNERSHIP: it owns a C buffer and implements no -retain/-release.
 */

#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSArray.h>	/* the doors carry the ranges as one flattened array of numbers */
#import <Foundation/NSNumber.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#include <stdlib.h>
#include <string.h>
/* ICU ANSWERS THE TWO PROPERTIES THE RULE SETS BELOW ARE DEFINED BY (see §15.5). This library
 * already links icuuc - five of its files use ICU for calendars and formatters - so this is a
 * header, not a dependency. */
#include <unicode/uchar.h>

#define FN_MAX_CHARACTER	0xFFFF

/* §63.56: THE TWO INTERNAL CONSTRUCTORS, DECLARED HERE AND NO LONGER IN THE HEADER. They are this library's
 * own path — `+characterSetWithCharactersInString:` and `+characterSetWithRange:` are `[[self alloc] initWith…]`
 * and the ten cached built-ins go through `-initWithRange:` — and Apple declares neither, so they were never
 * part of this class's PUBLIC surface. A class extension is the whole fix: the bodies, the callers and the
 * cached-set shape are untouched, and a caller outside this file can no longer see them. */
@interface NSCharacterSet ()
- (nullable id)initWithCharactersInString:(NSString *)string;
- (nullable id)initWithRange:(NSRange)range;
@end

@implementation NSCharacterSet

/* §C.3 item 4: the archiver asks for THIS, never for -class. */
- (Class)classForCoder
{
	return [NSCharacterSet class];
}

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

		for (i = 0; i < [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
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

/* ===================================================================================================
 * THE NSCoding DOORS (§63.18). KEYS OURS (Apple publishes no name for them, §11.6.1 D2's ground), defined
 * beside their only writer and reader as this thread's other field-carrying classes have done.
 *
 * THE RANGES ARE THE PAYLOAD, FLATTENED (location, length, location, length …) into ONE array of numbers: a
 * range is two numbers, so an odd-length array cannot be a set and the decoder NAMES that rather than rounding
 * it down. The decoder rebuilds through the same private `fn_add_range` every construction uses, and applies
 * the same CODE-UNIT clamp `-initWithRange:` applies — a set cannot hold what a unichar cannot say.
 * =================================================================================================== */
static NSString *const kRangesKey = @"NS.ranges";

- (void)encodeWithCoder:(NSCoder *)coder
{
	NSMutableArray *pairs = [NSMutableArray arrayWithCapacity:_rangeCount * 2];
	unsigned long i;

	for (i = 0; i < _rangeCount; i++) {
		[pairs addObject:[NSNumber numberWithUnsignedInt:_ranges[i * 2]]];
		[pairs addObject:[NSNumber numberWithUnsignedInt:_ranges[i * 2 + 1]]];
	}
	[coder encodeObject:pairs forKey:kRangesKey];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSArray *pairs = [coder decodeObjectForKey:kRangesKey];
	NSUInteger count = pairs != nil ? [pairs count] : 0;
	NSUInteger i;

	if ((count % 2) != 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSCharacterSet: the archive carries %lu range value(s), which is not a "
				   "whole number of (location, length) pairs",
				   (unsigned long)count];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	for (i = 0; i + 1 < count; i += 2) {
		unsigned int location = [[pairs objectAtIndex:i] unsignedIntValue];
		unsigned int length = [[pairs objectAtIndex:i + 1] unsignedIntValue];

		if (location <= FN_MAX_CHARACTER) {
			if (location + length > FN_MAX_CHARACTER + 1) {
				length = FN_MAX_CHARACTER + 1 - location;
			}
			fn_add_range(self, location, length);
		}
	}
	return self;
}

- (void)dealloc
{
	free(_ranges);
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
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
	NSData *mine = [self bitmapRepresentation];
	NSData *theirs = [other bitmapRepresentation];
	const unsigned char *a = [mine bytes];
	const unsigned char *b = [theirs bytes];
	NSUInteger n = [mine length];
	NSUInteger i;

	/*
	 * OVER THE PRIMITIVES (§C.3 item 5): two bitmaps, compared byte-wise. The version this replaces walked
	 * `other`'s PRIVATE range array, so it could only ever work for a class laid out as this one is - and a set
	 * is exactly the kind of thing a caller might implement itself. The first 8 bytes are this implementation's
	 * header (a magic and a range count), so the comparison starts after them.
	 */
	if ([theirs length] != n) {
		return NO;
	}
	for (i = 8; i < n; i++) {
		if ((b[i] & ~a[i]) != 0) {
			return NO;
		}
	}
	return YES;
}

/* §63.58: `-isEqualToCharacterSet:` was here and is GONE (user decision dec-d2dfbe0c080f14c2 — delete our own
 * doors outright and rewrite every caller). It was a convenience: Apple answers set equality with `-isEqual:`,
 * which this class implements just below, and the probe had already been asserting the two AGREE —
 * `[digits isEqualToCharacterSet:x] && [digits isEqual:x]` — which is the strongest possible evidence that one
 * of them was redundant. The body moved INTO `-isEqual:`, where the receiver's own comparison lives, so nothing
 * is lost: `-isSupersetOfSet:` in both directions is still exactly what makes two differently-written range
 * lists compare equal. */
- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSCharacterSet class]]) {
		return NO;
	}
	/* Equal when each is a superset of the other, which is true however the ranges were written down. */
	return [self isSupersetOfSet:(NSCharacterSet *)other] && [(NSCharacterSet *)other isSupersetOfSet:self];
}

- (NSUInteger)hash
{
	NSData *bitmap = [self bitmapRepresentation];
	const unsigned char *bytes = [bitmap bytes];
	NSUInteger n = [bitmap length];
	NSUInteger h = 2166136261UL;
	NSUInteger i;

	/*
	 * OVER THE PRIMITIVE (§C.3 item 5), AND IT FIXES A REAL DEFECT RATHER THAN JUST MOVING A READ: hashing the
	 * RANGES meant two sets that -isEqual: calls equal hashed differently whenever their ranges were written
	 * down differently - which the equality contract forbids. The members are what matters, so the members are
	 * what is hashed.
	 */
	for (i = 8; i < n; i++) {
		h ^= bytes[i];
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
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
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

/*
 * ---- the built-ins, each one a PUBLISHED DEFINITION --------------------------
 *
 * THESE TEN USED TO BE ASCII OR LATIN-1 APPROXIMATIONS, and measured they were wrong in every case
 * that mattered: +uppercaseLetterCharacterSet was `NSMakeRange('A', 26)` - 26 members where Apple
 * specifies Lu AND Lt - +whitespaceCharacterSet was the space alone, and +letterCharacterSet stopped
 * at LATIN-1. The api-complete inventory could not catch it: it asserts that a documented set EXISTS,
 * and nothing asserted what was IN one. §16 records the measurement; these are the same category
 * tables the four sets added in §15.5 use.
 */

+ (NSCharacterSet *)whitespaceCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Zs PLUS TAB (U+0009) - the tab is added explicitly BECAUSE IT IS NOT IN Zs, which is why
		 * Apple's definition names it separately. */
		static const int categories[] = { U_SPACE_SEPARATOR };
		NSMutableCharacterSet *built = fn_set_by_property(categories, 1, UCHAR_GENERAL_CATEGORY);

		[built addCharactersInRange:NSMakeRange(0x09, 1)];
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)whitespaceAndNewlineCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Z*, U+000A-U+000D, AND U+0085 - Apple's current definition, WHICH DOES NOT INCLUDE TAB.
		 * THAT IS A REAL BOUNDARY AND NOT AN OVERSIGHT: the tab is in +whitespaceCharacterSet and
		 * NOT here, and the probe asserts both halves. The older OpenStep/GNUstep documentation
		 * (which this library's sets were built from) lists space, tab and the newlines instead, so
		 * this is a place where the two admissible sources disagree and the modern Apple page wins;
		 * it is recorded here rather than silently chosen. */
		NSMutableCharacterSet *built = fn_set_by_property(fn_cat_z, 3, UCHAR_GENERAL_CATEGORY);

		[built addCharactersInRange:NSMakeRange(0x0A, 4)];	/* LF, VT, FF, CR */
		[built addCharactersInRange:NSMakeRange(0x85, 1)];	/* NEL */
		set = built;
	}
	return set;
}

/* THE SHARED ALPHABET OF THE URL SETS: `unreserved` is ALPHA / DIGIT plus a fixed punctuation run, and each
 * set then adds the characters ITS OWN production allows. The cache is the same `static` idiom the other
 * predefined sets use — these objects must outlive the pool a class door is called in. */
static NSCharacterSet *fn_url_set(NSString *extra, NSCharacterSet **cache)
{
	if (*cache == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built addCharactersInRange:NSMakeRange('a', 26)];
		[built addCharactersInRange:NSMakeRange('A', 26)];
		[built addCharactersInRange:NSMakeRange('0', 10)];
		[built addCharactersInString:@"-._~"];		/* the rest of unreserved */
		[built addCharactersInString:@"!$&'()*+,;="];	/* sub-delims */
		[built addCharactersInString:extra];		/* this component's own */
		*cache = built;
	}
	return *cache;
}

+ (NSCharacterSet *)URLUserAllowedCharacterSet
{
	static NSCharacterSet *set = nil;

	/* userinfo = *( unreserved / pct-encoded / sub-delims / ":" ) */
	return fn_url_set(@":", &set);
}

+ (NSCharacterSet *)URLPasswordAllowedCharacterSet
{
	/* AN ALIAS, NOT A COPY: a password is part of userinfo and the production is the same one, so two lists
	 * would be two chances to drift. */
	return [self URLUserAllowedCharacterSet];
}

+ (NSCharacterSet *)URLHostAllowedCharacterSet
{
	static NSCharacterSet *set = nil;

	/* reg-name = *( unreserved / pct-encoded / sub-delims ), plus the ":" of a port and the brackets of an
	 * IP-literal. NOT "/" and NOT "?" — those end the authority. */
	return fn_url_set(@":[]", &set);
}

+ (NSCharacterSet *)URLPathAllowedCharacterSet
{
	static NSCharacterSet *set = nil;

	/* pchar = unreserved / pct-encoded / sub-delims / ":" / "@", plus the "/" between segments. */
	return fn_url_set(@":@/", &set);
}

+ (NSCharacterSet *)URLQueryAllowedCharacterSet
{
	static NSCharacterSet *set = nil;

	/* query = *( pchar / "/" / "?" ) */
	return fn_url_set(@":@/?", &set);
}

+ (NSCharacterSet *)URLFragmentAllowedCharacterSet
{
	/* AN ALIAS, NOT A COPY: fragment = *( pchar / "/" / "?" ) is the SAME production as query. */
	return [self URLQueryAllowedCharacterSet];
}

+ (NSCharacterSet *)newlineCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* THE LITERAL CODE POINTS, because this set is a LIST and not a category: U+000A-U+000D,
		 * U+0085, U+2028 and U+2029. */
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built addCharactersInRange:NSMakeRange(0x0A, 4)];	/* LF, VT, FF, CR */
		[built addCharactersInRange:NSMakeRange(0x85, 1)];	/* NEL */
		[built addCharactersInRange:NSMakeRange(0x2028, 2)];	/* LINE and PARAGRAPH SEPARATOR */
		set = built;
	}
	return set;
}

+ (NSCharacterSet *)decimalDigitCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Nd, WHICH IS NOT '0'-'9': the Arabic-Indic and other decimal digits are members, and the
		 * OTHER numbers (Nl, No - Roman numerals, superscripts) are not. */
		static const int categories[] = { U_DECIMAL_DIGIT_NUMBER };

		set = fn_set_by_property(categories, 1, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)letterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* L* AND M*, which is Apple's definition: the marks are letters here because they combine
		 * with one. */
		set = fn_set_by_property(fn_cat_lm, 8, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)alphanumericCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* L*, M* AND N* - ALL THREE NUMBER CATEGORIES, which is why this is its own rule rather
		 * than the union of the two sets above: +decimalDigitCharacterSet is Nd alone, so that
		 * union would LOSE Nl and No (the Roman numerals among them). */
		set = fn_set_by_property(fn_cat_lmn, 11, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)punctuationCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* P*: the seven punctuation categories.  28 is over the ASCII range, because that is the
		 * point - the em dash and the curly quotes are punctuation too. */
		set = fn_set_by_property(fn_cat_p, 7, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)controlCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Cc AND Cf: the C0/C1 controls AND the format characters, which is where the soft hyphen
		 * (U+00AD), the byte-order mark and the joiners live. */
		set = fn_set_by_property(fn_cat_c, 2, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)lowercaseLetterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		static const int categories[] = { U_LOWERCASE_LETTER };

		set = fn_set_by_property(categories, 1, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)uppercaseLetterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Lu AND Lt, which is what makes +capitalizedLetterCharacterSet a SUBSET of this set -
		 * the relation the Latin-only version failed. */
		static const int categories[] = { U_UPPERCASE_LETTER, U_TITLECASE_LETTER };

		set = fn_set_by_property(categories, 2, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}


/*
 * THE NON-TABLE HALF OF THE TABLE GROUP (D7's kind (D)). This class stores a BMP range list, so
 * these are EXACT rather than approximate: a code point above the BMP is genuinely a non-member,
 * plane 0 is genuinely non-empty exactly when the set is, and any other plane is genuinely empty.
 *
 * THE BITMAP'S BYTE LAYOUT IS THIS LIBRARY'S and the ROUND TRIP is the contract - Apple documents
 * what the representation is for, not what is in it, which is the same standing as the byte-order
 * family and the alignment bits. A header holding a magic and the range count, then 8192 bytes of
 * BMP membership, which is what makes the file form meaningful: a file in this format is a character
 * set, and its representation is itself.
 */
#define FN_CHARSET_MAGIC 0x534e4346	/* 'FNCS', little-endian on disk */
#define FN_CHARSET_BITMAP_BYTES 8192	/* 65536 bits, the BMP */

- (BOOL)longCharacterIsMember:(UTF32Char)character
{
	if (character > 0xFFFF) {
		return NO;
	}
	return [self characterIsMember:(unichar)character];
}

- (BOOL)hasMemberInPlane:(uint8_t)plane
{
	return (plane == 0) && (_rangeCount > 0);
}

- (NSData *)bitmapRepresentation
{
	unsigned char *bytes = (unsigned char *)calloc(1, 8 + FN_CHARSET_BITMAP_BYTES);
	unsigned int header[2];
	NSData *result;
	unsigned long i;
	unsigned long offset;

	if (bytes == NULL) {
		return [NSData data];
	}
	header[0] = FN_CHARSET_MAGIC;
	header[1] = (unsigned int)_rangeCount;
	memcpy(bytes, header, 8);
	for (i = 0; i < _rangeCount; i++) {
		unsigned long loc = _ranges[i * 2];
		unsigned long len = _ranges[i * 2 + 1];
		unsigned long c;

		for (c = loc; c < loc + len && c <= 0xFFFF; c++) {
			bytes[8 + (c / 8)] |= (unsigned char)(1u << (c % 8));
		}
	}
	(void)offset;
	result = [NSData dataWithBytes:bytes length:8 + FN_CHARSET_BITMAP_BYTES];
	free(bytes);
	return result;
}

+ (NSCharacterSet *)characterSetWithBitmapRepresentation:(NSData *)data
{
	NSCharacterSet *set;
	unsigned int header[2];
	const unsigned char *bytes;
	unsigned long c;
	unsigned long start = 0;
	BOOL inRange = NO;

	if ([data length] < 8 + FN_CHARSET_BITMAP_BYTES) {
		return nil;
	}
	bytes = (const unsigned char *)[data bytes];
	memcpy(header, bytes, 8);
	if (header[0] != FN_CHARSET_MAGIC) {
		return nil;
	}
	/* A MUTABLE SET IS BUILT AND RETURNED: this method assembles ranges, and the immutable
	 * class has no -addCharactersInRange: — sending it one is an unrecognized selector, which is
	 * exactly the abort this probe caught. The answer is still an NSCharacterSet: the mutable
	 * class is its subclass. */
	set = [[NSMutableCharacterSet alloc] init];
	for (c = 0; c <= 0xFFFF; c++) {
		BOOL member = (bytes[8 + (c / 8)] & (1u << (c % 8))) != 0;

		if (member && !inRange) {
			start = c;
			inRange = YES;
		} else if (!member && inRange) {
			[(NSMutableCharacterSet *)set addCharactersInRange:NSMakeRange((NSUInteger)start, (NSUInteger)(c - start))];
			inRange = NO;
		}
	}
	if (inRange) {
		[(NSMutableCharacterSet *)set addCharactersInRange:NSMakeRange((NSUInteger)start, 0x10000 - (NSUInteger)start)];
	}
	return set;
}

+ (NSCharacterSet *)characterSetWithContentsOfFile:(NSString *)path
{
	NSData *data = [NSData dataWithContentsOfFile:path];

	return (data == nil) ? nil : [self characterSetWithBitmapRepresentation:data];
}

/*
 * illegalCharacterSet (D7's kind (D), the table group's rule-shaped member). The Unicode standard
 * DEFINES the noncharacters - FDD0..FDEF and the last two code points of each plane - and the
 * surrogates are not characters at all. On this BMP-only class that is four ranges, exactly, and no
 * table: the group's OTHER FOUR are the ICU-backed rules below (§15.5), and the "need general-category
 * data" reason this comment used to give was wrong - the data is already linked in.
 *
 * ONE BOUNDARY IS STATED RATHER THAN HIDDEN: the noncharacters at the end of the ASTRAL planes
 * (1FFFE/1FFFF and up) cannot be in a set that has no astral storage, and -longCharacterIsMember:
 * answers NO for everything above the BMP by construction. That is a property of the representation,
 * recorded in §11.6.1, not an omission here.
 */
+ (NSCharacterSet *)illegalCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];

		[built addCharactersInRange:NSMakeRange(0xD800, 0x800)];	/* the surrogates */
		[built addCharactersInRange:NSMakeRange(0xFDD0, 0x20)];		/* the noncharacters */
		[built addCharactersInRange:NSMakeRange(0xFFFE, 2)];		/* plane 0's last two */
		set = built;
	}
	return set;
}

/*
 * THE CATEGORY GROUPS THE PUBLISHED DEFINITIONS NAME, spelled once. Every built-in below is one of
 * these (or one of them plus a literal code point), so two sets cannot drift apart about what L* is.
 * The names are ICU's: U_OPEN_PUNCTUATION/U_CLOSE_PUNCTUATION are the parentheses/brackets.
 */
static const int fn_cat_l[] = {
	U_UPPERCASE_LETTER, U_LOWERCASE_LETTER, U_TITLECASE_LETTER, U_MODIFIER_LETTER, U_OTHER_LETTER
};
static const int fn_cat_m[] = {
	U_NON_SPACING_MARK, U_ENCLOSING_MARK, U_COMBINING_SPACING_MARK
};
static const int fn_cat_n[] = {
	U_DECIMAL_DIGIT_NUMBER, U_LETTER_NUMBER, U_OTHER_NUMBER
};
static const int fn_cat_s[] = {
	U_MATH_SYMBOL, U_CURRENCY_SYMBOL, U_MODIFIER_SYMBOL, U_OTHER_SYMBOL
};
static const int fn_cat_p[] = {
	U_CONNECTOR_PUNCTUATION, U_DASH_PUNCTUATION, U_START_PUNCTUATION, U_END_PUNCTUATION,
	U_INITIAL_PUNCTUATION, U_FINAL_PUNCTUATION, U_OTHER_PUNCTUATION
};
static const int fn_cat_z[] = {
	U_SPACE_SEPARATOR, U_LINE_SEPARATOR, U_PARAGRAPH_SEPARATOR
};
static const int fn_cat_c[] = {
	U_CONTROL_CHAR, U_FORMAT_CHAR
};
/* L* AND M*, and L*, M* AND N*: the two unions two published definitions name. */
static const int fn_cat_lm[] = {
	U_UPPERCASE_LETTER, U_LOWERCASE_LETTER, U_TITLECASE_LETTER, U_MODIFIER_LETTER, U_OTHER_LETTER,
	U_NON_SPACING_MARK, U_ENCLOSING_MARK, U_COMBINING_SPACING_MARK
};
static const int fn_cat_lmn[] = {
	U_UPPERCASE_LETTER, U_LOWERCASE_LETTER, U_TITLECASE_LETTER, U_MODIFIER_LETTER, U_OTHER_LETTER,
	U_NON_SPACING_MARK, U_ENCLOSING_MARK, U_COMBINING_SPACING_MARK,
	U_DECIMAL_DIGIT_NUMBER, U_LETTER_NUMBER, U_OTHER_NUMBER
};

/*
 * THE ICU-BACKED RULE SETS (§15.5, §16). APPLE DEFINES THESE FOUR BY PUBLISHED PROPERTIES, not by tables:
 *
 *   symbolCharacterSet           the characters in Unicode General Category S*
 *   capitalizedLetterCharacterSet the characters in Unicode General Category Lt
 *   nonBaseCharacterSet          the characters in Unicode General Category M*
 *   decomposableCharacterSet     characters representable as a composed sequence "by the definition of
 *                                STANDARD decomposition in version 3.2" of Unicode
 *
 * and ICU answers exactly those two things - the general category and the decomposition type - which
 * is why the register's claim that these needed DATA was wrong (§11.6.1 D7, corrected in §15.5): the
 * data was already linked into this library. ONE SCAN, TWO PROPERTIES: u_getIntPropertyValue() reads
 * the category for the first three and the decomposition type for the fourth, so the body below is
 * the same code with a different property and a different value set.
 *
 * BMP ONLY, THE SAME BOUNDARY illegalCharacterSet STATES: this class stores a BMP range list, so
 * 0..0xFFFF is the representation's whole domain and -longCharacterIsMember: answers NO above it. A
 * character whose membership is astral is therefore genuinely absent rather than wrongly so.
 *
 * AND THE MEMBERSHIP MOVES WITH UNICODE'S VERSION, which Apple's own guide says of the whole family:
 * the categories and the decomposition data are ICU's, so a code point whose property changes changes
 * its membership here. That is a property of the rule rather than a gap in it.
 *
 * `values` is a value set of `property`; the scan coalesces runs, so each set is a handful of ranges
 * and not one range per character.
 */
static NSMutableCharacterSet *fn_set_by_property(const int *values, unsigned int count, UProperty property)
{
	NSMutableCharacterSet *built = [[NSMutableCharacterSet alloc] init];
	unsigned int character;
	unsigned int runStart = 0;
	BOOL inRun = NO;

	for (character = 0; character <= FN_MAX_CHARACTER; character++) {
		int value = u_getIntPropertyValue((UChar32)character, property);
		BOOL member = NO;
		unsigned int i;

		for (i = 0; i < count; i++) {
			if (values[i] == value) {
				member = YES;
				break;
			}
		}
		if (member && !inRun) {
			runStart = character;
			inRun = YES;
		} else if (!member && inRun) {
			[built addCharactersInRange:NSMakeRange(runStart, character - runStart)];
			inRun = NO;
		}
	}
	if (inRun) {
		[built addCharactersInRange:NSMakeRange(runStart, FN_MAX_CHARACTER + 1 - runStart)];
	}
	return built;
}

+ (NSCharacterSet *)symbolCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		set = fn_set_by_property(fn_cat_s, 4, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)capitalizedLetterCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* Lt ALONE, which is what Apple says: NOT the uppercase letters. This set is small (about
		 * thirty titlecase digraphs) and it is a SUBSET of the Lu+Lt that
		 * +uppercaseLetterCharacterSet is specified to be. */
		static const int categories[] = { U_TITLECASE_LETTER };

		set = fn_set_by_property(categories, 1, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)nonBaseCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* M*: non-spacing, enclosing and combining-spacing marks. */
		set = fn_set_by_property(fn_cat_m, 3, UCHAR_GENERAL_CATEGORY);
	}
	return set;
}

+ (NSCharacterSet *)decomposableCharacterSet
{
	static NSCharacterSet *set = nil;

	if (set == nil) {
		/* U_DT_CANONICAL ALONE, which is the "standard decomposition" Apple's page names. A
		 * COMPATIBILITY or noBreak decomposition does NOT qualify - so U+00C0 (À, canonical) is a
		 * member and U+FB00 (the ff ligature) and U+00A0 (noBreak space) are not. That line is
		 * asserted in the probe rather than left to this comment. */
		static const int types[] = { U_DT_CANONICAL };

		set = fn_set_by_property(types, 1, UCHAR_DECOMPOSITION_TYPE);
	}
	return set;
}
@end

@implementation NSMutableCharacterSet

/* §C.3 item 4 FOR A PUBLIC SUBCLASS: NSMutableCharacterSet is public, so it names ITSELF - the
 * NSMutableAttributedString/NSDecimalNumber shape, not the unconditional one. */
- (Class)classForCoder
{
	return [NSMutableCharacterSet class];
}

/* §63.58: `+ (NSMutableCharacterSet *)characterSet` was here; its body was `[[self alloc] init]`, which is now
 * what every former caller writes. Gone on the user's decision (dec-d2dfbe0c080f14c2) because Apple's macOS
 * headers declare no bare `characterSet` on either class. */
- (void)addCharactersInString:(NSString *)string
{
	size_t i;

	for (i = 0; i < [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
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

	for (i = 0; i < [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
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
