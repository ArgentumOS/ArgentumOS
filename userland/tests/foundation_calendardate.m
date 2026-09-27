/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_calendardate — §62.67's acceptance: NSCalendarDate.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * EVERY CHECK USES A FIXED-OFFSET ZONE, deliberately: a named zone's answer depends on a daylight-saving
 * database and on the instant it is asked about, so a probe that asserted fields in "Europe/London" would be
 * asserting ICU's data rather than this class. One check does use the SYSTEM zone, and it asserts only what must
 * hold in any zone — that the instant is what the fields say it is.
 *
 * WHAT THIS CLASS ADDS TO AN NSDate IS TWO DECISIONS, so that is what is checked: the FORMAT (a string that both
 * -description and the parse doors use) and the ZONE (which decides how every field reads). The fields
 * themselves are computed and never stored, and the two checks that prove it are the ones that MOVE the zone and
 * watch every field change at once, and the one that round-trips an instant through text and back.
 *
 * THE CALENDAR-AWARE ARITHMETIC GETS ITS OWN CHECK because it is the difference between this door and adding a
 * number of seconds: 31 JANUARY PLUS ONE MONTH IS 28 FEBRUARY, and a fixed thirty-day add would answer 2 March.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

/* UTC+1: fixed all year, so every field below is arithmetic rather than a daylight-saving lookup. */
static NSTimeZone *fn_zone(void)
{
	NSTimeZone *zone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

	return zone != nil ? zone : [NSTimeZone systemTimeZone];
}

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CALENDARDATE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CALENDARDATE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	NSTimeZone *zone = fn_zone();		/* UTC+1 */
	NSCalendarDate *when;
	double instant;

	/* THE TWO THINGS A NEW DATE CARRIES, and the doors that set them. */
	{
		NSCalendarDate *fresh = [NSCalendarDate calendarDate];
		NSTimeZone *other = [NSTimeZone timeZoneForSecondsFromGMT:-18000];

		[fresh setCalendarFormat:@"%Y/%m/%d"];
		[fresh setTimeZone:other];
		check("the-format-and-the-zone-are-kept-and-settable",
		      [[NSCalendarDate calendarDate] calendarFormat] != nil &&
		      [[fresh calendarFormat] isEqualToString:@"%Y/%m/%d"] &&
		      other != nil && [[fresh timeZone] secondsFromGMT] == -18000,
		      [NSString stringWithFormat:@"format=%@ offset=%ld",
			[fresh calendarFormat], (long)[[fresh timeZone] secondsFromGMT]]);
	}

	/* THE FIELDS OF A DATE BUILT FROM NUMBERS, in a known zone. 2026-09-27 is a Sunday and the 270th day. */
	when = [NSCalendarDate dateWithYear:2026 month:9 day:27 hour:13 minute:45 second:30 timeZone:zone];
	check("the-fields-are-the-numbers-it-was-built-from",
	      when != nil && [when yearOfCommonEra] == 2026 && [when monthOfYear] == 9 &&
	      [when dayOfMonth] == 27 && [when hourOfDay] == 13 && [when minuteOfHour] == 45 &&
	      [when secondOfMinute] == 30,
	      [NSString stringWithFormat:@"%04ld-%02ld-%02ld %02ld:%02ld:%02ld",
		(long)[when yearOfCommonEra], (long)[when monthOfYear], (long)[when dayOfMonth],
		(long)[when hourOfDay], (long)[when minuteOfHour], (long)[when secondOfMinute]]);
	check("the-day-numbers-are-one-based-and-the-weekday-is-sunday-zero",
	      [when dayOfYear] == 270 && [when dayOfWeek] == 0,
	      [NSString stringWithFormat:@"dayOfYear=%ld (want 270) dayOfWeek=%ld (want 0, Sunday)",
		(long)[when dayOfYear], (long)[when dayOfWeek]]);

	/* COMPUTED, NOT STORED: moving the zone moves every field, and the INSTANT does not move at all. */
	instant = [when timeIntervalSince1970];
	[when setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:-18000]];	/* UTC-5, six hours behind */
	check("the-zone-moves-every-field-and-not-the-instant",
	      [when hourOfDay] == 7 && [when dayOfMonth] == 27 &&
	      [when timeIntervalSince1970] == instant,
	      [NSString stringWithFormat:@"hour=%ld (want 7) day=%ld instantDrift=%.0f",
		(long)[when hourOfDay], (long)[when dayOfMonth],
		[when timeIntervalSince1970] - instant]);
	[when setTimeZone:zone];

	/* THE INSTANT IN THE SYSTEM ZONE IS STILL THE INSTANT THAT WAS BUILT: whatever the zone is. */
	{
		NSTimeZone *system = [NSTimeZone systemTimeZone];
		NSCalendarDate *inSystem = [NSCalendarDate dateWithYear:2026 month:9 day:27 hour:13 minute:45
								second:30 timeZone:system];

		check("a-date-built-in-the-system-zone-holds-the-instant-it-prints",
		      inSystem != nil && [[inSystem descriptionWithCalendarFormat:@"%Y-%m-%d %H:%M:%S"]
			isEqualToString:@"2026-09-27 13:45:30"],
		      [NSString stringWithFormat:@"printed=%@",
			[inSystem descriptionWithCalendarFormat:@"%Y-%m-%d %H:%M:%S"]]);
	}

	/* THE TWO STRINGS, and the round trip through text. A ZONE-LESS STRING ROUND-TRIPS ITS FIELDS AND NOT ITS
	 * INSTANT, which is not a defect but the meaning of a zone-less format: the text does not say where it was
	 * written, so it is read in the READER'S zone — and the check that the instant survives belongs to the
	 * default format below, whose `%z` says what it means. */
	{
		NSString *printed = [when descriptionWithCalendarFormat:@"%Y-%m-%d %H:%M:%S"];
		NSCalendarDate *parsed = [[NSCalendarDate alloc] initWithString:printed
							       calendarFormat:@"%Y-%m-%d %H:%M:%S"];
		NSCalendarDate *bad = [[NSCalendarDate alloc] initWithString:@"not a date"
						       calendarFormat:@"%Y-%m-%d %H:%M:%S"];

		check("the-format-prints-and-parses-back",
		      [printed isEqualToString:@"2026-09-27 13:45:30"] && parsed != nil &&
		      [parsed yearOfCommonEra] == 2026 && [parsed monthOfYear] == 9 &&
		      [parsed dayOfMonth] == 27 && [parsed hourOfDay] == 13 && [parsed minuteOfHour] == 45 &&
		      [parsed secondOfMinute] == 30,
		      [NSString stringWithFormat:@"printed=%@ back=%04ld-%02ld-%02ld %02ld:%02ld:%02ld", printed,
			parsed != nil ? (long)[parsed yearOfCommonEra] : 0L,
			parsed != nil ? (long)[parsed monthOfYear] : 0L,
			parsed != nil ? (long)[parsed dayOfMonth] : 0L,
			parsed != nil ? (long)[parsed hourOfDay] : 0L,
			parsed != nil ? (long)[parsed minuteOfHour] : 0L,
			parsed != nil ? (long)[parsed secondOfMinute] : 0L]);
		check("a-string-that-does-not-match-the-format-answers-nil",
		      bad == nil,
		      [NSString stringWithFormat:@"bad=%@", bad]);
	}

	/* THE DEFAULT FORMAT (ours, §11.6.1 D2) prints and parses with NO format given: the text is the date. */
	{
		NSCalendarDate *parsed = [[NSCalendarDate alloc] initWithString:[when descriptionWithLocale:nil]];

		check("the-default-format-is-what-a-new-date-prints-and-parses",
		      parsed != nil && [parsed timeIntervalSince1970] == instant &&
		      [[when descriptionWithLocale:nil] rangeOfString:@"2026-09-27 13:45:30 +0100"].location !=
			NSNotFound,
		      [NSString stringWithFormat:@"printed=%@ back=%@", [when descriptionWithLocale:nil],
			parsed != nil ? [parsed descriptionWithLocale:nil] : @"nil"]);
	}

	/* CALENDAR-AWARE: 31 January plus one month is the LAST DAY OF FEBRUARY, not 2 March. */
	{
		NSCalendarDate *january = [NSCalendarDate dateWithYear:2026 month:1 day:31 hour:12 minute:0
								second:0 timeZone:zone];
		NSCalendarDate *february = [january dateByAddingYears:0 months:1 days:0 hours:0 minutes:0
							       seconds:0];

		check("adding-one-month-to-the-31st-lands-in-february",
		      [february monthOfYear] == 2 && [february dayOfMonth] == 28 &&
		      [february yearOfCommonEra] == 2026,
		      [NSString stringWithFormat:@"-> %04ld-%02ld-%02ld (want 2026-02-28)",
			(long)[february yearOfCommonEra], (long)[february monthOfYear],
			(long)[february dayOfMonth]]);
	}

	/* THE DECOMPOSITION, largest component first, and it INVERTS the addition: 2026-01-10 09:00 plus
	 * "2 months 5 days 1 hour" is 2026-03-15 10:00, so that is what "since" must answer. */
	{
		NSCalendarDate *from = [NSCalendarDate dateWithYear:2026 month:1 day:10 hour:9 minute:0 second:0
							      timeZone:zone];
		NSCalendarDate *to = [NSCalendarDate dateWithYear:2026 month:3 day:15 hour:10 minute:0 second:0
							    timeZone:zone];
		NSInteger years = -1, months = -1, days = -1, hours = -1, minutes = -1, seconds = -1;

		[to years:&years months:&months days:&days hours:&hours minutes:&minutes seconds:&seconds
		 sinceDate:from];
		check("the-decomposition-is-largest-component-first",
		      years == 0 && months == 2 && days == 5 && hours == 1 && minutes == 0 && seconds == 0,
		      [NSString stringWithFormat:@"%ldy %ldm %ldd %ldh %ldmin %lds (want 0y 2m 5d 1h 0min 0s)",
			(long)years, (long)months, (long)days, (long)hours, (long)minutes, (long)seconds]);
	}

	/* AND THE DAY NUMBER SINCE THE COMMON ERA, which is the one field with an absolute answer: 1970-01-01
	 * in UTC is day 719163. */
	{
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
		NSCalendarDate *epoch = [NSCalendarDate dateWithYear:1970 month:1 day:1 hour:0 minute:0 second:0
							       timeZone:utc];

		check("the-day-of-the-common-era-counts-from-year-one",
		      epoch != nil && [epoch dayOfCommonEra] == 719163 &&
		      [epoch timeIntervalSince1970] == 0.0,
		      [NSString stringWithFormat:@"day=%ld instant=%.0f (want 719163 and 0)",
			(long)[epoch dayOfCommonEra], [epoch timeIntervalSince1970]]);
	}

	/* THE CONSTANT DATES ARE THIS CLASS, not the NSDate the superclass would build. */
	{
		id past = [NSCalendarDate distantPast];
		id future = [NSCalendarDate distantFuture];

		check("the-constant-dates-are-calendar-dates",
		      [past isKindOfClass:[NSCalendarDate class]] && [future isKindOfClass:[NSCalendarDate class]] &&
		      [past compare:(NSDate *)future] == NSOrderedAscending,
		      [NSString stringWithFormat:@"past=%@ future=%@", [past descriptionWithLocale:nil],
			[future descriptionWithLocale:nil]]);
	}

	printf("FOUNDATION-CALENDARDATE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CALENDARDATE DONE\n");
	return failc == 0 ? 0 : 1;
}
