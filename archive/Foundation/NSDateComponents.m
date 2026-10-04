/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateComponents.m — the calendar's field bag.
 *
 * MANUAL OWNERSHIP (no owned objects of its own beyond the copied description).
 *
 * -isValidDateInCalendar: IS A ROUND TRIP, and that is the whole trick: build the
 * date from the fields, read the fields back, and compare the ones that were SET.
 * A day that does not exist (2026-02-30) is normalised by the calendar's own
 * arithmetic, so it comes back as 2026-03-02 and the comparison fails. No second
 * notion of "valid" is written down here, which is one fewer thing to disagree
 * with the calendar about.
 */

#import <Foundation/NSDateComponents.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>

@implementation NSDateComponents

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* "Not set" is a value, not a flag: see the header's sentinel note. */
	_era = NSDateComponentUndefined;
	_year = NSDateComponentUndefined;
	_quarter = NSDateComponentUndefined;
	_month = NSDateComponentUndefined;
	_day = NSDateComponentUndefined;
	_hour = NSDateComponentUndefined;
	_minute = NSDateComponentUndefined;
	_second = NSDateComponentUndefined;
	_nanosecond = NSDateComponentUndefined;
	_weekday = NSDateComponentUndefined;
	_weekdayOrdinal = NSDateComponentUndefined;
	_weekOfMonth = NSDateComponentUndefined;
	_weekOfYear = NSDateComponentUndefined;
	_yearForWeekOfYear = NSDateComponentUndefined;
	_dayOfYear = NSDateComponentUndefined;
	return self;
}

- (NSInteger)era { return _era; }
- (void)setEra:(NSInteger)value { _era = value; }
- (NSInteger)year { return _year; }
- (void)setYear:(NSInteger)value { _year = value; }
- (NSInteger)quarter { return _quarter; }
- (void)setQuarter:(NSInteger)value { _quarter = value; }
- (NSInteger)month { return _month; }
- (void)setMonth:(NSInteger)value { _month = value; }
- (NSInteger)day { return _day; }
- (void)setDay:(NSInteger)value { _day = value; }
- (NSInteger)hour { return _hour; }
- (void)setHour:(NSInteger)value { _hour = value; }
- (NSInteger)minute { return _minute; }
- (void)setMinute:(NSInteger)value { _minute = value; }
- (NSInteger)second { return _second; }
- (void)setSecond:(NSInteger)value { _second = value; }
- (NSInteger)nanosecond { return _nanosecond; }
- (void)setNanosecond:(NSInteger)value { _nanosecond = value; }
- (NSInteger)weekday { return _weekday; }
- (void)setWeekday:(NSInteger)value { _weekday = value; }
- (NSInteger)weekdayOrdinal { return _weekdayOrdinal; }
- (void)setWeekdayOrdinal:(NSInteger)value { _weekdayOrdinal = value; }
- (NSInteger)weekOfMonth { return _weekOfMonth; }
- (void)setWeekOfMonth:(NSInteger)value { _weekOfMonth = value; }
- (NSInteger)weekOfYear { return _weekOfYear; }
- (void)setWeekOfYear:(NSInteger)value { _weekOfYear = value; }
- (NSInteger)yearForWeekOfYear { return _yearForWeekOfYear; }
- (void)setYearForWeekOfYear:(NSInteger)value { _yearForWeekOfYear = value; }
- (NSInteger)dayOfYear { return _dayOfYear; }
- (void)setDayOfYear:(NSInteger)value { _dayOfYear = value; }
- (BOOL)leapMonth { return _leapMonth; }
- (void)setLeapMonth:(BOOL)value { _leapMonth = value; }
- (BOOL)repeatedDay { return _repeatedDay; }
- (void)setRepeatedDay:(BOOL)value { _repeatedDay = value; }

/* THE REFERENCES ARE RETAINED, not copied: a calendar is a value-less service object here, and the bag
 * does not own what it means. This is the first owned storage the class has, so it is also where
 * -dealloc appears. */
- (nullable NSCalendar *)calendar { return _calendar; }
- (void)setCalendar:(nullable NSCalendar *)calendar
{
	if (_calendar == calendar) {
		return;
	}
	[calendar retain];
	[_calendar release];
	_calendar = calendar;
}

- (nullable NSTimeZone *)timeZone { return _timeZone; }
- (void)setTimeZone:(nullable NSTimeZone *)timeZone
{
	if (_timeZone == timeZone) {
		return;
	}
	[timeZone retain];
	[_timeZone release];
	_timeZone = timeZone;
}

- (void)dealloc
{
	[_calendar release];
	[_timeZone release];
	[super dealloc];
}

- (nullable NSCalendar *)fnCalendarOrCurrent
{
	return _calendar != nil ? _calendar : [NSCalendar currentCalendar];
}

- (nullable NSDate *)date
{
	return [[self fnCalendarOrCurrent] dateFromComponents:self];
}

- (BOOL)validDate
{
	return [self isValidDateInCalendar:[self fnCalendarOrCurrent]];
}

- (NSInteger)week { return [self weekOfYear]; }
- (void)setWeek:(NSInteger)value { [self setWeekOfYear:value]; }

/* ONE SWITCH, AND BOTH DOORS USE IT: -valueForComponent: reads the field the unit names and
 * -setValue:forComponent: writes it, so the pair cannot disagree about which unit is which field. A unit
 * that is not a field of a bag is REFUSED BY NAME - including the two that name OBJECTS, which an
 * NSInteger answer cannot carry. */
- (NSInteger)valueForComponent:(NSCalendarUnit)unit
{
	switch (unit) {
	case NSCalendarUnitEra:			return _era;
	case NSCalendarUnitYear:		return _year;
	case NSCalendarUnitQuarter:		return _quarter;
	case NSCalendarUnitMonth:		return _month;
	case NSCalendarUnitDay:			return _day;
	case NSCalendarUnitHour:		return _hour;
	case NSCalendarUnitMinute:		return _minute;
	case NSCalendarUnitSecond:		return _second;
	case NSCalendarUnitNanosecond:		return _nanosecond;
	case NSCalendarUnitWeekday:		return _weekday;
	case NSCalendarUnitWeekdayOrdinal:	return _weekdayOrdinal;
	case NSCalendarUnitWeekOfMonth:		return _weekOfMonth;
	case NSCalendarUnitWeekOfYear:		return _weekOfYear;
	case NSCalendarUnitYearForWeekOfYear:	return _yearForWeekOfYear;
	case NSCalendarUnitDayOfYear:		return _dayOfYear;
	default:				break;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"-valueForComponent: %lu is not a field a component bag holds",
			   (unsigned long)unit];
	return NSDateComponentUndefined;
}

- (void)setValue:(NSInteger)value forComponent:(NSCalendarUnit)unit
{
	switch (unit) {
	case NSCalendarUnitEra:			_era = value; return;
	case NSCalendarUnitYear:		_year = value; return;
	case NSCalendarUnitQuarter:		_quarter = value; return;
	case NSCalendarUnitMonth:		_month = value; return;
	case NSCalendarUnitDay:			_day = value; return;
	case NSCalendarUnitHour:		_hour = value; return;
	case NSCalendarUnitMinute:		_minute = value; return;
	case NSCalendarUnitSecond:		_second = value; return;
	case NSCalendarUnitNanosecond:		_nanosecond = value; return;
	case NSCalendarUnitWeekday:		_weekday = value; return;
	case NSCalendarUnitWeekdayOrdinal:	_weekdayOrdinal = value; return;
	case NSCalendarUnitWeekOfMonth:		_weekOfMonth = value; return;
	case NSCalendarUnitWeekOfYear:		_weekOfYear = value; return;
	case NSCalendarUnitYearForWeekOfYear:	_yearForWeekOfYear = value; return;
	case NSCalendarUnitDayOfYear:		_dayOfYear = value; return;
	default:				break;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"-setValue:forComponent: %lu is not a field a component bag holds",
			   (unsigned long)unit];
}

- (BOOL)isValidDateInCalendar:(NSCalendar *)calendar
{
	NSDate *made;
	NSDateComponents *back;
	NSCalendarUnit asked;

	if (calendar == nil) {
		/* A nil calendar is a caller error, not a date that happens to be
		 * invalid: the library raises for a nil where the annotation says
		 * nonnull, as -compare: does. */
		[NSException raise:NSInvalidArgumentException
			    format:@"-isValidDateInCalendar: needs a calendar"];
	}

	/* A component that sets ONLY week fields is not a date: those fields are
	 * what a conversion FILLS IN, not a second way to say when. */
	if (_year == NSDateComponentUndefined && _month == NSDateComponentUndefined
	    && _day == NSDateComponentUndefined) {
		return NO;
	}

	made = [calendar dateFromComponents:self];
	if (made == nil) {
		return NO;
	}

	asked = NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth
	      | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute
	      | NSCalendarUnitSecond;
	back = [calendar components:asked fromDate:made];

	/* Compare only what the caller SET — an unset field is not a claim. */
	if (_era != NSDateComponentUndefined && [back era] != _era) return NO;
	if (_year != NSDateComponentUndefined && [back year] != _year) return NO;
	if (_month != NSDateComponentUndefined && [back month] != _month) return NO;
	if (_day != NSDateComponentUndefined && [back day] != _day) return NO;
	if (_hour != NSDateComponentUndefined && [back hour] != _hour) return NO;
	if (_minute != NSDateComponentUndefined && [back minute] != _minute) return NO;
	if (_second != NSDateComponentUndefined && [back second] != _second) return NO;
	return YES;
}

- (BOOL)isEqual:(id)other
{
	NSDateComponents *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSDateComponents class]]) {
		return NO;
	}
	them = (NSDateComponents *)other;
	return _era == [them era] && _year == [them year] && _quarter == [them quarter]
	    && _month == [them month] && _day == [them day]
	    && _hour == [them hour] && _minute == [them minute] && _second == [them second]
	    && _nanosecond == [them nanosecond] && _weekday == [them weekday]
	    && _weekdayOrdinal == [them weekdayOrdinal]
	    && _weekOfMonth == [them weekOfMonth] && _weekOfYear == [them weekOfYear]
	    && _yearForWeekOfYear == [them yearForWeekOfYear]
	    && _dayOfYear == [them dayOfYear] && _leapMonth == [them leapMonth]
	    && _repeatedDay == [them repeatedDay];
}

- (NSUInteger)hash
{
	/* The sentinel is a value, so the fields hash as they are — an unset field
	 * contributes as itself, which keeps equal objects equal. */
	NSUInteger h = 1469598103U;
	const NSInteger fields[17] = {
		_era, _year, _quarter, _month, _day, _hour, _minute,
		_second, _nanosecond, _weekday, _weekdayOrdinal,
		_weekOfMonth, _weekOfYear, _yearForWeekOfYear,
		_dayOfYear, (NSInteger)_leapMonth, (NSInteger)_repeatedDay
	};
	int i;

	for (i = 0; i < 17; i++) {
		h ^= (NSUInteger)fields[i];
		h *= 16777619U;
	}
	return h;
}

- (NSString *)description
{
	return [NSString stringWithFormat:
	    @"<NSDateComponents: era %ld year %ld month %ld day %ld %02ld:%02ld:%02ld>",
	    (long)_era, (long)_year, (long)_month, (long)_day,
	    (long)(_hour == NSDateComponentUndefined ? 0 : _hour),
	    (long)(_minute == NSDateComponentUndefined ? 0 : _minute),
	    (long)(_second == NSDateComponentUndefined ? 0 : _second)];
}

- (id)copy
{
	NSDateComponents *copy;

	copy = [[NSDateComponents alloc] init];
	[copy setEra:_era];
	[copy setYear:_year];
	[copy setQuarter:_quarter];
	[copy setMonth:_month];
	[copy setDay:_day];
	[copy setHour:_hour];
	[copy setMinute:_minute];
	[copy setSecond:_second];
	[copy setNanosecond:_nanosecond];
	[copy setWeekday:_weekday];
	[copy setWeekdayOrdinal:_weekdayOrdinal];
	[copy setWeekOfMonth:_weekOfMonth];
	[copy setWeekOfYear:_weekOfYear];
	[copy setYearForWeekOfYear:_yearForWeekOfYear];
	[copy setDayOfYear:_dayOfYear];
	[copy setLeapMonth:_leapMonth];
	[copy setRepeatedDay:_repeatedDay];
	/* A COPY IS THE SAME BAG READ THE SAME WAY, so the references come along. */
	[copy setCalendar:_calendar];
	[copy setTimeZone:_timeZone];
	return copy;
}

@end
