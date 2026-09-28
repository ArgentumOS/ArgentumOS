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

#include <unicode/unum.h>
#include <unicode/ustring.h>
#include <math.h>
#include <string.h>

#define FN_NF_MAX 256

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
	_pattern = pattern;
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
	_locale = locale;
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
	_zeroSymbol = string;
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
	_nilSymbol = string;
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

@end
