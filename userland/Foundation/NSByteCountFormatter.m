/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSByteCountFormatter.m — the division, the rounding and the unit choice.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words). It owns
 * nothing but its settings: no ICU handle, because ICU has no byte-count formatter to open.
 *
 * THE ARITHMETIC, IN ONE PLACE. Everything below is a pure function of (count, divisor, exponent), so
 * the doors and the class method all agree by construction rather than by three copies of a rule.
 *
 * NO pow() AND NO libm HERE: the divisor's power is built by repeated multiplication, which is exact
 * for the powers that matter (1000 and 1024 are both exactly representable in a double, and their
 * products stay exact well past the largest count a long long can hold) and keeps this file free of a
 * link-time dependency the rest of the class does not need.
 */

#import <Foundation/NSByteCountFormatter.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
/* THE MEASUREMENT DOORS' TWO CLASSES (W12): the value, and the unit it is converted to before the count
 * is formatted. */
#import <Foundation/NSMeasurement.h>
#import <Foundation/NSUnitInformationStorage.h>
/* NSLocale, for the -localeIdentifier send below: a forward-declared or untyped receiver makes that
 * send a warning (-Wobjc-method-access), which is exactly the same class of mistake NSCoder made in
 * NSISO8601DateFormatter.m one class ago. */
#import <Foundation/NSLocale.h>

/* ICU, for ONE thing: the locale's decimal separator. Apple's byte counts use it, and the alternative
 * — always printing '.' — is not a permitted variation (§11.6's three grounds do not cover it: ICU is
 * already in this image). The same call, and the same wrong-turn note about `ulocdata_getDelimiter`, is
 * in NSDecimalNumber.m; the helper is static there, so it is static here too, exactly as fn_df_zone and
 * its two siblings are. */
#include <unicode/unum.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>

#include <stdio.h>
#include <string.h>

static char fn_bcf_decimal_separator(id locale)
{
	char name[64];
	char utf8[8] = { 0 };
	UChar symbol[8];
	int32_t length = 0;
	int32_t utf8Length = 0;
	UErrorCode status = U_ZERO_ERROR;
	UNumberFormat *format = NULL;
	const char *identifier = NULL;

	if (locale != nil && [locale respondsToSelector:@selector(localeIdentifier)]) {
		identifier = [[locale localeIdentifier] UTF8String];
	}
	if (identifier == NULL) {
		identifier = uloc_getDefault();
	}
	{
		size_t i;

		for (i = 0; identifier[i] != '\0' && i + 1 < sizeof name; i++) {
			name[i] = (identifier[i] == '-') ? '_' : identifier[i];
		}
		name[i] = '\0';
	}
	format = unum_open(UNUM_DECIMAL, NULL, 0, name, NULL, &status);
	if (U_FAILURE(status) || format == NULL) {
		return '.';
	}
	length = unum_getSymbol(format, UNUM_DECIMAL_SEPARATOR_SYMBOL, symbol, 8, &status);
	unum_close(format);
	if (U_FAILURE(status) || length <= 0) {
		return '.';
	}
	u_strToUTF8(utf8, sizeof utf8 - 1, &utf8Length, symbol, length, &status);
	if (U_FAILURE(status) || utf8Length != 1) {
		return '.';
	}
	return utf8[0];
}

/* THE TWO DIVISORS, and the four styles collapse onto them for Apple's own stated reason: File is the
 * decimal style and Memory is the binary style (quotations in the header). */
static int fn_bcf_divisor(NSByteCountFormatterCountStyle style)
{
	switch (style) {
	case NSByteCountFormatterCountStyleMemory:
	case NSByteCountFormatterCountStyleBinary:
		return 1024;
	default:
		return 1000;
	}
}

/* Exponent 0 is BYTES, and English makes that one name singular for a count of one. */
static const char *fn_bcf_unit_name(int exponent, long long count)
{
	static const char *names[] = { "bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

	if (exponent <= 0) {
		return (count == 1) ? "byte" : "bytes";
	}
	if (exponent > 8) {
		exponent = 8;
	}
	return names[exponent];
}

/* Does the mask permit this exponent? Zero means UseDefault and permits everything — see the header's
 * note on why the "nothing chosen" case is spelled as a zero mask. */
static int fn_bcf_permits(NSByteCountFormatterUnits units, int exponent)
{
	if (units == NSByteCountFormatterUseDefault || units == NSByteCountFormatterUseAll) {
		return 1;
	}
	if (exponent <= 0) {
		return (units & NSByteCountFormatterUseBytes) != 0;
	}
	if (exponent >= 8) {
		return (units & NSByteCountFormatterUseYBOrHigher) != 0;
	}
	return (units & (1UL << exponent)) != 0;
}

/* The exponent the VALUE asks for, ignoring any mask: the largest unit under which the count is still
 * at least one. */
static int fn_bcf_magnitude_exponent(long long count, int divisor)
{
	double unit = 1.0;
	int exponent = 0;

	if (count < 0) {
		count = -count;
	}
	while (exponent < 8 && ((double)count / unit) >= (double)divisor) {
		exponent++;
		unit *= (double)divisor;
	}
	return exponent;
}

/* The unit a NEGATIVE count uses: the same magnitude walk on the absolute value. The sign is printed,
 * not judged — a byte count is a quantity and Apple has no refusal for a negative one. */
@implementation NSByteCountFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE DEFAULTS, and which of them are documented: allowedUnits' page says UseDefault "is the
	 * default"; the booleans' pages are silent, so the values here are this file's choice (a natural
	 * display, the count and the unit shown, one fraction digit as given). */
	_allowedUnits = NSByteCountFormatterUseDefault;
	_countStyle = NSByteCountFormatterCountStyleFile;
	_formattingContext = NSFormattingContextUnknown;
	_allowsNonnumericFormatting = YES;
	_includesActualByteCount = NO;
	_adaptive = NO;
	_includesCount = YES;
	_includesUnit = YES;
	_zeroPadsFractionDigits = NO;
	return self;
}

- (NSFormattingContext)formattingContext { return _formattingContext; }
- (void)setFormattingContext:(NSFormattingContext)context { _formattingContext = context; }
- (NSByteCountFormatterCountStyle)countStyle { return _countStyle; }
- (void)setCountStyle:(NSByteCountFormatterCountStyle)style { _countStyle = style; }
- (NSByteCountFormatterUnits)allowedUnits { return _allowedUnits; }
- (void)setAllowedUnits:(NSByteCountFormatterUnits)units { _allowedUnits = units; }
- (BOOL)allowsNonnumericFormatting { return _allowsNonnumericFormatting; }
- (void)setAllowsNonnumericFormatting:(BOOL)value { _allowsNonnumericFormatting = value; }
- (BOOL)includesActualByteCount { return _includesActualByteCount; }
- (void)setIncludesActualByteCount:(BOOL)value { _includesActualByteCount = value; }
- (BOOL)isAdaptive { return _adaptive; }
- (void)setAdaptive:(BOOL)value { _adaptive = value; }
- (BOOL)includesCount { return _includesCount; }
- (void)setIncludesCount:(BOOL)value { _includesCount = value; }
- (BOOL)includesUnit { return _includesUnit; }
- (void)setIncludesUnit:(BOOL)value { _includesUnit = value; }
- (BOOL)zeroPadsFractionDigits { return _zeroPadsFractionDigits; }
- (void)setZeroPadsFractionDigits:(BOOL)value { _zeroPadsFractionDigits = value; }

/* THE ONE DOOR THE ARITHMETIC COMES OUT OF. */
- (nullable NSString *)fnStringForByteCount:(long long)count
{
	int divisor = fn_bcf_divisor(_countStyle);
	int exponent = fn_bcf_magnitude_exponent(count, divisor);
	char number[64];
	char result[128];
	/* THE LOCALE'S SEPARATOR: the argument is always nil HERE, and that is not an oversight — Apple's
	 * NSByteCountFormatter has no locale property (measured: its page lists none), so the locale that
	 * governs is the SYSTEM's, which is what a nil argument resolves to. The helper keeps the parameter
	 * because it mirrors NSDecimalNumber.m's, where a locale IS a setting. */
	char separator = fn_bcf_decimal_separator(nil);
	BOOL nonnumeric = (_allowsNonnumericFormatting && count == 0);
	int i;

	/* THE MASK, APPLIED AFTER THE MAGNITUDE because it is a gate and not a starting point: the largest
	 * PERMITTED unit at or below what the value asks for, and if the mask permits nothing that small,
	 * the smallest permitted one (a caller who says "MB only" and hands us 5000 bytes gets MB, which is
	 * what they asked for). A mask that permits nothing at all is treated as UseDefault rather than as
	 * an error: Apple has no refusal here and an empty mask has no meaning. */
	if (!_adaptive) {
		int chosen = -1;

		for (i = exponent; i >= 0; i--) {
			if (fn_bcf_permits(_allowedUnits, i)) {
				chosen = i;
				break;
			}
		}
		if (chosen < 0) {
			for (i = 0; i <= 8; i++) {
				if (fn_bcf_permits(_allowedUnits, i)) {
					chosen = i;
					break;
				}
			}
		}
		if (chosen >= 0) {
			exponent = chosen;
		}
	}

	/* THE NUMBER. Bytes are whole; every larger unit gets one fraction digit, rounded — and the ".0" is
	 * dropped unless the caller asked for zero padding. */
	if (nonnumeric) {
		/* APPLE NAMES THE UNIT AND IT IS KB: the phrase is "Zero KB", not "Zero bytes" — a magnitude
		 * walk over a zero count stops at exponent 0, so the unit has to be chosen rather than computed
		 * here. The mask still gates it, so a caller who permitted only gigabytes gets "Zero GB". */
		snprintf(number, sizeof number, "Zero");
		if (fn_bcf_permits(_allowedUnits, 1)) {
			exponent = 1;
		}
	} else if (exponent == 0) {
		snprintf(number, sizeof number, "%lld", count);
	} else {
		double unit = 1.0;
		double value;

		for (i = 0; i < exponent; i++) {
			unit *= (double)divisor;
		}
		value = (double)count / unit;
		snprintf(number, sizeof number, "%.1f", value);
		if (!_zeroPadsFractionDigits) {
			size_t length = strlen(number);

			if (length >= 2 && number[length - 2] == '.' && number[length - 1] == '0') {
				number[length - 2] = '\0';
			}
		}
		/* THE LOCALE'S SEPARATOR, applied after the C-locale rendering above: one character, and a
		 * locale whose separator is wider than one byte falls back to '.' (the same declared boundary
		 * NSDecimalNumber.m makes, and for the same reason — nothing here parses it back). */
		if (separator != '.') {
			char *dot = strchr(number, '.');

			if (dot != NULL) {
				*dot = separator;
			}
		}
	}

	result[0] = '\0';
	if (_includesCount) {
		snprintf(result, sizeof result, "%s", number);
	}
	if (_includesUnit) {
		size_t used = strlen(result);

		snprintf(result + used, sizeof result - used, "%s%s",
			 (_includesCount && used > 0) ? " " : "", fn_bcf_unit_name(exponent, count));
	}
	if (_includesActualByteCount) {
		size_t used = strlen(result);

		snprintf(result + used, sizeof result - used, " (%lld bytes)", count);
	}
	return [NSString stringWithUTF8String:result];
}

- (nullable NSString *)stringFromByteCount:(long long)byteCount
{
	return [self fnStringForByteCount:byteCount];
}

+ (nullable NSString *)stringFromByteCount:(long long)byteCount
				 countStyle:(NSByteCountFormatterCountStyle)countStyle
{
	NSByteCountFormatter *formatter = [[[self alloc] init] autorelease];

	[formatter setCountStyle:countStyle];
	return [formatter stringFromByteCount:byteCount];
}

/* THE MEASUREMENT DOORS (W12): the measurement arrives in ITS OWN unit, so it is converted to BYTES
 * through its unit's converter before the count is formatted — which is the only way a "1 MiB" measurement
 * and a "1048576 bytes" count can produce the same string. A measurement of another dimension answers nil
 * ("not my kind of value"), rather than raising out of a door whose sibling doors answer nil. */
- (nullable NSString *)stringFromMeasurement:(NSMeasurement *)measurement
{
	NSMeasurement *inBytes;
	long long bytes;

	if (measurement == nil) {
		return nil;
	}
	if (![measurement canBeConvertedToUnit:[NSUnitInformationStorage bytes]]) {
		return nil;
	}
	inBytes = [measurement measurementByConvertingToUnit:[NSUnitInformationStorage bytes]];
	bytes = (long long)[inBytes doubleValue];
	return [self stringFromByteCount:bytes];
}

+ (nullable NSString *)stringFromMeasurement:(NSMeasurement *)measurement
				 countStyle:(NSByteCountFormatterCountStyle)countStyle
{
	NSByteCountFormatter *formatter = [[[self alloc] init] autorelease];

	[formatter setCountStyle:countStyle];
	return [formatter stringFromMeasurement:measurement];
}

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	/* Apple's page for THIS door says both kinds: "Formats … as a byte count (if … is an NSNumber) or
	 * specific byte measurement (if … is an NSMeasurement)". */
	if ([object isKindOfClass:[NSMeasurement class]]) {
		return [self stringFromMeasurement:(NSMeasurement *)object];
	}
	if (![object isKindOfClass:[NSNumber class]]) {
		return nil;
	}
	return [self stringFromByteCount:[(NSNumber *)object longLongValue]];
}

@end
