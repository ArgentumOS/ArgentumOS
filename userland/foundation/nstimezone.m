/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nstimezone.m — a zone by name, or by offset (F13.7a).
 *
 * MANUAL OWNERSHIP, and the second Foundation file that knows ICU exists (the first is nsdateformatter.m).
 * The class is IMMUTABLE, so every ICU lookup ends in a number or a string and no handle is kept:
 * holding a UCalendar open per zone would buy nothing but a lifetime to get wrong.
 *
 * THE NAME IS RENDERED WITH LIBC for a fixed-offset zone, as before: snprintf knows the padding
 * rules and a hand-written zero-pad would be a second thing to get wrong.
 *
 * THE BORROWED-BUFFER RULE (§6) applies to every conversion here: a zone identifier is SHORT
 * ("GMT", "UTC", "CET" are under nine characters, so tagged-pointer territory), so its -UTF8String
 * bytes are a decode scratch — consumed immediately, never held.
 */

#import <foundation/NSTimeZone.h>
#import <foundation/NSString.h>
#import <foundation/NSDate.h>
#import <foundation/NSArray.h>

#include <unicode/ucal.h>
#include <unicode/uenum.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <string.h>

#define FN_TZ_ID_MAX 96

/* THE ZONE'S OFFSET AT AN INSTANT, out of ICU: the raw offset plus whatever DST offset is in force
 * then. ICU reports both, so the two questions this class is asked — "what is the offset" and "is
 * it daylight saving" — are ONE lookup rather than two.
 *
 * ICU reports MILLISECONDS; this class speaks seconds, like the rest of Cocoa. */
static NSInteger fn_tz_offset(NSString *name, double secondsSince1970, BOOL *isDST)
{
	UChar identifier[FN_TZ_ID_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	UCalendar *calendar;
	int32_t raw;
	int32_t dst;

	if (isDST != NULL) {
		*isDST = NO;
	}
	if (name == nil) {
		return 0;
	}
	u_strFromUTF8(identifier, FN_TZ_ID_MAX, &length, [name UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return 0;
	}
	status = U_ZERO_ERROR;
	calendar = ucal_open(identifier, length, NULL, UCAL_DEFAULT, &status);
	if (U_FAILURE(status) || calendar == NULL) {
		return 0;
	}
	ucal_setMillis(calendar, (UDate)(secondsSince1970 * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return 0;
	}
	raw = ucal_get(calendar, UCAL_ZONE_OFFSET, &status);
	dst = ucal_get(calendar, UCAL_DST_OFFSET, &status);
	ucal_close(calendar);
	if (U_FAILURE(status)) {
		return 0;
	}
	if (isDST != NULL && dst != 0) {
		*isDST = YES;
	}
	return (NSInteger)((raw + dst) / 1000);
}

@implementation NSTimeZone

+ (NSTimeZone *)timeZoneForSecondsFromGMT:(NSInteger)seconds
{
	return [[self alloc] initWithSecondsFromGMT:seconds];
}

+ (NSTimeZone *)timeZoneWithName:(NSString *)name
{
	return [[self alloc] initWithName:name];
}

/* ICU'S DEFAULT ZONE, which is where a TZ the system sets would arrive — so this answers "GMT"
 * until something sets one, and follows it afterwards. The F7 note's reasoning ("the kernel clock
 * is UTC and nothing sets a user zone") is now a fact about ICU's data rather than a hard-coded
 * zero. */
+ (NSTimeZone *)systemTimeZone
{
	UChar identifier[FN_TZ_ID_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;
	char text[FN_TZ_ID_MAX * 2];
	int32_t used = 0;
	NSTimeZone *zone;

	length = ucal_getDefaultTimeZone(identifier, FN_TZ_ID_MAX, &status);
	if (U_FAILURE(status) || length <= 0) {
		return [[self alloc] initWithSecondsFromGMT:0];
	}
	status = U_ZERO_ERROR;
	u_strToUTF8(text, sizeof text, &used, identifier, length, &status);
	if (U_FAILURE(status)) {
		return [[self alloc] initWithSecondsFromGMT:0];
	}
	/* ICU'S UNKNOWN FALLBACK IS NOT A ZONE A SYSTEM REPORTS. When the environment names no zone
	 * (no TZ, and this system has no /etc/localtime - there is no /etc at all), ICU answers
	 * "Etc/Unknown", and reporting that as the system zone would be a name no caller can use and
	 * one Apple would never return. GMT is the same instant, is a REAL zone in the database, and
	 * is what a system with nothing configured means. (Measured in F13.7a's gate: without this,
	 * a calendar in the system zone no longer equals one in a fixed-offset UTC zone, which is
	 * correct-by-name and silently wrong in practice.) */
	if (strcmp(text, "Etc/Unknown") == 0) {
		return [[self alloc] initWithName:@"GMT"];
	}
	zone = [[self alloc] initWithName:[NSString stringWithUTF8String:text]];
	if (zone == nil) {
		return [[self alloc] initWithSecondsFromGMT:0];
	}
	return zone;
}

+ (NSTimeZone *)localTimeZone
{
	/* No per-application override ships, so the local zone IS the system zone — and when one
	 * arrives, this is the one line that changes. */
	return [self systemTimeZone];
}

+ (NSArray *)knownTimeZoneNames
{
	UErrorCode status = U_ZERO_ERROR;
	UEnumeration *zones = ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_ANY, NULL, NULL, &status);
	NSMutableArray *names;

	if (U_FAILURE(status) || zones == NULL) {
		return @[];
	}
	names = [NSMutableArray array];
	for (;;) {
		int32_t length = 0;
		const char *identifier = uenum_next(zones, &length, &status);
		NSString *text;

		if (U_FAILURE(status) || identifier == NULL) {
			break;
		}
		text = [NSString stringWithUTF8String:identifier];
		if (text != nil) {
			[names addObject:text];
		}
	}
	uenum_close(zones);
	return names;
}

- (id)initWithSecondsFromGMT:(NSInteger)seconds
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_name = nil;
	_secondsFromGMT = seconds;
	return self;
}

- (id)initWithName:(NSString *)name
{
	UChar identifier[FN_TZ_ID_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	UCalendar *probe;

	if (name == nil || [name length] == 0) {
		return nil;
	}
	u_strFromUTF8(identifier, FN_TZ_ID_MAX, &length, [name UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	/* THE NAME IS VALIDATED BY ASKING THE DATABASE, which is what makes an unknown identifier
	 * answer nil — Apple's answer too, and the reason this is a nullable initialiser. */
	status = U_ZERO_ERROR;
	probe = ucal_open(identifier, length, NULL, UCAL_DEFAULT, &status);
	if (U_FAILURE(status) || probe == NULL) {
		return nil;
	}
	ucal_close(probe);
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_name = name;
	_secondsFromGMT = 0;
	return self;
}

- (NSInteger)secondsFromGMTForDate:(NSDate *)date
{
	if (_name == nil) {
		return _secondsFromGMT;		/* the definition of a fixed-offset zone */
	}
	return fn_tz_offset(_name, [date timeIntervalSince1970], NULL);
}

- (NSInteger)secondsFromGMT
{
	if (_name == nil) {
		return _secondsFromGMT;
	}
	/* Without a date, Cocoa means "now". */
	return fn_tz_offset(_name, [[NSDate date] timeIntervalSince1970], NULL);
}

- (NSString *)name
{
	char buffer[32];
	NSInteger seconds = _secondsFromGMT;
	long hours, minutes;
	char sign;

	if (_name != nil) {
		return _name;
	}
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

- (BOOL)isDaylightSavingTime
{
	return [self isDaylightSavingTimeForDate:[NSDate date]];
}

- (BOOL)isDaylightSavingTimeForDate:(NSDate *)date
{
	BOOL isDST = NO;

	if (_name == nil) {
		/* NO, and not "unknown": a fixed offset has no transition rules, so there is no
		 * daylight saving to be in. */
		return NO;
	}
	(void)fn_tz_offset(_name, [date timeIntervalSince1970], &isDST);
	return isDST;
}

- (nullable NSDate *)nextDaylightSavingTimeTransitionAfterDate:(NSDate *)date
{
	UChar identifier[FN_TZ_ID_MAX];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	UCalendar *calendar;
	UDate transition = 0;
	UBool found;

	if (_name == nil) {
		return nil;			/* a transition IS a table entry */
	}
	u_strFromUTF8(identifier, FN_TZ_ID_MAX, &length, [_name UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	status = U_ZERO_ERROR;
	calendar = ucal_open(identifier, length, NULL, UCAL_DEFAULT, &status);
	if (U_FAILURE(status) || calendar == NULL) {
		return nil;
	}
	ucal_setMillis(calendar, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_FAILURE(status)) {
		ucal_close(calendar);
		return nil;
	}
	found = ucal_getTimeZoneTransitionDate(calendar, UCAL_TZ_TRANSITION_NEXT, &transition,
					       &status);
	ucal_close(calendar);
	if (U_FAILURE(status) || !found) {
		return nil;
	}
	return [NSDate dateWithTimeIntervalSince1970:(double)(transition / 1000.0)];
}

- (nullable NSDate *)nextDaylightSavingTimeTransition
{
	return [self nextDaylightSavingTimeTransitionAfterDate:[NSDate date]];
}

- (BOOL)isEqualToTimeZone:(NSTimeZone *)other
{
	if (other == nil) {
		return NO;
	}
	/* THE NAME IS THE IDENTITY, and for a fixed-offset zone the rendered name IS the offset
	 * ("GMT", "GMT+0530") — so one comparison covers both kinds. That is Cocoa's rule too: two
	 * zones are equal when they are the same ZONE, not when they happen to agree on the offset
	 * of one instant. */
	return [[self name] isEqualToString:[other name]];
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
	return [[self name] hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSTimeZone: %@>", [self name]];
}

/* Immutable: the name or the offset is set once. */
- (id)copy
{
	return self;
}

@end
