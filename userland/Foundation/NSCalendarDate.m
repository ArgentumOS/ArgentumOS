/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCalendarDate.m — an NSDate with a format and a zone (§62.67). MANUAL OWNERSHIP.
 *
 * THE WHOLE CLASS IS ONE INSTANT AND TWO DECISIONS, and the C library does the arithmetic: the instant is
 * NSDate's own (seconds since 1970), the fields come from `gmtime_r` of (instant + the zone's offset FOR THAT
 * INSTANT), and the two strings come from `strftime`/`strptime` — the same format language "calendar format"
 * names, which is why the doors are spelled after it.
 *
 * WHY gmtime_r AND AN OFFSET RATHER THAN localtime_r: this system's time zone comes from the FSH configuration
 * and reaches the library as an NSTimeZone (ICU answers it), NOT through the C library's TZ or /etc/localtime.
 * Breaking down the instant in UTC and adding the zone's OWN offset keeps the zone the RECEIVER'S — which is
 * what -setTimeZone: promises — and it makes the daylight-saving question the zone's (`-secondsFromGMTForDate:`,
 * which NSTimeZone documents as date-dependent) instead of the process's.
 *
 * AND timegm, NOT mktime, ON THE WAY BACK: mktime would apply the C library's zone ON TOP of the offset already
 * applied here, which is the classic double-count. The two helpers below must stay exact inverses, or a round
 * trip through this class drifts.
 */

/* FIRST, AND BEFORE ANY INCLUDE, BECAUSE IT HAS TO BE: `strptime` is XSI and `timegm` is a GNU/BSD extension, so
 * a glibc host declares neither unless something asks — and asking for XSI alone would take glibc's DEFAULT
 * declarations away from `timegm`, so it is the GNU switch that satisfies both. The guest's musl answers to the
 * same switch, and guarding it keeps a caller's own definition intact. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#import <Foundation/NSCalendarDate.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>
#import <Foundation/NSLocale.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* THE FORMAT A NEW DATE STARTS WITH. Apple's page publishes the property and no value, so this is ours: the
 * shape a caller can both print and parse, with the zone stated (§11.6.1 D2). */
static NSString *const FNDefaultCalendarFormat = @"%Y-%m-%d %H:%M:%S %z";

/* 2001-01-01, this library's reference date, in seconds since 1970 — the bridge between NSDate's own unit and
 * the one the C library's time functions use. */
#define FN_REFERENCE_INTERVAL 978307200.0

/* THE LAST DAY OF A MONTH, which is what a CALENDAR answers when a month is added to a date the next month has
 * no such day for (see -dateByAddingYears:…: — 31 January plus one month is 28 February). */
static int fn_days_in_month(int year, int month)
{
	static const int lengths[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

	if (month < 1 || month > 12) {
		return 31;
	}
	return lengths[month] + (month == 2 && leap ? 1 : 0);
}

/* THE ZONE'S OFFSET AT AN INSTANT: the zone is asked about the DATE, not about "now", which is the difference
 * between a calendar that handles daylight saving and one that handles it only at the moment it is asked. */
static NSInteger fn_offset_for(NSTimeZone *zone, double interval)
{
	NSDate *when;
	NSInteger offset;

	if (zone == nil) {
		return 0;
	}
	when = [[NSDate alloc] initWithTimeIntervalSinceReferenceDate:(interval - FN_REFERENCE_INTERVAL)];
	offset = [zone secondsFromGMTForDate:when];
	[when release];
	return offset;
}

/* THE INSTANT AS FIELDS IN `zone` — and the fields SAY WHICH ZONE THEY ARE IN, through `tm_gmtoff`, because
 * `strftime`'s `%z` and `%Z` read that member rather than anything the caller passes. gmtime_r() leaves it at
 * zero (it broke the instant down as UTC), so a date in UTC+1 printed "+0000" until the probe caught it: the
 * offset has to be written back into the field set for the text to agree with the fields. */
static void fn_break_down(NSTimeZone *zone, double interval, struct tm *out)
{
	NSInteger offset = fn_offset_for(zone, interval);
	time_t t = (time_t)(interval + (double)offset);

	memset(out, 0, sizeof(*out));
	gmtime_r(&t, out);
	out->tm_gmtoff = (long)offset;
	out->tm_zone = (offset == 0 ? "UTC" : NULL);
}

/* FIELDS BACK TO AN INSTANT: timegm minus the zone's offset — the reverse of fn_break_down. */
static double fn_compose(NSTimeZone *zone, const struct tm *fields)
{
	struct tm copy = *fields;
	double interval = (double)timegm(&copy);

	return interval - (double)fn_offset_for(zone, interval);
}

/* THE LOCALE IDENTIFIER TO APPLY, if the caller's locale object is one this library knows. */
static NSString *fn_locale_identifier(id locale)
{
	if (locale != nil && [locale isKindOfClass:[NSLocale class]]) {
		return [(NSLocale *)locale localeIdentifier];
	}
	return nil;
}

@implementation NSCalendarDate

+ (id)calendarDate
{
	return [[[self alloc] init] autorelease];
}

+ (nullable id)dateWithString:(NSString *)description calendarFormat:(NSString *)format
{
	return [[[self alloc] initWithString:description calendarFormat:format] autorelease];
}

+ (nullable id)dateWithString:(NSString *)description
	       calendarFormat:(NSString *)format
		       locale:(nullable id)locale
{
	return [[[self alloc] initWithString:description calendarFormat:format locale:locale] autorelease];
}

+ (nullable id)dateWithYear:(NSInteger)year
		      month:(NSUInteger)month
			day:(NSUInteger)day
		       hour:(NSUInteger)hour
		     minute:(NSUInteger)minute
		     second:(NSUInteger)second
		   timeZone:(nullable NSTimeZone *)aTimeZone
{
	return [[[self alloc] initWithYear:year
				     month:month
				       day:day
				      hour:hour
				    minute:minute
				    second:second
				  timeZone:aTimeZone] autorelease];
}

/* THE TWO CONSTANT DATES, ANSWERED AS THIS CLASS rather than as the NSDate the superclass would build. */
+ (instancetype)distantFuture
{
	return [[[self alloc] initWithTimeIntervalSinceReferenceDate:
		 [[NSDate distantFuture] timeIntervalSinceReferenceDate]] autorelease];
}

+ (instancetype)distantPast
{
	return [[[self alloc] initWithTimeIntervalSinceReferenceDate:
		 [[NSDate distantPast] timeIntervalSinceReferenceDate]] autorelease];
}

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_calendarFormat = [FNDefaultCalendarFormat copy];
		_timeZone = [[NSTimeZone systemTimeZone] retain];
	}
	return self;
}

- (void)dealloc
{
	[_calendarFormat release];
	[_timeZone release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

- (nullable NSString *)calendarFormat { return _calendarFormat; }

- (void)setCalendarFormat:(nullable NSString *)format
{
	/* A SNAPSHOT, THROUGH -initWithString: RATHER THAN -copy, for the reason the rest of this library states. */
	NSString *snapshot = format != nil ? [[NSString alloc] initWithString:format] : nil;

	[_calendarFormat release];
	_calendarFormat = snapshot;
}

- (nullable NSTimeZone *)timeZone { return _timeZone; }

- (void)setTimeZone:(nullable NSTimeZone *)aTimeZone
{
	NSTimeZone *kept = [aTimeZone retain];

	[_timeZone release];
	_timeZone = kept;
}

/* ---- THE FIELDS, each computed from the instant in the receiver's zone --------------------------------- */

- (NSInteger)yearOfCommonEra
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_year + 1900;
}

- (NSInteger)monthOfYear
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_mon + 1;
}

- (NSInteger)dayOfMonth
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_mday;
}

/* Sunday is 0, which is what tm_wday already means. */
- (NSInteger)dayOfWeek
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_wday;
}

/* 1-BASED: the day numbers a calendar prints rather than an index. */
- (NSInteger)dayOfYear
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_yday + 1;
}

/* THE DAY NUMBER SINCE THE COMMON ERA, 1-based: 1970-01-01 is day 719163 — the elapsed days between 0001-01-01
 * and that date (719162) plus one — and the answer is taken from the DAY the fields land on rather than from a
 * separate calculation, so it cannot disagree with -dayOfMonth at a boundary. */
- (NSInteger)dayOfCommonEra
{
	time_t t = (time_t)(_timeIntervalSince1970 + (double)fn_offset_for(_timeZone, _timeIntervalSince1970));
	struct tm fields;

	gmtime_r(&t, &fields);
	fields.tm_hour = 0;
	fields.tm_min = 0;
	fields.tm_sec = 0;
	return (NSInteger)(timegm(&fields) / 86400) + 719163;
}

- (NSInteger)hourOfDay
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_hour;
}

- (NSInteger)minuteOfHour
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_min;
}

- (NSInteger)secondOfMinute
{
	struct tm fields;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	return (NSInteger)fields.tm_sec;
}

/* ---- ARITHMETIC -------------------------------------------------------------------------------------- */

/* COMPONENT BY COMPONENT AND CALENDAR-AWARE, which is the header's stated choice: the receiver's fields are
 * broken down, the components added to THEM, and the result composed again — so adding a month to 31 January
 * lands where the calendar says (normalised by timegm) rather than a fixed thirty days later. */
- (NSCalendarDate *)dateByAddingYears:(NSInteger)year
			       months:(NSInteger)month
				 days:(NSInteger)day
				hours:(NSInteger)hour
			      minutes:(NSInteger)minute
			      seconds:(NSInteger)second
{
	struct tm fields;
	NSCalendarDate *result;

	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	fields.tm_year += (int)year;
	fields.tm_mon += (int)month;
	{
		/* YEARS AND MONTHS FIRST, AND THE DAY IS CLAMPED TO THE MONTH THEY LAND IN. This is the choice the
		 * header states and the probe asserts: 31 JANUARY PLUS ONE MONTH IS 28 FEBRUARY. Letting the fields
		 * carry instead — which is what timegm would do on its own — answers 2 or 3 March, the answer a
		 * fixed count of days gives, and the opposite of "the calendar decides". */
		int last;

		while (fields.tm_mon > 11) { fields.tm_mon -= 12; fields.tm_year++; }
		while (fields.tm_mon < 0) { fields.tm_mon += 12; fields.tm_year--; }
		last = fn_days_in_month(fields.tm_year + 1900, fields.tm_mon + 1);
		if (fields.tm_mday > last) {
			fields.tm_mday = last;
		}
	}
	fields.tm_mday += (int)day;
	fields.tm_hour += (int)hour;
	fields.tm_min += (int)minute;
	fields.tm_sec += (int)second;
	result = [[NSCalendarDate alloc] initWithTimeIntervalSinceReferenceDate:0];
	result->_timeIntervalSince1970 = fn_compose(_timeZone, &fields);
	[result setTimeZone:_timeZone];
	[result setCalendarFormat:_calendarFormat];
	return [result autorelease];
}

/* LARGEST COMPONENT FIRST, which is the only order in which the answer is well defined: walk the calendar
 * forward (or backward) by whole years while that does not overshoot, then by whole months, then hand the
 * remainder to the clock. EVERY component carries the sign of the whole difference. */
- (void)years:(nullable NSInteger *)yp
       months:(nullable NSInteger *)mop
	 days:(nullable NSInteger *)dp
	hours:(nullable NSInteger *)hp
      minutes:(nullable NSInteger *)mip
      seconds:(nullable NSInteger *)sp
    sinceDate:(NSCalendarDate *)date
{
	double target = [self timeIntervalSince1970];
	int forward = target >= [date timeIntervalSince1970];
	NSInteger years = 0;
	NSInteger months = 0;
	NSInteger days = 0;
	NSInteger hours = 0;
	NSInteger minutes = 0;
	NSInteger seconds = 0;
	NSCalendarDate *walking = [[NSCalendarDate alloc] init];
	NSCalendarDate *candidate;

	[walking setTimeZone:_timeZone];
	walking->_timeIntervalSince1970 = [date timeIntervalSince1970];
	for (;;) {
		double next;

		candidate = [walking dateByAddingYears:(forward ? 1 : -1) months:0 days:0 hours:0 minutes:0
					       seconds:0];
		next = [candidate timeIntervalSince1970];
		if (forward ? next > target : next < target) {
			break;
		}
		[walking release];
		walking = [candidate retain];
		years++;
	}
	for (;;) {
		double next;

		candidate = [walking dateByAddingYears:0 months:(forward ? 1 : -1) days:0 hours:0 minutes:0
					       seconds:0];
		next = [candidate timeIntervalSince1970];
		if (forward ? next > target : next < target) {
			break;
		}
		[walking release];
		walking = [candidate retain];
		months++;
	}
	{
		double rest = target - [walking timeIntervalSince1970];
		NSInteger whole;

		if (rest < 0) {
			rest = -rest;
		}
		whole = (NSInteger)rest;
		days = whole / 86400;
		whole -= days * 86400;
		hours = whole / 3600;
		whole -= hours * 3600;
		minutes = whole / 60;
		whole -= minutes * 60;
		seconds = whole;
		/* EVERY COMPONENT CARRIES THE SIGN OF THE WHOLE DIFFERENCE, so "-2 months, 0 days" never reads as
		 * "2 months, minus nothing". */
		if (!forward) {
			years = -years;
			months = -months;
			days = -days;
			hours = -hours;
			minutes = -minutes;
			seconds = -seconds;
		}
	}
	if (yp != NULL) { *yp = years; }
	if (mop != NULL) { *mop = months; }
	if (dp != NULL) { *dp = days; }
	if (hp != NULL) { *hp = hours; }
	if (mip != NULL) { *mip = minutes; }
	if (sp != NULL) { *sp = seconds; }
	[walking release];
}

/* ---- FORMATTING AND PARSING -------------------------------------------------------------------------- */

- (NSString *)descriptionWithCalendarFormat:(NSString *)format
{
	return [self descriptionWithCalendarFormat:format locale:nil];
}

- (NSString *)descriptionWithCalendarFormat:(NSString *)format locale:(nullable id)locale
{
	struct tm fields;
	char buffer[512];
	locale_t applied = (locale_t)0;
	locale_t previous = (locale_t)0;
	NSString *identifier = fn_locale_identifier(locale);
	size_t written;

	/* THE LOCALE IS APPLIED THROUGH THE C LIBRARY'S OWN OBJECTS when the caller gave one, and a nil or
	 * unresolvable identifier means the process's locale — which is the honest reading of "no locale given". */
	if (identifier != nil) {
		applied = newlocale(LC_ALL_MASK, [identifier UTF8String], (locale_t)0);
		if (applied != (locale_t)0) {
			previous = uselocale(applied);
		}
	}
	fn_break_down(_timeZone, _timeIntervalSince1970, &fields);
	written = strftime(buffer, sizeof(buffer), [(format != nil ? format : @"") UTF8String], &fields);
	if (applied != (locale_t)0) {
		uselocale(previous);
		freelocale(applied);
	}
	buffer[written < sizeof(buffer) ? written : sizeof(buffer) - 1] = '\0';
	return [NSString stringWithUTF8String:buffer];
}

- (NSString *)descriptionWithLocale:(nullable id)locale
{
	return [self descriptionWithCalendarFormat:_calendarFormat locale:locale];
}

- (nullable id)initWithString:(NSString *)description
{
	return [self initWithString:description calendarFormat:FNDefaultCalendarFormat locale:nil];
}

- (nullable id)initWithString:(NSString *)description calendarFormat:(NSString *)format
{
	return [self initWithString:description calendarFormat:format locale:nil];
}

- (nullable id)initWithString:(NSString *)description
	       calendarFormat:(NSString *)format
		       locale:(nullable id)locale
{
	struct tm fields;
	locale_t applied = (locale_t)0;
	locale_t previous = (locale_t)0;
	NSString *identifier = fn_locale_identifier(locale);
	const char *text;
	char *end;

	if (description == nil || format == nil) {
		[self release];
		return nil;
	}
	if (identifier != nil) {
		applied = newlocale(LC_ALL_MASK, [identifier UTF8String], (locale_t)0);
		if (applied != (locale_t)0) {
			previous = uselocale(applied);
		}
	}
	memset(&fields, 0, sizeof(fields));
	text = [description UTF8String];
	end = text != NULL ? strptime(text, [format UTF8String], &fields) : NULL;
	if (applied != (locale_t)0) {
		uselocale(previous);
		freelocale(applied);
	}
	if (end == NULL) {
		/* A STRING THAT DOES NOT MATCH THE FORMAT ANSWERS NIL rather than a date nobody asked for. */
		[self release];
		return nil;
	}
	/* NO YEAR NORMALISATION IS NEEDED, AND ONE WAS THE BUG THE PROBE CAUGHT: strptime ALREADY answers struct
	 * tm's own convention — tm_year is the year MINUS 1900, for `%Y` as much as for `%y` (a two-digit year's
	 * 1969/2000 century rule is applied by strptime itself). The first version of this code "fixed up" a year
	 * it took to be absolute, so 2026 parsed as 226 and printed as "226-09-27". */
	fields.tm_isdst = -1;
	self = [self init];
	if (self != nil) {
		/* A FORMAT THAT STATES A NUMERIC ZONE WINS, and it has to: the string says what it means, so the
		 * offset strptime read out of it is what the instant is built from rather than the receiver's own
		 * zone. That is also what makes a round trip through the DEFAULT format exact in any zone — the text
		 * carries `%z`, so re-reading it does not depend on where the reading happens. (%Z, a zone NAME, is
		 * printed and is not resolved here: turning a name into an offset is a lookup this class does not
		 * own.) */
		if (strstr([format UTF8String], "%z") != NULL) {
			/* THE OFFSET IS READ OUT FIRST, BECAUSE timegm OVERWRITES IT: timegm fills in the tm_gmtoff
			 * and tm_zone of the struct it is handed (with GMT's own, since that is the zone it is
			 * converting FOR), so reading the parsed offset in the same expression as the call that
			 * destroys it — and C does not order those two — cost a silent hour of drift. */
			long stated = (long)fields.tm_gmtoff;

			_timeIntervalSince1970 = (double)timegm(&fields) - (double)stated;
		} else {
			_timeIntervalSince1970 = fn_compose(_timeZone, &fields);
		}
		[self setCalendarFormat:format];
	}
	return self;
}

- (nullable id)initWithYear:(NSInteger)year
		      month:(NSUInteger)month
			day:(NSUInteger)day
		       hour:(NSUInteger)hour
		     minute:(NSUInteger)minute
		     second:(NSUInteger)second
		   timeZone:(nullable NSTimeZone *)aTimeZone
{
	struct tm fields;

	memset(&fields, 0, sizeof(fields));
	fields.tm_year = (int)year - 1900;
	fields.tm_mon = (int)month - 1;
	fields.tm_mday = (int)day;
	fields.tm_hour = (int)hour;
	fields.tm_min = (int)minute;
	fields.tm_sec = (int)second;
	fields.tm_isdst = -1;
	self = [self init];
	if (self != nil) {
		if (aTimeZone != nil) {
			[self setTimeZone:aTimeZone];
		}
		_timeIntervalSince1970 = fn_compose(_timeZone, &fields);
	}
	return self;
}

@end
