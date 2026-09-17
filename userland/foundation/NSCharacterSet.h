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

@interface NSCharacterSet : NSObject <NSCopying>
{
	unsigned int *_ranges;		/* pairs of (location, length) in code units */
	unsigned long _rangeCount;
	unsigned long _capacity;
}

+ (NSCharacterSet *)characterSetWithCharactersInString:(NSString *)string;
+ (NSCharacterSet *)characterSetWithRange:(NSRange)range;

/* The built-ins the string methods and ordinary callers reach for. */
+ (NSCharacterSet *)whitespaceCharacterSet;
+ (NSCharacterSet *)whitespaceAndNewlineCharacterSet;
+ (NSCharacterSet *)newlineCharacterSet;
+ (NSCharacterSet *)decimalDigitCharacterSet;
+ (NSCharacterSet *)letterCharacterSet;
+ (NSCharacterSet *)alphanumericCharacterSet;
+ (NSCharacterSet *)punctuationCharacterSet;
+ (NSCharacterSet *)controlCharacterSet;
+ (NSCharacterSet *)lowercaseLetterCharacterSet;
+ (NSCharacterSet *)uppercaseLetterCharacterSet;

- (id)initWithCharactersInString:(NSString *)string;
- (id)initWithRange:(NSRange)range;

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

@end

#endif /* FOUNDATION_NSCHARACTERSET_H */
