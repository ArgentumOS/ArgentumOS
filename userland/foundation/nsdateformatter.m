/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsdateformatter.m — the date formatter, on ICU (F13.6).
 *
 * ARC file, and the only place in the Foundation that knows ICU exists for dates. Two things here
 * are worth knowing before reading it:
 *
 *   * EVERY SETTER REBUILDS the underlying formatter. ICU bakes the locale, the zone, the styles
 *     and the pattern in at open time, so holding one and mutating it would silently ignore the
 *     change — the class would look like it worked and answer yesterday's question.
 *
 *   * THE BORROWED-BUFFER RULE (§6, learned the hard way in F11b): `-UTF8String`'s buffer is
 *     borrowed, and for a SHORT string (<9 ASCII characters, so most locale identifiers) it is a
 *     decode scratch that the NEXT `-UTF8String` in the same expression overwrites. Every
 *     conversion below takes its borrowed pointer and consumes it IMMEDIATELY, and anything that
 *     must outlive the next call is copied into a stack buffer first.
 */

#import <foundation/NSDateFormatter.h>
#import <foundation/NSString.h>
#import <foundation/NSDate.h>
#import <foundation/NSLocale.h>
#import <foundation/NSTimeZone.h>
#import <foundation/NSException.h>

#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One buffer size for text and patterns alike: a pattern or a formatted date longer than this is
 * not a case worth carrying memory for, and ICU answers U_BUFFER_OVERFLOW_ERROR if it happens. */
#define FN_DF_MAX 256

/* Apple's style -> ICU's. if/ELSE rather than a switch, deliberately: an exhaustive switch makes
 * the fall-through unreachable, and then a bad value becomes an undiagnosable illegal instruction
 * instead of a default (F11a's lesson, kept). */
static UDateFormatStyle fn_df_style(NSDateFormatterStyle style)
{
	if (style == NSDateFormatterShortStyle) {
		return UDAT_SHORT;
	}
	if (style == NSDateFormatterMediumStyle) {
		return UDAT_MEDIUM;
	}
	if (style == NSDateFormatterLongStyle) {
		return UDAT_LONG;
	}
	if (style == NSDateFormatterFullStyle) {
		return UDAT_FULL;
	}
	return UDAT_NONE;
}

/* THE ZONE CROSSES AS AN OFFSET, because that is all this Foundation's NSTimeZone can say: F7
 * refused the zone database and shipped a fixed offset. ICU accepts the GMT±HH:MM form, so the
 * offset survives the trip. When F13.7 un-refuses the name-based zones, THIS is the function that
 * grows a name branch — and nothing else has to move. */
static int32_t fn_df_zone(NSTimeZone *zone, UChar *dest, int32_t cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	NSInteger seconds;
	int sign, hours, minutes;
	char text[32];

	if (zone == nil) {
		return 0;
	}
	seconds = [zone secondsFromGMT];
	sign = seconds < 0 ? '-' : '+';
	if (seconds < 0) {
		seconds = -seconds;
	}
	hours = (int)(seconds / 3600);
	minutes = (int)((seconds % 3600) / 60);
	snprintf(text, sizeof text, "GMT%c%02d:%02d", sign, hours, minutes);
	u_strFromUTF8(dest, cap, &used, text, -1, &status);
	return U_FAILURE(status) ? 0 : used;
}

/* UTF-16 out of ICU, an NSString in. One helper so both directions and the template form agree. */
static NSString *fn_df_string(const UChar *text, int32_t length)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	char bytes[FN_DF_MAX * 4];

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

@implementation NSDateFormatter

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_dateStyle = NSDateFormatterNoStyle;
		_timeStyle = NSDateFormatterNoStyle;
		_lenient = YES;		/* Cocoa's default */
		_pattern = nil;
		_locale = nil;
		_timeZone = nil;
		[self fnRebuild];
	}
	return self;
}

- (void)dealloc
{
	if (_formatter != NULL) {
		udat_close((UDateFormat *)_formatter);
		_formatter = NULL;
	}
}

/* The one place a UDateFormat is made. Called by -init and by every setter. */
- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	NSLocale *locale;
	const char *localeText;
	UChar zone[32];
	int32_t zoneLen = 0;
	UChar pattern[FN_DF_MAX];
	int32_t patternLen = 0;

	if (_formatter != NULL) {
		udat_close((UDateFormat *)_formatter);
		_formatter = NULL;
	}

	/* THE PATTERN IS CONVERTED FIRST, so that the borrowed -UTF8String below has nothing left to
	 * clobber. */
	if (_pattern != nil) {
		u_strFromUTF8(pattern, FN_DF_MAX, &patternLen, [_pattern UTF8String], -1, &status);
		if (U_FAILURE(status)) {
			patternLen = 0;
		}
	}
	zoneLen = fn_df_zone(_timeZone, zone, 32);

	locale = _locale != nil ? _locale : [NSLocale currentLocale];
	if (locale == nil) {
		return;			/* nothing sensible to format with; the doors answer nil */
	}
	localeText = [[locale localeIdentifier] UTF8String];
	if (localeText == NULL || localeText[0] == 0) {
		return;
	}

	status = U_ZERO_ERROR;
	if (patternLen > 0) {
		/* UDAT_PATTERN is what makes udat_open honour the pattern at all: its first branch is
		 * `if (timeStyle != UDAT_PATTERN)`, which builds a STYLE formatter and ignores the
		 * pattern argument entirely (measured in F13.5's smoke probe). */
		_formatter = udat_open(UDAT_PATTERN, UDAT_NONE, localeText,
				       zoneLen > 0 ? zone : NULL, zoneLen, pattern, patternLen,
				       &status);
	} else if (_dateStyle != NSDateFormatterNoStyle || _timeStyle != NSDateFormatterNoStyle) {
		_formatter = udat_open(fn_df_style(_timeStyle), fn_df_style(_dateStyle), localeText,
				       zoneLen > 0 ? zone : NULL, zoneLen, NULL, 0, &status);
	} else {
		/* No pattern and no style: the formatter has been given nothing to say. It is NOT
		 * broken — see -stringFromDate:, which answers the empty string for exactly this. */
		_formatter = NULL;
		return;
	}
	if (U_FAILURE(status) || _formatter == NULL) {
		_formatter = NULL;
		return;
	}
	/* ICU 76 REMOVED the old TRUE/FALSE macros (they were deprecated for years), so the UBool
	 * here is spelled the long way rather than with a macro that no longer exists. */
	udat_setLenient((UDateFormat *)_formatter, (UBool)(_lenient ? 1 : 0));
}

- (nullable NSString *)stringFromDate:(NSDate *)date
{
	UChar text[FN_DF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (date == nil) {
		return nil;
	}
	if (_formatter == NULL) {
		if (_pattern == nil && _dateStyle == NSDateFormatterNoStyle
		    && _timeStyle == NSDateFormatterNoStyle) {
			return @"";	/* nothing was requested, so no field is formatted */
		}
		return nil;
	}
	length = udat_format((UDateFormat *)_formatter,
			     (UDate)([date timeIntervalSince1970] * 1000.0),
			     text, FN_DF_MAX, NULL, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return fn_df_string(text, length);
}

- (nullable NSDate *)dateFromString:(NSString *)string
{
	UChar text[FN_DF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	int32_t parsed = 0;
	UDate when;

	if (string == nil || _formatter == NULL) {
		return nil;
	}
	/* The borrowed pointer is consumed HERE and not held. */
	u_strFromUTF8(text, FN_DF_MAX, &length, [string UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	status = U_ZERO_ERROR;
	when = udat_parse((UDateFormat *)_formatter, text, length, &parsed, &status);
	/* `parsed == 0` is ICU's "nothing was consumed", which is a failure even without an error. */
	if (U_FAILURE(status) || parsed == 0) {
		return nil;
	}
	return [NSDate dateWithTimeIntervalSince1970:(double)(when / 1000.0)];
}

+ (nullable NSString *)localizedStringFromDate:(NSDate *)date
				     dateStyle:(NSDateFormatterStyle)dateStyle
				     timeStyle:(NSDateFormatterStyle)timeStyle
{
	NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
	NSString *answer;

	[formatter setDateStyle:dateStyle];
	[formatter setTimeStyle:timeStyle];
	answer = [formatter stringFromDate:date];
	return answer;
}

+ (nullable NSString *)dateFormatFromTemplate:(NSString *)template
				      options:(NSUInteger)options
				       locale:(nullable NSLocale *)locale
{
	UDateTimePatternGenerator *generator;	/* ICU's C name for it; UDatePatternGenerator is not a type */
	UChar skeleton[FN_DF_MAX];
	UChar pattern[FN_DF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t skeletonLen = 0;
	int32_t patternLen = 0;
	NSLocale *effective;

	(void)options;			/* Apple's reserved bitmask; nothing here changes behaviour */
	if (template == nil) {
		return nil;
	}
	effective = locale != nil ? locale : [NSLocale currentLocale];
	if (effective == nil) {
		return nil;
	}
	/* Open FIRST with the locale's borrowed bytes, then convert the template: the other order
	 * would have two borrowed buffers alive at once, which §6 forbids. */
	generator = udatpg_open([[effective localeIdentifier] UTF8String], &status);
	if (U_FAILURE(status) || generator == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	u_strFromUTF8(skeleton, FN_DF_MAX, &skeletonLen, [template UTF8String], -1, &status);
	if (U_FAILURE(status)) {
		udatpg_close(generator);
		return nil;
	}
	status = U_ZERO_ERROR;
	patternLen = udatpg_getBestPattern(generator, skeleton, skeletonLen, pattern, FN_DF_MAX,
					   &status);
	udatpg_close(generator);
	if (U_FAILURE(status) || patternLen <= 0) {
		return nil;
	}
	return fn_df_string(pattern, patternLen);
}

- (nullable NSString *)dateFormat
{
	if (_formatter == NULL) {
		return _pattern;
	}
	/* READ IT BACK FROM ICU, which is the only honest answer once a style pair is in play: the
	 * pattern a locale uses for a medium date is DATA, and guessing it here would be the table
	 * this class exists to avoid. */
	{
		UChar pattern[FN_DF_MAX];
		UErrorCode status = U_ZERO_ERROR;
		int32_t length = udat_toPattern((UDateFormat *)_formatter, false, pattern, FN_DF_MAX,
						&status);

		if (U_FAILURE(status) || length <= 0) {
			return _pattern;
		}
		return fn_df_string(pattern, length);
	}
}

- (void)setDateFormat:(nullable NSString *)string
{
	if (string == _pattern) {
		return;
	}
	_pattern = string;
	[self fnRebuild];
}

- (NSDateFormatterStyle)dateStyle
{
	return _dateStyle;
}

- (void)setDateStyle:(NSDateFormatterStyle)style
{
	_dateStyle = style;
	[self fnRebuild];
}

- (NSDateFormatterStyle)timeStyle
{
	return _timeStyle;
}

- (void)setTimeStyle:(NSDateFormatterStyle)style
{
	_timeStyle = style;
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

- (nullable NSTimeZone *)timeZone
{
	return _timeZone;
}

- (void)setTimeZone:(nullable NSTimeZone *)timeZone
{
	if (timeZone == _timeZone) {
		return;
	}
	_timeZone = timeZone;
	[self fnRebuild];
}

- (BOOL)isLenient
{
	return _lenient;
}

- (void)setLenient:(BOOL)flag
{
	_lenient = flag;
	if (_formatter != NULL) {
		udat_setLenient((UDateFormat *)_formatter, (UBool)(_lenient ? 1 : 0));
	}
}

- (id)copyWithZone:(nullable NSZone *)zone
{
	NSDateFormatter *copy = [[[self class] allocWithZone:zone] init];

	if (copy == nil) {
		return nil;
	}
	/* A formatter is MUTABLE, so a copy that shared this object's state would be a trap: the
	 * settings are carried over and the ICU handle is built fresh by the setters. */
	copy->_pattern = _pattern;
	copy->_locale = _locale;
	copy->_timeZone = _timeZone;
	copy->_dateStyle = _dateStyle;
	copy->_timeStyle = _timeStyle;
	copy->_lenient = _lenient;
	[copy fnRebuild];
	return copy;
}

- (NSString *)description
{
	NSString *pattern = [self dateFormat];

	return [NSString stringWithFormat:@"<%@: pattern %@, locale %@, zone %@>",
					  [self class],
					  pattern != nil ? pattern : @"(from styles)",
					  _locale != nil ? [_locale localeIdentifier] : @"(current)",
					  _timeZone != nil ? [_timeZone name] : @"(system)"];
}

@end
