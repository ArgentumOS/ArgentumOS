/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateComponents — the fields a calendar is asked for and answers with.
 * docs/design/foundation-plan.md, F7.
 *
 * A BAG OF MAYBE-SET FIELDS, which is why every accessor has a sentinel and a
 * setter: this object is the argument AND the answer of every NSCalendar
 * conversion (-components:fromDate: fills it, -dateFromComponents: consumes it),
 * and a field the caller never set has to be distinguishable from one set to
 * zero. The sentinel is NSDateComponentUndefined = NSIntegerMax, which is ALSO
 * NSNotFound — the same convention, not a second one invented here.
 *
 * NO @property ANYWHERE IN THIS LIBRARY (the plan, §7), so the accessors are
 * methods: `[components setYear:2026]`, and `[components year]`.
 *
 * THE WEEK FIELDS ARE ANSWERS, NOT INPUTS. -components:fromDate: fills
 * weekOfMonth / weekOfYear / yearForWeekOfYear, derived from the calendar's
 * firstWeekday and minimumDaysInFirstWeek; -dateFromComponents: reads the
 * CALENDAR fields (era, year, month, day, hour, minute, second) and treats a
 * component that sets only week fields as invalid. -isValidDateInCalendar:
 * answers that question rather than letting a half-specified date through.
 */

#ifndef FOUNDATION_NSDATECOMPONENTS_H
#define FOUNDATION_NSDATECOMPONENTS_H

#import <Foundation/NSObject.h>
/* FOR THE NSCalendarUnit TYPEDEF, which the two unit-addressable doors below take. NSCalendar.h
 * forward-declares THIS class and does not import it, so this is not a cycle. */
#import <Foundation/NSCalendar.h>

@class NSDate;
@class NSCalendar;
@class NSTimeZone;

/* The "this field was not set" sentinel. It is NSNotFound's value on purpose:
 * both mean "no such thing", and a second sentinel would be a second thing to
 * get wrong. */
#define NSDateComponentUndefined	NSIntegerMax

/* THE PRE-10.9 SENTINEL NAME (§62.46), and it is the SAME SENTINEL: Apple deprecated `NSUndefinedDateComponent` at
 * 10.9 in favour of `NSDateComponentUndefined`, so a program written before then must compile and MEAN THE SAME
 * THING - which is why this is the value and not a second one. It is written as a `#define` for the reason the
 * modern spelling is: `NSIntegerMax` does not fit an `int`, so it cannot be an enumerator, and the deprecated
 * spelling being an enumerator is a detail of Apple's header that a caller never relies on. */
#define NSUndefinedDateComponent	NSDateComponentUndefined

NS_ASSUME_NONNULL_BEGIN

@interface NSDateComponents : NSObject <NSCopying>
{
	NSInteger _era;
	NSInteger _year;
	NSInteger _quarter;
	NSInteger _month;
	NSInteger _day;
	NSInteger _hour;
	NSInteger _minute;
	NSInteger _second;
	NSInteger _nanosecond;
	NSInteger _weekday;
	NSInteger _weekdayOrdinal;
	NSInteger _weekOfMonth;
	NSInteger _weekOfYear;
	NSInteger _yearForWeekOfYear;
	NSInteger _dayOfYear;
	BOOL _leapMonth;
	BOOL _repeatedDay;
	NSCalendar *_calendar;
	NSTimeZone *_timeZone;
}

/* NSDateComponents is a mutable bag, so `+new` is the way to make one; there is
 * no factory because Cocoa has none either. -init starts every field UNDEFINED.
 * It is NOT re-declared here: NSObject already declares -init, and a re-declaration
 * carrying `nullable` conflicts with that inherited nonnull specifier (the F6
 * region makes NSObject's -init nonnull). A failed [super init] still answers nil;
 * what changed is that the header no longer says it twice. */

- (NSInteger)era;
- (void)setEra:(NSInteger)value;
- (NSInteger)year;
- (void)setYear:(NSInteger)value;
- (NSInteger)quarter;			/* 1-4, derived from the month on a conversion */
- (void)setQuarter:(NSInteger)value;
- (NSInteger)month;			/* 1-12 */
- (void)setMonth:(NSInteger)value;
- (NSInteger)day;			/* 1-31 */
- (void)setDay:(NSInteger)value;
- (NSInteger)hour;			/* 0-23 */
- (void)setHour:(NSInteger)value;
- (NSInteger)minute;			/* 0-59 */
- (void)setMinute:(NSInteger)value;
- (NSInteger)second;			/* 0-59, and 60 in the leap-second RECORD only */
- (void)setSecond:(NSInteger)value;
- (NSInteger)nanosecond;		/* 0-999999999, from NSDate's fractional part */
- (void)setNanosecond:(NSInteger)value;

/* The week-based fields. See the note above: answers, not a second input form. */
- (NSInteger)weekday;			/* 1 = the calendar's first weekday ... 7 */
- (void)setWeekday:(NSInteger)value;
- (NSInteger)weekdayOrdinal;		/* the nth <weekday> in the month: 1-based */
- (void)setWeekdayOrdinal:(NSInteger)value;
- (NSInteger)weekOfMonth;
- (void)setWeekOfMonth:(NSInteger)value;
- (NSInteger)weekOfYear;
- (void)setWeekOfYear:(NSInteger)value;
- (NSInteger)yearForWeekOfYear;
- (void)setYearForWeekOfYear:(NSInteger)value;

/* Is this a date the calendar can make? NO when a field is out of range, when
 * only week fields are set, or when the day does not exist in that month
 * (2026-02-30). The calendar is passed in, as in Cocoa, so a components object
 * does not have to own one. */
- (BOOL)isValidDateInCalendar:(NSCalendar *)calendar;

/* THE THREE FIELDS THAT ARE ONLY ANSWERS IN SOME CALENDARS, and the two REFERENCES that say HOW to read
 * a bag of fields. Apple's words for the first two: "The day of the year value of the date components",
 * "The calendar used to interpret the date components", "The time zone used to interpret the date
 * components". All five are STORED: nothing here derives them, because the derivation is the calendar's
 * (and this calendar does not offer a day-of-year conversion, which is why -components:fromDate: leaves
 * its DayOfYear alone rather than filling it with a guess).
 *
 * THE REFERENCES ARE INTERPRETATION CONTEXT, NOT COMPONENT VALUES: -isEqual:, -hash and -description
 * ignore them, while -copy carries them (a copy of a bag is the same bag, read the same way). */
- (NSInteger)dayOfYear;
- (void)setDayOfYear:(NSInteger)value;
- (BOOL)leapMonth;
- (void)setLeapMonth:(BOOL)value;
- (BOOL)repeatedDay;
- (void)setRepeatedDay:(BOOL)value;

/* SET BY THE CALLER, USED BY THE TWO DOORS BELOW. When none is set, -date and -validDate answer in the
 * CURRENT calendar (Apple's rule for -date is "the date calculated from the current components using
 * the stored calendar"; with no stored calendar the only calendar left is the current one). */
- (nullable NSCalendar *)calendar;
- (void)setCalendar:(nullable NSCalendar *)calendar;
- (nullable NSTimeZone *)timeZone;
- (void)setTimeZone:(nullable NSTimeZone *)timeZone;

/* THE TWO VALIDATION-CATEGORY ANSWERS. -date is -dateFromComponents: in the stored calendar (nil when
 * the fields make no date); -validDate is -isValidDateInCalendar: asked of that same calendar, so the
 * two cannot disagree about what "this bag is a date" means. */
- (nullable NSDate *)date;
- (BOOL)validDate;

/* THE UNIT-ADDRESSABLE PAIR. Apple's words: "Sets/Returns a value for a given calendar unit" — a
 * SWITCH over the units a bag of fields can hold, which is the calendar-field and week-field units plus
 * DayOfYear. EVERY OTHER UNIT IS REFUSED BY NAME, and two of them by necessity: Calendar and TimeZone
 * ask for OBJECTS where this door answers an NSInteger. */
- (void)setValue:(NSInteger)value forComponent:(NSCalendarUnit)unit;
- (NSInteger)valueForComponent:(NSCalendarUnit)unit;

/* THE DEPRECATED WEEK PAIR (Apple deprecates it in favour of -weekOfYear), KEPT AS AN ALIAS of the same
 * storage rather than as a second field: two fields would let the same bag answer two different weeks,
 * and the deprecation note says which one to use, not that they mean different things. */
- (NSInteger)week;
- (void)setWeek:(NSInteger)value;

- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

/* A mutable bag copies by value — the fields, not a shared reference. */
- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDATECOMPONENTS_H */
