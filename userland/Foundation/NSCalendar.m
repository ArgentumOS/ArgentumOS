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
#import <Foundation/NSArray.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSException.h>

#include <unicode/ucal.h>
#include <unicode/udat.h>	/* udat_getSymbols: the SYMBOL doors' data (F13.7c) */
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

/*
 * UChar -> NSString, the SAME conversion NSDateFormatter.m's symbols use, so the two families render
 * identical text for identical ICU data.
 */
static NSString *fn_cal_string(const UChar *text, int32_t length)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	char bytes[256];

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

/*
 * NSCalendarUnit -> ICU's field, or -1 for a unit that is not ONE ICU field (Quarter is a rule over
 * UCAL_MONTH; the DayOfYear / Calendar / TimeZone sentinels and the week-question sentinels are not
 * fields at all). Callers pass a SINGLE unit.
 */
static int32_t fn_cal_field(NSCalendarUnit unit)
{
	if (unit & NSCalendarUnitEra) return UCAL_ERA;
	if (unit & NSCalendarUnitYear) return UCAL_YEAR;
	if (unit & NSCalendarUnitMonth) return UCAL_MONTH;
	if (unit & NSCalendarUnitDay) return UCAL_DATE;
	if (unit & NSCalendarUnitHour) return UCAL_HOUR_OF_DAY;
	if (unit & NSCalendarUnitMinute) return UCAL_MINUTE;
	if (unit & NSCalendarUnitSecond) return UCAL_SECOND;
	if (unit & NSCalendarUnitWeekday) return UCAL_DAY_OF_WEEK;
	if (unit & NSCalendarUnitWeekdayOrdinal) return UCAL_DAY_OF_WEEK_IN_MONTH;
	if (unit & NSCalendarUnitWeekOfMonth) return UCAL_WEEK_OF_MONTH;
	if (unit & NSCalendarUnitWeekOfYear) return UCAL_WEEK_OF_YEAR;
	if (unit & NSCalendarUnitYearForWeekOfYear) return UCAL_YEAR_WOY;
	return -1;
}

/*
 * ONE FORMATTER PER CALL, opened on the calendar's OWN keyword so the symbols are the calendar's:
 * `en_US@calendar=hebrew` is what makes the month names Hebrew's rather than Gregorian's. `first`/
 * `count` are ICU's index range for the symbol type (months from 0, weekdays from UCAL_SUNDAY), and
 * the loop STOPS when the data does — which is how a thirteen-month Hebrew year yields its own count
 * without this file knowing anything about the Hebrew calendar. Mirrors NSDateFormatter.m's fnSymbols:.
 */
static NSArray *fn_cal_symbols(NSString *identifier, NSLocale *locale,
			       UDateFormatSymbolType type, int32_t first, int32_t count)
{
	const char *keyword = fn_cal_keyword(identifier);
	char localeBytes[192];
	UErrorCode status = U_ZERO_ERROR;
	UDateFormat *formatter;
	NSMutableArray *out;
	int32_t i;

	if (locale == nil) {
		return nil;
	}
	snprintf(localeBytes, sizeof localeBytes, "%s%s%s",
		 [[locale localeIdentifier] UTF8String],
		 keyword != NULL ? "@calendar=" : "",
		 keyword != NULL ? keyword : "");
	formatter = udat_open(UDAT_DEFAULT, UDAT_DEFAULT, localeBytes, NULL, 0, NULL, 0, &status);
	if (U_FAILURE(status) || formatter == NULL) {
		return nil;
	}
	out = [NSMutableArray array];
	for (i = 0; i < count; i++) {
		UChar symbol[64];
		int32_t length;
		NSString *text;

		status = U_ZERO_ERROR;
		length = udat_getSymbols(formatter, type, first + i, symbol, 64, &status);
		if (U_FAILURE(status)) {
			break;		/* no more symbols in the data */
		}
		text = fn_cal_string(symbol, length);
		[out addObject:text != nil ? text : @""];
	}
	udat_close(formatter);
	return out;
}

@interface NSCalendar (FNNextDate)
- (NSUInteger)fnNextDateHorizonDays;
- (void)fnApplyTimePartsOf:(NSDateComponents *)source to:(NSDateComponents *)target;
@end

@implementation NSCalendar

NSNotificationName const NSCalendarDayChangedNotification = @"NSCalendarDayChangedNotification";
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

- (BOOL)isEqual:(id)other
{
	NSCalendar *calendar;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSCalendar class]]) {
		return NO;
	}
	/* THE COMPARISON THAT USED TO BE -isEqualToCalendar:'S (§63.59): that name was this library's own -
	 * Apple declares no such door - and -isEqual: was its only caller. `calendar` is typed so every
	 * accessor below keeps its declared return type: on an `id` receiver, `[other firstWeekday] ==
	 * _firstWeekday` would compare a pointer with an integer. */
	calendar = (NSCalendar *)other;
	return [[calendar calendarIdentifier] isEqualToString:_identifier]
	    && [[calendar timeZone] isEqualToTimeZone:_timeZone]
	    && [calendar firstWeekday] == _firstWeekday
	    && [calendar minimumDaysInFirstWeek] == _minimumDaysInFirstWeek;
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

/* Mutable (its time zone, locale and week rules), so a copy is a real one. */
- (id)copy
{
	NSCalendar *copy;

	copy = [[NSCalendar alloc] initWithCalendarIdentifier:_identifier];
	[copy setTimeZone:_timeZone];
	if (_locale != nil) {
		[copy setLocale:_locale];
	}
	[copy setFirstWeekday:_firstWeekday];
	[copy setMinimumDaysInFirstWeek:_minimumDaysInFirstWeek];
	return copy;
}

/* =================================================================================================
 * SYMBOLS AND IDENTITY (F13.7c). The symbol arrays are the LOCALE's names for this calendar's units,
 * read from ICU (udat_getSymbols) against the calendar's OWN keyword — so a Hebrew calendar answers
 * Hebrew month names and a Gregorian one answers the Gregorian's. The COUNTS mirror NSDateFormatter.m
 * exactly (eras 2, months 13, weekdays 7 from UCAL_SUNDAY, quarters 4), so the two families render
 * identical arrays from identical data; the helper stops early when the data runs out, which is how a
 * calendar with fewer months answers fewer names without this file knowing which.
 * ================================================================================================= */

- (NSString *)calendarIdentifier
{
	return _identifier;
}

- (NSLocale *)locale
{
	/* Cocoa's default is the current locale; nil in the ivar means exactly that, so -locale never
	 * answers nil and every symbol door below has a locale to open against. */
	return _locale != nil ? _locale : [NSLocale currentLocale];
}

- (void)setLocale:(NSLocale *)locale
{
	_locale = locale;
}

+ (NSCalendar *)autoupdatingCurrentCalendar
{
	/* There is no per-user calendar database to track — the same refusal +currentCalendar makes — so
	 * the honest answer is the system's calendar: a FRESH Gregorian one, not a cached object that
	 * would have to be invalidated by a settings change this library cannot observe. */
	return [self calendarWithIdentifier:NSCalendarIdentifierGregorian];
}

- (NSString *)AMSymbol
{
	NSArray *symbols = fn_cal_symbols(_identifier, [self locale], UDAT_AM_PMS, 0, 2);

	return [symbols count] > 0 ? (NSString *)[symbols objectAtIndex:0] : nil;
}

- (NSString *)PMSymbol
{
	NSArray *symbols = fn_cal_symbols(_identifier, [self locale], UDAT_AM_PMS, 0, 2);

	return [symbols count] > 1 ? (NSString *)[symbols objectAtIndex:1] : nil;
}

- (NSArray *)eraSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_ERAS, 0, 2);
}

- (NSArray *)longEraSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_ERA_NAMES, 0, 2);
}

- (NSArray *)monthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_MONTHS, 0, 13);
}

- (NSArray *)shortMonthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_SHORT_MONTHS, 0, 13);
}

- (NSArray *)veryShortMonthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_NARROW_MONTHS, 0, 13);
}

- (NSArray *)standaloneMonthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_MONTHS, 0, 13);
}

- (NSArray *)shortStandaloneMonthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_SHORT_MONTHS, 0, 13);
}

- (NSArray *)veryShortStandaloneMonthSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_NARROW_MONTHS, 0, 13);
}

- (NSArray *)weekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)shortWeekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_SHORT_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)veryShortWeekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_NARROW_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)standaloneWeekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)shortStandaloneWeekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_SHORT_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)veryShortStandaloneWeekdaySymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_NARROW_WEEKDAYS, UCAL_SUNDAY, 7);
}

- (NSArray *)quarterSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_QUARTERS, 0, 4);
}

- (NSArray *)shortQuarterSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_SHORT_QUARTERS, 0, 4);
}

- (NSArray *)standaloneQuarterSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_QUARTERS, 0, 4);
}

- (NSArray *)shortStandaloneQuarterSymbols
{
	return fn_cal_symbols(_identifier, [self locale], UDAT_STANDALONE_SHORT_QUARTERS, 0, 4);
}

/* =================================================================================================
 * EXTRACTION (F13.7c). Every door reads from the SAME answer -components:fromDate: gives, so the
 * single-field door cannot disagree with the mask door.
 * ================================================================================================= */

- (NSNumber *)component:(NSCalendarUnit)unit fromDate:(NSDate *)date
{
	NSDateComponents *c = [self components:unit fromDate:date];
	NSInteger value = 0;

	/* PICKED, not re-derived: the field named by `unit` from the components -components:fromDate:
	 * just filled for exactly that unit. A unit this door does not report — the sentinels, Calendar,
	 * TimeZone — answers nil rather than a zero dressed as an answer. */
	if (unit == NSCalendarUnitEra) value = [c era];
	else if (unit == NSCalendarUnitYear) value = [c year];
	else if (unit == NSCalendarUnitQuarter) value = [c quarter];
	else if (unit == NSCalendarUnitMonth) value = [c month];
	else if (unit == NSCalendarUnitDay) value = [c day];
	else if (unit == NSCalendarUnitHour) value = [c hour];
	else if (unit == NSCalendarUnitMinute) value = [c minute];
	else if (unit == NSCalendarUnitSecond) value = [c second];
	else if (unit == NSCalendarUnitNanosecond) value = [c nanosecond];
	else if (unit == NSCalendarUnitWeekday) value = [c weekday];
	else if (unit == NSCalendarUnitWeekdayOrdinal) value = [c weekdayOrdinal];
	else if (unit == NSCalendarUnitWeekOfMonth) value = [c weekOfMonth];
	else if (unit == NSCalendarUnitWeekOfYear) value = [c weekOfYear];
	else if (unit == NSCalendarUnitYearForWeekOfYear) value = [c yearForWeekOfYear];
	else return nil;
	return [NSNumber numberWithInteger:value];
}

- (NSDateComponents *)componentsInTimeZone:(NSTimeZone *)timeZone fromDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, timeZone, _firstWeekday,
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
	[out setEra:(NSInteger)ucal_get(calendar, UCAL_ERA, &status)];
	[out setYear:(NSInteger)ucal_get(calendar, UCAL_YEAR, &status)];
	[out setQuarter:(NSInteger)(ucal_get(calendar, UCAL_MONTH, &status) / 3 + 1)];
	[out setMonth:(NSInteger)(ucal_get(calendar, UCAL_MONTH, &status) + 1)];
	[out setDay:(NSInteger)ucal_get(calendar, UCAL_DATE, &status)];
	[out setHour:(NSInteger)ucal_get(calendar, UCAL_HOUR_OF_DAY, &status)];
	[out setMinute:(NSInteger)ucal_get(calendar, UCAL_MINUTE, &status)];
	[out setSecond:(NSInteger)ucal_get(calendar, UCAL_SECOND, &status)];
	[out setNanosecond:(NSInteger)ucal_get(calendar, UCAL_MILLISECOND, &status) * 1000000];
	[out setWeekday:(NSInteger)ucal_get(calendar, UCAL_DAY_OF_WEEK, &status)];
	[out setWeekdayOrdinal:(NSInteger)ucal_get(calendar, UCAL_DAY_OF_WEEK_IN_MONTH, &status)];
	[out setWeekOfMonth:(NSInteger)ucal_get(calendar, UCAL_WEEK_OF_MONTH, &status)];
	[out setWeekOfYear:(NSInteger)ucal_get(calendar, UCAL_WEEK_OF_YEAR, &status)];
	[out setYearForWeekOfYear:(NSInteger)ucal_get(calendar, UCAL_YEAR_WOY, &status)];
	ucal_close(calendar);
	return out;
}

- (void)getEra:(NSInteger *)eraValuePointer
	  year:(NSInteger *)yearValuePointer
	 month:(NSInteger *)monthValuePointer
	   day:(NSInteger *)dayValuePointer
      fromDate:(NSDate *)date
{
	NSDateComponents *c = [self components:(NSCalendarUnitEra | NSCalendarUnitYear |
					       NSCalendarUnitMonth | NSCalendarUnitDay)
				      fromDate:date];

	if (eraValuePointer != NULL) *eraValuePointer = [c era];
	if (yearValuePointer != NULL) *yearValuePointer = [c year];
	if (monthValuePointer != NULL) *monthValuePointer = [c month];
	if (dayValuePointer != NULL) *dayValuePointer = [c day];
}

- (void)getEra:(NSInteger *)eraValuePointer
yearForWeekOfYear:(NSInteger *)yearForWeekOfYearValuePointer
      weekOfYear:(NSInteger *)weekOfYearValuePointer
	 weekday:(NSInteger *)weekdayValuePointer
	fromDate:(NSDate *)date
{
	NSDateComponents *c = [self components:(NSCalendarUnitEra | NSCalendarUnitYearForWeekOfYear |
					       NSCalendarUnitWeekOfYear | NSCalendarUnitWeekday)
				      fromDate:date];

	if (eraValuePointer != NULL) *eraValuePointer = [c era];
	if (yearForWeekOfYearValuePointer != NULL) *yearForWeekOfYearValuePointer = [c yearForWeekOfYear];
	if (weekOfYearValuePointer != NULL) *weekOfYearValuePointer = [c weekOfYear];
	if (weekdayValuePointer != NULL) *weekdayValuePointer = [c weekday];
}

- (void)getHour:(NSInteger *)hourValuePointer
	 minute:(NSInteger *)minuteValuePointer
	 second:(NSInteger *)secondValuePointer
     nanosecond:(NSInteger *)nanosecondValuePointer
       fromDate:(NSDate *)date
{
	NSDateComponents *c = [self components:(NSCalendarUnitHour | NSCalendarUnitMinute |
					       NSCalendarUnitSecond | NSCalendarUnitNanosecond)
				      fromDate:date];

	if (hourValuePointer != NULL) *hourValuePointer = [c hour];
	if (minuteValuePointer != NULL) *minuteValuePointer = [c minute];
	if (secondValuePointer != NULL) *secondValuePointer = [c second];
	if (nanosecondValuePointer != NULL) *nanosecondValuePointer = [c nanosecond];
}

/* =================================================================================================
 * CONSTRUCTION (F13.7c). A field passed as NSDateComponentUndefined is OMITTED, so the contract is
 * "the fields you name" rather than "the fields you name, plus a sentinel read as a maximum".
 * ================================================================================================= */

- (NSDate *)dateWithEra:(NSInteger)eraValue
		   year:(NSInteger)yearValue
		  month:(NSInteger)monthValue
		    day:(NSInteger)dayValue
		   hour:(NSInteger)hourValue
		 minute:(NSInteger)minuteValue
		 second:(NSInteger)secondValue
	     nanosecond:(NSInteger)nanosecondValue
{
	NSDateComponents *c = [[NSDateComponents alloc] init];

	if (eraValue != NSDateComponentUndefined) [c setEra:eraValue];
	if (yearValue != NSDateComponentUndefined) [c setYear:yearValue];
	if (monthValue != NSDateComponentUndefined) [c setMonth:monthValue];
	if (dayValue != NSDateComponentUndefined) [c setDay:dayValue];
	if (hourValue != NSDateComponentUndefined) [c setHour:hourValue];
	if (minuteValue != NSDateComponentUndefined) [c setMinute:minuteValue];
	if (secondValue != NSDateComponentUndefined) [c setSecond:secondValue];
	if (nanosecondValue != NSDateComponentUndefined) [c setNanosecond:nanosecondValue];
	return [self dateFromComponents:c];
}

- (NSDate *)dateWithEra:(NSInteger)eraValue
      yearForWeekOfYear:(NSInteger)yearForWeekOfYearValue
	      weekOfYear:(NSInteger)weekOfYearValue
		 weekday:(NSInteger)weekdayValue
		    hour:(NSInteger)hourValue
		  minute:(NSInteger)minuteValue
		  second:(NSInteger)secondValue
	      nanosecond:(NSInteger)nanosecondValue
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	NSDate *answer;

	if (calendar == NULL) {
		return nil;
	}
	/* THE WEEK FIELDS, not a year+month+day: UCAL_YEAR_WOY / UCAL_WEEK_OF_YEAR / UCAL_DAY_OF_WEEK are
	 * exactly the three this door is given, and lenient mode resolves them to an instant (measured:
	 * ISO week 1 of 2026 is Monday 2025-12-29). WHICH week is week 1 is the calendar's own rule —
	 * firstWeekday and minimumDaysInFirstWeek, the same two the -weeks probe sets. */
	ucal_setAttribute(calendar, UCAL_LENIENT, 1);
	ucal_clear(calendar);
	if (eraValue != NSDateComponentUndefined) {
		ucal_set(calendar, UCAL_ERA, (int32_t)eraValue);
	}
	if (yearForWeekOfYearValue != NSDateComponentUndefined) {
		ucal_set(calendar, UCAL_YEAR_WOY, (int32_t)yearForWeekOfYearValue);
	}
	if (weekOfYearValue != NSDateComponentUndefined) {
		ucal_set(calendar, UCAL_WEEK_OF_YEAR, (int32_t)weekOfYearValue);
	}
	if (weekdayValue != NSDateComponentUndefined) {
		ucal_set(calendar, UCAL_DAY_OF_WEEK, (int32_t)weekdayValue);
	}
	ucal_set(calendar, UCAL_HOUR_OF_DAY,
		 (int32_t)(hourValue == NSDateComponentUndefined ? 0 : hourValue));
	ucal_set(calendar, UCAL_MINUTE,
		 (int32_t)(minuteValue == NSDateComponentUndefined ? 0 : minuteValue));
	ucal_set(calendar, UCAL_SECOND,
		 (int32_t)(secondValue == NSDateComponentUndefined ? 0 : secondValue));
	/* ICU's coarsest sub-second field is the MILLISECOND, so a nanosecond count is floored to it. */
	ucal_set(calendar, UCAL_MILLISECOND,
		 (int32_t)(nanosecondValue == NSDateComponentUndefined ? 0 : nanosecondValue / 1000000));
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

/* =================================================================================================
 * SETTING (F13.7c). ucal_set puts a field on the SAME day — measured: 2023-11-14 22:13 with the hour
 * set to 9 is 2023-11-14 09:00, not the 15th — which is what "setting the time of day" means.
 * ================================================================================================= */

- (NSDate *)dateBySettingHour:(NSInteger)hour
		       minute:(NSInteger)minute
		       second:(NSInteger)second
		       ofDate:(NSDate *)date
		      options:(NSCalendarOptions)options
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	NSDate *answer;

	if (options != NSCalendarOptionsNone) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateBySettingHour:minute:second:ofDate:options: takes "
				   "NSCalendarOptionsNone here: the match policies describe a search over "
				   "candidate dates, not a field set"];
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek, &status);
	if (calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	ucal_set(calendar, UCAL_HOUR_OF_DAY, (int32_t)hour);
	ucal_set(calendar, UCAL_MINUTE, (int32_t)minute);
	ucal_set(calendar, UCAL_SECOND, (int32_t)second);
	ucal_set(calendar, UCAL_MILLISECOND, 0);
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

- (NSDate *)dateBySettingUnit:(NSCalendarUnit)unit
			value:(NSInteger)value
		       ofDate:(NSDate *)date
		      options:(NSCalendarOptions)options
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	int32_t field = fn_cal_field(unit);
	NSDate *answer;

	if (options != NSCalendarOptionsNone) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateBySettingUnit:value:ofDate:options: takes NSCalendarOptionsNone "
				   "here: the match policies describe a search, not a field set"];
	}
	if (field < 0 && unit != NSCalendarUnitQuarter) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateBySettingUnit: has no rule for that unit here"];
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek, &status);
	if (calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	/* UCAL_MONTH is 0-based where Apple's month is 1-based, and a QUARTER is three of those months;
	 * every other unit is ICU's own numbering (day 1-based, hour 0-based). */
	if (unit == NSCalendarUnitMonth) {
		ucal_set(calendar, UCAL_MONTH, (int32_t)(value - 1));
	} else if (unit == NSCalendarUnitQuarter) {
		ucal_set(calendar, UCAL_MONTH, (int32_t)((value - 1) * 3));
	} else {
		ucal_set(calendar, (UCalendarDateFields)field, (int32_t)value);
	}
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

/* =================================================================================================
 * RANGES (F13.7c). The answers come from the CALENDAR'S OWN LIMITS (ucal_getLimit), so a Hebrew leap
 * year's thirteen months and a Gregorian month's 28-31 days are the same code path. UCAL_MAXIMUM is
 * the widest a field can be (a Gregorian day: 31); UCAL_LEAST_MAXIMUM the narrowest maximum (28).
 * ================================================================================================= */

- (NSRange)fn_rangeForUnit:(NSCalendarUnit)unit least:(BOOL)least
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	int32_t field = fn_cal_field(unit);
	int32_t lo, hi;

	if (unit == NSCalendarUnitQuarter) {
		return NSMakeRange(1, 4);
	}
	if (field < 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-maximumRangeOfUnit: / -minimumRangeOfUnit: has no rule for that unit "
				   "here"];
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek, &status);
	if (calendar == NULL) {
		return NSMakeRange(NSNotFound, 0);
	}
	lo = ucal_getLimit(calendar, (UCalendarDateFields)field, UCAL_MINIMUM, &status);
	hi = ucal_getLimit(calendar, (UCalendarDateFields)field,
			   least ? UCAL_LEAST_MAXIMUM : UCAL_MAXIMUM, &status);
	ucal_close(calendar);
	/* MONTH's ICU location is 0-based where Apple's is 1-based; every other unit's location is ICU's. */
	return NSMakeRange((NSUInteger)lo + (unit == NSCalendarUnitMonth ? 1 : 0),
			   (NSUInteger)(hi - lo + 1));
}

- (NSRange)maximumRangeOfUnit:(NSCalendarUnit)unit
{
	return [self fn_rangeForUnit:unit least:NO];
}

- (NSRange)minimumRangeOfUnit:(NSCalendarUnit)unit
{
	return [self fn_rangeForUnit:unit least:YES];
}

- (NSUInteger)ordinalityOfUnit:(NSCalendarUnit)smaller
			inUnit:(NSCalendarUnit)larger
		       forDate:(NSDate *)date
{
	if (smaller == NSCalendarUnitDay && larger == NSCalendarUnitMonth) {
		return (NSUInteger)[[self component:NSCalendarUnitDay fromDate:date] integerValue];
	}
	if (smaller == NSCalendarUnitMonth && larger == NSCalendarUnitYear) {
		return (NSUInteger)[[self component:NSCalendarUnitMonth fromDate:date] integerValue];
	}
	if (smaller == NSCalendarUnitDay && larger == NSCalendarUnitYear) {
		UErrorCode status = U_ZERO_ERROR;
		UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
						  _minimumDaysInFirstWeek, &status);
		int32_t doy;

		if (calendar == NULL) {
			return 0;
		}
		ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
		doy = ucal_get(calendar, UCAL_DAY_OF_YEAR, &status);
		ucal_close(calendar);
		return (NSUInteger)doy;
	}
	/* The 0-based fields count from 1 in Apple's ordinals. */
	if (smaller == NSCalendarUnitHour && larger == NSCalendarUnitDay) {
		return (NSUInteger)([[self component:NSCalendarUnitHour fromDate:date] integerValue] + 1);
	}
	if (smaller == NSCalendarUnitMinute && larger == NSCalendarUnitHour) {
		return (NSUInteger)([[self component:NSCalendarUnitMinute fromDate:date] integerValue] + 1);
	}
	if (smaller == NSCalendarUnitSecond && larger == NSCalendarUnitMinute) {
		return (NSUInteger)([[self component:NSCalendarUnitSecond fromDate:date] integerValue] + 1);
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"-ordinalityOfUnit:inUnit:forDate: has no rule for that pair here"];
	return 0;
}

- (NSDate *)startOfDayForDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	NSDate *answer;

	if (calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
	ucal_set(calendar, UCAL_MINUTE, 0);
	ucal_set(calendar, UCAL_SECOND, 0);
	ucal_set(calendar, UCAL_MILLISECOND, 0);
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

/* =================================================================================================
 * COMPARISON AT A GRANULARITY (F13.7c). Both dates are TRUNCATED to the unit — the smaller fields
 * zeroed with ucal_set — and compared as instants, which is what "equal to this granularity" means.
 * An ERA and the WEEK units are not a single start this door resolves, and it raises for them.
 * ================================================================================================= */

- (NSDate *)fn_truncate:(NSDate *)date toUnit:(NSCalendarUnit)unit
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	NSDate *answer;

	if (calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	/* COARSEN one step at a time, so each branch zeroes everything smaller than itself. */
	if (unit == NSCalendarUnitSecond) {
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else if (unit == NSCalendarUnitMinute) {
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else if (unit == NSCalendarUnitHour) {
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else if (unit == NSCalendarUnitDay) {
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else if (unit == NSCalendarUnitMonth) {
		ucal_set(calendar, UCAL_DATE, 1);
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else if (unit == NSCalendarUnitYear) {
		ucal_set(calendar, UCAL_MONTH, 0);
		ucal_set(calendar, UCAL_DATE, 1);
		ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
		ucal_set(calendar, UCAL_MINUTE, 0);
		ucal_set(calendar, UCAL_SECOND, 0);
		ucal_set(calendar, UCAL_MILLISECOND, 0);
	} else {
		ucal_close(calendar);
		[NSException raise:NSInvalidArgumentException
			    format:@"-compareDate:toDate:toUnitGranularity: has no rule for that granularity "
				   "here (era and the week units are not one of them)"];
		return nil;
	}
	status = U_ZERO_ERROR;
	answer = fn_cal_date(calendar, &status);
	ucal_close(calendar);
	return answer;
}

- (NSComparisonResult)compareDate:(NSDate *)date
			   toDate:(NSDate *)other
		 toUnitGranularity:(NSCalendarUnit)unit
{
	NSDate *a = [self fn_truncate:date toUnit:unit];
	NSDate *b = [self fn_truncate:other toUnit:unit];
	double av, bv;

	if (a == nil || b == nil) {
		return NSOrderedSame;	/* only reachable if ICU refused the instant, not the granularity */
	}
	av = [a timeIntervalSince1970];
	bv = [b timeIntervalSince1970];
	if (av < bv) {
		return NSOrderedAscending;
	}
	if (av > bv) {
		return NSOrderedDescending;
	}
	return NSOrderedSame;
}

- (BOOL)isDate:(NSDate *)date
   equalToDate:(NSDate *)other
toUnitGranularity:(NSCalendarUnit)unit
{
	return [self compareDate:date toDate:other toUnitGranularity:unit] == NSOrderedSame;
}

/* THE DAY RELATIVE TO NOW, in the calendar's own zone: "today" is the day of the instant this is
 * asked at. A named zone's day is 23 or 25 hours at a transition, so ±86400 s is the neighbour DAY
 * only in a fixed-offset zone — which is the regime a caller asking these questions pins. */
- (BOOL)isDateInToday:(NSDate *)date
{
	return [self isDate:date inSameDayAsDate:[NSDate date]];
}

- (BOOL)isDateInTomorrow:(NSDate *)date
{
	return [self isDate:date inSameDayAsDate:[NSDate dateWithTimeIntervalSinceNow:86400.0]];
}

- (BOOL)isDateInYesterday:(NSDate *)date
{
	return [self isDate:date inSameDayAsDate:[NSDate dateWithTimeIntervalSinceNow:-86400.0]];
}

/* =================================================================================================
 * WEEKEND (F13.7c). ucal_isWeekend answers from THIS calendar's LOCALE REGION (en_US: Saturday and
 * Sunday) and its ZONE, so -isDateInWeekend: needs no table here. The two range doors walk whole
 * calendar DAYS with ucal_add (not 86400 s: a named zone's day can be 23 or 25 hours).
 * ================================================================================================= */

- (BOOL)isDateInWeekend:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	UBool weekend;

	if (calendar == NULL) {
		return NO;
	}
	weekend = ucal_isWeekend(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	ucal_close(calendar);
	return weekend ? YES : NO;
}

- (BOOL)rangeOfWeekendStartDate:(NSDate * _Nullable * _Nullable)datep
		       interval:(double * _Nullable)tip
		 containingDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday,
					  _minimumDaysInFirstWeek, &status);
	UDate day, runStart, runEnd;

	if (calendar == NULL) {
		return NO;
	}
	if (!ucal_isWeekend(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status)) {
		ucal_close(calendar);
		return NO;	/* a date that is not in a weekend has no weekend RANGE */
	}
	/* THE START OF THE DAY, then back while the previous day is a weekend day. */
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
	ucal_set(calendar, UCAL_MINUTE, 0);
	ucal_set(calendar, UCAL_SECOND, 0);
	ucal_set(calendar, UCAL_MILLISECOND, 0);
	status = U_ZERO_ERROR;
	day = ucal_getMillis(calendar, &status);
	runStart = day;
	for (;;) {
		UDate prev;

		status = U_ZERO_ERROR;
		ucal_setMillis(calendar, runStart, &status);
		ucal_add(calendar, UCAL_DATE, -1, &status);
		prev = ucal_getMillis(calendar, &status);
		if (U_FAILURE(status) || !ucal_isWeekend(calendar, prev, &status)) {
			break;
		}
		runStart = prev;
	}
	/* Then forward to the start of the last weekend day, whose successor is the end. */
	runEnd = day;
	for (;;) {
		UDate next;

		status = U_ZERO_ERROR;
		ucal_setMillis(calendar, runEnd, &status);
		ucal_add(calendar, UCAL_DATE, 1, &status);
		next = ucal_getMillis(calendar, &status);
		if (U_FAILURE(status) || !ucal_isWeekend(calendar, next, &status)) {
			break;
		}
		runEnd = next;
	}
	status = U_ZERO_ERROR;
	ucal_setMillis(calendar, runEnd, &status);
	ucal_add(calendar, UCAL_DATE, 1, &status);
	runEnd = ucal_getMillis(calendar, &status);
	ucal_close(calendar);
	if (datep != NULL) {
		*datep = [NSDate dateWithTimeIntervalSince1970:(double)runStart / 1000.0];
	}
	if (tip != NULL) {
		*tip = (double)(runEnd - runStart) / 1000.0;
	}
	return YES;
}

- (BOOL)nextWeekendStartDate:(NSDate * _Nullable * _Nullable)datep
		    interval:(double * _Nullable)tip
		     options:(NSCalendarOptions)options
		   afterDate:(NSDate *)date
{
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *calendar;
	UDate day;
	int i;
	NSDate *found = nil;
	double length = 0;

	if (options != NSCalendarOptionsNone) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-nextWeekendStartDate:interval:options:afterDate: takes "
				   "NSCalendarOptionsNone here: a backwards search is the search this "
				   "class does not perform"];
	}
	calendar = fn_cal_open(_identifier, _timeZone, _firstWeekday, _minimumDaysInFirstWeek, &status);
	if (calendar == NULL) {
		return NO;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	ucal_set(calendar, UCAL_HOUR_OF_DAY, 0);
	ucal_set(calendar, UCAL_MINUTE, 0);
	ucal_set(calendar, UCAL_SECOND, 0);
	ucal_set(calendar, UCAL_MILLISECOND, 0);
	status = U_ZERO_ERROR;
	day = ucal_getMillis(calendar, &status);
	/* The FIRST day AFTER `afterDate` that STARTS a weekend (is a weekend day whose predecessor is
	 * not): so a date already in a weekend gets the NEXT weekend, not the one it is in. Seven steps
	 * always reach a weekend. */
	for (i = 1; i <= 8; i++) {
		UDate cand, prev;

		status = U_ZERO_ERROR;
		ucal_setMillis(calendar, day, &status);
		ucal_add(calendar, UCAL_DATE, i, &status);
		cand = ucal_getMillis(calendar, &status);
		status = U_ZERO_ERROR;
		ucal_setMillis(calendar, cand, &status);
		ucal_add(calendar, UCAL_DATE, -1, &status);
		prev = ucal_getMillis(calendar, &status);
		if (!ucal_isWeekend(calendar, cand, &status)
		    || ucal_isWeekend(calendar, prev, &status)) {
			continue;
		}
		found = [NSDate dateWithTimeIntervalSince1970:(double)cand / 1000.0];
		{
			UDate runEnd = cand;

			for (;;) {
				UDate next;

				status = U_ZERO_ERROR;
				ucal_setMillis(calendar, runEnd, &status);
				ucal_add(calendar, UCAL_DATE, 1, &status);
				next = ucal_getMillis(calendar, &status);
				if (U_FAILURE(status) || !ucal_isWeekend(calendar, next, &status)) {
					break;
				}
				runEnd = next;
			}
			status = U_ZERO_ERROR;
			ucal_setMillis(calendar, runEnd, &status);
			ucal_add(calendar, UCAL_DATE, 1, &status);
			runEnd = ucal_getMillis(calendar, &status);
			length = (double)(runEnd - cand) / 1000.0;
		}
		break;
	}
	ucal_close(calendar);
	if (found == nil) {
		return NO;
	}
	if (datep != NULL) {
		*datep = found;
	}
	if (tip != NULL) {
		*tip = length;
	}
	return YES;
}

/* =================================================================================================
 * MATCHING AND THE COMPONENT DIFFERENCE (F13.7c)
 * ================================================================================================= */

- (BOOL)date:(NSDate *)date matchesComponents:(NSDateComponents *)components
{
	NSCalendarUnit all = NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitQuarter |
		NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute |
		NSCalendarUnitSecond | NSCalendarUnitNanosecond | NSCalendarUnitWeekday |
		NSCalendarUnitWeekdayOrdinal | NSCalendarUnitWeekOfMonth | NSCalendarUnitWeekOfYear |
		NSCalendarUnitYearForWeekOfYear;
	NSDateComponents *c = [self components:all fromDate:date];

	/* EVERY field the caller SET must agree; an undefined field is not a constraint. */
	if ([components era] != NSDateComponentUndefined && [components era] != [c era]) return NO;
	if ([components year] != NSDateComponentUndefined && [components year] != [c year]) return NO;
	if ([components quarter] != NSDateComponentUndefined && [components quarter] != [c quarter]) return NO;
	if ([components month] != NSDateComponentUndefined && [components month] != [c month]) return NO;
	if ([components day] != NSDateComponentUndefined && [components day] != [c day]) return NO;
	if ([components hour] != NSDateComponentUndefined && [components hour] != [c hour]) return NO;
	if ([components minute] != NSDateComponentUndefined && [components minute] != [c minute]) return NO;
	if ([components second] != NSDateComponentUndefined && [components second] != [c second]) return NO;
	if ([components nanosecond] != NSDateComponentUndefined && [components nanosecond] != [c nanosecond]) return NO;
	if ([components weekday] != NSDateComponentUndefined && [components weekday] != [c weekday]) return NO;
	if ([components weekdayOrdinal] != NSDateComponentUndefined && [components weekdayOrdinal] != [c weekdayOrdinal]) return NO;
	if ([components weekOfMonth] != NSDateComponentUndefined && [components weekOfMonth] != [c weekOfMonth]) return NO;
	if ([components weekOfYear] != NSDateComponentUndefined && [components weekOfYear] != [c weekOfYear]) return NO;
	if ([components yearForWeekOfYear] != NSDateComponentUndefined && [components yearForWeekOfYear] != [c yearForWeekOfYear]) return NO;
	return YES;
}

- (NSDateComponents *)components:(NSCalendarUnit)units
	      fromDateComponents:(NSDateComponents *)startingDateComp
		toDateComponents:(NSDateComponents *)resultDateComp
			 options:(NSCalendarOptions)options
{
	/* Each set is turned into a date through -dateFromComponents:, then the difference taken by the
	 * same walk -components:fromDate:toDate:options: uses. A nil set means "now", as in Cocoa. */
	NSDate *start = startingDateComp != nil
		? [self dateFromComponents:startingDateComp] : [NSDate date];
	NSDate *end = resultDateComp != nil
		? [self dateFromComponents:resultDateComp] : [NSDate date];

	if (start == nil || end == nil) {
		return [[NSDateComponents alloc] init];
	}
	return [self components:units fromDate:start toDate:end options:options];
}


- (NSDate *)nextDateAfterDate:(NSDate *)date
	    matchingComponents:(NSDateComponents *)components
		       options:(NSCalendarOptions)options
{
	/* THE DATE PARTS COME FROM THE START DATE, THE TIME PARTS FROM WHAT WAS ASKED FOR — which is what makes a
	 * day-sized step enough for both "the next 9:30" and "the next 15 March". A candidate that is not after the
	 * start moves a day forward before the search begins. */
	NSDateComponents *walk;
	NSDate *candidate;
	NSUInteger guard = 0;
	NSUInteger horizon = [self fnNextDateHorizonDays];

	(void)options;
	walk = [self components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay)
		       fromDate:date];
	[self fnApplyTimePartsOf:components to:walk];
	candidate = [self dateFromComponents:walk];
	while (candidate != nil && [candidate compare:date] != NSOrderedDescending && guard <= horizon) {
		NSDateComponents *one = [[NSDateComponents alloc] init];

		[one setDay:1];
		candidate = [self dateByAddingComponents:one toDate:candidate options:0];
		[one release];
		guard++;
	}
	for (guard = 0; candidate != nil && guard <= horizon; guard++) {
		if ([self date:candidate matchesComponents:components]) {
			return candidate;
		}
		{
			NSDateComponents *one = [[NSDateComponents alloc] init];

			[one setDay:1];
			candidate = [self dateByAddingComponents:one toDate:candidate options:0];
			[one release];
		}
	}
	return nil;
}

- (NSDate *)nextDateAfterDate:(NSDate *)date
		   matchingHour:(NSInteger)hourValue
		       minute:(NSInteger)minuteValue
		       second:(NSInteger)secondValue
		      options:(NSCalendarOptions)options
{
	NSDateComponents *components = [[NSDateComponents alloc] init];

	[components setHour:hourValue];
	[components setMinute:minuteValue];
	[components setSecond:secondValue];
	{
		NSDate *answer = [self nextDateAfterDate:date matchingComponents:components options:options];

		[components release];
		return answer;
	}
}

- (NSDate *)nextDateAfterDate:(NSDate *)date
		   matchingUnit:(NSCalendarUnit)unit
			value:(NSInteger)value
		      options:(NSCalendarOptions)options
{
	NSDateComponents *components = [[NSDateComponents alloc] init];

	/* THE UNIT NAMES ITS OWN COMPONENT, so the predicate below is exactly the unit asked for. */
	if (unit == NSCalendarUnitEra) { [components setEra:value]; }
	else if (unit == NSCalendarUnitYear) { [components setYear:value]; }
	else if (unit == NSCalendarUnitMonth) { [components setMonth:value]; }
	else if (unit == NSCalendarUnitDay) { [components setDay:value]; }
	else if (unit == NSCalendarUnitHour) { [components setHour:value]; }
	else if (unit == NSCalendarUnitMinute) { [components setMinute:value]; }
	else if (unit == NSCalendarUnitSecond) { [components setSecond:value]; }
	else if (unit == NSCalendarUnitWeekday) { [components setWeekday:value]; }
	else if (unit == NSCalendarUnitWeekdayOrdinal) { [components setWeekdayOrdinal:value]; }
	else if (unit == NSCalendarUnitQuarter) { [components setQuarter:value]; }
	{
		NSDate *answer = [self nextDateAfterDate:date matchingComponents:components options:options];

		[components release];
		return answer;
	}
}

- (void)enumerateDatesStartingAfterDate:(NSDate *)startDate
		     matchingComponents:(NSDateComponents *)components
				options:(NSCalendarOptions)options
			     usingBlock:(void (^)(NSDate *, BOOL *))block
{
	/* THE SAME SEARCH, REPEATED: the block is handed each match until it sets *stop, or the calendar's horizon
	 * runs out — the bound that keeps an unsatisfiable match from looping forever. */
	NSDate *cursor = startDate;
	NSUInteger guard = 0;
	NSUInteger horizon = [self fnNextDateHorizonDays];

	if (block == NULL) {
		return;
	}
	while (guard <= horizon) {
		NSDate *match = [self nextDateAfterDate:cursor matchingComponents:components
						options:options];
		BOOL stop = NO;

		if (match == nil) {
			return;
		}
		block(match, &stop);
		if (stop) {
			return;
		}
		cursor = match;
		guard++;
	}
}

/* §63.207: THE SEARCH'S TWO PRIVATE PIECES. The horizon is a STATED BOUND (ten years), so an unsatisfiable
 * match answers nil instead of looping; the time-part copy is what lets a day-sized step find "the next 9:30"
 * as well as "the next 15 March". */
- (NSUInteger)fnNextDateHorizonDays
{
	return 3660;
}

- (void)fnApplyTimePartsOf:(NSDateComponents *)source to:(NSDateComponents *)target
{
	if ([source hour] != NSDateComponentUndefined) {
		[target setHour:[source hour]];
	}
	if ([source minute] != NSDateComponentUndefined) {
		[target setMinute:[source minute]];
	}
	if ([source second] != NSDateComponentUndefined) {
		[target setSecond:[source second]];
	}
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
