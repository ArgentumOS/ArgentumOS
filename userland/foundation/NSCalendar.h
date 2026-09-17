/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCalendar — the GREGORIAN calendar, as rules. docs/design/foundation-plan.md, F7.
 *
 * THE BOUNDARY IS A RULE, NOT A TABLE, and a calendar is the family where that
 * line costs the most. The Gregorian calendar's own arithmetic IS a rule — leap
 * years, month lengths, the day of the week — so all of it ships. What does not
 * ship is every calendar whose arithmetic needs a table (Hebrew's moladot,
 * Islamic's sighting convention, Japanese's era list) and every service that
 * needs a database (time-zone names, DST transitions, date formatting). Those
 * are REFUSED BY NAME below rather than half-answered.
 *
 * THE SUBSTRATE IS LIBC, and that is NSDate's own note applied: `gmtime_r` turns
 * an absolute time into fields, `timegm` turns fields back into an absolute time
 * AND NORMALISES them (tm_mday = 32 becomes the 1st of the next month), and the
 * month-length rule falls out of that normalisation rather than being written
 * twice. The one thing libc does not give us is the CALENDAR's semantics — what
 * "add one month" does to the 31st, which day starts a week — and that is what
 * this class adds.
 *
 * A FIXED-OFFSET TIME ZONE makes the arithmetic exact: with no DST there is no
 * transition to step over, so a day is 86400 seconds and a difference is
 * subtraction. `-timeZone` defaults to UTC.
 */

#ifndef FOUNDATION_NSCALENDAR_H
#define FOUNDATION_NSCALENDAR_H

#import <foundation/NSObject.h>

@class NSDate;
@class NSTimeZone;
@class NSDateComponents;

/* NULLABILITY (F6's standing rule): the region opens here and closes at the foot
 * of the file, so every declaration below — the identifier constants, the two
 * typedef'd option sets and the class — is nonnull unless it says otherwise. */
NS_ASSUME_NONNULL_BEGIN

/*
 * Only the Gregorian one is honoured. The others are NAMED so that a call
 * compiles and is refused honestly — `+calendarWithIdentifier:` answers nil for
 * them — rather than silently behaving as if it had the table it lacks. The
 * spellings are Cocoa's.
 */
extern NSString *const NSCalendarIdentifierGregorian;
extern NSString *const NSCalendarIdentifierISO8601;		/* refused: a rule SET, not this calendar */
extern NSString *const NSCalendarIdentifierBuddhist;		/* refused: needs its era offset table */
extern NSString *const NSCalendarIdentifierJapanese;		/* refused: needs the era list */
extern NSString *const NSCalendarIdentifierHebrew;		/* refused: needs the molad table */
extern NSString *const NSCalendarIdentifierIslamic;		/* refused: needs a sighting convention */
extern NSString *const NSCalendarIdentifierChinese;		/* refused: needs the solstice table */

/* Cocoa's unit bitmask, and the VALUES ARE COCOA'S so a numeric test in existing
 * code still means the same thing. Every unit below is derived from the date
 * fields by a rule; none needs a table. */
typedef enum {
	NSCalendarUnitEra			= (1UL << 1),
	NSCalendarUnitYear			= (1UL << 2),
	NSCalendarUnitMonth			= (1UL << 3),
	NSCalendarUnitDay			= (1UL << 4),
	NSCalendarUnitHour			= (1UL << 5),
	NSCalendarUnitMinute			= (1UL << 6),
	NSCalendarUnitSecond			= (1UL << 7),
	NSCalendarUnitWeekday			= (1UL << 9),
	NSCalendarUnitWeekdayOrdinal		= (1UL << 10),
	NSCalendarUnitQuarter			= (1UL << 11),
	NSCalendarUnitWeekOfMonth		= (1UL << 12),
	NSCalendarUnitWeekOfYear		= (1UL << 13),
	NSCalendarUnitYearForWeekOfYear		= (1UL << 14),
	NSCalendarUnitNanosecond		= (1UL << 15),
	/* NOT here, and each because it is not a field of a date: Cocoa's
	 * NSCalendarUnitDayOfYear (a derivation this class does not offer),
	 * NSCalendarUnitCalendar and NSCalendarUnitTimeZone (they ask for the
	 * calendar and the zone OBJECTS in the result, which NSDateComponents does
	 * not carry). Declaring a unit that silently fills in nothing would be the
	 * half-answer this family refuses. */
	NSCalendarUnitCount			= 0
} NSCalendarUnit;

/*
 * The options -dateByAdding… takes. ZERO is the only honoured value: the other
 * two are named and REFUSED with NSInvalidArgumentException, because
 * `wrapComponents` and `searchBackwards` describe a search over a table of
 * candidate dates, and this calendar answers by arithmetic instead.
 */
typedef enum {
	NSCalendarOptionsNone		= 0,
	NSCalendarOptionsWrapComponents	= (1UL << 0),		/* refused */
	NSCalendarOptionsSearchBackwards = (1UL << 2)		/* refused */
} NSCalendarOptions;

@interface NSCalendar : NSObject <NSCopying>
{
	NSString *_identifier;
	NSTimeZone *_timeZone;
	NSUInteger _firstWeekday;		/* 1 = Sunday, ... 7 = Saturday */
	NSUInteger _minimumDaysInFirstWeek;
}

/* +currentCalendar is the Gregorian one with its defaults: UTC, Sunday first,
 * one day minimum. Cocoa's reads the user's settings, which are a database this
 * library does not have (the same call NSLocale made). */
+ (nullable NSCalendar *)currentCalendar;
/* nil for an identifier this library does not implement — see the list above. */
+ (nullable NSCalendar *)calendarWithIdentifier:(NSString *)identifier;

- (nullable id)init;
- (nullable id)initWithCalendarIdentifier:(NSString *)identifier;	/* nil for the refused ones */

- (NSString *)identifier;

/* A fixed-offset time zone; never nil (the default is UTC). */
- (NSTimeZone *)timeZone;
- (void)setTimeZone:(NSTimeZone *)zone;

/* The two week RULES. Sunday/1 are the Gregorian defaults. */
- (NSUInteger)firstWeekday;
- (void)setFirstWeekday:(NSUInteger)weekday;
- (NSUInteger)minimumDaysInFirstWeek;
- (void)setMinimumDaysInFirstWeek:(NSUInteger)days;

/* CONVERSION, both ways. -components:fromDate: fills ONLY the units asked for
 * and leaves the rest UNDEFINED; the week-based units are derived here (they are
 * answers, not inputs) using firstWeekday and minimumDaysInFirstWeek. */
- (NSDateComponents *)components:(NSCalendarUnit)units fromDate:(NSDate *)date;
/* nil for a component that is not a date: one that sets only the WEEK fields
 * (they are answers, not a second way to say when) or no year at all. */
- (nullable NSDate *)dateFromComponents:(NSDateComponents *)components;

/* ARITHMETIC. `components` carries the quantity per unit — year 1, month -2,
 * day 7 — and the result is NORMALISED: adding a month to the 31st lands on the
 * last day of the target month (2026-01-31 + 1 month = 2026-02-28, or the 29th in
 * a leap year), and adding days lets libc roll the fields. */
- (NSDate *)dateByAddingComponents:(NSDateComponents *)components
			    toDate:(NSDate *)date
			   options:(NSCalendarOptions)options;
- (NSDate *)dateByAddingUnit:(NSCalendarUnit)unit
		       value:(NSInteger)value
		      toDate:(NSDate *)date
		     options:(NSCalendarOptions)options;

/* RANGES. -rangeOfUnit:inUnit:forDate: answers a count: of days in a month, of
 * months in a year, of hours in a day (always 24 here — a fixed offset has no
 * transition). The location is 1 where the count starts at 1 (months, days) and 0
 * where it starts at 0 (hours). */
- (NSRange)rangeOfUnit:(NSCalendarUnit)smaller
		inUnit:(NSCalendarUnit)larger
	       forDate:(NSDate *)date;
/* The same question asked as a start and a length, which is what a caller
 * iterating a calendar usually wants. Both out-parameters may be NULL. */
- (BOOL)rangeOfUnit:(NSCalendarUnit)unit
	  startDate:(NSDate * _Nullable * _Nullable)datep
	   interval:(double * _Nullable)tip
	    forDate:(NSDate *)date;

- (BOOL)isDate:(NSDate *)date inSameDayAsDate:(NSDate *)other;

- (BOOL)isEqualToCalendar:(NSCalendar *)other;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

/* A calendar is MUTABLE (its time zone and week rules), so a copy is a real one
 * rather than `self`. */
- (id)copyWithZone:(NSZone *)zone;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCALENDAR_H */
