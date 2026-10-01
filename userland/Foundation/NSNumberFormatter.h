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
 * WHAT IS DEFERRED, named here rather than discovered later:
 *   * the ATTRIBUTED-string doors (`-attributedStringForZero`, `-attributedStringForNil`,
 *     `-attributedStringForNotANumber`, and the seven `-textAttributesFor…` dictionaries), which
 *     belong to the attributed-string family and are AppKit-DRAWING shaped;
 *   * `-roundingBehavior`, whose Apple type is NSDecimalNumberHandler — a rounding POLICY object whose
 *     effect is not separable from the `-roundingMode`/`-roundingIncrement` ICU already carries, so
 *     it is named here rather than stored as a value that would change nothing;
 *   * `-formattingContext` (a capitalization hint whose only consumer would be the spell-out style),
 *     `-localizesFormat` and `-partialStringValidationEnabled` (deprecated flags with no distinct
 *     door);
 *   * `-positiveInfinitySymbol`/`-negativeInfinitySymbol`, because ICU carries ONE infinity symbol
 *     (`UNUM_INFINITY_SYMBOL`) and spells a negative infinity with the minus sign — there is no
 *     substrate for two distinct settable symbols;
 *
 * WHAT A LATER PASS LANDED, EACH ON A DEFERRAL REASON THAT MEASURED FALSE:
 *   * `-positiveFormat`/`-negativeFormat`. The old note claimed ICU exposes ONE pattern with no
 *     positive/negative split. FALSE: the DECIMAL PATTERN LANGUAGE spells the negative subpattern
 *     after a ';', and `unum_toPattern` returns it verbatim — MEASURED: opening
 *     "#,##0.00;(#,##0.00)" formats -1234.5 as "(1,234.50)". The two doors are VIEWS of the one
 *     pattern `-format` holds, not separate storage;
 *   * `-generatesDecimalNumbers`. The old note filed the decimal-typed answer under "NSDecimalNumber's
 *     own surface" as if this library had none; `NSDecimalNumber.h` EXISTS, so the flag is honoured
 *     by answering the parse with an NSDecimalNumber;
 *   * `-minimum`/`-maximum`: Apple's documented RANGE over the INPUT (the parse), now a real check;
 *   * the `-formatterBehavior` trio (`+defaultFormatterBehavior`, `+setDefaultFormatterBehavior:` and
 *     the instance `-formatterBehavior`), now a stored, reported, seedable value — see the
 *     declaration for the ONE deviation (only the 10.4 behavior selects a rendering).
 *
 * STORAGE: the ICU handle is an opaque `void *` for the same reason it is in NSDateFormatter — this
 * header is staged for an ON-GUEST Objective-C rebuild, and including <unicode/unum.h> here would
 * drag ICU's headers onto that guest.
 */

#ifndef FOUNDATION_NSNUMBERFORMATTER_H
#define FOUNDATION_NSNUMBERFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSObjCRuntime.h>

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

/* WHERE A PADDED NUMBER GOES (2026-09-20). Names from Apple's documentation index;
 * values are ours (§11.6.1 D2, see NSFileManager.h). The padding trio that uses this
 * is still deferred — see the note at the top of this file — so the type is here
 * ahead of its user, which is the same order Cocoa declares them in. */
typedef enum {
	NSNumberFormatterPadBeforePrefix = 0,
	NSNumberFormatterPadAfterPrefix = 1,
	NSNumberFormatterPadBeforeSuffix = 2,
	NSNumberFormatterPadAfterSuffix = 3
} NSNumberFormatterPadPosition;

typedef enum {
	NSNumberFormatterBehaviorDefault = 0,
	NSNumberFormatterBehavior10_0 = 1,
	NSNumberFormatterBehavior10_4 = 2
} NSNumberFormatterBehavior;

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
	NSNumberFormatterBehavior _behavior;	/* reported, seeded from the class default */
	NSNumber *_minimum;		/* OURS: the range over the parse (see the declaration) */
	NSNumber *_maximum;
	BOOL _generatesDecimalNumbers;	/* OURS: whether the parse answers an NSDecimalNumber */
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

/* THE FORMATTER BEHAVIOR. Apple's only surviving behavior is the modern NSNumberFormatterBehavior10_4,
 * which IS what this class implements: the class default seeds each new instance and the value is
 * reported. ONE DEVIATION, DECLARED: the legacy NSNumberFormatterBehavior10_0 is accepted and
 * reported but does NOT select a distinct rendering, because only the modern behavior is implemented
 * here. */
+ (NSNumberFormatterBehavior)defaultFormatterBehavior;
+ (void)setDefaultFormatterBehavior:(NSNumberFormatterBehavior)behavior;
- (NSNumberFormatterBehavior)formatterBehavior;
- (void)setFormatterBehavior:(NSNumberFormatterBehavior)behavior;
- (nullable NSString *)format;
- (void)setFormat:(nullable NSString *)pattern;
- (nullable NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)locale;

/* THE TWO HALVES OF THE ONE PATTERN. Apple's -positiveFormat/-negativeFormat are the positive and
 * negative SUBPATTERNS of `-format`: ICU's decimal pattern language spells the negative one after a
 * ';', so these two doors are VIEWS of the single pattern rather than separate storage. */
- (nullable NSString *)positiveFormat;
- (void)setPositiveFormat:(nullable NSString *)format;
- (nullable NSString *)negativeFormat;
- (void)setNegativeFormat:(nullable NSString *)format;

/* LENIENCY is about PARSING, as in NSDateFormatter. ALLOWS FLOATS is the other half of it: with
 * NO, a string with a fraction is refused rather than rounded into an integer. */
- (BOOL)isLenient;
- (void)setLenient:(BOOL)flag;
- (BOOL)allowsFloats;
- (void)setAllowsFloats:(BOOL)flag;

/* THE RANGE OVER THE INPUT: Apple's -minimum/-maximum are the lowest/highest number allowed as
 * INPUT, so a value the parse falls outside of is REFUSED (nil). They constrain the PARSE; the
 * spelling (grouping, digits) stays the style's business. */
- (nullable NSNumber *)minimum;
- (void)setMinimum:(nullable NSNumber *)number;
- (nullable NSNumber *)maximum;
- (void)setMaximum:(nullable NSNumber *)number;

/* -generatesDecimalNumbers: YES makes the parse answer an NSDecimalNumber rather than a plain
 * NSNumber (Apple's rule); NO, the default, answers a plain NSNumber. */
- (BOOL)generatesDecimalNumbers;
- (void)setGeneratesDecimalNumbers:(BOOL)flag;

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

/* GROUPING AND THE DECIMAL POINT. Every one of these is an ICU FORMAT ATTRIBUTE written straight
 * through and read straight back — there is no state here to drift out of step. The values are the
 * ones the style's DATA carries until a caller overrides them. */
- (BOOL)alwaysShowsDecimalSeparator;
- (void)setAlwaysShowsDecimalSeparator:(BOOL)flag;
- (NSInteger)groupingSize;
- (void)setGroupingSize:(NSInteger)size;
- (NSInteger)secondaryGroupingSize;
- (void)setSecondaryGroupingSize:(NSInteger)size;
/* ⚠ `-minimumGroupingDigits` / `-setMinimumGroupingDigits:` WERE HERE AND ARE GONE (§63.57, user decision
 * dec-d2dfbe0c080f14c2). Measured against the complete macOS 14.5 corpus: `NSNumberFormatter.h` holds
 * `groupingSize`, `secondaryGroupingSize`, `usesGroupingSeparator` and `currencyGroupingSeparator`, and holds
 * NO `minimumGroupingDigits` — the name exists in the iOS surface only, so it fails the fidelity bar (§11) on
 * the platform this library declares. A near-neighbour grep is what settles it: the FAMILY is present and the
 * NAME is not, which is the signature of an extra rather than a removal. */

/* SIGNIFICANT DIGITS. `usesSignificantDigits` switches ICU's digit policy from the fraction-digit
 * limits above to the significant-digit limits below; the two limits are the min/max count. */
- (BOOL)usesSignificantDigits;
- (void)setUsesSignificantDigits:(BOOL)flag;
- (NSUInteger)minimumSignificantDigits;
- (void)setMinimumSignificantDigits:(NSUInteger)digits;
- (NSUInteger)maximumSignificantDigits;
- (void)setMaximumSignificantDigits:(NSUInteger)digits;

/* ROUNDING INCREMENT is ICU's only DOUBLE-valued number attribute, so it goes through the
 * double-attribute door rather than the int one the rest of this file's attributes use. A nil (or
 * zero) increment means "round to the fraction digits" — the ordinary behaviour. */
- (nullable NSNumber *)roundingIncrement;
- (void)setRoundingIncrement:(nullable NSNumber *)increment;

/* THE MONETARY AND PER-MILL SYMBOLS: ICU symbol attributes like the ones above. The two "currency"
 * separators are ICU's MONETARY separator pair — the separators a currency style uses — which is why
 * they are separate from -decimalSeparator/-groupingSeparator rather than aliases of them. */
- (nullable NSString *)perMillSymbol;
- (void)setPerMillSymbol:(nullable NSString *)string;
- (nullable NSString *)currencyDecimalSeparator;
- (void)setCurrencyDecimalSeparator:(nullable NSString *)string;
- (nullable NSString *)currencyGroupingSeparator;
- (void)setCurrencyGroupingSeparator:(nullable NSString *)string;

/* THE DEPRECATED SPELLINGS Cocoa kept. Each is an ALIAS of the modern door beside it — setting one is
 * seen by the other, which is exactly how a deprecated alias has to behave. */
- (BOOL)hasThousandSeparators;
- (void)setHasThousandSeparators:(BOOL)flag;
- (nullable NSString *)thousandSeparator;
- (void)setThousandSeparator:(nullable NSString *)string;

/* THE PADDING TRIO: how a number too short is padded out to -formatWidth. ICU carries all three (the
 * width, the pad position and the pad escape character), and Apple's NSNumberFormatterPadPosition
 * values ARE ICU's UNumberFormatPadPosition values, so the enum maps by identity through a named
 * helper rather than a cast that would hide the dependence. */
- (NSUInteger)formatWidth;
- (void)setFormatWidth:(NSUInteger)width;
- (NSNumberFormatterPadPosition)paddingPosition;
- (void)setPaddingPosition:(NSNumberFormatterPadPosition)position;
- (nullable NSString *)paddingCharacter;
- (void)setPaddingCharacter:(nullable NSString *)string;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNUMBERFORMATTER_H */
