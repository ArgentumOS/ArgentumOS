/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCalendar.m — every calendar ICU has, through ICU. docs/design/foundation-plan.md §10, F13.7b.
 *
 * WHAT THIS FILE USED TO BE, and why it is not any more: F7 shipped the Gregorian calendar "as
 * rules" on libc's field arithmetic and refused the other identifiers by name, because their tables
 * — the Hebrew molad, the Islamic sighting convention, the Chinese solstice, the Japanese era list
 * — are data this library did not have. §10 is the answer to that: ICU HAS the data, so the
 * identifiers are read out of it and the refusal is gone.
 *
 * ICU SELECTS A CALENDAR BY LOCALE KEYWORD, not by an enum: `@calendar=hebrew` in the locale ID is
 * the whole mechanism. That is why the mapping below is a string function rather than a table of
 * opaque handles, and why an unknown identifier answers NULL there and nil from the initialiser —
 * the way Cocoa treats a name it does not know.
 *
 * WHAT ICU GIVES, and what the libc version had to write itself:
 *   ucal_get        the fields of an instant;
 *   ucal_set        fields -> an instant, in LENIENT mode, so 30 February rolls as timegm did;
 *   ucal_add        THE CLAMP: 31 January + 1 month is the last day of February — now in EVERY
 *                   calendar this class supports, not only in Gregorian;
 *   ucal_roll       the wrap form, which is what NSCalendarWrapComponents means;
 *   ucal_getLimit   how many days a month has, how many months a year has — in the CALENDAR's own
 *                   rules, which is what "13 months" means in a leap year of the Hebrew calendar;
 *   UCAL_FIRST_DAY_OF_WEEK / UCAL_MINIMAL_DAYS_IN_FIRST_WEEK   the week rule, as attributes.
 *
 * AND ONE THING THIS CHANGE ALSO FIXES: UCAL_DAY_OF_WEEK is 1 = Sunday ABSOLUTELY, where the libc
 * path ROTATED `weekday` by the calendar's firstWeekday — which Cocoa does not do. A calendar's
 * week rule decides which week a day is in; it does not renumber Sunday.
 */

#import <Foundation/NSCalendar.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>

#include <unicode/ucal.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <string.h>

NSString *const NSCalendarIdentifierGregorian = @"gregorian";
NSString *const NSCalendarIdentifierISO8601 = @"iso8601";
NSString *const NSCalendarIdentifierBuddhist = @"buddhist";
NSString *const NSCalendarIdentifierChinese = @"chinese";
NSString *const NSCalendarIdentifierCoptic = @"coptic";
NSString *const NSCalendarIdentifierEthiopicAmeteMihret = @"ethiopic";
NSString *const NSCalendarIdentifierEthiopicAmeteAlem = @"ethioaa";
NSString *const NSCalendarIdentifierHebrew = @"hebrew";
NSString *const NSCalendarIdentifierIndian = @"indian";
NSString *const NSCalendarIdentifierIslamic = @"islamic";
NSString *const NSCalendarIdentifierIslamicCivil = @"islamic-civil";
NSString *const NSCalendarIdentifierIslamicTabular = @"islamic-tbla";
NSString *const NSCalendarIdentifierIslamicUmmAlQura = @"islamic-umalqura";
NSString *const NSCalendarIdentifierJapanese = @"japanese";
NSString *const NSCalendarIdentifierPersian = @"persian";
NSString *const NSCalendarIdentifierRepublicOfChina = @"roc";

#define FN_CAL_ID_MAX 64
#define FN_DAY_SECONDS 86400

/*
 * IDENTIFIER -> ICU'S CALENDAR KEYWORD, or NULL for a name this library does not know (which the
 * initialiser turns into nil). Every value below is ICU's own documented keyword, and the
 * comparison is against the CONSTANTS, so a caller cannot smuggle in a keyword this class has not
 * promised to support.
 */
static const char *fn_cal_keyword(NSString *identifier)
{
	if ([identifier isEqualToString:NSCalendarIdentifierGregorian]) return "gregorian";
	if ([identifier isEqualToString:NSCalendarIdentifierISO8601]) return "iso8601";
	if ([identifier isEqualToString:NSCalendarIdentifierBuddhist]) return "buddhist";
	if ([identifier isEqualToString:NSCalendarIdentifierChinese]) return "chinese";
	if ([identifier isEqualToString:NSCalendarIdentifierCoptic]) return "coptic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteMihret]) return "ethiopic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteAlem]) return "ethioaa";
	if ([identifier isEqualToString:NSCalendarIdentifierHebrew]) return "hebrew";
	if ([identifier isEqualToString:NSCalendarIdentifierIndian]) return "indian";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamic]) return "islamic";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicCivil]) return "islamic-civil";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicTabular]) return "islamic-tbla";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicUmmAlQura]) return "islamic-umalqura";
	if ([identifier isEqualToString:NSCalendarIdentifierJapanese]) return "japanese";
	if ([identifier isEqualToString:NSCalendarIdentifierPersian]) return "persian";
	if ([identifier isEqualToString:NSCalendarIdentifierRepublicOfChina]) return "roc";
	return NULL;
}

/*
 * THE ZONE, as ICU wants it: a NAMED zone goes in as its IANA identifier (so its DST rules apply),
 * a fixed-offset one as GMT±HH:MM — the same rule NSDateFormatter.m and NSTimeZone.m use, and the
 * reason all three agree about what "the calendar's zone" means.
 */
static int32_t fn_cal_zone(NSTimeZone *zone, UChar *dest, int32_t cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	NSString *name;
	NSInteger seconds;
	int sign, hours, minutes;
	char text[32];

	if (zone == nil) {
		return 0;
	}
	name = [zone name];
	if (name == nil) {
		return 0;
	}
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

/* ONE PLACE OPENS A CALENDAR, so the locale keyword, the zone and the week rule are set together
 * and cannot drift apart between the methods that use them. */
static UCalendar *fn_cal_open(NSString *identifier, NSTimeZone *zone, NSUInteger firstWeekday,
			      NSUInteger minimumDaysInFirstWeek, UErrorCode *status)
{
	const char *keyword = fn_cal_keyword(identifier);
	char localeText[FN_CAL_ID_MAX];
	UChar zoneText[FN_CAL_ID_MAX];
	int32_t zoneLength;
	UCalendar *calendar;

	if (keyword == NULL) {
		*status = U_ILLEGAL_ARGUMENT_ERROR;
		return NULL;
	}
	snprintf(localeText, sizeof localeText, "en_US@calendar=%s", keyword);
	zoneLength = fn_cal_zone(zone, zoneText, FN_CAL_ID_MAX);
	calendar = ucal_open(zoneLength > 0 ? zoneText : NULL, zoneLength, localeText,
			     UCAL_DEFAULT, status);
	if (U_FAILURE(*status) || calendar == NULL) {
		return NULL;
	}
	ucal_setAttribute(calendar, UCAL_FIRST_DAY_OF_WEEK, (int32_t)firstWeekday);
	ucal_setAttribute(calendar, UCAL_MINIMAL_DAYS_IN_FIRST_WEEK, (int32_t)minimumDaysInFirstWeek);
	return calendar;
}

static NSDate *fn_cal_date(UCalendar *calendar, UErrorCode *status)
{
	double millis = (double)ucal_getMillis(calendar, status);

	if (U_FAILURE(*status)) {
		return nil;
	}
	return [NSDate dateWithTimeIntervalSince1970:millis / 1000.0];
}

@implementation NSCalendar

NSString *const NSCalendarDayChangedNotification = @"NSCalendarDayChangedNotification";
NSCalendarIdentifier const NSCalendarIdentifierBangla = @"bangla";
NSCalendarIdentifier const NSCalendarIdentifierDangi = @"dangi";
NSCalendarIdentifier const NSCalendarIdentifierGujarati = @"gujarati";
NSCalendarIdentifier const NSCalendarIdentifierKannada = @"kannada";
NSCalendarIdentifier const NSCalendarIdentifierMalayalam = @"malayalam";
NSCalendarIdentifier const NSCalendarIdentifierMarathi = @"marathi";
NSCalendarIdentifier const NSCalendarIdentifierOdia = @"odia";
NSCalendarIdentifier const NSCalendarIdentifierTamil = @"tamil";
NSCalendarIdentifier const NSCalendarIdentifierTelugu = @"telugu";
NSCalendarIdentifier const NSCalendarIdentifierVietnamese = @"vietnamese";
NSCalendarIdentifier const NSCalendarIdentifierVikram = @"vikram";

+ (NSCalendar *)currentCalendar
{
	return [[self alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
}

+ (NSCalendar *)calendarWithIdentifier:(NSString *)identifier
{
	return [[self alloc] initWithCalendarIdentifier:identifier];
}

- (id)init
{
	return [self initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
}

- (id)initWithCalendarIdentifier:(NSString *)identifier
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE NAME IS VALIDATED AGAINST THE KEYWORD MAP, which is what makes an unknown identifier
	 * answer nil — Cocoa's answer too, and the reason this is nullable. */
	if (identifier == nil || fn_cal_keyword(identifier) == NULL) {
		return nil;
	}
	_identifier = [identifier copy];
	/* THE SYSTEM ZONE, not a hard-coded UTC: with F13.7a's database the honest default is the one
	 * the system reports (ICU's default, which follows TZ). */
	_timeZone = [NSTimeZone systemTimeZone];
	_firstWeekday = 1;			/* Sunday, Cocoa's default for a new calendar */
	_minimumDaysInFirstWeek = 1;
	return self;
}

- (NSString *)identifier
{
	return _identifier;
}

- (NSTimeZone *)timeZone
{
	return _timeZone;
}

- (void)setTimeZone:(NSTimeZone *)zone
{
	if (zone == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-setTimeZone: needs a time zone; a calendar always has one"];
	}
	_timeZone = zone;
}

- (NSUInteger)firstWeekday { return _firstWeekday; }
- (void)setFirstWeekday:(NSUInteger)weekday
{
	if (weekday < 1 || weekday > 7) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-setFirstWeekday: takes 1 (Sunday) through 7 (Saturday), not %lu",
				   (unsigned long)weekday];
	}
	_firstWeekday = weekday;
}
- (NSUInteger)minimumDaysInFirstWeek { return _minimumDaysInFirstWeek; }
- (void)setMinimumDaysInFirstWeek:(NSUInteger)days
{
	if (days < 1 || days > 7) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-setMinimumDaysInFirstWeek: takes 1 through 7, not %lu",
				   (unsigned long)days];
	}
	_minimumDaysInFirstWeek = days;
}

/* --- conversion ----------------------------------------------------------- */

- (NSDateComponents *)components:(NSCalendarUnit)units fromDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	NSDateComponents *out;

	if (calendar == NULL) {
		return [[NSDateComponents alloc] init];
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return [[NSDateComponents alloc] init];
	}
	out = [[NSDateComponents alloc] init];

	/* EVERY unit is read from the SAME open calendar, so a caller asking for two of them and one
	 * asking for all of them cannot get answers from different moments. */
	if (units & NSCalendarUnitEra) {
		[out setEra:(NSInteger)ucal_get(calendar, UCAL_ERA, &status)];
	}
	if (units & NSCalendarUnitYear) {
		[out setYear:(NSInteger)ucal_get(calendar, UCAL_YEAR, &status)];
	}
	if (units & NSCalendarUnitQuarter) {
		/* ICU has no quarter field: a quarter is a rule over the month, so it is computed. */
		[out setQuarter:(NSInteger)(ucal_get(calendar, UCAL_MONTH, &status) / 3 + 1)];
	}
	if (units & NSCalendarUnitMonth) {
		[out setMonth:(NSInteger)(ucal_get(calendar, UCAL_MONTH, &status) + 1)];
	}
	if (units & NSCalendarUnitDay) {
		[out setDay:(NSInteger)ucal_get(calendar, UCAL_DATE, &status)];
	}
	if (units & NSCalendarUnitHour) {
		[out setHour:(NSInteger)ucal_get(calendar, UCAL_HOUR_OF_DAY, &status)];
	}
	if (units & NSCalendarUnitMinute) {
		[out setMinute:(NSInteger)ucal_get(calendar, UCAL_MINUTE, &status)];
	}
	if (units & NSCalendarUnitSecond) {
		[out setSecond:(NSInteger)ucal_get(calendar, UCAL_SECOND, &status)];
	}
	if (units & NSCalendarUnitNanosecond) {
		[out setNanosecond:(NSInteger)ucal_get(calendar, UCAL_MILLISECOND, &status) * 1000000];
	}
	if (units & NSCalendarUnitWeekday) {
		[out setWeekday:(NSInteger)ucal_get(calendar, UCAL_DAY_OF_WEEK, &status)];
	}
	if (units & NSCalendarUnitWeekdayOrdinal) {
		[out setWeekdayOrdinal:(NSInteger)ucal_get(calendar, UCAL_DAY_OF_WEEK_IN_MONTH,
							   &status)];
	}
	if (units & NSCalendarUnitWeekOfMonth) {
		[out setWeekOfMonth:(NSInteger)ucal_get(calendar, UCAL_WEEK_OF_MONTH, &status)];
	}
	if (units & NSCalendarUnitWeekOfYear) {
		[out setWeekOfYear:(NSInteger)ucal_get(calendar, UCAL_WEEK_OF_YEAR, &status)];
	}
	if (units & NSCalendarUnitYearForWeekOfYear) {
		[out setYearForWeekOfYear:(NSInteger)ucal_get(calendar, UCAL_YEAR_WOY, &status)];
	}
	ucal_close(calendar);
	return out;
}

- (NSDate *)dateFromComponents:(NSDateComponents *)components
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	NSInteger year;
	NSInteger month;
	NSInteger day;
	NSInteger hour;
	NSInteger minute;
	NSInteger second;
	NSInteger era;
	NSDate *answer;

	if (components == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateFromComponents: needs components"];
	}
	year = [components year];
	if (year == NSDateComponentUndefined) {
		/* The week fields are ANSWERS, not a second way to say a date, and without a year there
		 * is nothing to convert. */
		return nil;
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek,
			       &status);
	if (calendar == NULL) {
		return nil;
	}
	month = [components month];
	day = [components day];
	hour = [components hour];
	minute = [components minute];
	second = [components second];
	era = [components era];

	/* ICU'S LENIENT MODE IS THE ONE timegm USED TO PROVIDE: an out-of-range field rolls, which is
	 * how the calendar answers 2026-02-30 without a second validity rule. A field the caller left
	 * undefined takes COCOA's default (1 for month and day, 0 for the time), not ICU's "now".
	 *
	 * LENIENCY IS AN ATTRIBUTE, not a function of its own: there is no ucal_setLenient (measured —
	 * that was a compile error), only UCAL_LENIENT on the attribute door. */
	ucal_setAttribute(calendar, UCAL_LENIENT, 1);
	ucal_clear(calendar);
	if (era != NSDateComponentUndefined) {
		ucal_set(calendar, UCAL_ERA, (int32_t)era);
	}
	ucal_set(calendar, UCAL_YEAR, (int32_t)year);
	ucal_set(calendar, UCAL_MONTH, (int32_t)((month == NSDateComponentUndefined ? 1 : month) - 1));
	ucal_set(calendar, UCAL_DATE, (int32_t)(day == NSDateComponentUndefined ? 1 : day));
	ucal_set(calendar, UCAL_HOUR_OF_DAY,
		 (int32_t)(hour == NSDateComponentUndefined ? 0 : hour));
	ucal_set(calendar, UCAL_MINUTE,
		 (int32_t)(minute == NSDateComponentUndefined ? 0 : minute));
	ucal_set(calendar, UCAL_SECOND,
		 (int32_t)(second == NSDateComponentUndefined ? 0 : second));
	ucal_set(calendar, UCAL_MILLISECOND, 0);
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

/* --- arithmetic ----------------------------------------------------------- */

- (NSDate *)dateByAddingComponents:(NSDateComponents *)components
			    toDate:(NSDate *)date
			   options:(NSCalendarOptions)options
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	NSInteger years, months, days, hours, minutes, seconds;
	BOOL roll;
	NSDate *answer;

	if (options & ~NSCalendarOptionsWrapComponents) {
		/* The other options describe a SEARCH over candidate dates; this calendar answers by
		 * arithmetic, so it refuses rather than pretending. */
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateByAddingComponents: takes NSCalendarOptionsNone or "
				   "NSCalendarOptionsWrapComponents here: the search options describe a "
				   "search, not arithmetic"];
	}
	roll = (options & NSCalendarOptionsWrapComponents) != 0;
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek,
			       &status);
	if (calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	years = [components year];
	months = [components month];
	days = [components day];
	hours = [components hour];
	minutes = [components minute];
	seconds = [components second];

	/* ICU'S add IS THE CLAMP — 31 January + 1 month is the last day of February, in WHATEVER
	 * calendar this is — and roll is the wrapping form NSCalendarWrapComponents asks for. */
	if (years != NSDateComponentUndefined && years != 0) {
		if (roll) {
			ucal_roll(calendar, UCAL_YEAR, (int32_t)years, &status);
		} else {
			ucal_add(calendar, UCAL_YEAR, (int32_t)years, &status);
		}
	}
	if (months != NSDateComponentUndefined && months != 0) {
		if (roll) {
			ucal_roll(calendar, UCAL_MONTH, (int32_t)months, &status);
		} else {
			ucal_add(calendar, UCAL_MONTH, (int32_t)months, &status);
		}
	}
	if (days != NSDateComponentUndefined && days != 0) {
		ucal_add(calendar, UCAL_DATE, (int32_t)days, &status);
	}
	if (hours != NSDateComponentUndefined && hours != 0) {
		ucal_add(calendar, UCAL_HOUR_OF_DAY, (int32_t)hours, &status);
	}
	if (minutes != NSDateComponentUndefined && minutes != 0) {
		ucal_add(calendar, UCAL_MINUTE, (int32_t)minutes, &status);
	}
	if (seconds != NSDateComponentUndefined && seconds != 0) {
		ucal_add(calendar, UCAL_SECOND, (int32_t)seconds, &status);
	}
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

- (NSDate *)dateByAddingUnit:(NSCalendarUnit)unit
		       value:(NSInteger)value
		      toDate:(NSDate *)date
		     options:(NSCalendarOptions)options
{
	NSDateComponents *amount = [[NSDateComponents alloc] init];

	if (unit == NSCalendarUnitYear) {
		[amount setYear:value];
	} else if (unit == NSCalendarUnitMonth) {
		[amount setMonth:value];
	} else if (unit == NSCalendarUnitDay) {
		[amount setDay:value];
	} else if (unit == NSCalendarUnitHour) {
		[amount setHour:value];
	} else if (unit == NSCalendarUnitMinute) {
		[amount setMinute:value];
	} else if (unit == NSCalendarUnitSecond) {
		[amount setSecond:value];
	} else {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateByAddingUnit: takes a unit this calendar can add "
				   "(year, month, day, hour, minute, second). An ERA is not one of them: "
				   "how many years an era is, is a table, not a rule."];
	}
	return [self dateByAddingComponents:amount toDate:date options:options];
}

/* THE FIELD-WISE DIFFERENCE (F13.7e), and the method F7 refused because its option semantics are a
 * table of cases. ICU IS that table: ucal_getFieldDifference walks the calendar forward by whole
 * units and answers how many fit — and THE WALK IS WHY THE FIELDS MUST BE TAKEN LARGEST FIRST,
 * because each call leaves the calendar where the last one stopped. That is what makes "1 month and
 * 1 day" from 31 January to 1 March a measurement rather than a division: the month takes the walk
 * to 28 February (the clamp), and the day is what is left. */
- (NSDateComponents *)components:(NSCalendarUnit)units
			fromDate:(NSDate *)startingDate
			  toDate:(NSDate *)resultDate
			 options:(NSCalendarOptions)options
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	NSDateComponents *out;
	UDate target;

	if (options != NSCalendarOptionsNone) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-components:fromDate:toDate:options: takes NSCalendarOptionsNone "
				   "here: wrapping the smaller units is a different question from how many "
				   "whole units fit"];
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek,
			       &status);
	if (calendar == NULL) {
		return [[NSDateComponents alloc] init];
	}
	/* The walk starts at the EARLIER date and is driven toward the later one. */
	ucal_setMillis(calendar, (UDate)([startingDate timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return [[NSDateComponents alloc] init];
	}
	target = (UDate)([resultDate timeIntervalSince1970] * 1000.0);
	out = [[NSDateComponents alloc] init];

	if (units & NSCalendarUnitYear) {
		[out setYear:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_YEAR, &status)];
	}
	if (units & NSCalendarUnitMonth) {
		[out setMonth:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_MONTH,
								 &status)];
	}
	if (units & NSCalendarUnitDay) {
		[out setDay:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_DATE, &status)];
	}
	if (units & NSCalendarUnitHour) {
		[out setHour:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_HOUR_OF_DAY,
								&status)];
	}
	if (units & NSCalendarUnitMinute) {
		[out setMinute:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_MINUTE,
								  &status)];
	}
	if (units & NSCalendarUnitSecond) {
		[out setSecond:(NSInteger)ucal_getFieldDifference(calendar, target, UCAL_SECOND,
								  &status)];
	}
	ucal_close(calendar);
	return out;
}

/* --- ranges --------------------------------------------------------------- */

- (NSRange)rangeOfUnit:(NSCalendarUnit)smaller
		inUnit:(NSCalendarUnit)larger
	       forDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	int32_t limit;

	if (calendar == NULL) {
		return NSMakeRange(0, 0);
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return NSMakeRange(0, 0);
	}
	/* THE ANSWERS COME FROM THE CALENDAR'S OWN LIMITS, which is what makes a Hebrew leap year's
	 * thirteen months and a Gregorian month's 28-31 days the same code path. */
	if ((smaller & NSCalendarUnitDay) && (larger & NSCalendarUnitMonth)) {
		limit = ucal_getLimit(calendar, UCAL_DATE, UCAL_ACTUAL_MAXIMUM, &status);
		if (limit > 0) {
			ucal_close(calendar);
			return NSMakeRange(1, (NSUInteger)limit);
		}
	} else if ((smaller & NSCalendarUnitDay) && (larger & NSCalendarUnitYear)) {
		limit = ucal_getLimit(calendar, UCAL_DAY_OF_YEAR, UCAL_ACTUAL_MAXIMUM, &status);
		if (limit > 0) {
			ucal_close(calendar);
			return NSMakeRange(1, (NSUInteger)limit);
		}
	} else if ((smaller & NSCalendarUnitMonth) && (larger & NSCalendarUnitYear)) {
		limit = ucal_getLimit(calendar, UCAL_MONTH, UCAL_ACTUAL_MAXIMUM, &status);
		if (limit >= 0) {
			ucal_close(calendar);
			return NSMakeRange(1, (NSUInteger)(limit + 1));
		}
	} else if ((smaller & NSCalendarUnitHour) && (larger & NSCalendarUnitDay)) {
		limit = ucal_getLimit(calendar, UCAL_HOUR_OF_DAY, UCAL_ACTUAL_MAXIMUM, &status);
		if (limit >= 0) {
			ucal_close(calendar);
			return NSMakeRange(0, (NSUInteger)(limit + 1));
		}
	} else if ((smaller & NSCalendarUnitMinute) && (larger & NSCalendarUnitHour)) {
		ucal_close(calendar);
		return NSMakeRange(0, 60);
	} else if ((smaller & NSCalendarUnitSecond) && (larger & NSCalendarUnitMinute)) {
		ucal_close(calendar);
		return NSMakeRange(0, 60);
	} else if ((smaller & NSCalendarUnitWeekOfYear) && (larger & NSCalendarUnitYear)) {
		limit = ucal_getLimit(calendar, UCAL_WEEK_OF_YEAR, UCAL_ACTUAL_MAXIMUM, &status);
		if (limit > 0) {
			ucal_close(calendar);
			return NSMakeRange(1, (NSUInteger)limit);
		}
	}
	ucal_close(calendar);
	[NSException raise:NSInvalidArgumentException
		    format:@"-rangeOfUnit:inUnit:forDate: has no rule for that pair here"];
	return NSMakeRange(0, 0);
}

- (BOOL)rangeOfUnit:(NSCalendarUnit)unit
	  startDate:(NSDate * _Nullable * _Nullable)datep
	   interval:(double *)tip
	    forDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	NSInteger length;
	NSDate *start;

	if (calendar == NULL) {
		return NO;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return NO;
	}
	if (unit == NSCalendarUnitSecond) {
		length = 1;
	} else if (unit == NSCalendarUnitMinute) {
		ucal_set(calendar, UCAL_SECOND, 0);
		length = 60;
	} else if (unit == NSCalendarUnitHour) {
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		length = 3600;
	} else if (unit == NSCalendarUnitDay) {
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		length = FN_DAY_SECONDS;
	} else if (unit == NSCalendarUnitMonth) {
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		ucal_set(calendar, UCAL_DATE, 1);
		length = (NSInteger)ucal_getLimit(calendar, UCAL_DATE, UCAL_ACTUAL_MAXIMUM, &status)
			 * FN_DAY_SECONDS;
	} else if (unit == NSCalendarUnitYear) {
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		ucal_set(calendar, UCAL_DATE, 1);
		ucal_set(calendar, UCAL_MONTH, 0);
		length = (NSInteger)ucal_getLimit(calendar, UCAL_DAY_OF_YEAR, UCAL_ACTUAL_MAXIMUM,
						  &status) * FN_DAY_SECONDS;
	} else {
		/* NO rather than raise: Cocoa answers NO for a unit it cannot give a start and a length
		 * for, and this is the query form. */
		ucal_close(calendar);
		return NO;
	}
	status = U_ZERO_ERROR;
	start = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	if (start == nil) {
		return NO;
	}
	if (datep != NULL) {
		*datep = start;
	}
	if (tip != NULL) {
		*tip = (double)length;
	}
	return YES;
}

- (BOOL)isDate:(NSDate *)date inSameDayAsDate:(NSDate *)other
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	int32_t year, month, day, otherYear, otherMonth, otherDay;

	if (calendar == NULL) {
		return NO;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	year = ucal_get(calendar, UCAL_YEAR, &status);
	month = ucal_get(calendar, UCAL_MONTH, &status);
	day = ucal_get(calendar, UCAL_DATE, &status);
	ucal_setMillis(calendar, (UDate)([other timeIntervalSince1970] * 1000.0), &status);
	otherYear = ucal_get(calendar, UCAL_YEAR, &status);
	otherMonth = ucal_get(calendar, UCAL_MONTH, &status);
	otherDay = ucal_get(calendar, UCAL_DATE, &status);
	ucal_close(calendar);
	return year == otherYear && month == otherMonth && day == otherDay;
}

/* --- identity ------------------------------------------------------------- */

- (BOOL)isEqualToCalendar:(NSCalendar *)other
{
	return other != nil && [[other identifier] isEqualToString:_identifier]
	    && [[other timeZone] isEqualToTimeZone:_timeZone]
	    && [other firstWeekday] == _firstWeekday
	    && [other minimumDaysInFirstWeek] == _minimumDaysInFirstWeek;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSCalendar class]]) {
		return NO;
	}
	return [self isEqualToCalendar:(NSCalendar *)other];
}

- (NSUInteger)hash
{
	return [_identifier hash] ^ _firstWeekday ^ (_minimumDaysInFirstWeek << 3);
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSCalendar: %@ %@ firstWeekday=%lu minDays=%lu>",
					 _identifier, [_timeZone name],
					 (unsigned long)_firstWeekday,
					 (unsigned long)_minimumDaysInFirstWeek];
}

/* Mutable (its time zone and week rules), so a copy is a real one. */
- (id)copy
{
	NSCalendar *copy;

	copy = [[NSCalendar alloc] initWithCalendarIdentifier:_identifier];
	[copy setTimeZone:_timeZone];
	[copy setFirstWeekday:_firstWeekday];
	[copy setMinimumDaysInFirstWeek:_minimumDaysInFirstWeek];
	return copy;
}

@end

/*
 * THE SHARED IDENTIFIER -> ICU CALENDAR KEYWORD BRIDGE (§10, F13.7e; its declarations are in
 * NSCalendar.h, with the rest of the private half). The comparison is against the CONSTANTS rather
 * than against string literals, so a caller cannot smuggle in a keyword this library has not
 * promised to support. It lives HERE rather than in a translation unit of its own because the
 * headers are now the class's own: no file is a private header's implementation any more.
 */
const char *fn_calendar_keyword(NSString *identifier)
{
	if (identifier == nil) {
		return NULL;
	}
	if ([identifier isEqualToString:NSCalendarIdentifierGregorian]) return "gregorian";
	if ([identifier isEqualToString:NSCalendarIdentifierISO8601]) return "iso8601";
	if ([identifier isEqualToString:NSCalendarIdentifierBuddhist]) return "buddhist";
	if ([identifier isEqualToString:NSCalendarIdentifierChinese]) return "chinese";
	if ([identifier isEqualToString:NSCalendarIdentifierCoptic]) return "coptic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteMihret]) return "ethiopic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteAlem]) return "ethioaa";
	if ([identifier isEqualToString:NSCalendarIdentifierHebrew]) return "hebrew";
	if ([identifier isEqualToString:NSCalendarIdentifierIndian]) return "indian";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamic]) return "islamic";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicCivil]) return "islamic-civil";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicTabular]) return "islamic-tbla";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicUmmAlQura]) return "islamic-umalqura";
	if ([identifier isEqualToString:NSCalendarIdentifierJapanese]) return "japanese";
	if ([identifier isEqualToString:NSCalendarIdentifierPersian]) return "persian";
	if ([identifier isEqualToString:NSCalendarIdentifierRepublicOfChina]) return "roc";
	return NULL;
}
