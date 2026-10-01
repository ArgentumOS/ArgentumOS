/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateFormatter.m — the date formatter, on ICU (F13.6).
 *
 * MANUAL OWNERSHIP, and the only place in the Foundation that knows ICU exists for dates. Two things here
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

#import <Foundation/NSDateFormatter.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSException.h>
#import "NSCalendar.h"		/* the shared identifier -> ICU calendar keyword map */

#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/ucal.h>
#include <unicode/udisplaycontext.h>
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

/* Apple's formatting context -> ICU's capitalization context. This IS the whole of what such a knob
 * means on this substrate: "where the text will appear", which ICU spells as a display context. */
static UDisplayContext fn_df_context(NSFormattingContext context)
{
	if (context == NSFormattingContextStandalone) {
		return UDISPCTX_CAPITALIZATION_FOR_STANDALONE;
	}
	if (context == NSFormattingContextListItem) {
		return UDISPCTX_CAPITALIZATION_FOR_UI_LIST_OR_MENU;
	}
	if (context == NSFormattingContextBeginningOfSentence) {
		return UDISPCTX_CAPITALIZATION_FOR_BEGINNING_OF_SENTENCE;
	}
	if (context == NSFormattingContextMiddleOfSentence) {
		return UDISPCTX_CAPITALIZATION_FOR_MIDDLE_OF_SENTENCE;
	}
	return UDISPCTX_CAPITALIZATION_NONE;
}

/* THE ZONE CROSSES BY NAME WHEN IT HAS ONE, and by OFFSET when it does not. F7's NSTimeZone could
 * only say "an offset in seconds"; F13.7a gave it the IANA database, so a NAMED zone now reaches
 * ICU as its identifier — which is what carries the daylight-saving rules with it. A fixed-offset
 * zone still goes as GMT±HH:MM, which is the whole of what such a zone means. */
static int32_t fn_df_zone(NSTimeZone *zone, UChar *dest, int32_t cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	NSInteger seconds;
	int sign, hours, minutes;
	char text[32];
	NSString *name;

	if (zone == nil) {
		return 0;
	}
	name = [zone name];
	if (name == nil) {
		return 0;
	}
	/* A fixed-offset zone RENDERS its name as the offset ("GMT", "GMT+0530"), so the GMT test
	 * separates the two kinds — and the colon form below is the one ICU was already measured to
	 * accept (F13.6's smoke probe). */
	if (![name hasPrefix:@"GMT"]) {
		u_strFromUTF8(dest, cap, &used, [name UTF8String], -1, &status);
		return U_FAILURE(status) ? 0 : used;
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
		_calendar = nil;
		_formattingContext = NSFormattingContextUnknown;
		_twoDigitStartDate = nil;
		_gregorianStartDate = nil;
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
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

/* The one place a UDateFormat is made. Called by -init and by every setter. */
- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	NSLocale *locale;
	char localeText[128];
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
	/* THE CALENDAR IS PART OF THE LOCALE in ICU — `@calendar=<keyword>` — which is how ONE
	 * formatter can render a Hebrew year or a Japanese era. The keyword comes from the shared map
	 * (NSCalendar.h); with no calendar set, the locale's own is used (Gregorian here). */
	{
		const char *keyword = _calendar != nil
			? fn_calendar_keyword([_calendar identifier]) : NULL;

		snprintf(localeText, sizeof localeText, "%s%s%s",
			 [[locale localeIdentifier] UTF8String],
			 keyword != NULL ? "@calendar=" : "",
			 keyword != NULL ? keyword : "");
	}
	if (localeText[0] == 0) {
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
		/* NO pattern and NO style: the formatter still EXISTS, with the locale's default styles —
		*and it has to*, because the SYMBOL arrays and -setLocalizedDateFormatFromTemplate: ask the
		 * DATA questions, and a formatter with no ICU handle behind it cannot answer them. (Measured:
		 * the symbol checks came back empty until this branch stopped leaving the handle NULL.)
		 * -stringFromDate: still answers the EMPTY string in this state, and that decision belongs
		 * at that door, where it now lives. */
		_formatter = udat_open(UDAT_DEFAULT, UDAT_DEFAULT, localeText,
				       zoneLen > 0 ? zone : NULL, zoneLen, NULL, 0, &status);
	}
	if (U_FAILURE(status) || _formatter == NULL) {
		_formatter = NULL;
		return;
	}
	/* ICU 76 REMOVED the old TRUE/FALSE macros (they were deprecated for years), so the UBool
	 * here is spelled the long way rather than with a macro that no longer exists. */
	udat_setLenient((UDateFormat *)_formatter, (UBool)(_lenient ? 1 : 0));
	/* AND THE THREE KNOBS THAT ARE ICU'S OWN, applied where ICU bakes them in. The display context
	 * is its capitalization; the two-digit pivot and the Gregorian cutover are set on the formatter
	 * and on its own calendar (borrowed, not owned — udat_getCalendar hands back the internal one). */
	{
		UErrorCode knob = U_ZERO_ERROR;

		udat_setContext((UDateFormat *)_formatter, fn_df_context(_formattingContext), &knob);
		if (_twoDigitStartDate != nil) {
			knob = U_ZERO_ERROR;
			udat_set2DigitYearStart((UDateFormat *)_formatter,
						(UDate)([_twoDigitStartDate timeIntervalSince1970] * 1000.0),
						&knob);
		}
		if (_gregorianStartDate != nil) {
			/* udat_getCalendar hands back a CONST calendar, but it is the formatter's OWN
			 * mutable one; the setter below is the documented reason to cast. */
			UCalendar *calendar = (UCalendar *)udat_getCalendar((UDateFormat *)_formatter);

			if (calendar != NULL) {
				knob = U_ZERO_ERROR;
				ucal_setGregorianChange(calendar,
							(UDate)([_gregorianStartDate timeIntervalSince1970] * 1000.0),
							&knob);
			}
		}
	}
}

- (nullable NSString *)stringFromDate:(NSDate *)date
{
	UChar text[FN_DF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (date == nil) {
		return nil;
	}
	/* NOTHING REQUESTED IS DECIDED HERE, not by a missing handle: a formatter with no pattern and
	 * no style has no field to render, so the answer is the empty string — Apple's behaviour — and
	 * the ICU handle may well exist behind it, because the SYMBOL doors need one. */
	if (_pattern == nil && _dateStyle == NSDateFormatterNoStyle
	    && _timeStyle == NSDateFormatterNoStyle) {
		return @"";
	}
	if (_formatter == NULL) {
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

- (id)copy
{
	NSDateFormatter *copy = [[[self class] alloc] init];

	if (copy == nil) {
		return nil;
	}
	/* A formatter is MUTABLE, so a copy that shared this object's state would be a trap: the
	 * settings are carried over and the ICU handle is built fresh by the setters. */
	copy->_pattern = _pattern;
	copy->_locale = _locale;
	copy->_timeZone = _timeZone;
	copy->_calendar = _calendar;
	copy->_dateStyle = _dateStyle;
	copy->_timeStyle = _timeStyle;
	copy->_lenient = _lenient;
	copy->_formattingContext = _formattingContext;
	copy->_twoDigitStartDate = _twoDigitStartDate;
	copy->_gregorianStartDate = _gregorianStartDate;
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

/* --- the calendar, the symbols, and the knobs (F13.7e) -------------------- */

- (nullable NSCalendar *)calendar
{
	return _calendar;
}

- (void)setCalendar:(nullable NSCalendar *)calendar
{
	if (calendar == _calendar) {
		return;
	}
	_calendar = calendar;
	[self fnRebuild];
}

/* ONE HELPER FOR THE ARRAYS, because fourteen getters that each did the same loop would be
 * fourteen chances to write the loop differently. `first`/`count` are ICU's index range for the
 * symbol type: months from 0, weekdays from UCAL_SUNDAY (1), eras and AM/PM from 0. The array
 * STOPS when the data does — which is how a thirteen-month Hebrew year yields thirteen month
 * symbols without this file knowing anything about the Hebrew calendar. */
- (nullable NSArray *)fnSymbols:(UDateFormatSymbolType)type
			  first:(int32_t)first
			  count:(int32_t)count
{
	NSMutableArray *out;
	int32_t i;

	if (_formatter == NULL) {
		return nil;
	}
	out = [NSMutableArray array];
	for (i = 0; i < count; i++) {
		UChar symbol[64];
		UErrorCode status = U_ZERO_ERROR;
		int32_t length = udat_getSymbols((UDateFormat *)_formatter, type, first + i, symbol, 64,
							 &status);
		NSString *text;

		if (U_FAILURE(status)) {
			break;		/* no more symbols in the data */
		}
		text = fn_df_string(symbol, length);
		[out addObject:text != nil ? text : @""];
	}
	return out;
}

- (nullable NSArray *)eraSymbols
{
	return [self fnSymbols:UDAT_ERAS first:0 count:2];
}

- (nullable NSArray *)longEraSymbols
{
	return [self fnSymbols:UDAT_ERA_NAMES first:0 count:2];
}

- (nullable NSArray *)monthSymbols
{
	return [self fnSymbols:UDAT_MONTHS first:0 count:13];
}

- (nullable NSArray *)shortMonthSymbols
{
	return [self fnSymbols:UDAT_SHORT_MONTHS first:0 count:13];
}

- (nullable NSArray *)veryShortMonthSymbols
{
	return [self fnSymbols:UDAT_NARROW_MONTHS first:0 count:13];
}

- (nullable NSArray *)standaloneMonthSymbols
{
	return [self fnSymbols:UDAT_STANDALONE_MONTHS first:0 count:13];
}

- (nullable NSArray *)shortStandaloneMonthSymbols
{
	return [self fnSymbols:UDAT_STANDALONE_SHORT_MONTHS first:0 count:13];
}

- (nullable NSArray *)veryShortStandaloneMonthSymbols
{
	return [self fnSymbols:UDAT_STANDALONE_NARROW_MONTHS first:0 count:13];
}

- (nullable NSArray *)weekdaySymbols
{
	return [self fnSymbols:UDAT_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)shortWeekdaySymbols
{
	return [self fnSymbols:UDAT_SHORT_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)veryShortWeekdaySymbols
{
	return [self fnSymbols:UDAT_NARROW_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)standaloneWeekdaySymbols
{
	return [self fnSymbols:UDAT_STANDALONE_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)shortStandaloneWeekdaySymbols
{
	return [self fnSymbols:UDAT_STANDALONE_SHORT_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)veryShortStandaloneWeekdaySymbols
{
	return [self fnSymbols:UDAT_STANDALONE_NARROW_WEEKDAYS first:UCAL_SUNDAY count:7];
}

- (nullable NSArray *)quarterSymbols
{
	return [self fnSymbols:UDAT_QUARTERS first:0 count:4];
}

- (nullable NSArray *)shortQuarterSymbols
{
	return [self fnSymbols:UDAT_SHORT_QUARTERS first:0 count:4];
}

- (nullable NSArray *)standaloneQuarterSymbols
{
	return [self fnSymbols:UDAT_STANDALONE_QUARTERS first:0 count:4];
}

- (nullable NSArray *)shortStandaloneQuarterSymbols
{
	return [self fnSymbols:UDAT_STANDALONE_SHORT_QUARTERS first:0 count:4];
}

- (nullable NSString *)AMSymbol
{
	UChar symbol[64];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return nil;
	}
	length = udat_getSymbols((UDateFormat *)_formatter, UDAT_AM_PMS, 0, symbol, 64, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	return fn_df_string(symbol, length);
}

- (nullable NSString *)PMSymbol
{
	UChar symbol[64];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return nil;
	}
	length = udat_getSymbols((UDateFormat *)_formatter, UDAT_AM_PMS, 1, symbol, 64, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	return fn_df_string(symbol, length);
}

- (void)setLocalizedDateFormatFromTemplate:(NSString *)template
{
	NSLocale *locale = _locale != nil ? _locale : [NSLocale currentLocale];
	UDateTimePatternGenerator *generator;
	UChar skeleton[FN_DF_MAX];
	UChar pattern[FN_DF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t skeletonLen = 0;
	int32_t patternLen = 0;

	if (template == nil || _formatter == NULL || locale == nil) {
		return;
	}
	/* The locale's borrowed bytes are consumed by the open, and only then the template's. */
	generator = udatpg_open([[locale localeIdentifier] UTF8String], &status);
	if (U_FAILURE(status) || generator == NULL) {
		return;
	}
	status = U_ZERO_ERROR;
	u_strFromUTF8(skeleton, FN_DF_MAX, &skeletonLen, [template UTF8String], -1, &status);
	if (U_SUCCESS(status)) {
		status = U_ZERO_ERROR;
		patternLen = udatpg_getBestPattern(generator, skeleton, skeletonLen, pattern, FN_DF_MAX,
						   &status);
	}
	udatpg_close(generator);
	if (U_FAILURE(status) || patternLen <= 0) {
		return;
	}
	status = U_ZERO_ERROR;
	/* NO STATUS HERE: udat_applyPattern is a 4-argument door (format, localized, pattern, length)
	 * with no UErrorCode — the same shape as the number formatter's attribute doors, and the
	 * second time this slice has paid for assuming otherwise (measured: "expected 4, have 5"). */
	udat_applyPattern((UDateFormat *)_formatter, false, pattern, patternLen);
	/* AND THE PATTERN IS REMEMBERED, because the formatter's own doors consult it: -stringFromDate:
	 * decides "nothing was requested" from _pattern and the styles, so a formatter whose ICU handle
	 * carries a template-derived pattern while _pattern stayed nil would answer the empty string.
	 * (Measured: that is exactly what it did.) */
	_pattern = fn_df_string(pattern, patternLen);
}

+ (NSDateFormatterBehavior)defaultFormatterBehavior
{
	return NSDateFormatterBehavior10_4;
}

- (NSDateFormatterBehavior)formatterBehavior
{
	return NSDateFormatterBehavior10_4;
}

- (void)setFormatterBehavior:(NSDateFormatterBehavior)behavior
{
	if (behavior == NSDateFormatterBehavior10_0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-setFormatterBehavior: takes the modern behaviour "
				   "(NSDateFormatterBehavior10_4, which is what "
				   "NSDateFormatterBehaviorDefault means here): the 10.0 formatter's "
				   "rules are a different set and do not ship"];
	}
}

- (BOOL)generatesCalendarDates
{
	return NO;
}

- (NSFormattingContext)formattingContext
{
	return _formattingContext;
}

- (void)setFormattingContext:(NSFormattingContext)context
{
	if (context == _formattingContext) {
		return;
	}
	_formattingContext = context;
	[self fnRebuild];
}

- (nullable NSDate *)twoDigitStartDate
{
	return _twoDigitStartDate;
}

- (void)setTwoDigitStartDate:(nullable NSDate *)date
{
	if (date == _twoDigitStartDate) {
		return;
	}
	_twoDigitStartDate = date;
	[self fnRebuild];
}

- (nullable NSDate *)gregorianStartDate
{
	return _gregorianStartDate;
}

- (void)setGregorianStartDate:(nullable NSDate *)date
{
	if (date == _gregorianStartDate) {
		return;
	}
	_gregorianStartDate = date;
	[self fnRebuild];
}

- (BOOL)allowsNaturalLanguage
{
	/* ICU parses the fields a pattern names; a natural-language phrase ("next Tuesday") is not one,
	 * and this class never guesses at one. */
	return NO;
}

- (instancetype)initWithDateFormat:(NSString *)format allowNaturalLanguage:(BOOL)flag
{
	self = [self init];
	if (self != nil) {
		/* The deprecated flag only ever PERMITTED a fuzzy parse this class does not do; the part of
		 * the door that means something — the pattern — is honoured. */
		(void)flag;
		[self setDateFormat:format];
	}
	return self;
}

- (BOOL)getObjectValue:(id _Nullable * _Nullable)obj
	     forString:(NSString *)string
		 range:(inout NSRange *)rangep
		 error:(out NSError * _Nullable * _Nullable)error
{
	NSDate *date;
	NSString *text = string;

	if (obj == NULL) {
		return NO;
	}
	(void)error;	/* NO means "not a date"; the door carries no reason to report */
	/* A non-empty range names the SUBSTRING to read, exactly as Apple's contract says; an empty or
	 * absent one reads the whole string. */
	if (rangep != NULL && rangep->length > 0
	    && rangep->location + rangep->length <= [string length]) {
		text = [string substringWithRange:*rangep];
	}
	date = [self dateFromString:text];
	if (date == nil) {
		return NO;
	}
	*obj = date;
	return YES;
}

@end
