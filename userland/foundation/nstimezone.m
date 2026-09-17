/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nstimezone.m — a fixed offset from UTC.
 *
 * ARC file. The one ivar is a number, so this class does not own anything and
 * implements no -retain/-release (the ARC/MRR seam, plan §6).
 *
 * THE NAME IS RENDERED WITH LIBC rather than -stringWithFormat:, which is the
 * same call NSNumber and NSData make: snprintf knows the padding rules, and a
 * hand-written zero-pad would be a second thing to get wrong.
 */

#import <foundation/NSTimeZone.h>
#import <foundation/NSString.h>
#import <foundation/NSDate.h>
#include <stdio.h>

@implementation NSTimeZone

+ (NSTimeZone *)timeZoneForSecondsFromGMT:(NSInteger)seconds
{
	return [[self alloc] initWithSecondsFromGMT:seconds];
}

/* UTC, and the header says why: this system has no configured zone to report. */
+ (NSTimeZone *)systemTimeZone
{
	return [[self alloc] initWithSecondsFromGMT:0];
}

+ (NSTimeZone *)localTimeZone
{
	return [[self alloc] initWithSecondsFromGMT:0];
}

- (id)initWithSecondsFromGMT:(NSInteger)seconds
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_secondsFromGMT = seconds;
	return self;
}

- (NSInteger)secondsFromGMT
{
	return _secondsFromGMT;
}

/* The offset is a constant, so the date is not consulted — and for a fixed-offset
 * zone that is not a shortcut but the definition. */
- (NSInteger)secondsFromGMTForDate:(NSDate *)date
{
	(void)date;
	return _secondsFromGMT;
}

- (NSString *)name
{
	char buffer[32];
	NSInteger seconds = _secondsFromGMT;
	long hours, minutes;
	char sign;

	if (seconds == 0) {
		return @"GMT";
	}
	sign = (seconds < 0) ? '-' : '+';
	if (seconds < 0) {
		seconds = -seconds;
	}
	hours = (long)(seconds / 3600);
	minutes = (long)((seconds % 3600) / 60);
	snprintf(buffer, sizeof buffer, "GMT%c%02ld%02ld", sign, hours, minutes);
	return [NSString stringWithUTF8String:buffer];
}

/* NO, and not "unknown": a fixed offset has no transition rules, so there is no
 * daylight saving to be in. A zone that wanted to observe it would need the
 * table this class does not ship. */
- (BOOL)isDaylightSavingTime
{
	return NO;
}

- (BOOL)isDaylightSavingTimeForDate:(NSDate *)date
{
	(void)date;
	return NO;
}

- (BOOL)isEqualToTimeZone:(NSTimeZone *)other
{
	return other != nil && [other secondsFromGMT] == _secondsFromGMT;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSTimeZone class]]) {
		return NO;
	}
	return [self isEqualToTimeZone:(NSTimeZone *)other];
}

- (NSUInteger)hash
{
	return (NSUInteger)(_secondsFromGMT ^ (_secondsFromGMT >> 32));
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSTimeZone: %@>", [self name]];
}

/* Immutable: the offset is set once. */
- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;
}

@end
