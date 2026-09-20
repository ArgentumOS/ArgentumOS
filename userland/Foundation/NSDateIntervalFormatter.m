/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateIntervalFormatter.m — styles and templates become an ICU skeleton.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words).
 *
 * THE SKELETON IS THE WHOLE TRANSLATION. ICU's interval formatter is opened with a SKELETON — a set of
 * fields with no order and no separators, e.g. "yMMMdHm" — and ICU arranges those fields the way the
 * locale's date and time patterns do, then picks the interval pattern that prints the common part
 * once. That is why this file contains a field list and no format string: the ordering is the
 * locale's, and writing it here would be the table §11's rule/table line forbids.
 *
 * THE ZONE FIELDS ARE "z"/"zzzz" IN THE SKELETON for the Long and Full time styles, which is how the
 * abbreviated and full zone names get in: the skeleton's grammar allows them, so they are requested
 * the same way every other field is rather than appended as text afterwards.
 *
 * THE ICU CALL IS udtitvfmt_* AND IT LIVES IN <unicode/udateintervalformat.h>, NOT dtitvfmt.h: ICU 76
 * keeps the C++ DateIntervalFormat in the latter and split the C API out (measured — dtitvfmt.h
 * declares no udtitvfmt_* name at all, which is worth knowing before concluding the function is
 * missing).
 */

#import <Foundation/NSDateIntervalFormatter.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateInterval.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>

#include <unicode/udateintervalformat.h>
#include <unicode/ustring.h>

#include <string.h>
#include <stdio.h>		/* snprintf: the locale key is assembled, exactly as NSDateFormatter does it */

#define FN_DIF_MAX 512

/* A STYLE, AS FIELDS. No separators and no order: see the header's note. */
static const char *fn_dif_date_skeleton(NSDateIntervalFormatterStyle style)
{
	switch (style) {
	case NSDateIntervalFormatterShortStyle:		return "yMd";
	case NSDateIntervalFormatterMediumStyle:	return "yMMMd";
	case NSDateIntervalFormatterLongStyle:		return "yMMMMd";
	case NSDateIntervalFormatterFullStyle:		return "yMMMMEEEEd";
	default:					return "";
	}
}

static const char *fn_dif_time_skeleton(NSDateIntervalFormatterStyle style)
{
	switch (style) {
	/* THE HOUR FIELD IS "j", NOT "H", AND THAT CORRECTION WAS MEASURED (this file's first version
	 * asked for "H"). "H" is hour 0-23 — a FIELD, so requesting it forces a 24-hour clock on every
	 * locale, and the probe caught en_US rendering "12:00 – 13:00" where the locale's own convention
	 * is "12:00 PM – 1:00 PM". "j" is ICU's LOCALE-PREFERRED hour: the pattern generator resolves it
	 * to h12 or h23 per locale. Choosing the field is this library's business; choosing the clock is
	 * the locale's, which is §11's rule/table line one field over. */
	case NSDateIntervalFormatterShortStyle:		return "jm";
	/* SECONDS ARE WHAT SEPARATE Medium FROM Short, and the zone names what separates Long and Full —
	 * the same three-step widening the locale's own time styles have. */
	case NSDateIntervalFormatterMediumStyle:	return "jms";
	case NSDateIntervalFormatterLongStyle:		return "jmsz";
	case NSDateIntervalFormatterFullStyle:		return "jmszzzz";
	default:					return "";
	}
}

/* The zone as ICU wants it: by NAME when it has one (which carries the daylight rules), as a GMT
 * offset otherwise — the same translation NSDateFormatter and the ISO formatter make. */
static int32_t fn_dif_zone(NSTimeZone *zone, UChar *dest, int32_t cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	NSString *name = zone != nil ? [zone name] : nil;
	const char *text;

	if (name == nil) {
		return 0;
	}
	text = [name UTF8String];
	if (text == NULL) {
		return 0;
	}
	u_strFromUTF8(dest, cap, &used, text, -1, &status);
	if (U_FAILURE(status)) {
		return 0;
	}
	return used;
}

@implementation NSDateIntervalFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* Nothing configured: see the header's note on why this is the default. */
	_dateStyle = NSDateIntervalFormatterNoStyle;
	_timeStyle = NSDateIntervalFormatterNoStyle;
	_dateTemplate = nil;
	_calendar = nil;
	_locale = nil;
	_timeZone = nil;
	_formatter = NULL;
	return self;
}

- (void)dealloc
{
	[_dateTemplate release];
	[_calendar release];
	[_locale release];
	[_timeZone release];
	if (_formatter != NULL) {
		udtitvfmt_close((UDateIntervalFormat *)_formatter);
		_formatter = NULL;
	}
	[super dealloc];
}

/* The effective settings: what was set, or the default that setting nil means. */
- (NSLocale *)fnLocale { return _locale != nil ? _locale : [NSLocale currentLocale]; }
- (NSCalendar *)fnCalendar { return _calendar != nil ? _calendar : [NSCalendar currentCalendar]; }
- (NSTimeZone *)fnTimeZone { return _timeZone != nil ? _timeZone : [NSTimeZone systemTimeZone]; }

- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	char skeleton[FN_DIF_MAX];
	char localeText[128];
	UChar uskeleton[FN_DIF_MAX];
	UChar zone[64];
	int32_t skeletonLen = 0;
	int32_t zoneLen = 0;
	NSLocale *locale = [self fnLocale];
	const char *keyword = NULL;

	if (_formatter != NULL) {
		udtitvfmt_close((UDateIntervalFormat *)_formatter);
		_formatter = NULL;
	}
	if (locale == nil) {
		return;
	}

	/* DATE, THEN TIME — or the template INSTEAD of both. Concatenating two skeletons is how a date
	 * field set and a time field set become one request; ICU does not care about the order they are
	 * given in, only about which fields are present. */
	skeleton[0] = '\0';
	if (_dateTemplate != nil && [_dateTemplate length] > 0) {
		snprintf(skeleton, sizeof skeleton, "%s", [_dateTemplate UTF8String]);
	} else {
		const char *datePart = fn_dif_date_skeleton(_dateStyle);
		const char *timePart = fn_dif_time_skeleton(_timeStyle);

		snprintf(skeleton, sizeof skeleton, "%s%s", datePart, timePart);
	}
	if (skeleton[0] == '\0') {
		return;			/* no fields at all: the doors answer nil */
	}

	/* THE CALENDAR IS PART OF THE LOCALE in ICU, `@calendar=<keyword>` — the shared map in
	 * NSCalendar.h, exactly as NSDateFormatter uses it. */
	{
		NSCalendar *calendar = [self fnCalendar];

		if (calendar != nil) {
			keyword = fn_calendar_keyword([calendar identifier]);
		}
	}
	snprintf(localeText, sizeof localeText, "%s%s%s", [[locale localeIdentifier] UTF8String],
		 keyword != NULL ? "@calendar=" : "", keyword != NULL ? keyword : "");
	if (localeText[0] == 0) {
		return;
	}

	/* The skeleton's BYTES first, so the borrowed -UTF8String of the zone below has nothing left to
	 * clobber — the ordering trap NSDateFormatter's own rebuild documents. */
	u_strFromUTF8(uskeleton, FN_DIF_MAX, &skeletonLen, skeleton, -1, &status);
	if (U_FAILURE(status)) {
		return;
	}
	zoneLen = fn_dif_zone([self fnTimeZone], zone, 64);

	status = U_ZERO_ERROR;
	_formatter = udtitvfmt_open(localeText, uskeleton, skeletonLen,
				    zoneLen > 0 ? zone : NULL, zoneLen, &status);
	if (U_FAILURE(status) || _formatter == NULL) {
		_formatter = NULL;
		return;
	}
}

- (nullable NSString *)stringFromDate:(NSDate *)fromDate toDate:(NSDate *)toDate
{
	UChar text[FN_DIF_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (fromDate == nil || toDate == nil) {
		return nil;
	}
	if (_formatter == NULL) {
		return nil;
	}
	length = udtitvfmt_format((const UDateIntervalFormat *)_formatter,
				  (UDate)([fromDate timeIntervalSince1970] * 1000.0),
				  (UDate)([toDate timeIntervalSince1970] * 1000.0),
				  text, FN_DIF_MAX, NULL, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	{
		char bytes[FN_DIF_MAX * 4];
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

- (nullable NSString *)stringFromDateInterval:(NSDateInterval *)dateInterval
{
	if (dateInterval == nil) {
		return nil;
	}
	return [self stringFromDate:[dateInterval startDate] toDate:[dateInterval endDate]];
}

- (NSDateIntervalFormatterStyle)dateStyle { return _dateStyle; }

- (void)setDateStyle:(NSDateIntervalFormatterStyle)style
{
	if (style == _dateStyle) {
		return;
	}
	_dateStyle = style;
	[self fnRebuild];
}

- (NSDateIntervalFormatterStyle)timeStyle { return _timeStyle; }

- (void)setTimeStyle:(NSDateIntervalFormatterStyle)style
{
	if (style == _timeStyle) {
		return;
	}
	_timeStyle = style;
	[self fnRebuild];
}

- (nullable NSString *)dateTemplate { return _dateTemplate; }

- (void)setDateTemplate:(nullable NSString *)value
{
	/* COPIED: a caller may hand in an NSMutableString and edit it afterwards, and the formatter must
	 * not change its behaviour because of it (Apple declares the property `copy`). */
	id copy = [value copy];
	[_dateTemplate release];
	_dateTemplate = copy;
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
	[self fnRebuild];
}

- (NSLocale *)locale { return [self fnLocale]; }

- (void)setLocale:(nullable NSLocale *)value
{
	if (value == _locale) {
		return;
	}
	id copy = [value copy];
	[_locale release];
	_locale = copy;
	[self fnRebuild];
}

- (NSTimeZone *)timeZone { return [self fnTimeZone]; }

- (void)setTimeZone:(nullable NSTimeZone *)value
{
	if (value == _timeZone) {
		return;
	}
	id copy = [value copy];
	[_timeZone release];
	_timeZone = copy;
	[self fnRebuild];
}

@end
