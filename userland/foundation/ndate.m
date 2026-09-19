/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ndate.m — a point in time.
 *
 * MANUAL OWNERSHIP. `+date` reads the system clock through libc (the kernel provides
 * it); the sub-second part comes from gettimeofday.
 */

#import <foundation/NSDate.h>
#import <foundation/NSString.h>
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
	return self;		/* immutable */
}

- (id)mutableCopy
{
	return self;
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

@end
