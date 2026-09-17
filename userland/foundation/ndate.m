/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ndate.m — a point in time.
 *
 * ARC file. `+date` reads the system clock through libc (the kernel provides
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

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;
}

- (id)mutableCopyWithZone:(NSZone *)zone
{
	(void)zone;
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
