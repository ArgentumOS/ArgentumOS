/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDecimalNumber.m — the object form of NSDecimal (W3's second half).
 *
 * THE ONE RULE THAT SHAPES EVERYTHING HERE: the arithmetic is NOT in this file. Every operation calls the
 * C functions in NSDecimal.m and then does exactly two things with the answer — it asks the BEHAVIOUR what
 * to do about the error code, and it applies the behaviour's SCALE. That is Apple's design and it is why
 * there are two forms of each method: the plain one with +defaultBehavior, and the `withBehavior:` one.
 *
 * THE DEFAULT BEHAVIOUR, transcribed from the documentation's WORDS into flags (the documentation gives no
 * numbers): no rounding (NSRoundPlain, and the scale is NSDecimalNoScale so the mode is never consulted),
 * 38 digits assumed, and RAISE on overflow, underflow and divide-by-zero but NOT on loss of precision.
 * That last asymmetry is the interesting one and the probe asserts it: `1e20 × 1e20` throws, while a sum
 * that merely loses a digit quietly returns the rounded value.
 *
 * THE STRING FORM IS LOCALE-AWARE, and this is not decoration: the documentation says the decimal
 * separator of `+decimalNumberWithString:` is the DEFAULT LOCALE'S (a period in the US, a comma in France).
 * Hardcoding '.' would make a documented behaviour false, so the separator is asked of ICU
 * (`ulocdata_getDelimiter`) for the locale given, or for ICU's default when none is.
 *
 * CONVERSION RULES, OUR OWN AND DOCUMENTED WHERE APPLE IS SILENT: a double converts to a decimal through
 * its SHORTEST ROUND-TRIPPING decimal form at at most 38 significant digits (what a user reading the
 * double would write); the integer accessors go through -doubleValue and truncate toward zero, which is
 * exact for every integer a double holds exactly and is the boundary this v1 declares.
 */

#import <Foundation/NSDecimalNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSObjCRuntime.h>
#include <unicode/unum.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <string.h>

/* THE CANONICAL TEXT OF A DECIMAL, in one place: -doubleValue parses it and -descriptionWithLocale:
 * re-punctuates it, and both need the same string. (The first draft called it twice in one expression.) */
static const char *fn_decimal_text(const NSDecimal *decimal)
{
	static char buffer[NSDecimalMaxExponent + NSDecimalMaxDigits + (-NSDecimalMinExponent) + 8];

	snprintf(buffer, sizeof buffer, "%s", [NSDecimalString(decimal, nil) UTF8String]);
	return buffer;
}

NSString * const NSDecimalNumberExactnessException = @"NSDecimalNumberExactnessException";
NSString * const NSDecimalNumberOverflowException = @"NSDecimalNumberOverflowException";
NSString * const NSDecimalNumberUnderflowException = @"NSDecimalNumberUnderflowException";
NSString * const NSDecimalNumberDivideByZeroException = @"NSDecimalNumberDivideByZeroException";

static id <NSDecimalNumberBehaviors> fn_default_behavior = nil;

/* THE LOCALE'S DECIMAL SEPARATOR, FROM ICU'S NUMBER-FORMAT DATA — and this went through a wrong turn
 * worth recording: `ulocdata_getDelimiter` looks like the obvious call, but its enum is about QUOTATION
 * marks and exemplar sets, and there is no decimal-separator member at all. The symbol belongs to the
 * number formatter (UNUM_DECIMAL_SEPARATOR_SYMBOL), which is what this asks, converting the UTF-16 symbol
 * ICU returns back to UTF-8.
 *
 * A SEPARATOR WIDER THAN ONE BYTE IS A DECLARED BOUNDARY, not an accident: the parser below scans bytes,
 * so a locale whose separator is multi-byte (Arabic's U+066B, say) falls back to '.' rather than
 * mis-parsing. A locale we cannot name is not an error either: it falls back to ICU's default locale,
 * which is what "the default locale" means. */
static char fn_decimal_separator(id locale)
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
	/* ICU names locales with '_' where Cocoa uses '-'; either spelling is accepted here. */
	{
		size_t i;

		for (i = 0; i < sizeof name - 1 && identifier[i] != '\0'; i++) {
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

@implementation NSDecimalNumber

+ (NSDecimalNumber *)decimalNumberWithString:(NSString *)numberValue
{
	return [[[self alloc] initWithString:numberValue] autorelease];
}

+ (NSDecimalNumber *)decimalNumberWithString:(NSString *)numberValue locale:(id)locale
{
	return [[[self alloc] initWithString:numberValue locale:locale] autorelease];
}

+ (NSDecimalNumber *)decimalNumberWithMantissa:(unsigned long long)mantissa exponent:(short)exponent
				    isNegative:(BOOL)flag
{
	return [[[self alloc] initWithMantissa:mantissa exponent:exponent isNegative:flag] autorelease];
}

+ (NSDecimalNumber *)decimalNumberWithDecimal:(NSDecimal)decimal
{
	return [[[self alloc] initWithDecimal:decimal] autorelease];
}

/* THE PARSER. The grammar the documentation describes, and nothing beyond it: an optional sign, digits, at
 * most one decimal separator, an optional single E/e exponent with its own optional sign. A string that
 * does not fit yields NaN rather than raising — a documented rule of ours, since Apple says only that the
 * locale forms exist. */
- (id)initWithString:(NSString *)numberValue locale:(id)locale
{
	const char *text;
	char buffer[128];
	char separator;
	int i = 0;
	int negative = 0;
	int seenDigit = 0;
	int seenPoint = 0;
	int exponent = 0;
	int fractionDigits = 0;
	unsigned char digits[NSDecimalMaxDigits + 1];
	int digitCount = 0;
	int exponentSign = 0;
	int seenExponentMarker = 0;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (numberValue == nil) {
		[self release];
		return nil;
	}
	text = [numberValue UTF8String];
	if (text == NULL) {
		text = "";
	}
	separator = fn_decimal_separator(locale);
	if (text[0] == '+' || text[0] == '-') {
		negative = (text[0] == '-');
		i = 1;
	}
	for (; text[i] != '\0' && text[i] != 'E' && text[i] != 'e'; i++) {
		char c = text[i];

		if (c >= '0' && c <= '9') {
			if (digitCount < NSDecimalMaxDigits + 1) {
				digits[digitCount++] = (unsigned char)(c - '0');
			}
			seenDigit = 1;
			if (seenPoint) {
				fractionDigits++;
			}
			continue;
		}
		if (c == separator && !seenPoint) {
			seenPoint = 1;
			continue;
		}
		break;				/* anything else ends the number: the result is NaN below */
	}
	if (text[i] == 'E' || text[i] == 'e') {
		int j = i + 1;

		seenExponentMarker = 1;
		if (text[j] == '+' || text[j] == '-') {
			exponentSign = (text[j] == '-') ? -1 : 1;
			j++;
		} else {
			exponentSign = 1;
		}
		if (!(text[j] >= '0' && text[j] <= '9')) {
			seenDigit = 0;		/* an exponent marker with no digits is not a number */
		}
		for (; text[j] >= '0' && text[j] <= '9'; j++) {
			if (exponent < 100000) {
				exponent = exponent * 10 + (text[j] - '0');
			}
		}
		i = j;
	}
	if (!seenDigit || text[i] != '\0' || seenExponentMarker == 0) {
		if (!seenDigit || text[i] != '\0') {
			/* NOT A NUMBER. NaN is reached through the public C surface by the one operation the model
			 * refuses (0 ÷ 0), which is exactly what "the string did not parse" means. */
			NSDecimal zero = { 0, 0, 0, 0, 0, { 0 } };

			NSDecimalDivide(&_decimal, &zero, &zero, NSRoundPlain);
			return self;
		}
	}
	/* THE MANTISSA ARRIVES MOST-SIGNIFICANT FIRST, and the store is least-significant first. */
	{
		int k;

		_decimal._isNaN = 0;
		_decimal._length = 0;
		for (k = 0; k < digitCount; k++) {
			_decimal._digits[digitCount - 1 - k] = digits[k];
		}
		for (; k < NSDecimalMaxDigits + 1; k++) {
			_decimal._digits[k] = 0;
		}
		while (digitCount > 0 && _decimal._digits[digitCount - 1] == 0) {
			digitCount--;
		}
		_decimal._length = (unsigned char)digitCount;
		_decimal._isNegative = (unsigned char)(digitCount > 0 && negative);
		_decimal._exponent = (signed char)(digitCount == 0 ? 0 : -(fractionDigits) + exponentSign * exponent);
		_decimal._isCompact = 0;
	}
	return self;
}

- (id)initWithString:(NSString *)numberValue
{
	return [self initWithString:numberValue locale:nil];
}

/* A MANTISSA AND AN EXPONENT, straight from the value model. The mantissa is a 64-bit unsigned integer, so
 * it is at most 20 digits — the conversion is exact by construction. */
- (id)initWithMantissa:(unsigned long long)mantissa exponent:(short)exponent isNegative:(BOOL)flag
{
	unsigned char digits[32];
	int n = 0;
	int i;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (mantissa == 0) {
		_decimal._length = 0;
		_decimal._exponent = 0;
		_decimal._isNegative = 0;
		_decimal._isCompact = 0;
		_decimal._isNaN = 0;
		for (i = 0; i < NSDecimalMaxDigits + 1; i++) {
			_decimal._digits[i] = 0;
		}
		return self;
	}
	while (mantissa > 0 && n < 32) {
		digits[n++] = (unsigned char)(mantissa % 10);
		mantissa /= 10;
	}
	memcpy(_decimal._digits, digits, (size_t)n);
	for (i = n; i < NSDecimalMaxDigits + 1; i++) {
		_decimal._digits[i] = 0;
	}
	_decimal._length = (unsigned char)n;
	_decimal._exponent = (signed char)exponent;
	_decimal._isNegative = (unsigned char)(flag ? 1 : 0);
	_decimal._isCompact = 0;
	_decimal._isNaN = 0;
	return self;
}

- (id)initWithDecimal:(NSDecimal)decimal
{
	self = [super init];
	if (self != nil) {
		_decimal = decimal;
	}
	return self;
}

+ (NSDecimalNumber *)zero
{
	return [self decimalNumberWithMantissa:0 exponent:0 isNegative:NO];
}

+ (NSDecimalNumber *)one
{
	return [self decimalNumberWithMantissa:1 exponent:0 isNegative:NO];
}

+ (NSDecimalNumber *)notANumber
{
	return [self decimalNumberWithString:@"x"];	/* the documented grammar refuses it */
}

+ (NSDecimalNumber *)maximumDecimalNumber
{
	return [self decimalNumberWithDecimal:NSDecimalMax];
}

+ (NSDecimalNumber *)minimumDecimalNumber
{
	return [self decimalNumberWithDecimal:NSDecimalMin];
}

+ (id <NSDecimalNumberBehaviors>)defaultBehavior
{
	if (fn_default_behavior == nil) {
		fn_default_behavior = [[NSDecimalNumberHandler defaultDecimalNumberHandler] retain];
	}
	return fn_default_behavior;
}

+ (void)setDefaultBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	if (fn_default_behavior == behavior) {
		return;
	}
	[fn_default_behavior release];
	fn_default_behavior = [behavior retain];
}

/* THE ACCESSORS. */
- (NSDecimal)decimalValue
{
	return _decimal;
}

- (const char *)objCType
{
	return "d";				/* a decimal number answers as a double, as Cocoa's does */
}

- (double)doubleValue
{
	const char *text = fn_decimal_text(&_decimal);
	double value = 0.0;
	int i = 0;
	int negative = 0;

	if (_decimal._isNaN) {
		return 0.0;
	}
	if (text[0] == '-') {
		negative = 1;
		i = 1;
	}
	for (; text[i] >= '0' && text[i] <= '9'; i++) {
		value = value * 10.0 + (text[i] - '0');
	}
	if (text[i] == '.') {
		double scale = 1.0;

		for (i++; text[i] >= '0' && text[i] <= '9'; i++) {
			scale /= 10.0;
			value += (text[i] - '0') * scale;
		}
	}
	if (text[i] == 'e' || text[i] == 'E') {
		int exponent = 0;
		int sign = 1;
		double factor = 1.0;

		i++;
		if (text[i] == '+' || text[i] == '-') {
			sign = (text[i] == '-') ? -1 : 1;
			i++;
		}
		for (; text[i] >= '0' && text[i] <= '9'; i++) {
			exponent = exponent * 10 + (text[i] - '0');
		}
		/* THE EXPONENT IS APPLIED BY REPEATED MULTIPLICATION, because the model's range (−128..127) can
		 * overshoot what pow() would do exactly at the edges. */
		for (i = 0; i < exponent; i++) {
			factor *= 10.0;
		}
		if (sign < 0) {
			value /= factor;
		} else {
			value *= factor;
		}
	}
	return negative ? -value : value;
}

- (float)floatValue
{
	return (float)[self doubleValue];
}

- (int)intValue
{
	return (int)[self doubleValue];
}

- (NSInteger)integerValue
{
	return (NSInteger)[self doubleValue];
}

- (long long)longLongValue
{
	return (long long)[self doubleValue];
}

- (unsigned long long)unsignedLongLongValue
{
	return (unsigned long long)[self doubleValue];
}

- (BOOL)boolValue
{
	return [self doubleValue] != 0.0;
}

- (NSString *)stringValue
{
	return NSDecimalString(&_decimal, nil);
}

- (NSString *)description
{
	return [self stringValue];
}

- (NSString *)descriptionWithLocale:(id)locale
{
	char buffer[NSDecimalMaxExponent + NSDecimalMaxDigits + (-NSDecimalMinExponent) + 8];
	const char *text = fn_decimal_text(&_decimal);
	char separator = fn_decimal_separator(locale);
	size_t i;
	size_t n = 0;

	for (i = 0; text[i] != '\0' && n < sizeof buffer - 1; i++) {
		buffer[n++] = (text[i] == '.') ? separator : text[i];
	}
	buffer[n] = '\0';
	{
		/* +stringWithUTF8String: is declared `id _Nullable` in this tree, so the two possible answers are
		 * spelled out rather than assumed: the caller gets a string either way. */
		NSString *text2 = [NSString stringWithUTF8String:buffer];

		return (text2 != nil) ? text2 : @"";
	}
}

/* COMPARISON, and this class OWNS IT: NSNumber's value comparison reads the scalar union, which a decimal
 * does not have. Equal means the same NUMBER, so 1.50 and 1.5 are equal, and equality reaches across the
 * class boundary by asking the other object for its decimal value. */
- (NSComparisonResult)compare:(NSNumber *)otherNumber
{
	NSDecimal other;

	if (otherNumber == nil) {
		return NSOrderedDescending;
	}
	other = [otherNumber decimalValue];
	return NSDecimalCompare(&_decimal, &other);
}

- (BOOL)isEqualToNumber:(NSNumber *)number
{
	if (number == nil) {
		return NO;
	}
	return [self compare:number] == NSOrderedSame;
}

- (BOOL)isEqual:(id)object
{
	if (object == self) {
		return YES;
	}
	if (object == nil || ![object isKindOfClass:[NSNumber class]]) {
		return NO;
	}
	return [self isEqualToNumber:object];
}

/* THE EXACT INTEGER A DECIMAL HOLDS, when it holds one. The hash has to use this rather than -doubleValue,
 * because a plain NSNumber holding 9007199254740993 (2^53 + 1) hashes as the INTEGER and the contract is
 * that equal objects hash alike — while the double would round that number down and split the two. */
static int fn_exact_integer(const NSDecimal *decimal, unsigned long long *out)
{
	unsigned long long value = 0;
	int i;

	if (decimal->_isNaN || decimal->_exponent < 0) {
		return 0;
	}
	for (i = (int)decimal->_length - 1; i >= 0; i--) {
		if (value > (0xFFFFFFFFFFFFFFFFULL - decimal->_digits[i]) / 10) {
			return 0;
		}
		value = value * 10 + decimal->_digits[i];
	}
	for (i = 0; i < (int)decimal->_exponent; i++) {
		if (value > 0xFFFFFFFFFFFFFFFFULL / 10) {
			return 0;
		}
		value *= 10;
	}
	if (decimal->_isNegative) {
		value = (unsigned long long)(-(long long)value);	/* two's complement, as NSNumber does */
	}
	*out = value;
	return 1;
}

- (NSUInteger)hash
{
	/* THE HASH HAS TO AGREE WITH -isEqual:, AND -isEqual: REACHES ACROSS THE CLASS BOUNDARY: a plain
	 * NSNumber holding the same value is equal to a decimal. So this repeats NSNumber's own
	 * canonicalisation — integral values as their integer, others by their double's bits — on the double
	 * this decimal converts to, and two equal numbers hash alike however they were built. (A string hash
	 * would have been shorter and WRONG: [NSNumber numberWithInt:1] would no longer hash with
	 * [NSDecimalNumber one].) */
	double d;
	long long whole;
	unsigned long h = 2166136261UL;
	unsigned long long integer;

	if (fn_exact_integer(&_decimal, &integer)) {
		h ^= (unsigned long)integer;
		return h * 16777619UL;
	}
	d = [self doubleValue];
	whole = (long long)d;

	if ((double)whole == d) {
		h ^= (unsigned long)whole;
		return h * 16777619UL;
	}
	{
		union {
			double d;
			unsigned long u;
		} bits;

		bits.d = d;
		h ^= bits.u;
		return h * 16777619UL;
	}
}


/* ---- the arithmetic -------------------------------------------------------------------------------- */

/* ONE PRIVATE ROAD FOR EVERY OPERATION, and the reason is that every operation needs the SAME two things
 * done to its answer: ask the behaviour what to do about the error code, and apply the behaviour's scale.
 * Apple's documentation gives both as phrases rather than numbers — "the number the behavior specifies" and
 * "the number of digits after the decimal point in the values returned by the decimalNumberBy... methods" —
 * so they live here, once. */
- (NSDecimalNumber *)fn_resultFrom:(NSCalculationError)error operation:(SEL)operation
			  behavior:(id <NSDecimalNumberBehaviors>)behavior
			      left:(NSDecimalNumber *)left right:(NSDecimalNumber *)right
			     value:(const NSDecimal *)value
{
	NSDecimal result = *value;

	if (error != NSCalculationNoError) {
		NSDecimalNumber *substitute = [behavior exceptionDuringOperation:operation error:error
							      leftOperand:left rightOperand:right];

		/* A behaviour that RETURNS a number replaces the result. A behaviour that raises never gets
		 * here, and one that returns nil is saying "ignore the error" — which is why the default
		 * behaviour's refusal to raise on loss of precision means a rounded value, not an exception. */
		if (substitute != nil) {
			return substitute;
		}
	}
	{
		short scale = [behavior scale];

		if (scale != NSDecimalNoScale) {
			NSDecimal rounded;

			NSDecimalRound(&rounded, &result, scale, [behavior roundingMode]);
			result = rounded;
		}
	}
	return [NSDecimalNumber decimalNumberWithDecimal:result];
}

/* THE FOUR BINARY OPERATIONS SHARE ONE SHAPE (this is the whole reason the C surface is function pointers
 * here): result, left, right, mode. */
typedef NSCalculationError (*FnBinaryOp)(NSDecimal *, const NSDecimal *, const NSDecimal *, NSRoundingMode);

- (NSDecimalNumber *)fn_binary:(FnBinaryOp)operation selector:(SEL)selector
			  with:(NSDecimalNumber *)other behavior:(id <NSDecimalNumberBehaviors>)behavior
{
	id <NSDecimalNumberBehaviors> effective = (behavior != nil) ? behavior : [[self class] defaultBehavior];
	NSDecimal otherValue = [other decimalValue];
	NSDecimal value;
	NSCalculationError error = operation(&value, &_decimal, &otherValue, [effective roundingMode]);

	return [self fn_resultFrom:error operation:selector behavior:effective
			      left:self right:other value:&value];
}

- (NSDecimalNumber *)decimalNumberByAdding:(NSDecimalNumber *)decimalNumber
{
	return [self fn_binary:NSDecimalAdd selector:_cmd with:decimalNumber behavior:nil];
}

- (NSDecimalNumber *)decimalNumberByAdding:(NSDecimalNumber *)decimalNumber
			      withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	return [self fn_binary:NSDecimalAdd selector:_cmd with:decimalNumber behavior:behavior];
}

- (NSDecimalNumber *)decimalNumberBySubtracting:(NSDecimalNumber *)decimalNumber
{
	return [self fn_binary:NSDecimalSubtract selector:_cmd with:decimalNumber behavior:nil];
}

- (NSDecimalNumber *)decimalNumberBySubtracting:(NSDecimalNumber *)decimalNumber
				   withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	return [self fn_binary:NSDecimalSubtract selector:_cmd with:decimalNumber behavior:behavior];
}

- (NSDecimalNumber *)decimalNumberByMultiplyingBy:(NSDecimalNumber *)decimalNumber
{
	return [self fn_binary:NSDecimalMultiply selector:_cmd with:decimalNumber behavior:nil];
}

- (NSDecimalNumber *)decimalNumberByMultiplyingBy:(NSDecimalNumber *)decimalNumber
				     withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	return [self fn_binary:NSDecimalMultiply selector:_cmd with:decimalNumber behavior:behavior];
}

- (NSDecimalNumber *)decimalNumberByDividingBy:(NSDecimalNumber *)decimalNumber
{
	return [self fn_binary:NSDecimalDivide selector:_cmd with:decimalNumber behavior:nil];
}

- (NSDecimalNumber *)decimalNumberByDividingBy:(NSDecimalNumber *)decimalNumber
				  withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	return [self fn_binary:NSDecimalDivide selector:_cmd with:decimalNumber behavior:behavior];
}

- (NSDecimalNumber *)decimalNumberByRaisingToPower:(NSUInteger)power
				      withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	id <NSDecimalNumberBehaviors> effective = (behavior != nil) ? behavior : [[self class] defaultBehavior];
	NSDecimal value;
	NSCalculationError error = NSDecimalPower(&value, &_decimal, power, [effective roundingMode]);

	return [self fn_resultFrom:error operation:_cmd behavior:effective
			      left:self right:nil value:&value];
}

- (NSDecimalNumber *)decimalNumberByRaisingToPower:(NSUInteger)power
{
	return [self decimalNumberByRaisingToPower:power withBehavior:nil];
}

- (NSDecimalNumber *)decimalNumberByMultiplyingByPowerOf10:(short)power
					      withBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	id <NSDecimalNumberBehaviors> effective = (behavior != nil) ? behavior : [[self class] defaultBehavior];
	NSDecimal value;
	NSCalculationError error = NSDecimalMultiplyByPowerOf10(&value, &_decimal, power,
							       [effective roundingMode]);

	return [self fn_resultFrom:error operation:_cmd behavior:effective
			      left:self right:nil value:&value];
}

- (NSDecimalNumber *)decimalNumberByMultiplyingByPowerOf10:(short)power
{
	return [self decimalNumberByMultiplyingByPowerOf10:power withBehavior:nil];
}

- (NSDecimalNumber *)decimalNumberByRoundingAccordingToBehavior:(id <NSDecimalNumberBehaviors>)behavior
{
	id <NSDecimalNumberBehaviors> effective = (behavior != nil) ? behavior : [[self class] defaultBehavior];
	short scale = [effective scale];
	NSDecimal value = _decimal;

	if (scale != NSDecimalNoScale) {
		NSDecimal rounded;

		NSDecimalRound(&rounded, &value, scale, [effective roundingMode]);
		value = rounded;
	}
	return [NSDecimalNumber decimalNumberWithDecimal:value];
}

@end

/* ---- the handler ------------------------------------------------------------------------------------ */

@implementation NSDecimalNumberHandler

+ (NSDecimalNumberHandler *)defaultDecimalNumberHandler
{
	/* THE DOCUMENTED DEFAULT, in flags. The words are "does not round numbers off, assumes precision does
	 * not exceed 38 significant digits, and raises an exception on divide-by-zero or when a number is too
	 * big or too small to represent" — five flags fall out of that: no rounding, no raise on exactness
	 * (which is NOT in the list), and a raise on the other three. The asymmetry is asserted by the probe. */
	static NSDecimalNumberHandler *shared = nil;

	if (shared == nil) {
		shared = [[self alloc] initWithRoundingMode:NSRoundPlain scale:NSDecimalNoScale
					    raiseOnExactness:NO
					     raiseOnOverflow:YES
					    raiseOnUnderflow:YES
					   raiseOnDivideByZero:YES];
	}
	return shared;
}

+ (NSDecimalNumberHandler *)decimalNumberHandlerWithRoundingMode:(NSRoundingMode)roundingMode
							   scale:(short)scale
						raiseOnExactness:(BOOL)raiseOnExactness
						 raiseOnOverflow:(BOOL)raiseOnOverflow
						raiseOnUnderflow:(BOOL)raiseOnUnderflow
					       raiseOnDivideByZero:(BOOL)raiseOnDivideByZero
{
	return [[[self alloc] initWithRoundingMode:roundingMode scale:scale
				   raiseOnExactness:raiseOnExactness
				    raiseOnOverflow:raiseOnOverflow
				   raiseOnUnderflow:raiseOnUnderflow
				  raiseOnDivideByZero:raiseOnDivideByZero] autorelease];
}

- (id)initWithRoundingMode:(NSRoundingMode)roundingMode scale:(short)scale
	  raiseOnExactness:(BOOL)raiseOnExactness
	   raiseOnOverflow:(BOOL)raiseOnOverflow
	  raiseOnUnderflow:(BOOL)raiseOnUnderflow
	 raiseOnDivideByZero:(BOOL)raiseOnDivideByZero
{
	self = [super init];
	if (self != nil) {
		_roundingMode = roundingMode;
		_scale = scale;
		_raiseOnExactness = raiseOnExactness;
		_raiseOnOverflow = raiseOnOverflow;
		_raiseOnUnderflow = raiseOnUnderflow;
		_raiseOnDivideByZero = raiseOnDivideByZero;
	}
	return self;
}

- (NSRoundingMode)roundingMode
{
	return _roundingMode;
}

- (short)scale
{
	return _scale;
}

/* THE RAISE TABLE: one flag per error code, and the exception name each raises. A flag that is NO means the
 * error is IGNORED — nil comes back and the arithmetic keeps the rounded value. */
- (NSDecimalNumber *)exceptionDuringOperation:(SEL)method error:(NSCalculationError)error
				  leftOperand:(NSDecimalNumber *)leftOperand
				 rightOperand:(NSDecimalNumber *)rightOperand
{
	const char *selectorName = sel_getName(method);

	(void)leftOperand;
	(void)rightOperand;
	switch (error) {
	case NSCalculationLossOfPrecision:
		if (_raiseOnExactness) {
			[NSException raise:NSDecimalNumberExactnessException
				    format:@"%s lost precision", selectorName];
		}
		break;
	case NSCalculationOverflow:
		if (_raiseOnOverflow) {
			[NSException raise:NSDecimalNumberOverflowException
				    format:@"%s overflowed", selectorName];
		}
		break;
	case NSCalculationUnderflow:
		if (_raiseOnUnderflow) {
			[NSException raise:NSDecimalNumberUnderflowException
				    format:@"%s underflowed", selectorName];
		}
		break;
	case NSCalculationDivideByZero:
		if (_raiseOnDivideByZero) {
			[NSException raise:NSDecimalNumberDivideByZeroException
				    format:@"%s divided by zero", selectorName];
		}
		break;
	default:
		break;
	}
	return nil;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInt:(int)_roundingMode forKey:@"roundingMode"];
	[coder encodeInt:(int)_scale forKey:@"scale"];
	[coder encodeBool:_raiseOnExactness forKey:@"raiseOnExactness"];
	[coder encodeBool:_raiseOnOverflow forKey:@"raiseOnOverflow"];
	[coder encodeBool:_raiseOnUnderflow forKey:@"raiseOnUnderflow"];
	[coder encodeBool:_raiseOnDivideByZero forKey:@"raiseOnDivideByZero"];
}

- (id)initWithCoder:(NSCoder *)coder
{
	return [self initWithRoundingMode:(NSRoundingMode)[coder decodeIntForKey:@"roundingMode"]
				    scale:(short)[coder decodeIntForKey:@"scale"]
				raiseOnExactness:[coder decodeBoolForKey:@"raiseOnExactness"]
				 raiseOnOverflow:[coder decodeBoolForKey:@"raiseOnOverflow"]
				raiseOnUnderflow:[coder decodeBoolForKey:@"raiseOnUnderflow"]
			       raiseOnDivideByZero:[coder decodeBoolForKey:@"raiseOnDivideByZero"]];
}

@end
