/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDate.m — a point in time.
 *
 * MANUAL OWNERSHIP. `+date` reads the system clock through libc (the kernel provides
 * it); the sub-second part comes from gettimeofday.
 */

#import <Foundation/NSDate.h>
#import <Foundation/NSTimeZone.h>	/* §63.229: the zone is set on the object */
#import <Foundation/NSCalendarDate.h>	/* §63.229: the CalendarFormat: doors answer an NSCalendarDate */
#import <Foundation/NSCoder.h>	/* the coder forms below */
#import <Foundation/NSString.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

@implementation NSDate

+ (NSDate *)date
{
	struct timeval now;

	if (gettimeofday(&now, NULL) == 0) {
		return [[self alloc] initWithTimeIntervalSince1970:
			(double)now.tv_sec + (double)now.tv_usec / 1000000.0];
	}
	return [[self alloc] initWithTimeIntervalSince1970:(double)time(NULL)];
}

+ (NSDate *)dateWithTimeIntervalSince1970:(double)seconds
{
	return [[self alloc] initWithTimeIntervalSince1970:seconds];
}

/* The reference date is Cocoa's: 1 January 2001, GMT. */
#define FN_REFERENCE_DATE_SECONDS	978307200.0

+ (NSDate *)dateWithTimeIntervalSinceNow:(double)seconds
{
	return [[self alloc] initWithTimeIntervalSinceNow:seconds];
}

+ (NSDate *)dateWithTimeInterval:(double)seconds sinceDate:(NSDate *)date
{
	return [[self alloc] initWithTimeInterval:seconds sinceDate:date];
}

+ (NSDate *)dateWithTimeIntervalSinceReferenceDate:(double)seconds
{
	return [[self alloc] initWithTimeIntervalSinceReferenceDate:seconds];
}

+ (double)timeIntervalSinceReferenceDate
{
	return [[self date] timeIntervalSinceReferenceDate];
}

+ (NSDate *)distantPast
{
	return [[self alloc] initWithTimeIntervalSince1970:-62135596800.0];	/* 0001-01-01 */
}

+ (NSDate *)distantFuture
{
	return [[self alloc] initWithTimeIntervalSince1970:64092211200.0];	/* 4001-01-01 */
}

- (id)initWithTimeIntervalSinceNow:(double)seconds
{
	return [self initWithTimeIntervalSince1970:
		[[NSDate date] timeIntervalSince1970] + seconds];
}

- (id)initWithTimeInterval:(double)seconds sinceDate:(NSDate *)date
{
	return [self initWithTimeIntervalSince1970:
		[date timeIntervalSince1970] + seconds];
}

- (double)timeIntervalSinceNow
{
	/*
	 * self MINUS now, the same subtraction direction as -timeIntervalSinceDate:
	 * The inverted version reported a NEGATIVE interval for a future date, which
	 * the date-extras check caught: a date an hour ahead has +3600, not -3600.
	 */
	return _timeIntervalSince1970 - [[NSDate date] timeIntervalSince1970];
}

- (id)initWithTimeIntervalSinceReferenceDate:(double)seconds
{
	return [self initWithTimeIntervalSince1970:FN_REFERENCE_DATE_SECONDS + seconds];
}

+ (id)dateWithSRAbsoluteTime:(double)seconds
{
	return [self dateWithTimeIntervalSinceReferenceDate:seconds];
}

- (id)initWithSRAbsoluteTime:(double)seconds
{
	return [self initWithTimeIntervalSinceReferenceDate:seconds];
}

- (double)srAbsoluteTime
{
	return [self timeIntervalSinceReferenceDate];
}

+ (NSDate *)now
{
	return [self date];
}

+ (NSDate *)dateWithString:(NSString *)description
{
	/* APPLE'S DOCUMENTED FORM, parsed by hand because there is no strptime door here:
	 * "YYYY-MM-DD HH:MM:SS +HHMM". nil for anything else, which is the documented answer. */
	NSInteger year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	char zoneSign = 0;
	int zoneHour = 0, zoneMinute = 0;
	const char *text = [description UTF8String];
	int consumed = 0;

	if (text == NULL || [description length] == 0) {
		return nil;
	}
	if (sscanf(text, "%4ld-%2ld-%2ld %2ld:%2ld:%2ld %c%2d%2d%n", (long *)&year, (long *)&month,
		   (long *)&day, (long *)&hour, (long *)&minute, (long *)&second, &zoneSign, &zoneHour,
		   &zoneMinute, &consumed) != 9) {
		return nil;
	}
	if ((zoneSign != '+' && zoneSign != '-') || text[consumed] != '\0') {	/* one byte past the match IS the end check */
		return nil;
	}
	{
		/* THE FIELDS ARE UTC, so the offset is applied to reach the instant: the epoch here is 2001, and
		 * the day arithmetic is Howard Hinnant's days-from-civil. */
		NSInteger y = year, m = (month >= 3) ? month - 3 : month + 9;

		if (month <= 2) {
			y -= 1;	/* the March-based year days_from_civil requires: WITHOUT it every January and
				 * February lands exactly one year late (the probe said 31536000 for 2001-01-01) */
		}
		NSInteger era = (y >= 0 ? y : y - 399) / 400;
		NSInteger yoe = y - era * 400;
		NSInteger doy = (153 * m + 2) / 5 + day - 1;
		NSInteger doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		NSInteger days = era * 146097 + doe - 719468;
		double seconds = (double)days * 86400.0 + (double)hour * 3600.0 + (double)minute * 60.0 +
				 (double)second;
		int offset = (zoneSign == '-' ? -1 : 1) * (zoneHour * 3600 + zoneMinute * 60);

		seconds -= (double)offset;			/* the string's own offset -> UTC */
		seconds -= 978307200.0;				/* 2001-01-01 GMT in the Unix epoch */
		return [self dateWithTimeIntervalSinceReferenceDate:seconds];
	}
}

- (NSDate *)addTimeInterval:(double)seconds
{
	return [self dateByAddingTimeInterval:seconds];
}



- (double)timeIntervalSinceReferenceDate
{
	return _timeIntervalSince1970 - FN_REFERENCE_DATE_SECONDS;
}

- (NSDate *)dateByAddingTimeInterval:(double)seconds
{
	return [[NSDate alloc] initWithTimeIntervalSince1970:_timeIntervalSince1970 + seconds];
}

- (NSString *)descriptionWithLocale:(id)locale
{
	(void)locale;
	return [self description];
}

- (id)initWithTimeIntervalSince1970:(double)seconds
{
	self = [super init];
	if (self != nil) {
		_timeIntervalSince1970 = seconds;
	}
	return self;
}

- (double)timeIntervalSince1970
{
	return _timeIntervalSince1970;
}

- (double)timeIntervalSinceDate:(NSDate *)other
{
	return _timeIntervalSince1970 - [other timeIntervalSince1970];
}

- (BOOL)isEqualToDate:(NSDate *)other
{
	if (other == nil) {
		return NO;
	}
	return _timeIntervalSince1970 == [other timeIntervalSince1970];
}

- (NSComparisonResult)compare:(NSDate *)other
{
	double b = [other timeIntervalSince1970];

	if (_timeIntervalSince1970 < b) {
		return NSOrderedAscending;
	}
	if (_timeIntervalSince1970 > b) {
		return NSOrderedDescending;
	}
	return NSOrderedSame;
}

- (NSDate *)earlierDate:(NSDate *)other
{
	return ([self compare:other] <= 0) ? self : other;
}

- (NSDate *)laterDate:(NSDate *)other
{
	return ([self compare:other] >= 0) ? self : other;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSDate class]]) {
		return NO;
	}
	return [self isEqualToDate:(NSDate *)other];
}

- (unsigned long)hash
{
	/* The same rule as NSNumber: an integral value hashes as its integer, so
	 * two dates built from one instant agree. */
	long long whole = (long long)_timeIntervalSince1970;
	unsigned long h = 2166136261UL;

	if ((double)whole == _timeIntervalSince1970) {
		h ^= (unsigned long)whole;
		return h * 16777619UL;
	}
	{
		union {
			double d;
			unsigned long u;
		} bits;

		bits.d = _timeIntervalSince1970;
		h ^= bits.u;
		return h * 16777619UL;
	}
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}


- (NSString *)description
{
	time_t seconds = (time_t)_timeIntervalSince1970;
	struct tm parts;
	char buffer[64];

	if (gmtime_r(&seconds, &parts) != NULL &&
	    strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S +0000", &parts) > 0) {
		return [NSString stringWithUTF8String:buffer];
	}
	snprintf(buffer, sizeof buffer, "%g seconds since 1970", _timeIntervalSince1970);
	return [NSString stringWithUTF8String:buffer];
}


/*
 * NSCoding FOR A DATE (D7's kind (D): NSCoding conformance). This is the FIRST class in this
 * library to implement the protocol — the archiver, the coder and the protocol all ship, and until
 * now NOTHING OF OURS COULD BE ARCHIVED, which is a gap worth naming rather than a detail. A date is
 * one double, so the pair is one key each way; the key's SPELLING is this library's, because it is
 * internal to our own archive format and a program never sees it (Apple's date key is not published
 * and not a contract).
 */
- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeDouble:_timeIntervalSince1970 forKey:@"NS.time"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithTimeIntervalSince1970:[coder decodeDoubleForKey:@"NS.time"]];
}

- (NSCalendarDate *)dateWithCalendarFormat:(NSString *)format timeZone:(NSTimeZone *)aTimeZone
{
	/* NSCalendarDate IS AN NSDate SUBCLASS whose FORMAT AND ZONE ARE SETTABLE, so this is the inherited
	 * initializer plus the pair — one formatting engine, not two. */
	NSCalendarDate *date = [[NSCalendarDate alloc] initWithTimeIntervalSinceReferenceDate:
							[self timeIntervalSinceReferenceDate]];

	[date setCalendarFormat:format];
	[date setTimeZone:aTimeZone];
	return [date autorelease];
}

- (NSString *)descriptionWithCalendarFormat:(NSString *)format
				   timeZone:(NSTimeZone *)aTimeZone
				     locale:(id)locale
{
	/* THE LOCALE FORM IS THE ONE NSCalendarDate HAS; the zone goes on the OBJECT first, which is why this door
	 * needs no third spelling from it. */
	NSCalendarDate *date = [self dateWithCalendarFormat:format timeZone:aTimeZone];

	return [date descriptionWithCalendarFormat:format locale:locale];
}
@end
