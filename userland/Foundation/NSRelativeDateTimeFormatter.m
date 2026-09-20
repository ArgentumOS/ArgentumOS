/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRelativeDateTimeFormatter.m — ICU names the span; this file decides WHICH span.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words).
 *
 * THE ONE RULE THIS FILE OWNS is the unit choice, and it is written once, in fn_rdf_unit, because all
 * three doors need it and three copies of a rule is three rules. Everything else is ICU: the wording,
 * the capitalization, the number's spelling, the locale.
 *
 * THE CALENDAR IS USED FOR THE DIFFERENCE, not seconds arithmetic: a month is not 30 days and a year is
 * not 365, and this system HAS a calendar (NSCalendar, shipped and gate-verified), so approximating
 * here would be a difference Apple does not have for no gain.
 *
 * ICU'S ADOPT PARAMETER, SAID OUT LOUD: ureldatefmt_open takes a UNumberFormat to ADOPT, and the
 * SPELL-OUT style is what it is for — that is how "two months ago" is asked for, rather than a
 * vocabulary this file would have to invent. Once passed, the format belongs to the ICU formatter and
 * must not be closed here (ICU's contract for an adopt parameter); on the failure path ICU has already
 * dealt with it, so this file leaves it alone either way.
 */

#import <Foundation/NSRelativeDateTimeFormatter.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSString.h>

#include <unicode/ureldatefmt.h>
#include <unicode/udisplaycontext.h>
#include <unicode/unum.h>
#include <unicode/ustring.h>

#include <string.h>

#define FN_RDF_MAX 256

/* THE STYLE, AS ICU'S WIDTH. Full and SpellOut both take the LONG patterns and differ in the NUMBER
 * FORMAT; Short and Abbreviated both take the SHORT patterns, because Apple's own examples for them are
 * the same string ("2 mo. ago") — see the header. */
static UDateRelativeDateTimeFormatterStyle fn_rdf_width(NSRelativeDateTimeFormatterUnitsStyle style)
{
	switch (style) {
	case NSRelativeDateTimeFormatterUnitsStyleShort:
	case NSRelativeDateTimeFormatterUnitsStyleAbbreviated:
		return UDAT_STYLE_SHORT;
	default:
		return UDAT_STYLE_LONG;
	}
}

/* WHERE THE TEXT APPEARS, AS ICU'S CAPITALIZATION CONTEXT — which is what makes a beginning-of-sentence
 * formatter write "Yesterday" where a standalone one writes "yesterday". Unknown and Dynamic both ask
 * for no change: the formatter's own default is the honest answer for "we were not told". */
static UDisplayContext fn_rdf_capitalization(NSFormattingContext context)
{
	switch (context) {
	case NSFormattingContextBeginningOfSentence:
		return UDISPCTX_CAPITALIZATION_FOR_BEGINNING_OF_SENTENCE;
	case NSFormattingContextMiddleOfSentence:
		return UDISPCTX_CAPITALIZATION_FOR_MIDDLE_OF_SENTENCE;
	case NSFormattingContextListItem:
		return UDISPCTX_CAPITALIZATION_FOR_UI_LIST_OR_MENU;
	case NSFormattingContextStandalone:
		return UDISPCTX_CAPITALIZATION_FOR_STANDALONE;
	default:
		return UDISPCTX_CAPITALIZATION_NONE;
	}
}

/* ONE UNIT, ONE OFFSET — the pair ICU wants, from the fields of a calendar difference. The LARGEST
 * non-zero unit names the span (the header's rule), and the order IS the rule, so it is written as an
 * ordered list rather than a table. */
typedef struct {
	URelativeDateTimeUnit unit;
	NSInteger value;
} fn_rdf_field;

static void fn_rdf_pick(const fn_rdf_field *fields, int count, URelativeDateTimeUnit *unit, double *offset)
{
	int i;

	/* The first non-zero field in the order given. If EVERY field is zero the span is zero, and the
	 * smallest unit is the right name for it ("1 second ago" is ICU's zero-length answer for the
	 * second unit, and it is a better answer than an empty string). */
	*unit = fields[count - 1].unit;
	*offset = 0.0;
	for (i = 0; i < count; i++) {
		if (fields[i].value != 0) {
			*unit = fields[i].unit;
			*offset = (double)fields[i].value;
			return;
		}
	}
}

/*
 * AN UNSET FIELD IS NOT A ZERO FIELD, AND THIS IS THE BUG THE PROBE FOUND (2026-09-20).
 *
 * NSDateComponents says "this field was never set" with NSDateComponentUndefined (NSIntegerMax) rather
 * than with zero — its own header states the reason, that a field set to zero and a field nobody set are
 * different facts. This file's first version read every field as a plain integer, so a bag holding only
 * `day = -3` had `year = NSIntegerMax`, the ordered pick chose "year", and the answer was
 * **"in 9,223,372,036,854,776,000 years"**. The probe asserted the exact string rather than "not nil",
 * which is the only reason it was caught.
 *
 * FOR UNIT SELECTION an unset field is exactly as uninteresting as a zero one, so it maps to zero HERE,
 * at the point where the fields are read, rather than inside the pick — the pick's rule ("the largest
 * unit with a count") stays true and one line does the translation.
 */
static NSInteger fn_rdf_set(NSInteger value)
{
	return (value == NSDateComponentUndefined) ? 0 : value;
}

@implementation NSRelativeDateTimeFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* Apple's defaults, so far as they are knowable: the NUMERIC style is the one this class is for
	 * ("1 day ago"), the FULL unit style, no special context, and the calendar and locale resolved at
	 * build time from the system's. */
	_dateTimeStyle = NSRelativeDateTimeFormatterStyleNumeric;
	_unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
	_formattingContext = NSFormattingContextUnknown;
	_calendar = nil;
	_locale = nil;
	_formatter = NULL;
	[self fnRebuild];
	return self;
}

- (void)dealloc
{
	[_calendar release];
	[_locale release];
	if (_formatter != NULL) {
		ureldatefmt_close((URelativeDateTimeFormatter *)_formatter);
		_formatter = NULL;
	}
	[super dealloc];
}

- (NSCalendar *)fnCalendar { return _calendar != nil ? _calendar : [NSCalendar currentCalendar]; }
- (NSLocale *)fnLocale { return _locale != nil ? _locale : [NSLocale currentLocale]; }

- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	char name[64];
	UNumberFormat *spellout = NULL;
	const char *identifier;

	if (_formatter != NULL) {
		ureldatefmt_close((URelativeDateTimeFormatter *)_formatter);
		_formatter = NULL;
	}
	identifier = [[self fnLocale] localeIdentifier] != nil
		? [[[self fnLocale] localeIdentifier] UTF8String] : NULL;
	if (identifier == NULL) {
		return;
	}
	{
		size_t i;

		for (i = 0; identifier[i] != '\0' && i + 1 < sizeof name; i++) {
			name[i] = (identifier[i] == '-') ? '_' : identifier[i];
		}
		name[i] = '\0';
	}
	if (name[0] == '\0') {
		return;
	}

	/* THE SPELL-OUT STYLE IS EXACTLY WHAT ICU'S ADOPT PARAMETER IS FOR: a number format that spells the
	 * quantity, so "two months ago" comes from the same data as "2 months ago". */
	if (_unitsStyle == NSRelativeDateTimeFormatterUnitsStyleSpellOut) {
		spellout = unum_open(UNUM_SPELLOUT, NULL, 0, name, NULL, &status);
		if (U_FAILURE(status)) {
			spellout = NULL;
		}
		status = U_ZERO_ERROR;
	}
	_formatter = ureldatefmt_open(name, spellout, fn_rdf_width(_unitsStyle),
				      fn_rdf_capitalization(_formattingContext), &status);
	if (U_FAILURE(status) || _formatter == NULL) {
		_formatter = NULL;
		return;
	}
}

/* The one place a span becomes text: ICU's named form for the Named style, its numeric form otherwise. */
- (nullable NSString *)fnStringForOffset:(double)offset unit:(URelativeDateTimeUnit)unit
{
	UChar text[FN_RDF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (_formatter == NULL) {
		return nil;
	}
	if (_dateTimeStyle == NSRelativeDateTimeFormatterStyleNamed) {
		length = ureldatefmt_format((const URelativeDateTimeFormatter *)_formatter,
					    offset, unit, text, FN_RDF_MAX, &status);
	} else {
		length = ureldatefmt_formatNumeric((const URelativeDateTimeFormatter *)_formatter,
						   offset, unit, text, FN_RDF_MAX, &status);
	}
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	{
		char bytes[FN_RDF_MAX * 4];
		int32_t used = 0;

		status = U_ZERO_ERROR;
		u_strToUTF8(bytes, (int32_t)sizeof bytes, &used, text, length, &status);
		if (U_FAILURE(status)) {
			return nil;
		}
		bytes[used] = '\0';
		return [NSString stringWithUTF8String:bytes];
	}
}

- (nullable NSString *)localizedStringForDate:(NSDate *)date relativeToDate:(NSDate *)referenceDate
{
	NSCalendar *calendar = [self fnCalendar];
	NSDateComponents *diff;
	fn_rdf_field fields[5];
	URelativeDateTimeUnit unit;
	double offset;

	if (date == nil || referenceDate == nil || calendar == nil) {
		return nil;
	}
	/* A CALENDAR DIFFERENCE, so "1 month ago" is a month and not 30 days. The units asked for are the
	 * five this class can name; ICU's WEEK and QUARTER are reachable through the components door, and
	 * a week is deliberately NOT in this list so that a 7-day span is "7 days ago" rather than a week
	 * the calendar did not actually mean (see the header's note on the rule being ours). */
	diff = [calendar components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay |
				     NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
			   fromDate:referenceDate
			     toDate:date
			    options:0];
	if (diff == nil) {
		return nil;
	}
	fields[0].unit = UDAT_REL_UNIT_YEAR;	fields[0].value = [diff year];
	fields[1].unit = UDAT_REL_UNIT_MONTH;	fields[1].value = [diff month];
	fields[2].unit = UDAT_REL_UNIT_DAY;	fields[2].value = [diff day];
	fields[3].unit = UDAT_REL_UNIT_HOUR;	fields[3].value = [diff hour];
	fields[4].unit = UDAT_REL_UNIT_MINUTE;	fields[4].value = [diff minute];
	fn_rdf_pick(fields, 5, &unit, &offset);
	return [self fnStringForOffset:offset unit:unit];
}

- (nullable NSString *)localizedStringFromDateComponents:(NSDateComponents *)components
{
	fn_rdf_field fields[7];
	URelativeDateTimeUnit unit;
	double offset;

	if (components == nil) {
		return nil;
	}
	/* The components ARE the span, so the same ordered pick applies and no calendar is involved: the
	 * caller has already said which unit they mean — which is why every field goes through fn_rdf_set,
	 * whose note records the "in 9,223,372,036,854,776,000 years" answer that skipping it produced. */
	fields[0].unit = UDAT_REL_UNIT_YEAR;	fields[0].value = fn_rdf_set([components year]);
	fields[1].unit = UDAT_REL_UNIT_MONTH;	fields[1].value = fn_rdf_set([components month]);
	fields[2].unit = UDAT_REL_UNIT_WEEK;	fields[2].value = fn_rdf_set([components weekOfYear]);
	fields[3].unit = UDAT_REL_UNIT_DAY;	fields[3].value = fn_rdf_set([components day]);
	fields[4].unit = UDAT_REL_UNIT_HOUR;	fields[4].value = fn_rdf_set([components hour]);
	fields[5].unit = UDAT_REL_UNIT_MINUTE;	fields[5].value = fn_rdf_set([components minute]);
	fields[6].unit = UDAT_REL_UNIT_SECOND;	fields[6].value = fn_rdf_set([components second]);
	fn_rdf_pick(fields, 7, &unit, &offset);
	return [self fnStringForOffset:offset unit:unit];
}

- (nullable NSString *)localizedStringFromTimeInterval:(NSTimeInterval)timeInterval
{
	URelativeDateTimeUnit unit;
	double offset = timeInterval;

	/* A time interval arrives as SECONDS, so the magnitude picks the unit — and the boundaries are this
	 * file's, stated rather than discovered. Note the deliberate absence of week here too. */
	if (offset >= -60.0 && offset <= 60.0) {
		unit = UDAT_REL_UNIT_SECOND;
	} else if (offset > -3600.0 && offset < 3600.0) {
		unit = UDAT_REL_UNIT_MINUTE;
		offset /= 60.0;
	} else if (offset > -86400.0 && offset < 86400.0) {
		unit = UDAT_REL_UNIT_HOUR;
		offset /= 3600.0;
	} else if (offset > -2678400.0 && offset < 2678400.0) {
		unit = UDAT_REL_UNIT_DAY;
		offset /= 86400.0;
	} else if (offset > -32140800.0 && offset < 32140800.0) {
		unit = UDAT_REL_UNIT_MONTH;
		offset /= 2678400.0;
	} else {
		unit = UDAT_REL_UNIT_YEAR;
		offset /= 32140800.0;
	}
	/* WHOLE UNITS: ICU's relative patterns are written for counts, so a fractional offset would ask the
	 * data for a form it does not have. Rounding is the honest reduction, and it is why a 90-minute
	 * interval is "2 hours ago" rather than "1.5 hours ago". */
	return [self fnStringForOffset:(double)(long long)(offset < 0 ? offset - 0.5 : offset + 0.5)
				  unit:unit];
}

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	if (![object isKindOfClass:[NSDate class]]) {
		return nil;
	}
	/* Apple's own description of this door: a date relative to "the current date and time". */
	return [self localizedStringForDate:(NSDate *)object relativeToDate:[NSDate date]];
}

- (NSRelativeDateTimeFormatterStyle)dateTimeStyle { return _dateTimeStyle; }

- (void)setDateTimeStyle:(NSRelativeDateTimeFormatterStyle)style
{
	if (style == _dateTimeStyle) {
		return;
	}
	_dateTimeStyle = style;
	[self fnRebuild];
}

- (NSRelativeDateTimeFormatterUnitsStyle)unitsStyle { return _unitsStyle; }

- (void)setUnitsStyle:(NSRelativeDateTimeFormatterUnitsStyle)style
{
	if (style == _unitsStyle) {
		return;
	}
	_unitsStyle = style;
	[self fnRebuild];
}

- (NSCalendar *)calendar { return [self fnCalendar]; }

- (void)setCalendar:(nullable NSCalendar *)value
{
	if (value == _calendar) {
		return;
	}
	id copy = [value copy];

	[_calendar release];
	_calendar = copy;
}

- (NSLocale *)locale { return [self fnLocale]; }

- (void)setLocale:(nullable NSLocale *)value
{
	if (value == _locale) {
		return;
	}
	{
		id copy = [value copy];

		[_locale release];
		_locale = copy;
	}
	[self fnRebuild];
}

- (NSFormattingContext)formattingContext { return _formattingContext; }

- (void)setFormattingContext:(NSFormattingContext)context
{
	if (context == _formattingContext) {
		return;
	}
	_formattingContext = context;
	[self fnRebuild];
}

@end
