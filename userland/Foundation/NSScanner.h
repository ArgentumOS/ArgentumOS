/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSScanner — a cursor over a string that reads ONE THING at a time and leaves the position where it stopped.
 * §62.20 of docs/design/foundation-plan.md; the last open name in the Pattern Matching family.
 *
 * IT IS THE THIRD MEMBER OF THAT FAMILY AND THE ONE THAT IS NOT A MATCHER: `NSRegularExpression` finds every
 * occurrence of a pattern, `NSTextCheckingResult` is what it answers with, and this reads a string IN ORDER —
 * an integer here, a word there — which is what a parser or a config reader wants instead of a pattern per
 * field. The string family's own `-intValue` and `-doubleValue` are the same job done blind (they take the
 * whole string); a scanner is told where to stop and where it stopped.
 *
 * THE WHOLE CONTRACT IS THE POSITION, so the rules that move it are written here once:
 *
 *   * THE CHARACTERS TO BE SKIPPED (whitespace and newlines by default) are skipped BEFORE an element is examined and
 *     never ONCE ONE IS BEING SCANNED. So `-scanInt:` on `"  42"` answers 42 and leaves the position at 4,
 *     while `-scanCharactersFromSet:` with the DIGITS set on the same text answers 42 too — the skip ran
 *     first. And a set that is COVERED BY the skip set scans nothing at all, which is the case Apple's own
 *     page names ("if you scan for something made of characters in the set to be skipped"): there is nothing
 *     left to scan because the skip already ate it;
 *   * THE SKIPPED CHARACTERS ARE MATCHED AS SINGLE VALUES and case detection does not apply to them — so they
 *     are NOT folded with `-caseSensitive` and a composed character can never be one of them. That is why the
 *     skip uses `-characterIsMember:` per UTF-16 unit rather than any string comparison;
 *   * `-scanLocation` IS AN INPUT: it may be set BACKWARD to rescan, which is Apple's stated reason for the
 *     property ("useful for backing up to rescan after an error"). Setting it PAST THE END raises
 *     `NSRangeException` — Apple's contract, and the only raise on this class;
 *   * `-isAtEnd` IGNORES the characters that would be skipped (whitespace left at the tail means the END) and
 *     DOES NOT MOVE the position, because a question is not a scan;
 *   * A SCAN THAT FAILS MOVES NOTHING, and answers NO. `-scanString:intoString:` and `-scanUpToString:intoString:`
 *     both leave the position alone when they fail — including the case where the search string is the FIRST
 *     thing at the position (`-scanUpToString:` then scans an empty run, which is not a scan);
 *   * AND EVERY SCAN TAKES `NULL` AS ITS RESULT, which is how a caller says "skip past this without giving me
 *     the text". Apple documents that for every one of them.
 *
 * THE NUMERIC GRAMMARS ARE OURS (§11.6.1 D2), AND THEY HAVE TO BE STATED BECAUSE APPLE PUBLISHES NO GRAMMAR:
 * every numeric page says what it returns and that "overflow is considered a valid representation", and not
 * one of them says what a representation IS. What this class accepts, and the one place each choice shows:
 *
 *   * DECIMAL INTEGERS (`-scanInt:`, `-scanInteger:`, `-scanLongLong:`, `-scanUnsignedLongLong:`) are digits
 *     with an OPTIONAL LEADING SIGN — except `-scanUnsignedLongLong:`, which REFUSES a `-`, since the method
 *     exists to reach the values a signed long long cannot hold;
 *   * AN OVERFLOWING RUN IS VALID AND THE POSITION MOVES PAST ALL OF IT (Apple says both). The VALUE is the
 *     accumulator taken in the requested width — i.e. it WRAPS, which is what a C accumulator does and what
 *     Apple's "valid" implies without publishing a number;
 *   * DECIMAL FLOATS (`-scanDouble:`, `-scanFloat:`) are an optional sign, digits, an optional fraction and an
 *     optional `e`/`E` exponent — AT LEAST ONE DIGIT in total. THE DIGITS ARE REQUIRED, so the IEEE spellings
 *     of infinity and NaN are NOT scanned: Apple's page says the scanner skips past "excess digits" and
 *     publishes no test for those words, and a scanner that answered YES to `"inf"` would be inventing one;
 *   * HEX INTEGERS take an optional `0x`/`0X`; HEX FLOATS **REQUIRE** IT, which is Apple's own sentence ("the
 *     hexadecimal float representation must be preceded by `0x` or `0X`") and so is a contract rather than a
 *     choice. A hex float is read by the C library's own C99 hex-float rule (`0x1.8p1` is 3.0), because
 *     re-implementing IEEE-754 hexadecimal rounding here would be a second answer to a settled question;
 *   * `locale` AFFECTS ONE THING AND IT IS THE DECIMAL SEPARATOR, which is exactly what Apple's page for the
 *     property says ("a scanner uses the locale's decimal separator to distinguish the integer and fractional
 *     parts"). The separator is read from ICU by the locale's identifier — THE SAME ROUTE `NSNumberFormatter`
 *     ALREADY USES for the same fact, so the two agree by construction — and this library's `NSLocale` cannot
 *     answer it through `-objectForKey:` because it has no locale database (its own header says so), which is
 *     why the lookup goes to ICU rather than through the object. WITH NO LOCALE SET, SCANNING IS
 *     NON-LOCALIZED, which is Apple's sentence too ("a scanner with no locale set uses non-localized values"),
 *     so `"3,5"` scans as 3 and stops at the comma until a locale says otherwise;
 *   * `-scanDecimal:` takes the same decimal grammar and builds the NSDecimal ITSELF (this tree has no
 *     string→NSDecimal parser, and Apple's `NSDecimalFromString` is deprecated). TWO RULES ARE OURS AND ARE
 *     NAMED AT THE METHOD: digits beyond the 38 the format holds are TRUNCATED toward zero with the exponent
 *     raised to match (the only thing a fixed-width mantissa can do), and an exponent outside the
 *     representation's −128…127 CLAMPS — which is the saturation rule `NSDecimal.h` already documents for the
 *     arithmetic rather than a second convention invented here.
 *
 * WHAT IS NOT HERE, NAMED RATHER THAN LEFT SILENT: the SWIFT-ONLY members Apple documents beside the ObjC ones
 * — `scanCharacter()`, `currentIndex`, and the `scanInt(representation:)`/`scanDouble(representation:)` family
 * that takes a `Scanner.NumberRepresentation` — are excluded by §11.5's swift-only ground, exactly as every
 * other Swift-only overload in this library is. There is no `-initWithCoder:` either: `NSScanner` conforms to
 * `NSCopying` on Apple's page and to no coding protocol.
 */

#ifndef FOUNDATION_NSSCANNER_H
#define FOUNDATION_NSSCANNER_H

#import <Foundation/NSDecimal.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

@class NSCharacterSet;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSScanner : NSObject <NSCopying>
{
	NSString *_string;
	NSUInteger _location;
	BOOL _caseSensitive;
	NSCharacterSet *_skipSet;
	id _locale;
	NSString *_decimalSeparator;	/* the locale's separator, resolved once in -setLocale: */
}

+ (instancetype)scannerWithString:(NSString *)string;

/* APPLE'S DECLARATION ANSWERS `id` HERE AND THIS ONE KEEPS IT: `instancetype` would be narrower, and code
 * written against Apple's header that assigns the answer to something else must keep compiling. */
+ (id)localizedScannerWithString:(NSString *)string;

- (instancetype)initWithString:(NSString *)string;

@property (readonly, copy) NSString *string;
@property NSUInteger scanLocation;
@property BOOL caseSensitive;
@property (copy, nullable) NSCharacterSet *charactersToBeSkipped;
@property (retain, nullable) id locale;
@property (readonly, getter=isAtEnd) BOOL atEnd;

- (BOOL)scanCharactersFromSet:(NSCharacterSet *)set intoString:(NSString * _Nullable * _Nullable)result;
- (BOOL)scanUpToCharactersFromSet:(NSCharacterSet *)set intoString:(NSString * _Nullable * _Nullable)result;
- (BOOL)scanString:(NSString *)string intoString:(NSString * _Nullable * _Nullable)result;
- (BOOL)scanUpToString:(NSString *)string intoString:(NSString * _Nullable * _Nullable)result;

- (BOOL)scanDecimal:(NSDecimal *)dcm;
- (BOOL)scanDouble:(double * _Nullable)result;
- (BOOL)scanFloat:(float * _Nullable)result;
- (BOOL)scanHexDouble:(double * _Nullable)result;
- (BOOL)scanHexFloat:(float * _Nullable)result;
- (BOOL)scanHexInt:(unsigned int * _Nullable)result;
- (BOOL)scanHexLongLong:(unsigned long long * _Nullable)result;
- (BOOL)scanInteger:(NSInteger * _Nullable)result;
- (BOOL)scanInt:(int * _Nullable)result;
- (BOOL)scanLongLong:(long long * _Nullable)result;
- (BOOL)scanUnsignedLongLong:(unsigned long long * _Nullable)result;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSCANNER_H */
