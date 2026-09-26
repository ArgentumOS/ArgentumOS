/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSScanner.m — the implementation, MANUAL OWNERSHIP. docs/design/foundation-plan.md §62.20.
 *
 * THE POSITION IS THE WHOLE STATE, and every rule in this file is about when it moves: a scan skips first and
 * then reads; a scan that finds nothing moves nothing; and `-isAtEnd` answers the question without moving it
 * (which is why it saves and restores).
 *
 * THE NUMERIC GRAMMAR IS WRITTEN OUT HERE RATHER THAN DELEGATED, because the one thing a scanner must do that
 * `strtod` alone cannot is tell the caller WHERE THE NUMBER ENDED — and for a LOCALIZED separator `strtod` cannot
 * even start, since it knows only '.'. So each numeric door first finds the extent of a representation with the
 * rules the header states, and then hands the normalized text to the C library for the VALUE: one piece of code
 * decides what a number looks like, and a settled question (IEEE-754 rounding, C99 hexadecimal floats) is not
 * answered a second time here.
 *
 * ICU IS ON THIS FILE'S INCLUDE PATH FOR ONE FACT: the locale's DECIMAL SEPARATOR, read by identifier exactly as
 * NSNumberFormatter reads it — so the scanner and the formatter cannot disagree about what "3,5" means in de_DE.
 * That is why this file is in the mk's ICU list, and the reason the lookup does not go through
 * `-[NSLocale objectForKey:]`, which answers nil for it here (this library's NSLocale has no locale database and
 * its own header says so).
 */

#import <Foundation/NSScanner.h>
#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSException.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSString.h>

#include <unicode/unum.h>
#include <unicode/ustring.h>	/* u_strToUTF8 */
#include <unicode/utypes.h>

#include <stdlib.h>
#include <string.h>

/* ---- CHARACTERS, AND THE ONE CONVERSION ICU NEEDS ------------------------------------------------ */

static BOOL fn_digit(unichar c)
{
	return c >= '0' && c <= '9';
}

static int fn_hex_value(unichar c)
{
	if (c >= '0' && c <= '9') {
		return (int)(c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return (int)(c - 'a') + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return (int)(c - 'A') + 10;
	}
	return -1;
}

/* A UTF-16 BUFFER ICU FILLED, AS AN NSString. The bytes go through UTF-8, which is this library's storage. */
static NSString *fn_string_from_uchars(const UChar *text, int32_t length)
{
	char bytes[64];
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;

	if (length <= 0) {
		return nil;
	}
	u_strToUTF8(bytes, (int32_t)sizeof bytes - 1, &used, text, length, &status);
	if (U_FAILURE(status) || used <= 0) {
		return nil;
	}
	bytes[used] = '\0';
	return [NSString stringWithUTF8String:bytes];
}

/* THE LOCALE'S DECIMAL SEPARATOR, OR `@"."` WHEN THERE IS NO LOCALE TO ASK. Two facts are load-bearing:
 *
 *   * THE IDENTIFIER IS COPIED OUT BEFORE ICU SEES IT. `-UTF8String` hands out a BORROWED buffer that the next
 *     call in the same expression may overwrite (the trap F11's predicate work recorded), so the bytes are copied
 *     into a local first and ICU reads the copy;
 *   * A LOCALE THAT CANNOT NAME ITSELF IS NOT AN ERROR, it is "non-localized" — the same answer a scanner with no
 *     locale gives. Apple's property is typed `id` for exactly this reason. */
static NSString *fn_decimal_separator(id locale)
{
	char identifier[128];
	const char *borrowed;
	UChar text[16];
	UNumberFormat *format;
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (locale == nil || ![locale respondsToSelector:@selector(localeIdentifier)]) {
		return @".";
	}
	borrowed = [[locale localeIdentifier] UTF8String];
	if (borrowed == NULL || borrowed[0] == '\0' || strlen(borrowed) >= sizeof identifier) {
		return @".";
	}
	memcpy(identifier, borrowed, strlen(borrowed) + 1);
	format = unum_open(UNUM_DECIMAL, NULL, 0, identifier, NULL, &status);
	if (format == NULL || U_FAILURE(status)) {
		return @".";
	}
	status = U_ZERO_ERROR;
	length = unum_getSymbol(format, UNUM_DECIMAL_SEPARATOR_SYMBOL, text, 16, &status);
	unum_close(format);
	if (U_FAILURE(status) || length <= 0) {
		return @".";
	}
	{
		NSString *answer = fn_string_from_uchars(text, length);

		return answer != nil ? answer : @".";
	}
}

/* ---- MATCHING ------------------------------------------------------------------------------------ */

static BOOL fn_matches_at(NSString *text, NSUInteger at, NSString *needle, BOOL caseSensitive)
{
	NSUInteger length = [needle length];

	if (length == 0 || at + length > [text length]) {
		return NO;
	}
	if (caseSensitive) {
		return [[text substringWithRange:NSMakeRange(at, length)] isEqualToString:needle];
	}
	return [[text substringWithRange:NSMakeRange(at, length)] compare:needle
								  options:NSCaseInsensitiveSearch] == NSOrderedSame;
}

/* ---- THE NUMERIC EXTENTS -------------------------------------------------------------------------
 *
 * FOUR GRAMMARS, AND EACH ONE ANSWERS WHERE THE REPRESENTATION ENDED. That is the fact a scanner owes its caller
 * and the fact `strtod` alone cannot give for a localized separator.
 *
 * THE NORMALIZED BUFFER IS 256 BYTES AND A REPRESENTATION LONGER THAN THAT IS SCANNED TO ITS TRUE END while the
 * VALUE comes from the text that fitted — the same shape Apple describes for a float ("skips past excess digits
 * in the case of overflow, so the scanner's position is past the entire representation"), and a case no keyboard
 * can reach: the longest C double is under forty characters. */

#define FN_NUM_BUFFER 256

/* DECIMAL INTEGERS: an optional sign and then digits. The accumulator WRAPS, which is what the header states
 * (Apple says an overflowing run is a valid representation and publishes no value for it). */
static BOOL fn_decimal_integer(NSString *text, NSUInteger from, NSUInteger end, BOOL allowSign,
			      NSUInteger *outEnd, BOOL *outNegative, unsigned long long *outValue)
{
	NSUInteger i = from;
	unsigned long long value = 0;
	BOOL negative = NO;
	BOOL any = NO;

	if (allowSign && i < end) {
		unichar c = [text characterAtIndex:i];

		if (c == '-' || c == '+') {
			negative = (c == '-');
			i++;
		}
	}
	while (i < end) {
		unichar c = [text characterAtIndex:i];

		if (!fn_digit(c)) {
			break;
		}
		value = value * 10ULL + (unsigned long long)(c - '0');
		any = YES;
		i++;
	}
	if (!any) {
		return NO;
	}
	*outEnd = i;
	*outNegative = negative;
	*outValue = value;
	return YES;
}

/* DECIMAL FLOATS: an optional sign, digits, an optional fraction (with THIS SCANNER'S separator) and an optional
 * `e`/`E` exponent. At least one digit is required, and an exponent that is not followed by a digit is not
 * consumed — `"12e"` scans as 12 and leaves the `e` for the caller. */
static BOOL fn_decimal_float(NSString *text, NSUInteger from, NSUInteger end, NSString *separator,
			     NSUInteger *outEnd, char *out, size_t outSize)
{
	NSUInteger i = from;
	NSUInteger separatorLength = [separator length];
	NSUInteger digits = 0;
	size_t n = 0;

#define FN_PUT(c) do { if (n + 1 < outSize) { out[n] = (char)(c); } n++; } while (0)

	if (i < end) {
		unichar c = [text characterAtIndex:i];

		if (c == '-' || c == '+') {
			FN_PUT(c);
			i++;
		}
	}
	while (i < end && fn_digit([text characterAtIndex:i])) {
		FN_PUT([text characterAtIndex:i]);
		digits++;
		i++;
	}
	if (separatorLength > 0 && i + separatorLength <= end && fn_matches_at(text, i, separator, YES)) {
		FN_PUT('.');
		i += separatorLength;
		while (i < end && fn_digit([text characterAtIndex:i])) {
			FN_PUT([text characterAtIndex:i]);
			digits++;
			i++;
		}
	}
	if (digits > 0 && i < end) {
		unichar e = [text characterAtIndex:i];

		if (e == 'e' || e == 'E') {
			NSUInteger k = i + 1;
			BOOL sign = NO;
			NSUInteger exponentDigits = 0;

			if (k < end) {
				unichar s = [text characterAtIndex:k];

				if (s == '-' || s == '+') {
					sign = YES;
					k++;
				}
			}
			while (k < end && fn_digit([text characterAtIndex:k])) {
				exponentDigits++;
				k++;
			}
			if (exponentDigits > 0) {
				NSUInteger j;

				FN_PUT('e');
				if (sign) {
					FN_PUT([text characterAtIndex:i + 1]);
				}
				for (j = i + (sign ? 2 : 1); j < k; j++) {
					FN_PUT([text characterAtIndex:j]);
				}
				i = k;
			}
		}
	}
#undef FN_PUT
	if (digits == 0) {
		return NO;
	}
	out[n < outSize ? n : outSize - 1] = '\0';
	*outEnd = i;
	return YES;
}

/* HEX INTEGERS: an optional `0x`/`0X` and then hexadecimal digits — the prefix is optional HERE, which is Apple's
 * word for these two doors ("may optionally be preceded by"). */
static BOOL fn_hex_integer(NSString *text, NSUInteger from, NSUInteger end,
			  NSUInteger *outEnd, unsigned long long *outValue)
{
	NSUInteger i = from;
	unsigned long long value = 0;
	BOOL any = NO;

	if (i + 1 < end && [text characterAtIndex:i] == '0' &&
	    ([text characterAtIndex:i + 1] == 'x' || [text characterAtIndex:i + 1] == 'X')) {
		i += 2;
	}
	while (i < end) {
		int d = fn_hex_value([text characterAtIndex:i]);

		if (d < 0) {
			break;
		}
		value = value * 16ULL + (unsigned long long)d;
		any = YES;
		i++;
	}
	if (!any) {
		return NO;
	}
	*outEnd = i;
	*outValue = value;
	return YES;
}

/* HEX FLOATS: THE PREFIX IS REQUIRED (Apple's sentence), so `1.8p1` is not one and `0x1.8p1` is. The normalized
 * text keeps the prefix and the `p` exponent, which is the C99 spelling the C library parses. */
static BOOL fn_hex_float(NSString *text, NSUInteger from, NSUInteger end, char *out, size_t outSize,
			 NSUInteger *outEnd)
{
	NSUInteger i = from;
	NSUInteger digits = 0;
	size_t n = 0;

#define FN_PUT(c) do { if (n + 1 < outSize) { out[n] = (char)(c); } n++; } while (0)

	if (i < end) {
		unichar c = [text characterAtIndex:i];

		if (c == '-' || c == '+') {
			FN_PUT(c);
			i++;
		}
	}
	if (!(i + 1 < end && [text characterAtIndex:i] == '0' &&
	      ([text characterAtIndex:i + 1] == 'x' || [text characterAtIndex:i + 1] == 'X'))) {
		return NO;
	}
	FN_PUT('0');
	FN_PUT('x');
	i += 2;
	while (i < end && fn_hex_value([text characterAtIndex:i]) >= 0) {
		FN_PUT([text characterAtIndex:i]);
		digits++;
		i++;
	}
	if (i < end && [text characterAtIndex:i] == '.') {
		FN_PUT('.');
		i++;
		while (i < end && fn_hex_value([text characterAtIndex:i]) >= 0) {
			FN_PUT([text characterAtIndex:i]);
			digits++;
			i++;
		}
	}
	if (digits > 0 && i < end) {
		unichar p = [text characterAtIndex:i];

		if (p == 'p' || p == 'P') {
			NSUInteger k = i + 1;
			BOOL sign = NO;
			NSUInteger exponentDigits = 0;

			if (k < end) {
				unichar s = [text characterAtIndex:k];

				if (s == '-' || s == '+') {
					sign = YES;
					k++;
				}
			}
			while (k < end && fn_digit([text characterAtIndex:k])) {
				exponentDigits++;
				k++;
			}
			if (exponentDigits > 0) {
				NSUInteger j;

				FN_PUT('p');
				if (sign) {
					FN_PUT([text characterAtIndex:i + 1]);
				}
				for (j = i + (sign ? 2 : 1); j < k; j++) {
					FN_PUT([text characterAtIndex:j]);
				}
				i = k;
			}
		}
	}
#undef FN_PUT
	if (digits == 0) {
		return NO;
	}
	out[n < outSize ? n : outSize - 1] = '\0';
	*outEnd = i;
	return YES;
}

/* THE DIGITS AND THE EXPONENT OF A DECIMAL, FOR `-scanDecimal:`. See the header for the two rules that are ours;
 * this function is where they live: keeping the FIRST 38 significant digits raises the exponent by the number
 * dropped, which is a truncation toward zero that leaves the magnitude alone. */
static BOOL fn_decimal_parts(NSString *text, NSUInteger from, NSUInteger end, NSString *separator,
			     NSUInteger *outEnd, BOOL *outNegative, char *digits, size_t digitsSize,
			     NSUInteger *outCount, long *outExponent)
{
	NSUInteger i = from;
	NSUInteger separatorLength = [separator length];
	NSUInteger count = 0;
	NSUInteger dropped = 0;
	long fractionCount = 0;
	long explicitExponent = 0;
	BOOL negative = NO;
	BOOL any = NO;

	if (i < end) {
		unichar c = [text characterAtIndex:i];

		if (c == '-' || c == '+') {
			negative = (c == '-');
			i++;
		}
	}
	while (i < end && fn_digit([text characterAtIndex:i])) {
		unichar c = [text characterAtIndex:i];

		any = YES;
		/* LEADING ZEROS ARE NOT SIGNIFICANT, and a long run of them must not fill the buffer. */
		if (c != '0' || count > 0) {
			if (count < digitsSize) {
				digits[count++] = (char)c;
			} else {
				dropped++;
			}
		}
		i++;
	}
	if (separatorLength > 0 && i + separatorLength <= end && fn_matches_at(text, i, separator, YES)) {
		i += separatorLength;
		while (i < end && fn_digit([text characterAtIndex:i])) {
			unichar c = [text characterAtIndex:i];

			any = YES;
			if (count < digitsSize) {
				digits[count++] = (char)c;
			} else {
				dropped++;
			}
			fractionCount++;
			i++;
		}
	}
	if (any && i < end) {
		unichar e = [text characterAtIndex:i];

		if (e == 'e' || e == 'E') {
			NSUInteger k = i + 1;
			BOOL sign = NO;
			unsigned long long exponent = 0;
			NSUInteger exponentDigits = 0;

			if (k < end) {
				unichar s = [text characterAtIndex:k];

				if (s == '-' || s == '+') {
					sign = YES;
					k++;
				}
			}
			while (k < end && fn_digit([text characterAtIndex:k])) {
				exponent = exponent * 10ULL +
					   (unsigned long long)([text characterAtIndex:k] - '0');
				exponentDigits++;
				k++;
			}
			if (exponentDigits > 0) {
				/* SATURATED RATHER THAN WRAPPED, on the way in as well as on the way out: an exponent the text
				 * spells larger than the format can hold is already outside its range. */
				if (exponent > 100000ULL) {
					exponent = 100000ULL;
				}
				explicitExponent = sign ? -(long)exponent : (long)exponent;
				i = k;
			}
		}
	}
	if (!any) {
		return NO;
	}
	{
		/* SIGNIFICANT DIGITS START AT THE FIRST NON-ZERO ONE, and stripping leading zeros does not move the
		 * exponent (they carry no value). */
		NSUInteger first = 0;
		NSUInteger significant;
		NSUInteger extraDropped = 0;
		NSUInteger k;

		while (first < count && digits[first] == '0') {
			first++;
		}
		significant = count - first;
		if (significant > (NSUInteger)NSDecimalMaxDigits) {
			extraDropped = significant - (NSUInteger)NSDecimalMaxDigits;
			significant = (NSUInteger)NSDecimalMaxDigits;
		}
		*outCount = significant;
		*outNegative = negative;
		*outExponent = -fractionCount + (long)dropped + (long)extraDropped + explicitExponent;
		/* THE MANTISSA TRAVELS OUT LEAST-SIGNIFICANT-FIRST, which is the NSDecimal layout, so the flip happens
		 * once here rather than in every caller. */
		{
			char flipped[64];
			NSUInteger limit = significant < sizeof flipped ? significant : sizeof flipped;

			for (k = 0; k < limit; k++) {
				flipped[k] = digits[first + significant - 1 - k];
			}
			for (k = 0; k < limit; k++) {
				digits[k] = flipped[k];
			}
		}
	}
	*outEnd = i;
	return YES;
}

@implementation NSScanner

+ (instancetype)scannerWithString:(NSString *)string
{
	return [[[self alloc] initWithString:string] autorelease];
}

/* APPLE'S SENTENCE: "the locale is set with +currentLocale". */
+ (id)localizedScannerWithString:(NSString *)string
{
	NSScanner *scanner = [[self alloc] initWithString:string];

	if (scanner != nil) {
		[scanner setLocale:[NSLocale currentLocale]];
		[scanner autorelease];
	}
	return scanner;
}

- (instancetype)initWithString:(NSString *)string
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_string = [string copy];
	_location = 0;
	/* THE TWO DEFAULTS APPLE STATES: case sensitive, and whitespace and newlines skipped. */
	_caseSensitive = YES;
	_skipSet = [[NSCharacterSet whitespaceAndNewlineCharacterSet] copy];
	_locale = nil;
	_decimalSeparator = [@"." copy];
	return self;
}

- (void)dealloc
{
	NSString *string = _string;
	NSCharacterSet *skip = _skipSet;
	id locale = _locale;
	NSString *separator = _decimalSeparator;

	_string = nil;
	_skipSet = nil;
	_locale = nil;
	_decimalSeparator = nil;
	[string release];
	[skip release];
	[locale release];
	[separator release];
	[super dealloc];
}

/* A SCANNER IS MUTABLE — `-scanLocation` moves — SO `-copy` IS A REAL COPY AND NOT `[self retain]`. This is the
 * one class in the library where the immutability rule does not apply, and the copy is INDEPENDENT: moving one
 * scanner's position leaves the other's alone while both scan the same string. */
- (id)copy
{
	NSScanner *copy = [[NSScanner alloc] initWithString:_string];

	if (copy == nil) {
		return nil;
	}
	copy->_location = _location;
	copy->_caseSensitive = _caseSensitive;
	[copy setCharactersToBeSkipped:_skipSet];
	[copy setLocale:_locale];
	return copy;
}

/* ---- THE STRING AND THE CONFIGURATION ------------------------------------------------------------ */

- (NSString *)string
{
	return _string;
}

- (NSUInteger)scanLocation
{
	return _location;
}

- (void)setScanLocation:(NSUInteger)location
{
	if (location > [_string length]) {
		[NSException raise:NSRangeException
			    format:@"-[NSScanner setScanLocation:%lu]: beyond the end of a %lu-character string",
				   (unsigned long)location, (unsigned long)[_string length]];
	}
	_location = location;
}

- (BOOL)caseSensitive
{
	return _caseSensitive;
}

- (void)setCaseSensitive:(BOOL)flag
{
	_caseSensitive = flag;
}

- (NSCharacterSet *)charactersToBeSkipped
{
	return _skipSet;
}

- (void)setCharactersToBeSkipped:(NSCharacterSet *)set
{
	NSCharacterSet *old = _skipSet;

	_skipSet = [set copy];
	[old release];
}

- (id)locale
{
	return _locale;
}

- (void)setLocale:(id)locale
{
	id old = _locale;
	NSString *oldSeparator = _decimalSeparator;

	_locale = [locale retain];
	_decimalSeparator = [fn_decimal_separator(locale) copy];
	[old release];
	[oldSeparator release];
}

/* IF ONLY CHARACTERS THAT WOULD BE SKIPPED REMAIN, THE SCANNER IS AT THE END — and asking does not move it,
 * because a question is not a scan (that is what the save and restore are for). */
- (BOOL)isAtEnd
{
	NSUInteger save = _location;
	NSUInteger length = [_string length];
	BOOL end;

	while (_location < length && _skipSet != nil &&
	       [_skipSet characterIsMember:[_string characterAtIndex:_location]]) {
		_location++;
	}
	end = (_location >= length) ? YES : NO;
	_location = save;
	return end;
}

- (void)fnSkipIgnoredCharacters
{
	NSUInteger length = [_string length];

	while (_location < length && _skipSet != nil &&
	       [_skipSet characterIsMember:[_string characterAtIndex:_location]]) {
		_location++;
	}
}

/* ---- SCANNING CHARACTERS AND STRINGS ------------------------------------------------------------- */

- (BOOL)scanCharactersFromSet:(NSCharacterSet *)set intoString:(NSString * _Nullable * _Nullable)result
{
	NSUInteger length = [_string length];
	NSUInteger start;

	[self fnSkipIgnoredCharacters];
	start = _location;
	while (_location < length && [set characterIsMember:[_string characterAtIndex:_location]]) {
		_location++;
	}
	if (_location == start) {
		return NO;
	}
	if (result != NULL) {
		*result = [_string substringWithRange:NSMakeRange(start, _location - start)];
	}
	return YES;
}

- (BOOL)scanUpToCharactersFromSet:(NSCharacterSet *)set intoString:(NSString * _Nullable * _Nullable)result
{
	NSUInteger length = [_string length];
	NSUInteger start;

	[self fnSkipIgnoredCharacters];
	start = _location;
	while (_location < length && ![set characterIsMember:[_string characterAtIndex:_location]]) {
		_location++;
	}
	/* NOTHING TO SCAN IS NOT A SCAN: this is the case where the only characters before the stop are the ones that
	 * were skipped, and Apple's page names it as a NO. */
	if (_location == start) {
		return NO;
	}
	if (result != NULL) {
		*result = [_string substringWithRange:NSMakeRange(start, _location - start)];
	}
	return YES;
}

- (BOOL)scanString:(NSString *)string intoString:(NSString * _Nullable * _Nullable)result
{
	NSUInteger length = [_string length];
	NSUInteger needle = [string length];

	[self fnSkipIgnoredCharacters];
	if (!fn_matches_at(_string, _location, string, _caseSensitive)) {
		return NO;
	}
	/* THE SUBSTRING OF THE RECEIVER, not the caller's object: under case-insensitive scanning the two are equal
	 * and not identical, and Apple's sentence is "returning an equivalent string object" — i.e. the text that was
	 * actually there. */
	if (result != NULL) {
		*result = [_string substringWithRange:NSMakeRange(_location, needle)];
	}
	if (length - _location < needle) {
		_location = length;
	} else {
		_location += needle;
	}
	return YES;
}

- (BOOL)scanUpToString:(NSString *)string intoString:(NSString * _Nullable * _Nullable)result
{
	NSUInteger length = [_string length];
	NSUInteger start;
	NSRange found;

	if ([string length] == 0) {
		return NO;
	}
	[self fnSkipIgnoredCharacters];
	start = _location;
	found = [_string rangeOfString:string
			       options:(_caseSensitive ? 0 : NSCaseInsensitiveSearch)
				 range:NSMakeRange(start, length - start)];
	if (found.location == NSNotFound) {
		/* THE TERMINATOR IS NOWHERE: the rest of the string IS the scan and the position goes to the end. */
		if (start >= length) {
			return NO;
		}
		if (result != NULL) {
			*result = [_string substringWithRange:NSMakeRange(start, length - start)];
		}
		_location = length;
		return YES;
	}
	/* THE TERMINATOR IS FIRST: there is nothing to scan, so nothing moves. */
	if (found.location == start) {
		return NO;
	}
	if (result != NULL) {
		*result = [_string substringWithRange:NSMakeRange(start, found.location - start)];
	}
	/* AND THE POSITION IS THE BEGINNING OF THE TERMINATOR, which is Apple's sentence — so a caller can scan the
	 * terminator itself next. */
	_location = found.location;
	return YES;
}

/* ---- SCANNING NUMBERS ---------------------------------------------------------------------------
 *
 * EVERY ONE OF THESE IS THE SAME FIVE STEPS: skip; find the extent of a representation; parse the normalized
 * text (or the accumulated digits); advance the position to the extent's end; and write the value only when the
 * caller asked for one. AND NOTHING MOVES WHEN THE GRAMMAR DOES NOT MATCH. */

- (BOOL)scanInt:(int * _Nullable)result
{
	unsigned long long value = 0;
	BOOL negative = NO;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_integer(_string, _location, [_string length], YES, &end, &negative, &value)) {
		return NO;
	}
	if (result != NULL) {
		/* THE ACCUMULATOR IN THE REQUESTED WIDTH, and the negative case is `0 - value` FIRST so that the two's
		 * complement of the magnitude is what the cast sees. */
		*result = (int)(negative ? 0ULL - value : value);
	}
	_location = end;
	return YES;
}

- (BOOL)scanInteger:(NSInteger * _Nullable)result
{
	unsigned long long value = 0;
	BOOL negative = NO;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_integer(_string, _location, [_string length], YES, &end, &negative, &value)) {
		return NO;
	}
	if (result != NULL) {
		*result = (NSInteger)(negative ? 0ULL - value : value);
	}
	_location = end;
	return YES;
}

- (BOOL)scanLongLong:(long long * _Nullable)result
{
	unsigned long long value = 0;
	BOOL negative = NO;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_integer(_string, _location, [_string length], YES, &end, &negative, &value)) {
		return NO;
	}
	if (result != NULL) {
		*result = (long long)(negative ? 0ULL - value : value);
	}
	_location = end;
	return YES;
}

- (BOOL)scanUnsignedLongLong:(unsigned long long * _Nullable)result
{
	unsigned long long value = 0;
	BOOL negative = NO;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	/* A LEADING `+` IS TAKEN AND A LEADING `-` IS REFUSED, because this door exists to reach the values a signed
	 * long long cannot hold and a negative number is not one of them. Apple publishes no grammar here; the header
	 * states the choice. */
	if (!fn_decimal_integer(_string, _location, [_string length], YES, &end, &negative, &value)) {
		return NO;
	}
	if (negative) {
		return NO;
	}
	if (result != NULL) {
		*result = value;
	}
	_location = end;
	return YES;
}

- (BOOL)scanDouble:(double * _Nullable)result
{
	char text[FN_NUM_BUFFER];
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_float(_string, _location, [_string length], _decimalSeparator, &end, text,
			      sizeof text)) {
		return NO;
	}
	if (result != NULL) {
		*result = strtod(text, NULL);
	}
	_location = end;
	return YES;
}

- (BOOL)scanFloat:(float * _Nullable)result
{
	char text[FN_NUM_BUFFER];
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_float(_string, _location, [_string length], _decimalSeparator, &end, text,
			      sizeof text)) {
		return NO;
	}
	if (result != NULL) {
		*result = strtof(text, NULL);
	}
	_location = end;
	return YES;
}

- (BOOL)scanHexInt:(unsigned int * _Nullable)result
{
	unsigned long long value = 0;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_hex_integer(_string, _location, [_string length], &end, &value)) {
		return NO;
	}
	if (result != NULL) {
		*result = (unsigned int)value;
	}
	_location = end;
	return YES;
}

- (BOOL)scanHexLongLong:(unsigned long long * _Nullable)result
{
	unsigned long long value = 0;
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_hex_integer(_string, _location, [_string length], &end, &value)) {
		return NO;
	}
	if (result != NULL) {
		*result = value;
	}
	_location = end;
	return YES;
}

- (BOOL)scanHexDouble:(double * _Nullable)result
{
	char text[FN_NUM_BUFFER];
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_hex_float(_string, _location, [_string length], text, sizeof text, &end)) {
		return NO;
	}
	if (result != NULL) {
		*result = strtod(text, NULL);
	}
	_location = end;
	return YES;
}

- (BOOL)scanHexFloat:(float * _Nullable)result
{
	char text[FN_NUM_BUFFER];
	NSUInteger end = 0;

	[self fnSkipIgnoredCharacters];
	if (!fn_hex_float(_string, _location, [_string length], text, sizeof text, &end)) {
		return NO;
	}
	if (result != NULL) {
		*result = strtof(text, NULL);
	}
	_location = end;
	return YES;
}

- (BOOL)scanDecimal:(NSDecimal *)dcm
{
	char digits[64];
	NSUInteger count = 0;
	NSUInteger end = 0;
	BOOL negative = NO;
	long exponent = 0;
	NSDecimal value;

	[self fnSkipIgnoredCharacters];
	if (!fn_decimal_parts(_string, _location, [_string length], _decimalSeparator, &end, &negative,
			      digits, sizeof digits, &count, &exponent)) {
		return NO;
	}
	memset(&value, 0, sizeof value);
	value._isNegative = negative ? 1 : 0;
	value._length = (unsigned char)count;
	{
		NSUInteger k;

		for (k = 0; k < count; k++) {
			value._digits[k] = (unsigned char)(digits[k] - '0');
		}
	}
	/* THE EXPONENT CLAMPS, which is this family's own rule for a value the representation cannot hold (rule 1 of
	 * NSDecimal.h) rather than a second convention — and the text IS a decimal representation, which is what this
	 * door reports, so it answers YES. */
	if (exponent > 127) {
		exponent = 127;
	}
	if (exponent < -128) {
		exponent = -128;
	}
	value._exponent = (signed char)exponent;
	if (count == 0) {
		value._exponent = 0;
		value._isNegative = 0;
	}
	if (dcm != NULL) {
		*dcm = value;
		/* THE CANONICAL FORM OF THIS TYPE: the family's own compaction, so a scanned 1.50 and a computed 1.5 are
		 * one value with one printed form instead of two representations that merely compare equal. */
		NSDecimalCompact(dcm);
	}
	_location = end;
	return YES;
}

@end
