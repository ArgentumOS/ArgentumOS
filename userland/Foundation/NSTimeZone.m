/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTimeZone.m — a zone by name, or by offset (F13.7a).
 *
 * MANUAL OWNERSHIP, and the second Foundation file that knows ICU exists (the first is NSDateFormatter.m).
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

#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSArray.h>

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

	if (name == nil || [name lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
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
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}


/* ================== FOUR TIME ZONE DOORS (§63.123) ==================
 * ⚠ ONE ICU ROUTE FOR ALL FOUR. **AND THE TWO FACTS THAT COST A ROUND EACH ARE WRITTEN INTO IT: `ucal_open` TAKES
 * `const UChar *` — UTF-16 — SO THE IDENTIFIER IS CONVERTED (the compiler said so for a whole round before I read
 * it: ‘incompatible pointer types’, which is what “a fixed zone answered UTC” looked like from outside), AND ITS
 * LOCALE ARGUMENT IS THE THIRD ONE, WHICH I DROPPED ONCE AND THE COMPILER COUNTED.** */
static NSString *fn_tz_name_for_kind(NSTimeZone *zone, UCalendarDisplayNameType kind, NSLocale *locale)
{
	NSString *ident = [zone name];
	const char *tag;
	char fixed[32];
	UChar zoneID[64];
	UChar buffer[256];
	int32_t idLength = 0;
	int32_t length;
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *cal;

	if (ident == nil) {
		NSInteger minutes = [zone secondsFromGMT] / 60;
		NSInteger abs = minutes < 0 ? -minutes : minutes;

		snprintf(fixed, sizeof fixed, "GMT%c%02ld:%02ld", minutes < 0 ? '-' : '+',
			 (long)(abs / 60), (long)(abs % 60));
		tag = fixed;
	} else {
		tag = [ident UTF8String];
	}
	u_strFromUTF8(zoneID, 64, &idLength, tag, -1, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	status = U_ZERO_ERROR;
	cal = ucal_open(zoneID, idLength, NULL, UCAL_DEFAULT, &status);	/* ⚠ NULL is the locale */
	if (U_FAILURE(status) || cal == NULL) {
		return nil;
	}
	length = ucal_getTimeZoneDisplayName(cal, kind,
					     locale == nil ? NULL : [[locale localeIdentifier] UTF8String],
					     buffer, 256, &status);
	ucal_close(cal);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:buffer length:(NSUInteger)length];
}

- (NSString *)abbreviation
{
	return [self abbreviationForDate:[NSDate date]];
}

- (NSString *)abbreviationForDate:(NSDate *)date
{
	(void)date;
	if (_name == nil) {
		/* ⚠ A FIXED-OFFSET ZONE'S ABBREVIATION IS ARITHMETIC, NOT A LOOKUP — and the reading is ours (D2). It is the
		 * spelling ICU itself uses for such zones. */
		NSInteger minutes = _secondsFromGMT / 60;
		NSInteger abs = minutes < 0 ? -minutes : minutes;

		return [NSString stringWithFormat:@"GMT%c%02ld:%02ld", minutes < 0 ? '-' : '+',
						 (long)(abs / 60), (long)(abs % 60)];
	}
	return fn_tz_name_for_kind(self, [self isDaylightSavingTime] ? UCAL_SHORT_DST : UCAL_SHORT_STANDARD, nil);
}

- (NSTimeInterval)daylightSavingTimeOffsetForDate:(NSDate *)date
{
	/* ⚠ THE IVAR AND NOT `-name`: the DOOR SYNTHESIZES a name for a fixed-offset zone, so it never answers nil —
	 * which is exactly how a fixed zone came to report an hour of daylight saving. */
	NSString *ident = _name;
	UChar zoneID[64];
	int32_t idLength = 0;
	UErrorCode status = U_ZERO_ERROR;
	UCalendar *cal;
	int32_t savings;

	if (ident == nil) {
		return 0.0;	/* a fixed-offset zone has no daylight saving: an ANSWER, not a gap */
	}
	u_strFromUTF8(zoneID, 64, &idLength, [ident UTF8String], -1, &status);
	if (U_FAILURE(status)) {
		return 0.0;
	}
	status = U_ZERO_ERROR;
	cal = ucal_open(zoneID, idLength, NULL, UCAL_DEFAULT, &status);
	if (U_FAILURE(status) || cal == NULL) {
		return 0.0;
	}
	/* ⚠⚠ AND THE SECOND FACT THE COMPILER HAD BEEN GIVING ME WITH THE TYPES IN PARENTHESES: `ucal_getDSTSavings`
	 * TAKES THE ZONE ID — `const UChar *` — AND NOT A CALENDAR, which is what ‘passing UCalendar * to const UChar *’
	 * says. It also answers the ZONE'S saving rather than the DATE'S, so the calendar is what decides WHETHER the date is
	 * in daylight saving at all: **A DOOR NAMED ‘…ForDate:’ MUST ASK ABOUT THE DATE, AND THE ZONE ONLY ANSWERS BY HOW
	 * MUCH.** */
	ucal_setMillis(cal, (UDate)([date timeIntervalSince1970] * 1000.0), &status);
	if (U_SUCCESS(status) && ucal_inDaylightTime(cal, &status)) {
		savings = ucal_getDSTSavings(zoneID, &status);
	} else {
		savings = 0;
	}
	ucal_close(cal);
	return U_FAILURE(status) ? 0.0 : (NSTimeInterval)savings / 1000.0;
}

- (NSString *)localizedName:(NSTimeZoneNameStyle)style locale:(NSLocale *)locale
{
	UCalendarDisplayNameType kind;

	if (_name == nil) {
		return [self abbreviationForDate:[NSDate date]];
	}
	switch (style) {
	case NSTimeZoneNameStyleShortStandard: kind = UCAL_SHORT_STANDARD; break;
	case NSTimeZoneNameStyleDaylightSaving: kind = UCAL_DST; break;
	case NSTimeZoneNameStyleShortDaylightSaving: kind = UCAL_SHORT_DST; break;
	case NSTimeZoneNameStyleGeneric:
	case NSTimeZoneNameStyleShortGeneric:
	case NSTimeZoneNameStyleStandard:
	default: kind = UCAL_STANDARD; break;
	}
	return fn_tz_name_for_kind(self, kind, locale);
}

/* ⚠⚠ `+defaultTimeZone` IS NOT ICU'S: A CLASS STORE WHOSE SETTER RELEASES ITS PREDECESSOR. A setter that forgets the
 * old value is a leak — §63.79's lesson one class over — and a nil set falls back to the system zone. */
static NSTimeZone *fn_default_time_zone = nil;

+ (NSTimeZone *)defaultTimeZone
{
	return fn_default_time_zone != nil ? fn_default_time_zone : [self systemTimeZone];
}

+ (void)setDefaultTimeZone:(NSTimeZone *)aTimeZone
{
	NSTimeZone *old = fn_default_time_zone;

	fn_default_time_zone = [aTimeZone copy];
	[old release];
}

@end
