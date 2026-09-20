/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCharacterSet — a set of characters, for the string methods that take one.
 * docs/design/foundation-plan.md, the dependency queue.
 *
 * A SET OF UNICHAR RANGES, held as (location, length) pairs and scanned linearly:
 * with a handful of ranges a scan beats a bitmap, and the representation stays
 * small enough to read. The built-in sets are ASCII (plus the C0 whitespace
 * controls), and that is stated rather than implied — this Foundation makes no
 * Unicode character-class claims, exactly as it makes none about case.
 *
 * It exists because three NSString methods are specified in terms of it
 * (-rangeOfCharacterFromSet:, -componentsSeparatedByCharactersInSet: and
 * -stringByTrimmingCharactersInSet:), and a named dependency is the work queue.
 */

#ifndef FOUNDATION_NSCHARACTERSET_H
#define FOUNDATION_NSCHARACTERSET_H

#import <foundation/NSObject.h>

@class NSString;
@class NSData;	/* the bitmap representation's type */

/* NULLABILITY (F6, the last slice): NONNULL by default, and the exceptions are
 * MEASURED or inherited by PROPAGATION:
 *   - -initWithCharactersInString: and -initWithRange: are the two `return nil;`
 *     sites in ncharacterset.m;
 *   - +characterSetWithCharactersInString:/+characterSetWithRange: are
 *     `return [[self alloc] initWith...]`, so they inherit that;
 *   - so do the TEN BUILT-INS, and that is worth stating because the shape is easy
 *     to miss: each is `static NSCharacterSet *set = nil; if (set == nil) { set =
 *     [[NSCharacterSet alloc] initWithRange:...]; } return set;` — a cache, but the
 *     thing it caches is a constructor's result, so a failed build answers nil; the
 *     same reading that made +data: nullable in slice 3;
 *   - -invertedSet is NOT nullable: it builds from the receiver's own ranges. */
/* Apple spells the 32-bit code point type UTF32Char, and this library had no such name: the API
 * surface is the specification (§11), so the type is defined rather than approximated with
 * `unsigned int`, which would be a difference a consumer could see. */
#include <stdint.h>	/* uint8_t, for -hasMemberInPlane: */
typedef unsigned int UTF32Char;

NS_ASSUME_NONNULL_BEGIN

@interface NSCharacterSet : NSObject <NSCopying>
{
	unsigned int *_ranges;		/* pairs of (location, length) in code units */
	unsigned long _rangeCount;
	unsigned long _capacity;
}

+ (nullable NSCharacterSet *)characterSetWithCharactersInString:(NSString *)string;
+ (nullable NSCharacterSet *)characterSetWithRange:(NSRange)range;

/* The built-ins the string methods and ordinary callers reach for. */
+ (nullable NSCharacterSet *)whitespaceCharacterSet;
+ (nullable NSCharacterSet *)whitespaceAndNewlineCharacterSet;
+ (nullable NSCharacterSet *)newlineCharacterSet;
+ (nullable NSCharacterSet *)decimalDigitCharacterSet;
+ (nullable NSCharacterSet *)letterCharacterSet;
/* THE RULE-SHAPED ONE OF THE FIVE (D7's kind (D)), and the first to land: a DEFINITION from the
 * standard - the surrogates plus the noncharacters - so it is a rule, not a table. */
+ (nullable NSCharacterSet *)illegalCharacterSet;
/* THE OTHER FOUR, WHICH LANDED IN §15.5 - AND THE "NEEDS TABLES" CLAIM WAS WRONG, which is why they
 * waited: Apple defines three of them by Unicode GENERAL CATEGORY (S*, Lt, M*) and the fourth by
 * Unicode 3.2's STANDARD DECOMPOSITION, and ICU - already linked into this library for five of its
 * files, before this one - answers exactly those properties. So each is a ONE-PASS RULE over data
 * that was already in the image, and the register's correction is recorded at the implementations. */
+ (nullable NSCharacterSet *)symbolCharacterSet;
+ (nullable NSCharacterSet *)capitalizedLetterCharacterSet;
+ (nullable NSCharacterSet *)nonBaseCharacterSet;
+ (nullable NSCharacterSet *)decomposableCharacterSet;
+ (nullable NSCharacterSet *)alphanumericCharacterSet;
+ (nullable NSCharacterSet *)punctuationCharacterSet;
+ (nullable NSCharacterSet *)controlCharacterSet;
+ (nullable NSCharacterSet *)lowercaseLetterCharacterSet;
+ (nullable NSCharacterSet *)uppercaseLetterCharacterSet;

- (nullable id)initWithCharactersInString:(NSString *)string;
- (nullable id)initWithRange:(NSRange)range;

- (BOOL)characterIsMember:(unichar)character;
- (NSCharacterSet *)invertedSet;
- (BOOL)isSupersetOfSet:(NSCharacterSet *)other;
- (BOOL)isEqualToCharacterSet:(NSCharacterSet *)other;


/* FIVE MORE (D7's kind (D), the table group's non-table half). The class stores a BMP range list, so
 * membership ABOVE the BMP is exactly NO rather than unknown, and plane 0 is exactly "non-empty".
 * The bitmap representation's BYTE LAYOUT IS THIS LIBRARY'S - Apple documents what it is FOR, not
 * what is in it - and the ROUND TRIP is the contract, exactly as it is for the byte-order family
 * (§14.3): a header naming the library and the range count, then the 8192-byte BMP map. */
- (BOOL)longCharacterIsMember:(UTF32Char)character;
- (BOOL)hasMemberInPlane:(uint8_t)plane;
- (NSData *)bitmapRepresentation;
+ (nullable NSCharacterSet *)characterSetWithBitmapRepresentation:(NSData *)data;
+ (nullable NSCharacterSet *)characterSetWithContentsOfFile:(NSString *)path;
@end

@interface NSMutableCharacterSet : NSCharacterSet

+ (NSMutableCharacterSet *)characterSet;

- (void)addCharactersInString:(NSString *)string;
- (void)addCharactersInRange:(NSRange)range;
- (void)removeCharactersInString:(NSString *)string;
- (void)removeCharactersInRange:(NSRange)range;
- (void)invert;
- (void)formUnionWithCharacterSet:(NSCharacterSet *)other;
- (void)formIntersectionWithCharacterSet:(NSCharacterSet *)other;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSCHARACTERSET_H */
