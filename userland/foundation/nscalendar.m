/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nscalendar.m — the Gregorian calendar, as rules, on libc's field arithmetic.
 *
 * ARC file.
 *
 * THE DIVISION OF LABOUR IS THE DESIGN. libc owns two things and this file does
 * not re-implement either:
 *
 *   gmtime_r  absolute seconds -> fields
 *   timegm    fields -> absolute seconds, AND NORMALISES THEM (tm_mday = 32
 *             becomes the 1st of the next month, tm_mon = 12 becomes January of
 *             the next year)
 *
 * Everything else here is the CALENDAR's semantics, which libc does not have:
 * what "add one month" means on the 31st, how long a month is, what week a day
 * falls in, and which day starts a week. Those are rules, and they are written
 * once, in the helpers at the top.
 *
 * A FIXED-OFFSET TIME ZONE is what makes the arithmetic exact: the zone is a
 * SHIFT of the absolute time, so there is never a transition to step over, and a
 * day is always 86400 seconds.
 */

#import <foundation/NSCalendar.h>
#import <foundation/NSDate.h>
#import <foundation/NSDateComponents.h>
#import <foundation/NSTimeZone.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>
#include <time.h>
#include <string.h>
#include <math.h>

NSString *const NSCalendarIdentifierGregorian = @"gregorian";
NSString *const NSCalendarIdentifierISO8601 = @"iso8601";
NSString *const NSCalendarIdentifierBuddhist = @"buddhist";
NSString *const NSCalendarIdentifierJapanese = @"japanese";
NSString *const NSCalendarIdentifierHebrew = @"hebrew";
NSString *const NSCalendarIdentifierIslamic = @"islamic";
NSString *const NSCalendarIdentifierChinese = @"chinese";

/* The day the Gregorian calendar counts from, in the raw seconds argument of
 * NSDate. It is libc's own epoch, named here so the round trip through timegm
 * reads as one. */
#define FN_DAY_SECONDS	86400

/*
 * The month lengths, as a rule rather than a table: February is 28 unless the
 * year is a leap year, and libc's normalisation is the test — build the 1st of
 * the NEXT month, step back a day, and read the day number back. That way the
 * leap rule lives in exactly one place (libc's) and cannot disagree with the
 * arithmetic this file does.
 */
static int fn_days_in_month(int year, int month)
{
	struct tm fields;

	memset(&fields, 0, sizeof fields);
	fields.tm_year = year - 1900;
	fields.tm_mon = month;			/* the NEXT month; tm_mon is 0-based, so month is +1 already */
	fields.tm_mday = 1;
	timegm(&fields);
	fields.tm_mday = 0;			/* the last day of the previous month */
	timegm(&fields);
	return fields.tm_mday;
}

/* Days since the epoch for a calendar date, at midnight — via libc. */
static long long fn_days_since_epoch(int year, int month, int day)
{
	struct tm fields;

	memset(&fields, 0, sizeof fields);
	fields.tm_year = year - 1900;
	fields.tm_mon = month - 1;
	fields.tm_mday = day;
	return (long long)timegm(&fields) / FN_DAY_SECONDS;
}

static int fn_day_of_year(int year, int month, int day)
{
	static const int starts[13] = { 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	int doy = starts[month] + day;

	/* 0-based day-of-year, then +1; the leap day is after February. */
	if (month > 2 && fn_days_in_month(year, 2) == 29) {
		doy += 1;
	}
	return doy;
}

/*
 * The WEEK RULE, which is Cocoa's: week 1 of a year is the first week that
 * contains at least `minimumDays` days of that year, where a week STARTS on
 * `firstWeekday`. Everything about weekOfYear / weekOfMonth /
 * yearForWeekOfYear follows from this one function, so they cannot disagree.
 */
static int fn_week_start_doy(int year, int firstWeekday, int minimumDays)
{
	int jan1w = 0;				/* 1 = firstWeekday ... 7 */
	int offset, start;
	long long days;

	days = fn_days_since_epoch(year, 1, 1);
	/* tm_wday is 0 = Sunday; convert to the calendar's own 1..7 numbering. */
	jan1w = (int)((((days + 4) % 7) + 7) % 7);		/* 0 = Sunday */
	jan1w = ((jan1w - (firstWeekday - 1) + 7) % 7) + 1;

	offset = jan1w - 1;			/* days from Jan 1 back to the week's first day */
	start = 1 - offset;			/* day-of-year of the week that contains Jan 1 */
	if (1 - start + 1 < minimumDays) {
		start += 7;
	}
	return start;
}

/* The day-of-year of the firstWeekday on or before a date: the WEEK this date
 * belongs to starts there. ONE function, so every week field is derived from the
 * same reading of "which week is this". */
static int fn_week_floor(int year, int month, int day, int firstWeekday)
{
	long long days = fn_days_since_epoch(year, month, day);
	int tmwday = (int)((((days + 4) % 7) + 7) % 7);		/* 0 = Sunday */
	int back = (tmwday - (firstWeekday - 1) + 7) % 7;

	return fn_day_of_year(year, month, day) - back;
}

/* Week 1 of a year is the first week containing at least `minimumDays` days of it
 * (Cocoa's rule), and fn_week_start_doy is where that year's weeks begin. The
 * three cases below are the whole of weekOfYear / yearForWeekOfYear. */
static int fn_week_of_year(int year, int month, int day, int firstWeekday, int minimumDays,
			   int *yearForWeekOfYear)
{
	int start = fn_week_start_doy(year, firstWeekday, minimumDays);
	int floorDoy = fn_week_floor(year, month, day, firstWeekday);
	int lastDoy = fn_day_of_year(year, 12, 31);
	int week;

	if (floorDoy < start) {
		/* It is the LAST week of the previous year. */
		int prev = year - 1;

		*yearForWeekOfYear = prev;
		return (fn_week_floor(prev, 12, 31, firstWeekday)
			- fn_week_start_doy(prev, firstWeekday, minimumDays)) / 7 + 1;
	}
	week = (floorDoy - start) / 7 + 1;

	/* The next year's weeks begin at `boundary`, expressed in THIS year's
	 * day-of-year terms (31 December's day-of-year plus the next year's start,
	 * which may be <= 0 or >= 1). A week that has crossed it is week 1 of the
	 * next year — which is exactly what makes a 53-week year come out right. */
	if (week >= 52) {
		int boundary = lastDoy + fn_week_start_doy(year + 1, firstWeekday, minimumDays);

		if (floorDoy >= boundary) {
			*yearForWeekOfYear = year + 1;
			return 1;
		}
	}
	*yearForWeekOfYear = year;
	return week;
}

@implementation NSCalendar

+ (NSCalendar *)currentCalendar
{
	return [[self alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
}

+ (NSCalendar *)calendarWithIdentifier:(NSString *)identifier
{
	/* The refused identifiers are NAMED so a call compiles and is rejected
	 * honestly: this library has no table for them. */
	if (![identifier isEqualToString:NSCalendarIdentifierGregorian]) {
		return nil;
	}
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
	if (identifier == nil || ![identifier isEqualToString:NSCalendarIdentifierGregorian]) {
		return nil;
	}
	_identifier = [identifier copy];
	/* THE SYSTEM ZONE, not a hard-coded UTC: with F13.7a's database the honest default is the one
	 * the system reports (ICU's default, which follows TZ). In this guest that is GMT, so the
	 * arithmetic below is unchanged — but it is now a fact about the zone rather than a constant
	 * that would quietly disagree with a configured one. */
	_timeZone = [NSTimeZone systemTimeZone];
	_firstWeekday = 1;			/* Sunday, Cocoa's Gregorian default */
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

/* --- the libc bridge ------------------------------------------------------ */

- (void)fnFieldsForDate:(NSDate *)date into:(struct tm *)out
{
	/* THE OFFSET IS THE ONE IN FORCE AT THAT INSTANT: for a named zone it is not a constant, and
	 * asking for it is what makes a calendar agree with the wall clock across a DST change. */
	double absolute = [date timeIntervalSince1970]
			+ (double)[_timeZone secondsFromGMTForDate:date];
	time_t seconds = (time_t)floor(absolute);

	gmtime_r(&seconds, out);
}

- (NSDate *)fnDateFromFields:(struct tm *)fields
{
	time_t seconds = timegm(fields);
	NSDate *candidate;
	NSInteger offset;

	/* TWO PASSES, because the offset needed is the one in force AT THE ANSWER, and asking for it
	 * needs an answer to ask about. The first pass is out by at most a DST hour; the second uses
	 * the candidate instant's own offset. (While the offset was a constant, one pass was exact.) */
	offset = [_timeZone secondsFromGMTForDate:
			[NSDate dateWithTimeIntervalSince1970:(double)seconds]];
	candidate = [NSDate dateWithTimeIntervalSince1970:(double)(seconds - offset)];
	offset = [_timeZone secondsFromGMTForDate:candidate];
	return [NSDate dateWithTimeIntervalSince1970:(double)(seconds - offset)];
}

/* --- conversion ----------------------------------------------------------- */

- (NSDateComponents *)components:(NSCalendarUnit)units fromDate:(NSDate *)date
{
	NSDateComponents *out = [[NSDateComponents alloc] init];
	struct tm fields;
	int year, month, day;

	[self fnFieldsForDate:date into:&fields];
	year = fields.tm_year + 1900;
	month = fields.tm_mon + 1;
	day = fields.tm_mday;

	/* EVERY unit is derived here, so a caller asking for two of them and a
	 * caller asking for all of them cannot get different answers. */
	if (units & NSCalendarUnitEra) {
		[out setEra:(year > 0) ? 1 : 0];
	}
	if (units & NSCalendarUnitYear) {
		[out setYear:year];
	}
	if (units & NSCalendarUnitQuarter) {
		[out setQuarter:(month - 1) / 3 + 1];
	}
	if (units & NSCalendarUnitMonth) {
		[out setMonth:month];
	}
	if (units & NSCalendarUnitDay) {
		[out setDay:day];
	}
	if (units & NSCalendarUnitHour) {
		[out setHour:fields.tm_hour];
	}
	if (units & NSCalendarUnitMinute) {
		[out setMinute:fields.tm_min];
	}
	if (units & NSCalendarUnitSecond) {
		[out setSecond:fields.tm_sec];
	}
	if (units & NSCalendarUnitNanosecond) {
		double fraction = [date timeIntervalSince1970];
		fraction = fraction - floor(fraction);
		[out setNanosecond:(NSInteger)(fraction * 1e9)];
	}
	if (units & NSCalendarUnitWeekday) {
		/* 0 = Sunday in tm_wday; the calendar numbers from firstWeekday. */
		NSInteger w = ((fields.tm_wday - ((NSInteger)_firstWeekday - 1) + 7) % 7) + 1;
		[out setWeekday:w];
	}
	if (units & NSCalendarUnitWeekdayOrdinal) {
		[out setWeekdayOrdinal:(day - 1) / 7 + 1];
	}
	if (units & (NSCalendarUnitWeekOfYear | NSCalendarUnitWeekOfMonth
		     | NSCalendarUnitYearForWeekOfYear)) {
		int yearForWeek = year;
		int weekOfYear = fn_week_of_year(year, month, day, (int)_firstWeekday,
						 (int)_minimumDaysInFirstWeek, &yearForWeek);
		/* weekOfMonth is the same reading applied inside the month: how many
		 * weeks past the week the month began in. */
		int weekOfMonth = (fn_week_floor(year, month, day, (int)_firstWeekday)
				   - fn_week_floor(year, month, 1, (int)_firstWeekday)) / 7 + 1;

		if (units & NSCalendarUnitWeekOfYear) {
			[out setWeekOfYear:weekOfYear];
		}
		if (units & NSCalendarUnitYearForWeekOfYear) {
			[out setYearForWeekOfYear:yearForWeek];
		}
		if (units & NSCalendarUnitWeekOfMonth) {
			[out setWeekOfMonth:weekOfMonth];
		}
	}
	return out;
}

- (NSDate *)dateFromComponents:(NSDateComponents *)components
{
	struct tm fields;
	NSInteger year = [components year];
	NSInteger month = [components month];
	NSInteger day = [components day];
	NSInteger era = [components era];

	if (components == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateFromComponents: needs components"];
	}
	if (year == NSDateComponentUndefined) {
		/* The week fields are ANSWERS, not a second way to say a date (the
		 * header says so), and without a year there is nothing to convert. */
		return nil;
	}
	if (era == 0) {
		year = 1 - year;		/* 1 BC is year 0, 2 BC is year -1 */
	}
	if (month == NSDateComponentUndefined) month = 1;
	if (day == NSDateComponentUndefined) day = 1;

	memset(&fields, 0, sizeof fields);
	fields.tm_year = (int)(year - 1900);
	fields.tm_mon = (int)(month - 1);
	fields.tm_mday = (int)day;
	fields.tm_hour = ([components hour] == NSDateComponentUndefined) ? 0 : (int)[components hour];
	fields.tm_min = ([components minute] == NSDateComponentUndefined) ? 0 : (int)[components minute];
	fields.tm_sec = ([components second] == NSDateComponentUndefined) ? 0 : (int)[components second];
	/* timegm normalises: an out-of-range day rolls, which is how the calendar
	 * answers 2026-02-30 without a second validity rule. */
	return [self fnDateFromFields:&fields];
}

/* --- arithmetic ----------------------------------------------------------- */

- (NSDate *)dateByAddingComponents:(NSDateComponents *)components
			    toDate:(NSDate *)date
			   options:(NSCalendarOptions)options
{
	struct tm fields;
	NSInteger years = [components year];
	NSInteger months = [components month];
	NSInteger days = [components day];
	NSInteger hours = [components hour];
	NSInteger minutes = [components minute];
	NSInteger seconds = [components second];

	if (options != NSCalendarOptionsNone) {
		/* The other options describe a SEARCH over candidate dates; this
		 * calendar answers by arithmetic, so it refuses rather than pretending. */
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateByAddingComponents: takes NSCalendarOptionsNone here: "
				   "wrapComponents and searchBackwards describe a search, not arithmetic"];
	}
	[self fnFieldsForDate:date into:&fields];

	/* Years and months first, at the FIELD level, because they are the units with
	 * the clamp rule; then the day counts, which libc rolls for us.
	 *
	 * THE ROLL IS DONE WITH THE DAY OUT OF THE WAY, and that is the whole trick.
	 * timegm normalises tm_mday too, so rolling January 31 into February with
	 * tm_mday still 31 gives 3 MARCH — the clamp would then be clamping March,
	 * and 31 January + 1 month would come out as 31 March. Setting the day to 1,
	 * rolling, clamping against the target month's length and only then putting
	 * the wanted day back is what makes the answer the last day of February. */
	if (years != NSDateComponentUndefined && years != 0) {
		int wantDay = fields.tm_mday;

		fields.tm_year += (int)years;
		fields.tm_mday = 1;
		timegm(&fields);
		{
			int limit = fn_days_in_month(fields.tm_year + 1900, fields.tm_mon + 1);

			fields.tm_mday = (wantDay > limit) ? limit : wantDay;
		}
	}
	if (months != NSDateComponentUndefined && months != 0) {
		int wantDay = fields.tm_mday;

		fields.tm_mon += (int)months;
		fields.tm_mday = 1;
		timegm(&fields);
		{
			int limit = fn_days_in_month(fields.tm_year + 1900, fields.tm_mon + 1);
			/* THE CLAMP: 31 January + 1 month is the last day of February, 28 or
			 * 29 by year, which is what a calendar does and a bare month count
			 * would not. */
			fields.tm_mday = (wantDay > limit) ? limit : wantDay;
		}
	}
	if (days != NSDateComponentUndefined && days != 0) {
		fields.tm_mday += (int)days;
	}
	if (hours != NSDateComponentUndefined && hours != 0) {
		fields.tm_hour += (int)hours;
	}
	if (minutes != NSDateComponentUndefined && minutes != 0) {
		fields.tm_min += (int)minutes;
	}
	if (seconds != NSDateComponentUndefined && seconds != 0) {
		fields.tm_sec += (int)seconds;
	}
	return [self fnDateFromFields:&fields];
}

- (NSDate *)dateByAddingUnit:(NSCalendarUnit)unit
		       value:(NSInteger)value
		      toDate:(NSDate *)date
		     options:(NSCalendarOptions)options
{
	NSDateComponents *amount = [[NSDateComponents alloc] init];

	switch (unit) {
	case NSCalendarUnitYear:
		[amount setYear:value];
		break;
	case NSCalendarUnitMonth:
		[amount setMonth:value];
		break;
	case NSCalendarUnitDay:
		[amount setDay:value];
		break;
	case NSCalendarUnitHour:
		[amount setHour:value];
		break;
	case NSCalendarUnitMinute:
		[amount setMinute:value];
		break;
	case NSCalendarUnitSecond:
		[amount setSecond:value];
		break;
	default:
		[NSException raise:NSInvalidArgumentException
			    format:@"-dateByAddingUnit: takes a unit this calendar can add "
				   "(year, month, day, hour, minute, second). An ERA is not one of "
				   "them: how many years an era is, is a table, not a rule."];
	}
	return [self dateByAddingComponents:amount toDate:date options:options];
}

/* --- ranges --------------------------------------------------------------- */

- (NSRange)rangeOfUnit:(NSCalendarUnit)smaller
		inUnit:(NSCalendarUnit)larger
	       forDate:(NSDate *)date
{
	struct tm fields;
	int year, month;

	[self fnFieldsForDate:date into:&fields];
	year = fields.tm_year + 1900;
	month = fields.tm_mon + 1;

	/* The pairs whose answer is a rule. Everything else needs a table (a
	 * calendar's own month lengths for another calendar, a transition for DST)
	 * and is refused rather than guessed. */
	if ((smaller & NSCalendarUnitDay) && (larger & NSCalendarUnitMonth)) {
		return NSMakeRange(1, (NSUInteger)fn_days_in_month(year, month));
	}
	if ((smaller & NSCalendarUnitDay) && (larger & NSCalendarUnitYear)) {
		return NSMakeRange(1, (NSUInteger)((fn_days_in_month(year, 2) == 29) ? 366 : 365));
	}
	if ((smaller & NSCalendarUnitMonth) && (larger & NSCalendarUnitYear)) {
		return NSMakeRange(1, 12);
	}
	if ((smaller & NSCalendarUnitHour) && (larger & NSCalendarUnitDay)) {
		/* Always 24 here: a fixed offset has no transition to shorten a day. */
		return NSMakeRange(0, 24);
	}
	if ((smaller & NSCalendarUnitMinute) && (larger & NSCalendarUnitHour)) {
		return NSMakeRange(0, 60);
	}
	if ((smaller & NSCalendarUnitSecond) && (larger & NSCalendarUnitMinute)) {
		return NSMakeRange(0, 60);
	}
	if ((smaller & NSCalendarUnitWeekOfYear) && (larger & NSCalendarUnitYear)) {
		int start = fn_week_start_doy(year, (int)_firstWeekday, (int)_minimumDaysInFirstWeek);
		int doy = fn_day_of_year(year, 12, 31);

		return NSMakeRange(1, (NSUInteger)((doy - start) / 7 + 1));
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"-rangeOfUnit:inUnit:forDate: has no rule for that pair here "
			   "(a table would be needed)"];
	return NSMakeRange(0, 0);
}

- (BOOL)rangeOfUnit:(NSCalendarUnit)unit
	  startDate:(NSDate * _Nullable * _Nullable)datep
	   interval:(double *)tip
	    forDate:(NSDate *)date
{
	struct tm fields;
	NSInteger length;

	[self fnFieldsForDate:date into:&fields];
	fields.tm_hour = 0;
	fields.tm_min = 0;
	fields.tm_sec = 0;

	switch (unit) {
	case NSCalendarUnitSecond:
		[self fnFieldsForDate:date into:&fields];
		length = 1;
		break;
	case NSCalendarUnitMinute:
		fields.tm_sec = 0;
		length = 60;
		break;
	case NSCalendarUnitHour:
		fields.tm_min = 0;
		length = 3600;
		break;
	case NSCalendarUnitDay:
		length = FN_DAY_SECONDS;
		break;
	case NSCalendarUnitMonth:
		fields.tm_mday = 1;
		length = (NSInteger)fn_days_in_month(fields.tm_year + 1900, fields.tm_mon + 1)
			 * FN_DAY_SECONDS;
		break;
	case NSCalendarUnitYear:
		fields.tm_mon = 0;
		fields.tm_mday = 1;
		length = ((fn_days_in_month(fields.tm_year + 1900, 2) == 29) ? 366 : 365)
			 * (NSInteger)FN_DAY_SECONDS;
		break;
	default:
		/* NO rather than raise: Cocoa answers NO for a unit it cannot give a
		 * start and a length for, and this is the query form. */
		return NO;
	}
	if (datep != NULL) {
		*datep = [self fnDateFromFields:&fields];
	}
	if (tip != NULL) {
		*tip = (double)length;
	}
	return YES;
}

- (BOOL)isDate:(NSDate *)date inSameDayAsDate:(NSDate *)other
{
	struct tm a, b;

	[self fnFieldsForDate:date into:&a];
	[self fnFieldsForDate:other into:&b];
	return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday;
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
	return [_identifier hash] ^ _firstWeekday ^ (_minimumDaysInFirstWeek << 3)
	     ^ (NSUInteger)[_timeZone secondsFromGMT];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSCalendar: %@ %@ firstWeekday=%lu minDays=%lu>",
					 _identifier, [_timeZone name],
					 (unsigned long)_firstWeekday,
					 (unsigned long)_minimumDaysInFirstWeek];
}

/* Mutable (its time zone and week rules), so a copy is a real one. */
- (id)copyWithZone:(NSZone *)zone
{
	NSCalendar *copy;

	(void)zone;
	copy = [[NSCalendar alloc] initWithCalendarIdentifier:_identifier];
	[copy setTimeZone:_timeZone];
	[copy setFirstWeekday:_firstWeekday];
	[copy setMinimumDaysInFirstWeek:_minimumDaysInFirstWeek];
	return copy;
}

@end
