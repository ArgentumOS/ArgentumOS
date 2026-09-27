/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSCalendarDate — A DATE THAT KNOWS ITS FORMAT AND ITS ZONE (plan §62.67), and the last class row of
 * `Fundamentals / Deprecated`. APPLE-DEPRECATED AND THEREFORE IN SCOPE (§62.24): a surface built so that an
 * older application compiles is defeated by excluding exactly what such an application calls.
 *
 * IT IS AN `NSDate` SUBCLASS RATHER THAN A DATE-LIKE VALUE, which is Apple's shape and it decides the design:
 * the CELLAR is NSDate's own instant (this library stores seconds since 1970), and what this class adds is the
 * two things the older API carried AROUND an instant — a CALENDAR FORMAT (`strftime`/`strptime` spelling, which
 * is what Apple's own "calendar format" means) and a TIME ZONE that says how the instant reads as fields.
 *
 * THE FIELDS ARE THEREFORE COMPUTED, NOT STORED: -dayOfMonth, -hourOfDay, -monthOfYear and the rest are the
 * broken-down form of the instant IN THE RECEIVER'S ZONE, which is why -setTimeZone: changes all of them at once
 * and why they are never cached. That is also what makes the day-dependent zone question honest: the offset is
 * asked for the DATE (`-secondsFromGMTForDate:`, which NSTimeZone documents as date-dependent because daylight
 * saving is), not for "now".
 *
 * WHAT IS OURS, AND SAID HERE BECAUSE APPLE'S PAGES PUBLISH THE MESSAGE AND NOT THE RULE (§11.6.1 D2):
 *
 *   * THE DEFAULT CALENDAR FORMAT is `%Y-%m-%d %H:%M:%S %z` — the page states the property and no value. It is
 *     the shape a caller of this class expects to be able to parse back, and the probe pins it;
 *   * `-dateByAddingYears:…:` adds the components CALENDAR-AWARE — years and months first, THEN CLAMPING THE DAY
 *     TO THE LAST DAY OF THE MONTH IT LANDS IN, then days and the clock. 31 JANUARY PLUS ONE MONTH IS 28
 *     FEBRUARY. The other reading (add the fields and let the carry normalise, answering 3 March) is what a
 *     fixed count of days gives, and this receiver's own -timeIntervalSince1970 already offers that;
 *   * `-years:months:…:sinceDate:` decomposes the difference LARGEST COMPONENT FIRST (years, then months, then
 *     days, then the clock), which is the only order in which the answer is well defined.
 */

#import <Foundation/NSDate.h>

NS_ASSUME_NONNULL_BEGIN

@class NSString;
@class NSTimeZone;
@class NSLocale;

@interface NSCalendarDate : NSDate
{
@protected
	NSString *_calendarFormat;		/* copied; the strftime/strptime spelling of this date's format */
	NSTimeZone *_timeZone;			/* retained; how the instant reads as fields */
}

/* THE DOORS THAT MAKE ONE. `+calendarDate` is "now" in the system zone with the default format; the parse door
 * answers nil when the string does not match the format; the field door builds one from broken-down numbers. */
+ (id)calendarDate;
+ (nullable id)dateWithString:(NSString *)description calendarFormat:(NSString *)format;
+ (nullable id)dateWithString:(NSString *)description
	       calendarFormat:(NSString *)format
		       locale:(nullable id)locale;
+ (nullable id)dateWithYear:(NSInteger)year
		      month:(NSUInteger)month
			day:(NSUInteger)day
		       hour:(NSUInteger)hour
		     minute:(NSUInteger)minute
		     second:(NSUInteger)second
		   timeZone:(nullable NSTimeZone *)aTimeZone;
+ (instancetype)distantFuture;
+ (instancetype)distantPast;

/* THE TWO STRINGS THAT DEFINE IT, and both are settable: the format is what -descriptionWithCalendarFormat:
 * takes by default and what the parse doors use, and the zone is what every field accessor reads through. */
- (nullable NSString *)calendarFormat;
- (void)setCalendarFormat:(nullable NSString *)format;
- (nullable NSTimeZone *)timeZone;
- (void)setTimeZone:(nullable NSTimeZone *)aTimeZone;

/* THE FIELDS, each computed from the instant in the receiver's zone. `-dayOfWeek` is Sunday = 0, and
 * `-dayOfYear` and `-dayOfCommonEra` are 1-BASED (the day numbers a calendar prints, not an index). */
- (NSInteger)dayOfCommonEra;
- (NSInteger)dayOfMonth;
- (NSInteger)dayOfWeek;
- (NSInteger)dayOfYear;
- (NSInteger)hourOfDay;
- (NSInteger)minuteOfHour;
- (NSInteger)monthOfYear;
- (NSInteger)secondOfMinute;
- (NSInteger)yearOfCommonEra;

/* ARITHMETIC AND DECOMPOSITION — the two doors that make this more than a formatter (see the header's note for
 * the reading of each). */
- (NSCalendarDate *)dateByAddingYears:(NSInteger)year
			       months:(NSInteger)month
				 days:(NSInteger)day
				hours:(NSInteger)hour
			      minutes:(NSInteger)minute
			      seconds:(NSInteger)second;
- (void)years:(nullable NSInteger *)yp
       months:(nullable NSInteger *)mop
	 days:(nullable NSInteger *)dp
	hours:(nullable NSInteger *)hp
      minutes:(nullable NSInteger *)mip
      seconds:(nullable NSInteger *)sp
    sinceDate:(NSCalendarDate *)date;

/* THE PARSE FAMILY, and the formatting family: `locale` is applied through the C library's own locale objects
 * (`newlocale`/`uselocale`) when it is given, and a nil or unresolvable one means the process's locale. */
- (nullable id)initWithString:(NSString *)description;
- (nullable id)initWithString:(NSString *)description calendarFormat:(NSString *)format;
- (nullable id)initWithString:(NSString *)description
	       calendarFormat:(NSString *)format
		       locale:(nullable id)locale;
- (nullable id)initWithYear:(NSInteger)year
		      month:(NSUInteger)month
			day:(NSUInteger)day
		       hour:(NSUInteger)hour
		     minute:(NSUInteger)minute
		     second:(NSUInteger)second
		   timeZone:(nullable NSTimeZone *)aTimeZone;

- (NSString *)descriptionWithCalendarFormat:(NSString *)format;
- (NSString *)descriptionWithCalendarFormat:(NSString *)format locale:(nullable id)locale;
- (NSString *)descriptionWithLocale:(nullable id)locale;

@end

NS_ASSUME_NONNULL_END
