/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ndatecomponents.m — the calendar's field bag.
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

#import <foundation/NSDateComponents.h>
#import <foundation/NSCalendar.h>
#import <foundation/NSDate.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>

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
	    && _yearForWeekOfYear == [them yearForWeekOfYear];
}

- (NSUInteger)hash
{
	/* The sentinel is a value, so the fields hash as they are — an unset field
	 * contributes as itself, which keeps equal objects equal. */
	NSUInteger h = 1469598103U;
	const NSInteger fields[14] = {
		_era, _year, _quarter, _month, _day, _hour, _minute,
		_second, _nanosecond, _weekday, _weekdayOrdinal,
		_weekOfMonth, _weekOfYear, _yearForWeekOfYear
	};
	int i;

	for (i = 0; i < 14; i++) {
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

- (id)copyWithZone:(NSZone *)zone
{
	NSDateComponents *copy;

	(void)zone;
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
	return copy;
}

@end
