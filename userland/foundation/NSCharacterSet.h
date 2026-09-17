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
