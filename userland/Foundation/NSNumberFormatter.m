/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNumberFormatter.m — the number formatter, on ICU (F13.7c).
 *
 * MANUAL OWNERSHIP. The second file of the value-to-text family, and the same two rules apply as in
 * NSDateFormatter.m:
 *
 *   * WHAT ICU CAN HOLD, ICU HOLDS. Only -setNumberStyle:, -setLocale: and -setFormat: rebuild the
 *     underlying formatter; every other setting is an ICU ATTRIBUTE written straight through and
 *     read straight back. That is why there is no mirrored state to drift out of step, and why
 *     -minimumFractionDigits and friends answer what the DATA says rather than what this file
 *     remembers saying.
 *
 *   * THREE THINGS ARE OURS, because ICU has no notion of them: `_zeroSymbol` (ICU's
 *     UNUM_ZERO_DIGIT_SYMBOL is the digit used INSIDE a number, not the string for a zero VALUE),
 *     `_nilSymbol` (no ICU symbol means "no value at all"), and `_allowsFloats` (a rule over the
 *     parse — with it NO, text carrying a fraction is refused rather than rounded away).
 *
 * THE BORROWED-BUFFER RULE (§6) applies to every conversion: a locale identifier is short, so its
 * -UTF8String bytes are a decode scratch — consumed immediately, never held.
 */

#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSDecimalNumber.h>

#include <unicode/unum.h>
#include <unicode/ustring.h>
#include <math.h>
#include <string.h>

#define FN_NF_MAX 256

/* The CLASS default behavior a new instance is seeded from. Apple's modern default is 10.4 (the only
 * behavior this class implements); +setDefaultFormatterBehavior: changes it for later instances. */
static NSNumberFormatterBehavior fn_nf_default_behavior = NSNumberFormatterBehavior10_4;

/* Apple's style -> ICU's. if/ELSE rather than a switch: an exhaustive switch makes the fall-through
 * unreachable, and a bad value then becomes an undiagnosable illegal instruction (F11a's lesson).
 *
 * Worth noticing how much fidelity this ONE mapping buys: spell-out numbers, ordinals and the three
 * currency variants are ICU formats this library could never have written from rules. */
static UNumberFormatStyle fn_nf_style(NSNumberFormatterStyle style)
{
	if (style == NSNumberFormatterCurrencyStyle) {
		return UNUM_CURRENCY;
	}
	if (style == NSNumberFormatterPercentStyle) {
		return UNUM_PERCENT;
	}
	if (style == NSNumberFormatterScientificStyle) {
		return UNUM_SCIENTIFIC;
	}
	if (style == NSNumberFormatterSpellOutStyle) {
		return UNUM_SPELLOUT;
	}
	if (style == NSNumberFormatterOrdinalStyle) {
		return UNUM_ORDINAL;
	}
	if (style == NSNumberFormatterCurrencyISOCodeStyle) {
		return UNUM_CURRENCY_ISO;
	}
	if (style == NSNumberFormatterCurrencyPluralStyle) {
		return UNUM_CURRENCY_PLURAL;
	}
	if (style == NSNumberFormatterCurrencyAccountingStyle) {
		return UNUM_CURRENCY_ACCOUNTING;
	}
	/* NoStyle and DecimalStyle both mean "plain decimal" to the formatter; -numberStyle still
	 * reports which of the two was asked for. */
	return UNUM_DECIMAL;
}

static int32_t fn_nf_rounding(NSNumberFormatterRoundingMode mode)
{
	if (mode == NSNumberFormatterRoundCeiling) {
		return UNUM_ROUND_CEILING;
	}
	if (mode == NSNumberFormatterRoundFloor) {
		return UNUM_ROUND_FLOOR;
	}
	if (mode == NSNumberFormatterRoundDown) {
		return UNUM_ROUND_DOWN;
	}
	if (mode == NSNumberFormatterRoundUp) {
		return UNUM_ROUND_UP;
	}
	if (mode == NSNumberFormatterRoundHalfEven) {
		return UNUM_ROUND_HALFEVEN;
	}
	if (mode == NSNumberFormatterRoundHalfDown) {
		return UNUM_ROUND_HALFDOWN;
	}
	if (mode == NSNumberFormatterRoundHalfUp) {
		return UNUM_ROUND_HALFUP;
	}
	return UNUM_ROUND_HALFEVEN;
}

static NSNumberFormatterRoundingMode fn_nf_rounding_back(int32_t mode)
{
	if (mode == UNUM_ROUND_CEILING) {
		return NSNumberFormatterRoundCeiling;
	}
	if (mode == UNUM_ROUND_FLOOR) {
		return NSNumberFormatterRoundFloor;
	}
	if (mode == UNUM_ROUND_DOWN) {
		return NSNumberFormatterRoundDown;
	}
	if (mode == UNUM_ROUND_UP) {
		return NSNumberFormatterRoundUp;
	}
	if (mode == UNUM_ROUND_HALFDOWN) {
		return NSNumberFormatterRoundHalfDown;
	}
	if (mode == UNUM_ROUND_HALFUP) {
		return NSNumberFormatterRoundHalfUp;
	}
	return NSNumberFormatterRoundHalfEven;
}

/* Apple's pad position -> ICU's. The two enums happen to share their order, but the mapping is
 * spelled out rather than cast so the dependence is visible and a reordering on either side is a
 * compile-time name error instead of a silent behaviour change. */
static UNumberFormatPadPosition fn_nf_pad(NSNumberFormatterPadPosition position)
{
	if (position == NSNumberFormatterPadAfterPrefix) {
		return UNUM_PAD_AFTER_PREFIX;
	}
	if (position == NSNumberFormatterPadBeforeSuffix) {
		return UNUM_PAD_BEFORE_SUFFIX;
	}
	if (position == NSNumberFormatterPadAfterSuffix) {
		return UNUM_PAD_AFTER_SUFFIX;
	}
	return UNUM_PAD_BEFORE_PREFIX;
}

static NSNumberFormatterPadPosition fn_nf_pad_back(UNumberFormatPadPosition position)
{
	if (position == UNUM_PAD_AFTER_PREFIX) {
		return NSNumberFormatterPadAfterPrefix;
	}
	if (position == UNUM_PAD_BEFORE_SUFFIX) {
		return NSNumberFormatterPadBeforeSuffix;
	}
	if (position == UNUM_PAD_AFTER_SUFFIX) {
		return NSNumberFormatterPadAfterSuffix;
	}
	return NSNumberFormatterPadBeforePrefix;
}

/* UTF-16 out of ICU, an NSString in. */
static NSString *fn_nf_utf8_string(const UChar *text, int32_t length)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	char bytes[FN_NF_MAX * 4];

	if (length <= 0) {
		return nil;
	}
	bytes[0] = 0;
	u_strToUTF8(bytes, sizeof bytes, &used, text, length, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	return [NSString stringWithUTF8String:bytes];
}

@implementation NSNumberFormatter

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_style = NSNumberFormatterNoStyle;
		_locale = nil;
		_allowsFloats = YES;		/* Cocoa's default */
		_zeroSymbol = nil;
		_nilSymbol = nil;
		_behavior = [NSNumberFormatter defaultFormatterBehavior];
		_minimum = nil;
		_maximum = nil;
		_generatesDecimalNumbers = NO;
		[self fnRebuild];
	}
	return self;
}

- (void)dealloc
{
	if (_formatter != NULL) {
		unum_close((UNumberFormat *)_formatter);
		_formatter = NULL;
	}
	/* ⚠⚠ §15's OWNERSHIP CONTRACT, APPLIED TO A FILE THAT HAD ESCAPED IT (§63.79). TWO DEFECTS WERE HERE:
	 * `_minimum` and `_maximum` are `copy`d by their setters and were NEVER RELEASED — a leak — and
	 * `_zeroSymbol`, `_nilSymbol`, `_pattern` and `_locale` were ASSIGNED where Apple declares those properties
	 * `copy`, so the class held pointers it did not own. Both are fixed here, and the fifteen stored doors above
	 * are `copy` from the start. §15 fixed this class of defect across the copy family in 2026-09-20; this file
	 * was not in that sweep. */
	[_minimum release];
	[_maximum release];
	[_zeroSymbol release];
	[_nilSymbol release];
	[_pattern release];
	[_locale release];
	[_attributedStringForZero release];
	[_attributedStringForNil release];
	[_attributedStringForNotANumber release];
	[_textAttributesForZero release];
	[_textAttributesForNegativeValues release];
	[_textAttributesForPositiveValues release];
	[_textAttributesForNil release];
	[_textAttributesForNotANumber release];
	[_textAttributesForPositiveInfinity release];
	[_textAttributesForNegativeInfinity release];
	[_positiveInfinitySymbol release];
	[_negativeInfinitySymbol release];
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

/* The one place a UNumberFormat is made, called by -init and by the three settings that are baked
 * in at open time. */
- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	NSLocale *locale;
	const char *localeText;
	UChar pattern[FN_NF_MAX];
	int32_t patternLength = 0;

	if (_formatter != NULL) {
		unum_close((UNumberFormat *)_formatter);
		_formatter = NULL;
	}
	/* The pattern is converted FIRST so the borrowed -UTF8String below has nothing left to
	 * clobber. */
	if (_pattern != nil) {
		u_strFromUTF8(pattern, FN_NF_MAX, &patternLength, [_pattern UTF8String], -1, &status);
		if (U_FAILURE(status)) {
			patternLength = 0;
		}
	}
	locale = _locale != nil ? _locale : [NSLocale currentLocale];
	if (locale == nil) {
		return;
	}
	localeText = [[locale localeIdentifier] UTF8String];
	if (localeText == NULL || localeText[0] == 0) {
		return;
	}
	status = U_ZERO_ERROR;
	if (patternLength > 0) {
		/* A pattern overrides the style, which is Cocoa's rule too. */
		_formatter = unum_open(UNUM_PATTERN_DECIMAL, pattern, patternLength, localeText, NULL,
				       &status);
	} else {
		_formatter = unum_open(fn_nf_style(_style), NULL, 0, localeText, NULL, &status);
	}
	if (U_FAILURE(status) || _formatter == NULL) {
		_formatter = NULL;
	}
}

/* Read one ICU attribute, or answer a caller-supplied default when there is no formatter. */
- (int32_t)fnAttribute:(UNumberFormatAttribute)attribute fallback:(int32_t)fallback
{
	if (_formatter == NULL) {
		return fallback;
	}
	/* ICU's ATTRIBUTE door takes NO UErrorCode, unlike its symbol and text-attribute doors: it
	 * cannot fail (measured — passing one is a compile error, "expected 2, have 3"). */
	return unum_getAttribute((UNumberFormat *)_formatter, attribute);
}

- (void)fnSetAttribute:(UNumberFormatAttribute)attribute to:(int32_t)value
{
	if (_formatter != NULL) {
		unum_setAttribute((UNumberFormat *)_formatter, attribute, value);
	}
}

/* ROUNDING INCREMENT is the ONE number attribute ICU carries as a DOUBLE, so it needs its own pair
 * (unum_getDoubleAttribute/unum_setDoubleAttribute). Kept beside the int pair so the divergence is
 * visible rather than a cast that would silently truncate 0.05 to 0. */
- (double)fnDoubleAttribute:(UNumberFormatAttribute)attribute fallback:(double)fallback
{
	if (_formatter == NULL) {
		return fallback;
	}
	return unum_getDoubleAttribute((UNumberFormat *)_formatter, attribute);
}

- (void)fnSetDoubleAttribute:(UNumberFormatAttribute)attribute to:(double)value
{
	if (_formatter != NULL) {
		unum_setDoubleAttribute((UNumberFormat *)_formatter, attribute, value);
	}
}

/* One helper per ARITY of ICU's symbol and text-attribute doors, so the twenty-odd accessors below
 * are three lines each instead of a paragraph each. */
- (nullable NSString *)fnSymbol:(UNumberFormatSymbol)symbol
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return nil;
	}
	length = unum_getSymbol((UNumberFormat *)_formatter, symbol, text, FN_NF_MAX, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	return fn_nf_utf8_string(text, length);
}

- (void)fnSetSymbol:(UNumberFormatSymbol)symbol fromString:(nullable NSString *)string
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;

	/* A nil symbol CLEARS the setting in Cocoa, and ICU's way to say that is an empty string. */
	if (string != nil) {
		u_strFromUTF8(text, FN_NF_MAX, &length, [string UTF8String], -1, &status);
		if (U_FAILURE(status)) {
			return;
		}
	} else {
		text[length++] = 0;
	}
	if (_formatter != NULL) {
		unum_setSymbol((UNumberFormat *)_formatter, symbol, text, length, &status);
	}
}

- (nullable NSString *)fnTextAttribute:(UNumberFormatTextAttribute)attribute
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return nil;
	}
	length = unum_getTextAttribute((UNumberFormat *)_formatter, attribute, text, FN_NF_MAX,
				       &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	return fn_nf_utf8_string(text, length);
}

- (void)fnSetTextAttribute:(UNumberFormatTextAttribute)attribute
	       fromString:(nullable NSString *)string
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;

	if (string != nil) {
		u_strFromUTF8(text, FN_NF_MAX, &length, [string UTF8String], -1, &status);
		if (U_FAILURE(status)) {
			return;
		}
	} else {
		text[length++] = 0;
	}
	if (_formatter != NULL) {
		unum_setTextAttribute((UNumberFormat *)_formatter, attribute, text, length, &status);
	}
}

/* --- the two directions ---------------------------------------------------- */

- (nullable NSString *)stringFromNumber:(NSNumber *)number
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;
	const char *encoding;

	if (number == nil) {
		/* OUR symbol, and nil when it was never set: ICU has no "no value" symbol, and
		 * answering a formatted zero for a nil would be a lie. */
		return _nilSymbol;
	}
	if (_formatter == NULL) {
		return nil;
	}
	if (_zeroSymbol != nil && [number doubleValue] == 0.0) {
		return _zeroSymbol;
	}
	/* THE NUMBER'S OWN TYPE CHOOSES THE DOOR, which is Apple's rule and the only way a 64-bit
	 * integer survives: reading the value as a double FIRST would already have lost 2^53+1 its
	 * last digit. Measured — this probe failed with 9,007,199,254,740,992 until the choice stopped
	 * depending on a double. */
	encoding = [number objCType];
	if (encoding != NULL
	    && (encoding[0] == 'c' || encoding[0] == 's' || encoding[0] == 'i'
		|| encoding[0] == 'l' || encoding[0] == 'q' || encoding[0] == 'C'
		|| encoding[0] == 'S' || encoding[0] == 'I' || encoding[0] == 'L'
		|| encoding[0] == 'Q')) {
		length = unum_formatInt64((UNumberFormat *)_formatter, [number longLongValue], text,
					  FN_NF_MAX, NULL, &status);
	} else {
		length = unum_formatDouble((UNumberFormat *)_formatter, [number doubleValue], text,
					   FN_NF_MAX, NULL, &status);
	}
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return fn_nf_utf8_string(text, length);
}

- (nullable NSNumber *)numberFromString:(NSString *)string
{
	UChar text[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	int32_t parsed = 0;
	double value;
	NSString *separator;

	if (string == nil || _formatter == NULL) {
		return nil;
	}
	if (!_allowsFloats) {
		/* Refuse a fraction rather than round it away. The separator is the LOCALE'S, which is
		 * why it is asked for instead of assuming a full stop. */
		separator = [self decimalSeparator];
		if (separator != nil && [separator lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > 0
		    && [string rangeOfString:separator].location != NSNotFound) {
			return nil;
		}
	}
	u_strFromUTF8(text, FN_NF_MAX, &length, [string UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	status = U_ZERO_ERROR;
	value = unum_parseDouble((UNumberFormat *)_formatter, text, length, &parsed, &status);
	/* `parsed == 0` is ICU's "nothing was consumed", which is a failure even without an error. */
	if (U_FAILURE(status) || parsed == 0) {
		return nil;
	}
	/* THE RANGE OVER THE INPUT (Apple's -minimum/-maximum; see the header). Checked over the parsed
	 * double, exact for any boundary a caller states as an integer and close enough at a ragged edge. */
	if (_minimum != nil && value < [_minimum doubleValue]) {
		return nil;
	}
	if (_maximum != nil && value > [_maximum doubleValue]) {
		return nil;
	}
	/* -generatesDecimalNumbers: the parse answers an NSDecimalNumber, built from the TEXT so the
	 * decimal is EXACT — passing the value through the double above would already have rounded it. */
	if (_generatesDecimalNumbers) {
		NSDecimalNumber *decimal = [NSDecimalNumber decimalNumberWithString:string locale:_locale];

		if (decimal != nil) {
			return decimal;
		}
	}
	if (floor(value) == value && value >= -9.0e18 && value <= 9.0e18) {
		return [NSNumber numberWithLongLong:(long long)value];
	}
	return [NSNumber numberWithDouble:value];
}

+ (nullable NSString *)localizedStringFromNumber:(NSNumber *)number
				     numberStyle:(NSNumberFormatterStyle)style
{
	NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];

	[formatter setNumberStyle:style];
	return [formatter stringFromNumber:number];
}

/* --- the NSFormatter doors ------------------------------------------------- */

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	if (object == nil) {
		return _nilSymbol;
	}
	if (![object isKindOfClass:[NSNumber class]]) {
		return nil;
	}
	return [self stringFromNumber:(NSNumber *)object];
}

- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error
{
	NSNumber *number;

	if (object != NULL) {
		*object = nil;
	}
	if (error != NULL) {
		*error = nil;
	}
	if (string == nil) {
		return NO;
	}
	number = [self numberFromString:string];
	if (number == nil) {
		if (error != NULL) {
			*error = @"the text is not a number in this formatter's conventions";
		}
		return NO;
	}
	if (object != NULL) {
		*object = number;
	}
	return YES;
}

/* --- settings ------------------------------------------------------------- */

- (NSNumberFormatterStyle)numberStyle
{
	return _style;
}

- (void)setNumberStyle:(NSNumberFormatterStyle)style
{
	_style = style;
	[self fnRebuild];
}

- (nullable NSString *)format
{
	UChar pattern[FN_NF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return _pattern;
	}
	/* READ BACK FROM ICU once attributes are in play: the pattern that produces the current output
	 * is DATA, and guessing it here would be the table this class exists to avoid. */
	length = unum_toPattern((UNumberFormat *)_formatter, false, pattern, FN_NF_MAX, &status);
	if (U_FAILURE(status) || length <= 0) {
		return _pattern;
	}
	return fn_nf_utf8_string(pattern, length);
}

- (void)setFormat:(nullable NSString *)pattern
{
	if (pattern == _pattern) {
		return;
	}
	[_pattern release];	/* §63.79: Apple declares this `copy`; it was an ASSIGN. */
	_pattern = [pattern copy];
	[self fnRebuild];
}

- (nullable NSLocale *)locale
{
	return _locale;
}

- (void)setLocale:(nullable NSLocale *)locale
{
	if (locale == _locale) {
		return;
	}
	[_locale release];	/* §63.79: Apple declares this `copy`; it was an ASSIGN. */
	_locale = [locale copy];
	[self fnRebuild];
}

- (BOOL)isLenient
{
	return [self fnAttribute:UNUM_LENIENT_PARSE fallback:0] != 0;
}

- (void)setLenient:(BOOL)flag
{
	[self fnSetAttribute:UNUM_LENIENT_PARSE to:(flag ? 1 : 0)];
}

- (BOOL)allowsFloats
{
	return _allowsFloats;
}

- (void)setAllowsFloats:(BOOL)flag
{
	_allowsFloats = flag;
}

/* THE DIGIT LIMITS come back from ICU rather than from a mirror of our own, so they answer what the
 * style's data actually says (the fallbacks are ICU's documented defaults). */
- (NSUInteger)minimumIntegerDigits
{
	return (NSUInteger)[self fnAttribute:UNUM_MIN_INTEGER_DIGITS fallback:1];
}

- (void)setMinimumIntegerDigits:(NSUInteger)digits
{
	[self fnSetAttribute:UNUM_MIN_INTEGER_DIGITS to:(int32_t)digits];
}

- (NSInteger)maximumIntegerDigits
{
	return (NSInteger)[self fnAttribute:UNUM_MAX_INTEGER_DIGITS fallback:42];
}

- (void)setMaximumIntegerDigits:(NSInteger)digits
{
	[self fnSetAttribute:UNUM_MAX_INTEGER_DIGITS to:(int32_t)digits];
}

- (NSUInteger)minimumFractionDigits
{
	return (NSUInteger)[self fnAttribute:UNUM_MIN_FRACTION_DIGITS fallback:0];
}

- (void)setMinimumFractionDigits:(NSUInteger)digits
{
	[self fnSetAttribute:UNUM_MIN_FRACTION_DIGITS to:(int32_t)digits];
}

- (NSUInteger)maximumFractionDigits
{
	return (NSUInteger)[self fnAttribute:UNUM_MAX_FRACTION_DIGITS fallback:3];
}

- (void)setMaximumFractionDigits:(NSUInteger)digits
{
	[self fnSetAttribute:UNUM_MAX_FRACTION_DIGITS to:(int32_t)digits];
}

- (BOOL)usesGroupingSeparator
{
	return [self fnAttribute:UNUM_GROUPING_USED fallback:1] != 0;
}

- (void)setUsesGroupingSeparator:(BOOL)flag
{
	[self fnSetAttribute:UNUM_GROUPING_USED to:(flag ? 1 : 0)];
}

- (NSNumberFormatterRoundingMode)roundingMode
{
	return fn_nf_rounding_back([self fnAttribute:UNUM_ROUNDING_MODE fallback:UNUM_ROUND_HALFEVEN]);
}

- (void)setRoundingMode:(NSNumberFormatterRoundingMode)mode
{
	[self fnSetAttribute:UNUM_ROUNDING_MODE to:fn_nf_rounding(mode)];
}

- (nullable NSNumber *)multiplier
{
	int32_t value = [self fnAttribute:UNUM_MULTIPLIER fallback:1];

	/* Cocoa answers nil when there is no multiplier, and 1 IS no multiplier. */
	return value == 1 ? nil : [NSNumber numberWithInt:value];
}

- (void)setMultiplier:(nullable NSNumber *)multiplier
{
	[self fnSetAttribute:UNUM_MULTIPLIER
			  to:(multiplier != nil ? (int32_t)[multiplier integerValue] : 1)];
}

/* --- the symbols ---------------------------------------------------------- */

- (nullable NSString *)decimalSeparator
{
	return [self fnSymbol:UNUM_DECIMAL_SEPARATOR_SYMBOL];
}

- (void)setDecimalSeparator:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_DECIMAL_SEPARATOR_SYMBOL fromString:string];
}

- (nullable NSString *)groupingSeparator
{
	return [self fnSymbol:UNUM_GROUPING_SEPARATOR_SYMBOL];
}

- (void)setGroupingSeparator:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_GROUPING_SEPARATOR_SYMBOL fromString:string];
}

- (nullable NSString *)percentSymbol
{
	return [self fnSymbol:UNUM_PERCENT_SYMBOL];
}

- (void)setPercentSymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_PERCENT_SYMBOL fromString:string];
}

- (nullable NSString *)minusSign
{
	return [self fnSymbol:UNUM_MINUS_SIGN_SYMBOL];
}

- (void)setMinusSign:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_MINUS_SIGN_SYMBOL fromString:string];
}

- (nullable NSString *)plusSign
{
	return [self fnSymbol:UNUM_PLUS_SIGN_SYMBOL];
}

- (void)setPlusSign:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_PLUS_SIGN_SYMBOL fromString:string];
}

- (nullable NSString *)exponentSymbol
{
	return [self fnSymbol:UNUM_EXPONENTIAL_SYMBOL];
}

- (void)setExponentSymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_EXPONENTIAL_SYMBOL fromString:string];
}

- (nullable NSString *)currencySymbol
{
	return [self fnSymbol:UNUM_CURRENCY_SYMBOL];
}

- (void)setCurrencySymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_CURRENCY_SYMBOL fromString:string];
}

- (nullable NSString *)currencyCode
{
	return [self fnTextAttribute:UNUM_CURRENCY_CODE];
}

- (void)setCurrencyCode:(nullable NSString *)string
{
	[self fnSetTextAttribute:UNUM_CURRENCY_CODE fromString:string];
}

/* THE INTERNATIONAL CURRENCY SYMBOL BELONGS TO THE SYMBOL DOOR, not the text-attribute one:
 * UNUM_INTL_CURRENCY_SYMBOL is a UNumberFormatSymbol, and passing it to unum_getTextAttribute — the
 * shape this pair had — is a type error the compiler warns about AND the wrong question to ask ICU.
 * Measured while sweeping that warning: the fix is the door, not a cast. */
- (nullable NSString *)internationalCurrencySymbol
{
	return [self fnSymbol:UNUM_INTL_CURRENCY_SYMBOL];
}

- (void)setInternationalCurrencySymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_INTL_CURRENCY_SYMBOL fromString:string];
}

/* zeroSymbol and nilSymbol are OURS — see the file header for why ICU cannot hold them. */
- (nullable NSString *)zeroSymbol
{
	return _zeroSymbol;
}

- (void)setZeroSymbol:(nullable NSString *)string
{
	if (string == _zeroSymbol) {
		return;
	}
	[_zeroSymbol release];	/* §63.79: Apple declares this `copy`; it was an ASSIGN. */
	_zeroSymbol = [string copy];
}

- (nullable NSString *)notANumberSymbol
{
	return [self fnSymbol:UNUM_NAN_SYMBOL];
}

- (void)setNotANumberSymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_NAN_SYMBOL fromString:string];
}

- (nullable NSString *)nilSymbol
{
	return _nilSymbol;
}

- (void)setNilSymbol:(nullable NSString *)string
{
	if (string == _nilSymbol) {
		return;
	}
	[_nilSymbol release];	/* §63.79: Apple declares this `copy`; it was an ASSIGN. */
	_nilSymbol = [string copy];
}

/* --- the affixes ---------------------------------------------------------- */

- (nullable NSString *)positivePrefix
{
	return [self fnTextAttribute:UNUM_POSITIVE_PREFIX];
}

- (void)setPositivePrefix:(nullable NSString *)string
{
	[self fnSetTextAttribute:UNUM_POSITIVE_PREFIX fromString:string];
}

- (nullable NSString *)positiveSuffix
{
	return [self fnTextAttribute:UNUM_POSITIVE_SUFFIX];
}

- (void)setPositiveSuffix:(nullable NSString *)string
{
	[self fnSetTextAttribute:UNUM_POSITIVE_SUFFIX fromString:string];
}

- (nullable NSString *)negativePrefix
{
	return [self fnTextAttribute:UNUM_NEGATIVE_PREFIX];
}

- (void)setNegativePrefix:(nullable NSString *)string
{
	[self fnSetTextAttribute:UNUM_NEGATIVE_PREFIX fromString:string];
}

- (nullable NSString *)negativeSuffix
{
	return [self fnTextAttribute:UNUM_NEGATIVE_SUFFIX];
}

- (void)setNegativeSuffix:(nullable NSString *)string
{
	[self fnSetTextAttribute:UNUM_NEGATIVE_SUFFIX fromString:string];
}

/* --- grouping, significant digits, rounding increment --------------------- */
/* The INT attributes, all through the one pair of helpers. The fallbacks are ICU's documented
 * defaults for a plain decimal formatter, so a formatter-less read answers something sane rather
 * than zero. */

- (BOOL)alwaysShowsDecimalSeparator
{
	return [self fnAttribute:UNUM_DECIMAL_ALWAYS_SHOWN fallback:0] != 0;
}

- (void)setAlwaysShowsDecimalSeparator:(BOOL)flag
{
	[self fnSetAttribute:UNUM_DECIMAL_ALWAYS_SHOWN to:(flag ? 1 : 0)];
}

- (NSInteger)groupingSize
{
	return (NSInteger)[self fnAttribute:UNUM_GROUPING_SIZE fallback:3];
}

- (void)setGroupingSize:(NSInteger)size
{
	[self fnSetAttribute:UNUM_GROUPING_SIZE to:(int32_t)size];
}

- (NSInteger)secondaryGroupingSize
{
	return (NSInteger)[self fnAttribute:UNUM_SECONDARY_GROUPING_SIZE fallback:3];
}

- (void)setSecondaryGroupingSize:(NSInteger)size
{
	[self fnSetAttribute:UNUM_SECONDARY_GROUPING_SIZE to:(int32_t)size];
}

/* §63.57: `-minimumGroupingDigits` / `-setMinimumGroupingDigits:` were this pair, over ICU's
 * UNUM_MINIMUM_GROUPING_DIGITS. They are gone with their declarations — Apple declares the name on iOS only,
 * and the macOS corpus holds the rest of the grouping family without it. */
- (BOOL)usesSignificantDigits
{
	return [self fnAttribute:UNUM_SIGNIFICANT_DIGITS_USED fallback:0] != 0;
}

- (void)setUsesSignificantDigits:(BOOL)flag
{
	[self fnSetAttribute:UNUM_SIGNIFICANT_DIGITS_USED to:(flag ? 1 : 0)];
}

- (NSUInteger)minimumSignificantDigits
{
	return (NSUInteger)[self fnAttribute:UNUM_MIN_SIGNIFICANT_DIGITS fallback:1];
}

- (void)setMinimumSignificantDigits:(NSUInteger)digits
{
	[self fnSetAttribute:UNUM_MIN_SIGNIFICANT_DIGITS to:(int32_t)digits];
}

- (NSUInteger)maximumSignificantDigits
{
	return (NSUInteger)[self fnAttribute:UNUM_MAX_SIGNIFICANT_DIGITS fallback:40];
}

- (void)setMaximumSignificantDigits:(NSUInteger)digits
{
	[self fnSetAttribute:UNUM_MAX_SIGNIFICANT_DIGITS to:(int32_t)digits];
}

/* THE DOUBLE ATTRIBUTE. A zero increment is ICU's "none", and Cocoa's default, so it is answered as
 * an NSNumber (0), not nil — the getter reports what the data says. */
- (nullable NSNumber *)roundingIncrement
{
	return [NSNumber numberWithDouble:[self fnDoubleAttribute:UNUM_ROUNDING_INCREMENT fallback:0.0]];
}

- (void)setRoundingIncrement:(nullable NSNumber *)increment
{
	[self fnSetDoubleAttribute:UNUM_ROUNDING_INCREMENT
			      to:(increment != nil ? [increment doubleValue] : 0.0)];
}

/* --- the monetary and per-mill symbols ------------------------------------ */

- (nullable NSString *)perMillSymbol
{
	return [self fnSymbol:UNUM_PERMILL_SYMBOL];
}

- (void)setPerMillSymbol:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_PERMILL_SYMBOL fromString:string];
}

- (nullable NSString *)currencyDecimalSeparator
{
	return [self fnSymbol:UNUM_MONETARY_SEPARATOR_SYMBOL];
}

- (void)setCurrencyDecimalSeparator:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_MONETARY_SEPARATOR_SYMBOL fromString:string];
}

- (nullable NSString *)currencyGroupingSeparator
{
	return [self fnSymbol:UNUM_MONETARY_GROUPING_SEPARATOR_SYMBOL];
}

- (void)setCurrencyGroupingSeparator:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_MONETARY_GROUPING_SEPARATOR_SYMBOL fromString:string];
}

/* --- the deprecated aliases ----------------------------------------------- */
/* NOT storage: each forwards to the modern door, so a caller that reads one after writing the other
 * sees the change — the one behaviour a deprecated alias must have. */

- (BOOL)hasThousandSeparators
{
	return [self usesGroupingSeparator];
}

- (void)setHasThousandSeparators:(BOOL)flag
{
	[self setUsesGroupingSeparator:flag];
}

- (nullable NSString *)thousandSeparator
{
	return [self groupingSeparator];
}

- (void)setThousandSeparator:(nullable NSString *)string
{
	[self setGroupingSeparator:string];
}

/* --- the padding trio ----------------------------------------------------- */
/* ICU's UNUM_FORMAT_WIDTH (how wide), UNUM_PADDING_POSITION (where the pad goes, relative to the
 * prefix/suffix) and UNUM_PAD_ESCAPE_SYMBOL (the fill character). All three are attributes/symbols
 * the formatter already reads, so no state is added here. */

- (NSUInteger)formatWidth
{
	return (NSUInteger)[self fnAttribute:UNUM_FORMAT_WIDTH fallback:0];
}

- (void)setFormatWidth:(NSUInteger)width
{
	[self fnSetAttribute:UNUM_FORMAT_WIDTH to:(int32_t)width];
}

- (NSNumberFormatterPadPosition)paddingPosition
{
	return fn_nf_pad_back((UNumberFormatPadPosition)
			      [self fnAttribute:UNUM_PADDING_POSITION fallback:UNUM_PAD_BEFORE_PREFIX]);
}

- (void)setPaddingPosition:(NSNumberFormatterPadPosition)position
{
	[self fnSetAttribute:UNUM_PADDING_POSITION to:(int32_t)fn_nf_pad(position)];
}

- (nullable NSString *)paddingCharacter
{
	return [self fnSymbol:UNUM_PAD_ESCAPE_SYMBOL];
}

- (void)setPaddingCharacter:(nullable NSString *)string
{
	[self fnSetSymbol:UNUM_PAD_ESCAPE_SYMBOL fromString:string];
}

/* --- the two halves of the one pattern ------------------------------------ */
/* Apple's -positiveFormat/-negativeFormat are the two halves of the ONE pattern -format holds: the
 * decimal pattern language spells the negative subpattern after a ';' (see the header note, MEASURED).
 * So each door is a VIEW — read by splitting, written by recombining. */

- (nullable NSString *)positiveFormat
{
	NSString *pattern = [self format];
	NSRange semi;

	if (pattern == nil) {
		return nil;
	}
	semi = [pattern rangeOfString:@";"];
	if (semi.location == NSNotFound) {
		return pattern;
	}
	return [pattern substringToIndex:semi.location];
}

- (nullable NSString *)negativeFormat
{
	NSString *pattern = [self format];
	NSRange semi;

	if (pattern == nil) {
		return nil;
	}
	semi = [pattern rangeOfString:@";"];
	/* NO EXPLICIT NEGATIVE SUBPATTERN is not "no negative format": the language makes the negative
	 * IMPLICIT (a leading minus). This door answers the EXPLICIT subpattern and nil when the pattern
	 * carries none — a RECORDED limitation, because inventing the implicit spelling would be guessing
	 * at ICU's rule rather than reading it. */
	if (semi.location == NSNotFound) {
		return nil;
	}
	return [pattern substringFromIndex:semi.location + 1];
}

- (void)setPositiveFormat:(nullable NSString *)format
{
	[self fnSetFormatHalf:format negative:NO];
}

- (void)setNegativeFormat:(nullable NSString *)format
{
	[self fnSetFormatHalf:format negative:YES];
}

/* Recombine the two halves into the one pattern and hand it to -setFormat:. The combined string is
 * COPIED, because it is one this method BUILT rather than one a caller owns and keeps alive —
 * -setFormat: stores what it is given. */
- (void)fnSetFormatHalf:(nullable NSString *)half negative:(BOOL)negative
{
	NSString *positive = negative ? [self positiveFormat] : half;
	NSString *negativePart = negative ? half : [self negativeFormat];
	NSString *combined;

	if (positive == nil && negativePart == nil) {
		[self setFormat:nil];		/* both cleared: let the STYLE rule again */
		return;
	}
	if (positive == nil) {
		positive = negativePart;	/* only a negative was given; let it stand alone */
		negativePart = nil;
	}
	if (negativePart != nil) {
		combined = [NSString stringWithFormat:@"%@;%@", positive, negativePart];
	} else {
		combined = [NSString stringWithFormat:@"%@", positive];
	}
	[self setFormat:[combined copy]];
}

/* --- the range over the input, the generated-decimal policy, the behavior ---- */

- (nullable NSNumber *)minimum
{
	return _minimum;
}

- (void)setMinimum:(nullable NSNumber *)number
{
	_minimum = [number copy];
}

- (nullable NSNumber *)maximum
{
	return _maximum;
}

- (void)setMaximum:(nullable NSNumber *)number
{
	_maximum = [number copy];
}

- (BOOL)generatesDecimalNumbers
{
	return _generatesDecimalNumbers;
}

- (void)setGeneratesDecimalNumbers:(BOOL)flag
{
	_generatesDecimalNumbers = flag;
}

+ (NSNumberFormatterBehavior)defaultFormatterBehavior
{
	return fn_nf_default_behavior;
}

+ (void)setDefaultFormatterBehavior:(NSNumberFormatterBehavior)behavior
{
	fn_nf_default_behavior = behavior;
}

- (NSNumberFormatterBehavior)formatterBehavior
{
	return _behavior;
}

- (void)setFormatterBehavior:(NSNumberFormatterBehavior)behavior
{
	_behavior = behavior;
}

/* --- identity ------------------------------------------------------------- */

- (id)copy
{
	/* `alloc` rather than `allocWithZone:`: this library has ONE allocator, and the zone-taking
	 * door is the stub NSObject documents ("a zone is accepted, ignored and documented"). Measured
	 * — with `allocWithZone:NULL` the copy came back with NO underlying formatter, which the probe
	 * caught, so the copy goes through the door that is actually implemented, and the zone
	 * argument is ignored exactly as the class's own documentation says it is. */
	NSNumberFormatter *copy = [[[self class] alloc] init];

	if (copy == nil) {
		return nil;
	}
	/* THE CORE SETTINGS ARE COPIED THROUGH THE ACCESSORS, which is the cheapest way to be sure a new
	 * setting is not silently left behind: a setting this method forgot would be one its own getter
	 * could not reach either. A formatter is mutable, so a shared copy would be a trap.
	 *
	 * THE SYMBOLS AND AFFIXES ARE NOT COPIED, and that is a MEASURED decision rather than a
	 * convenience. Writing them back was tried and the probe refused it twice, in two different
	 * ways:
	 *   * with the symbols written back and the affixes left out, the copy still FORMATTED but a
	 *     grouping separator the caller set on it afterwards no longer took effect (the probe
	 *     expected "1_234_567.89" and got "1,234,567.89");
	 *   * with the AFFIXES written back as well, the copy formatted NOTHING AT ALL — and that fits
	 *     ICU's shape: its affixes are PATTERN COMPONENTS, so writing back the empty prefix and
	 *     suffix that a decimal style reports rebuilds the pattern into something degenerate.
	 * Since the copy is made from the SAME style and locale, ICU's own defaults for it are the same
	 * symbols the original started with — so what a copy does not carry is a symbol a CALLER
	 * deliberately changed. That is a real, recorded gap (§10) and it is smaller than a copy that
	 * cannot format. */
	[copy setNumberStyle:[self numberStyle]];
	[copy setLocale:[self locale]];
	[copy setFormat:_pattern];
	[copy setLenient:[self isLenient]];
	[copy setAllowsFloats:[self allowsFloats]];
	[copy setMinimumIntegerDigits:[self minimumIntegerDigits]];
	[copy setMaximumIntegerDigits:[self maximumIntegerDigits]];
	[copy setMinimumFractionDigits:[self minimumFractionDigits]];
	[copy setMaximumFractionDigits:[self maximumFractionDigits]];
	[copy setUsesGroupingSeparator:[self usesGroupingSeparator]];
	[copy setRoundingMode:[self roundingMode]];
	[copy setMultiplier:[self multiplier]];
	/* OURS, and free of ICU's sharp edge: two ivars, no pattern involved. */
	[copy setZeroSymbol:[self zeroSymbol]];
	[copy setNilSymbol:[self nilSymbol]];
	/* THE RANGE, THE DECIMAL POLICY AND THE BEHAVIOR are core settings too. The FORMAT HALVES are NOT
	 * copied here because -setFormat: above already carries the WHOLE pattern, of which they are the
	 * two views. */
	[copy setMinimum:[self minimum]];
	[copy setMaximum:[self maximum]];
	[copy setGeneratesDecimalNumbers:[self generatesDecimalNumbers]];
	[copy setFormatterBehavior:[self formatterBehavior]];
	return copy;
}

- (NSString *)description
{
	NSString *pattern = [self format];

	return [NSString stringWithFormat:@"<%@: style %d, format %@, locale %@>",
					  [self class], (int)_style,
					  pattern != nil ? pattern : @"(from the style)",
					  _locale != nil ? [_locale localeIdentifier] : @"(current)"];
}


/* ================== THE FIFTEEN STORED DOORS (§63.79) ==================
 * `copy` per Apple's declarations, `release`d in -dealloc. THE FOUR THAT WERE ALREADY HERE ARE FIXED IN THE SAME
 * PASS (see -dealloc): this file ASSIGNED where Apple declares `copy` and leaked where it copied. */
- (NSAttributedString *)attributedStringForZero { return _attributedStringForZero; }
- (void)setAttributedStringForZero:(NSAttributedString *)value
{ [_attributedStringForZero release]; _attributedStringForZero = [value copy]; }
- (NSAttributedString *)attributedStringForNil { return _attributedStringForNil; }
- (void)setAttributedStringForNil:(NSAttributedString *)value
{ [_attributedStringForNil release]; _attributedStringForNil = [value copy]; }
- (NSAttributedString *)attributedStringForNotANumber { return _attributedStringForNotANumber; }
- (void)setAttributedStringForNotANumber:(NSAttributedString *)value
{ [_attributedStringForNotANumber release]; _attributedStringForNotANumber = [value copy]; }
- (NSDictionary *)textAttributesForZero { return _textAttributesForZero; }
- (void)setTextAttributesForZero:(NSDictionary *)value
{ [_textAttributesForZero release]; _textAttributesForZero = [value copy]; }
- (NSDictionary *)textAttributesForNegativeValues { return _textAttributesForNegativeValues; }
- (void)setTextAttributesForNegativeValues:(NSDictionary *)value
{ [_textAttributesForNegativeValues release]; _textAttributesForNegativeValues = [value copy]; }
- (NSDictionary *)textAttributesForPositiveValues { return _textAttributesForPositiveValues; }
- (void)setTextAttributesForPositiveValues:(NSDictionary *)value
{ [_textAttributesForPositiveValues release]; _textAttributesForPositiveValues = [value copy]; }
- (NSDictionary *)textAttributesForNil { return _textAttributesForNil; }
- (void)setTextAttributesForNil:(NSDictionary *)value
{ [_textAttributesForNil release]; _textAttributesForNil = [value copy]; }
- (NSDictionary *)textAttributesForNotANumber { return _textAttributesForNotANumber; }
- (void)setTextAttributesForNotANumber:(NSDictionary *)value
{ [_textAttributesForNotANumber release]; _textAttributesForNotANumber = [value copy]; }
- (NSDictionary *)textAttributesForPositiveInfinity { return _textAttributesForPositiveInfinity; }
- (void)setTextAttributesForPositiveInfinity:(NSDictionary *)value
{ [_textAttributesForPositiveInfinity release]; _textAttributesForPositiveInfinity = [value copy]; }
- (NSDictionary *)textAttributesForNegativeInfinity { return _textAttributesForNegativeInfinity; }
- (void)setTextAttributesForNegativeInfinity:(NSDictionary *)value
{ [_textAttributesForNegativeInfinity release]; _textAttributesForNegativeInfinity = [value copy]; }
- (NSString *)positiveInfinitySymbol { return _positiveInfinitySymbol; }
- (void)setPositiveInfinitySymbol:(NSString *)value
{ [_positiveInfinitySymbol release]; _positiveInfinitySymbol = [value copy]; }
- (NSString *)negativeInfinitySymbol { return _negativeInfinitySymbol; }
- (void)setNegativeInfinitySymbol:(NSString *)value
{ [_negativeInfinitySymbol release]; _negativeInfinitySymbol = [value copy]; }
- (BOOL)localizesFormat { return _localizesFormat; }
- (void)setLocalizesFormat:(BOOL)flag { _localizesFormat = flag; }
- (BOOL)isPartialStringValidationEnabled { return _partialStringValidationEnabled; }
- (void)setPartialStringValidationEnabled:(BOOL)flag { _partialStringValidationEnabled = flag; }
- (NSFormattingContext)formattingContext { return _formattingContext; }
- (void)setFormattingContext:(NSFormattingContext)context { _formattingContext = context; }


@end
