/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNumberFormatter — a number as text, and text as a number, in a locale's own conventions.
 * docs/design/foundation-plan.md §10 (the un-refusal program), slice F13.7c.
 *
 * THE SECOND DATA FAMILY F7 REFUSED, and the same reason and the same answer as NSDateFormatter: a
 * number's separators, digits, currency placement and rounding conventions are DATA, ICU ships it,
 * and NOTHING IN THIS FILE ENCODES A FORMAT.
 *
 * ICU's number formats cover Apple's spectrum directly, so the whole style enum maps onto them:
 * decimal, currency, percent, scientific, SPELL-OUT (ICU's rule-based number spelling), ordinal,
 * and the three currency variants (ISO code, plural, accounting). This is one of the places where
 * binding ICU buys fidelity that a hand-written formatter could not reach at all.
 *
 * WHAT IS DEFERRED, named here rather than discovered later: the `NSDecimalNumber` doors
 * (`-generatesDecimalNumbers` and the `-numberFromString:` answer's type) — this library has no
 * NSDecimalNumber, so a parse answers an NSNumber, which is also what Apple does when the flag is
 * off; the ATTRIBUTED-string doors (`-attributedStringForZero`, `-attributedStringForString:` and
 * friends), which belong to the attributed-string family; the padding trio
 * (`-formatWidth`/`-paddingCharacter`/`-paddingPosition`); and the deprecated
 * `-formatterBehavior` pair, whose only surviving value is the modern behaviour this class already
 * implements.
 *
 * STORAGE: the ICU handle is an opaque `void *` for the same reason it is in NSDateFormatter — this
 * header is staged for an ON-GUEST Objective-C rebuild, and including <unicode/unum.h> here would
 * drag ICU's headers onto that guest.
 */

#ifndef FOUNDATION_NSNUMBERFORMATTER_H
#define FOUNDATION_NSNUMBERFORMATTER_H

#import <foundation/NSFormatter.h>
#import <foundation/NSObjCRuntime.h>

@class NSString;
@class NSNumber;
@class NSLocale;

/* Apple's styles, and the raw values are APPLE'S (7 is not used, in Cocoa either). */
typedef enum {
	NSNumberFormatterNoStyle = 0,
	NSNumberFormatterDecimalStyle = 1,
	NSNumberFormatterCurrencyStyle = 2,
	NSNumberFormatterPercentStyle = 3,
	NSNumberFormatterScientificStyle = 4,
	NSNumberFormatterSpellOutStyle = 5,
	NSNumberFormatterOrdinalStyle = 6,
	NSNumberFormatterCurrencyISOCodeStyle = 8,
	NSNumberFormatterCurrencyPluralStyle = 9,
	NSNumberFormatterCurrencyAccountingStyle = 10
} NSNumberFormatterStyle;

/* Apple's rounding modes, and ICU has one for each. */
typedef enum {
	NSNumberFormatterRoundCeiling = 0,
	NSNumberFormatterRoundFloor = 1,
	NSNumberFormatterRoundDown = 2,
	NSNumberFormatterRoundUp = 3,
	NSNumberFormatterRoundHalfEven = 4,
	NSNumberFormatterRoundHalfDown = 5,
	NSNumberFormatterRoundHalfUp = 6
} NSNumberFormatterRoundingMode;

NS_ASSUME_NONNULL_BEGIN

@interface NSNumberFormatter : NSFormatter
{
	void *_formatter;		/* a UNumberFormat *; opaque so this header needs no ICU */
	NSNumberFormatterStyle _style;	/* ICU's formatter does not report its own style */
	NSString *_pattern;		/* set through -setFormat:; a pattern overrides the style */
	NSLocale *_locale;
	BOOL _allowsFloats;		/* OURS: a rule over the parse, not an ICU attribute */
	NSString *_zeroSymbol;		/* OURS: ICU has no symbol for a zero VALUE */
	NSString *_nilSymbol;		/* OURS: ICU has no symbol for "no value at all" */
}

- (instancetype)init;

/* One number, one call, no formatter to keep — Apple's convenience. */
+ (nullable NSString *)localizedStringFromNumber:(NSNumber *)number
				     numberStyle:(NSNumberFormatterStyle)style;

/* BOTH DIRECTIONS. `-numberFromString:` answers nil for text that is not a number, and honours
 * -isLenient and -allowsFloats the way Cocoa describes. */
- (nullable NSString *)stringFromNumber:(NSNumber *)number;
- (nullable NSNumber *)numberFromString:(NSString *)string;

- (NSNumberFormatterStyle)numberStyle;
- (void)setNumberStyle:(NSNumberFormatterStyle)style;
- (nullable NSString *)format;
- (void)setFormat:(nullable NSString *)pattern;
- (nullable NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)locale;

/* LENIENCY is about PARSING, as in NSDateFormatter. ALLOWS FLOATS is the other half of it: with
 * NO, a string with a fraction is refused rather than rounded into an integer. */
- (BOOL)isLenient;
- (void)setLenient:(BOOL)flag;
- (BOOL)allowsFloats;
- (void)setAllowsFloats:(BOOL)flag;

- (NSUInteger)minimumIntegerDigits;
- (void)setMinimumIntegerDigits:(NSUInteger)digits;
/* -1 (the default) means "no limit", as in Cocoa. */
- (NSInteger)maximumIntegerDigits;
- (void)setMaximumIntegerDigits:(NSInteger)digits;
- (NSUInteger)minimumFractionDigits;
- (void)setMinimumFractionDigits:(NSUInteger)digits;
- (NSUInteger)maximumFractionDigits;
- (void)setMaximumFractionDigits:(NSUInteger)digits;

- (BOOL)usesGroupingSeparator;
- (void)setUsesGroupingSeparator:(BOOL)flag;
- (NSNumberFormatterRoundingMode)roundingMode;
- (void)setRoundingMode:(NSNumberFormatterRoundingMode)mode;
- (nullable NSNumber *)multiplier;
- (void)setMultiplier:(nullable NSNumber *)multiplier;

/* THE SYMBOLS, each read from and written to the formatter's data. */
- (nullable NSString *)decimalSeparator;
- (void)setDecimalSeparator:(nullable NSString *)string;
- (nullable NSString *)groupingSeparator;
- (void)setGroupingSeparator:(nullable NSString *)string;
- (nullable NSString *)percentSymbol;
- (void)setPercentSymbol:(nullable NSString *)string;
- (nullable NSString *)minusSign;
- (void)setMinusSign:(nullable NSString *)string;
- (nullable NSString *)plusSign;
- (void)setPlusSign:(nullable NSString *)string;
- (nullable NSString *)exponentSymbol;
- (void)setExponentSymbol:(nullable NSString *)string;
- (nullable NSString *)currencySymbol;
- (void)setCurrencySymbol:(nullable NSString *)string;
- (nullable NSString *)currencyCode;
- (void)setCurrencyCode:(nullable NSString *)string;
- (nullable NSString *)internationalCurrencySymbol;
- (void)setInternationalCurrencySymbol:(nullable NSString *)string;
/* The string this formatter answers for a ZERO value, and for a nil one. */
- (nullable NSString *)zeroSymbol;
- (void)setZeroSymbol:(nullable NSString *)string;
- (nullable NSString *)notANumberSymbol;
- (void)setNotANumberSymbol:(nullable NSString *)string;
- (nullable NSString *)nilSymbol;
- (void)setNilSymbol:(nullable NSString *)string;

/* The affixes around the number, which is where a locale's sign and currency conventions live. */
- (nullable NSString *)positivePrefix;
- (void)setPositivePrefix:(nullable NSString *)string;
- (nullable NSString *)positiveSuffix;
- (void)setPositiveSuffix:(nullable NSString *)string;
- (nullable NSString *)negativePrefix;
- (void)setNegativePrefix:(nullable NSString *)string;
- (nullable NSString *)negativeSuffix;
- (void)setNegativeSuffix:(nullable NSString *)string;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNUMBERFORMATTER_H */
