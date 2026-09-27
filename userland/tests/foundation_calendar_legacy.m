/*
 * foundation_calendar_legacy.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE PRE-10.9 CALENDAR UNIT NAMES (§62.46). Apple deprecated sixteen unit names, the wrap option and the undefined
 * sentinel at 10.9, and §62.24's policy put them back so that a program written before then compiles.
 *
 * THE PROPERTY THIS PROBE EXISTS FOR IS NOT THAT THE NAMES EXIST BUT THAT THEY MEAN THE SAME THING. A restored name
 * given a bit of its own would compile and compute a different date — the worst kind of success. So every check here
 * compares the LEGACY SPELLING against the modern one through the calendar itself: the same fields come out, and the
 * comparison is made on the values a `-components:fromDate:` answers rather than on the constants, because the
 * constants agreeing is exactly what a wrong implementation would also make true of its own twin.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-CALENDAR-LEGACY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CALENDAR-LEGACY %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	NSCalendar *calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
	NSDateComponents *when = [[NSDateComponents alloc] init];
	NSDate *date;

	[when setYear:1969];
	[when setMonth:7];
	[when setDay:20];
	[when setHour:20];
	[when setMinute:17];
	[when setSecond:40];
	date = [calendar dateFromComponents:when];

	/* --- THE SIXTEEN NAMES MEAN THE MODERN UNITS ----------------------------------------------------- */
	{
		struct { NSCalendarUnit legacy; NSCalendarUnit modern; const char *name; } pairs[] = {
			{ NSEraCalendarUnit, NSCalendarUnitEra, "NSEraCalendarUnit" },
			{ NSYearCalendarUnit, NSCalendarUnitYear, "NSYearCalendarUnit" },
			{ NSMonthCalendarUnit, NSCalendarUnitMonth, "NSMonthCalendarUnit" },
			{ NSDayCalendarUnit, NSCalendarUnitDay, "NSDayCalendarUnit" },
			{ NSHourCalendarUnit, NSCalendarUnitHour, "NSHourCalendarUnit" },
			{ NSMinuteCalendarUnit, NSCalendarUnitMinute, "NSMinuteCalendarUnit" },
			{ NSSecondCalendarUnit, NSCalendarUnitSecond, "NSSecondCalendarUnit" },
			{ NSWeekCalendarUnit, NSCalendarUnitWeekOfYear, "NSWeekCalendarUnit" },
			{ NSWeekdayCalendarUnit, NSCalendarUnitWeekday, "NSWeekdayCalendarUnit" },
			{ NSWeekdayOrdinalCalendarUnit, NSCalendarUnitWeekdayOrdinal, "NSWeekdayOrdinalCalendarUnit" },
			{ NSQuarterCalendarUnit, NSCalendarUnitQuarter, "NSQuarterCalendarUnit" },
			{ NSWeekOfMonthCalendarUnit, NSCalendarUnitWeekOfMonth, "NSWeekOfMonthCalendarUnit" },
			{ NSWeekOfYearCalendarUnit, NSCalendarUnitWeekOfYear, "NSWeekOfYearCalendarUnit" },
			{ NSYearForWeekOfYearCalendarUnit, NSCalendarUnitYearForWeekOfYear, "NSYearForWeekOfYearCalendarUnit" },
			{ NSTimeZoneCalendarUnit, NSCalendarUnitTimeZone, "NSTimeZoneCalendarUnit" },
			{ NSCalendarCalendarUnit, NSCalendarUnitCalendar, "NSCalendarCalendarUnit" }
		};
		int count = (int)(sizeof(pairs) / sizeof(pairs[0]));
		int i, same = 0;
		NSString *firstWrong = nil;

		for (i = 0; i < count; i++) {
			if (pairs[i].legacy == pairs[i].modern) {
				same++;
			} else if (firstWrong == nil) {
				firstWrong = [NSString stringWithUTF8String:pairs[i].name];
			}
		}
		check("the-sixteen-legacy-names-are-the-modern-units",
		      same == count,
		      firstWrong == nil
			? [NSString stringWithFormat:@"all %d legacy names carry the value of the unit they name", count]
			: [NSString stringWithFormat:@"%d of %d agree; the first that does not is %@", same, count,
				firstWrong]);
	}

	/* --- AND A LEGACY SPELLING ASKS FOR THE SAME FIELDS, WHICH IS THE PROPERTY THAT MATTERS ------------- */
	{
		NSDateComponents *modern = [calendar components:(NSCalendarUnitEra | NSCalendarUnitYear |
								 NSCalendarUnitMonth | NSCalendarUnitDay |
								 NSCalendarUnitWeekday | NSCalendarUnitQuarter)
						       fromDate:date];
		NSDateComponents *legacy = [calendar components:(NSEraCalendarUnit | NSYearCalendarUnit |
								 NSMonthCalendarUnit | NSDayCalendarUnit |
								 NSWeekdayCalendarUnit | NSQuarterCalendarUnit)
						       fromDate:date];

		check("a-legacy-spelling-asks-for-the-same-fields",
		      [legacy era] == [modern era] && [legacy year] == [modern year] &&
		      [legacy month] == [modern month] && [legacy day] == [modern day] &&
		      [legacy weekday] == [modern weekday] && [legacy quarter] == [modern quarter] &&
		      [legacy year] == 1969 && [legacy month] == 7 && [legacy day] == 20,
		      [NSString stringWithFormat:@"legacy {era %d, year %d, month %d, day %d} vs modern {era %d, year %d, "
			@"month %d, day %d}", (int)[legacy era], (int)[legacy year], (int)[legacy month], (int)[legacy day],
			(int)[modern era], (int)[modern year], (int)[modern month], (int)[modern day]]);

		/* THE WEEK IS THE ONE PAIRING THAT COULD HAVE BEEN MADE TWO WAYS, so it is measured rather than asserted:
		 * the legacy name means the WEEK OF THE YEAR, and a date asked for both answers the same number. */
		NSDateComponents *legacyWeek = [calendar components:NSWeekCalendarUnit fromDate:date];
		NSDateComponents *modernWeek = [calendar components:NSCalendarUnitWeekOfYear fromDate:date];

		check("the-legacy-week-name-is-the-week-of-the-year",
		      [legacyWeek weekOfYear] != NSDateComponentUndefined &&
		      [legacyWeek weekOfYear] == [modernWeek weekOfYear],
		      [NSString stringWithFormat:@"asked for through NSWeekCalendarUnit the calendar answers weekOfYear %d, "
			@"and through NSCalendarUnitWeekOfYear %d", (int)[legacyWeek weekOfYear],
			(int)[modernWeek weekOfYear]]);
	}

	/* --- A NEGATIVE CONTROL: THE INSTRUMENT CAN TELL THE UNITS APART ---------------------------------- */
	{
		NSDateComponents *onlyMonth = [calendar components:NSMonthCalendarUnit fromDate:date];

		/* THIS CHECK EXISTS SO THE OTHERS ARE NOT VACUOUS. If a legacy unit were answered by the calendar no
		 * matter which bit was asked for, every comparison above would pass and mean nothing. Asking for the
		 * month ALONE must leave the year undefined. */
		check("a-unit-asked-for-alone-answers-only-itself",
		      [onlyMonth month] == 7 && [onlyMonth year] == NSDateComponentUndefined &&
		      [onlyMonth day] == NSDateComponentUndefined,
		      [NSString stringWithFormat:@"month %d present, year %d and day %d undefined as they must be",
			(int)[onlyMonth month], (int)[onlyMonth year], (int)[onlyMonth day]]);
	}

	/* --- THE WRAP OPTION AND THE UNDEFINED SENTINEL --------------------------------------------------- */
	{
		NSDateComponents *empty = [[NSDateComponents alloc] init];

		check("the-legacy-sentinel-is-the-modern-one",
		      NSUndefinedDateComponent == NSDateComponentUndefined &&
		      [empty era] == NSUndefinedDateComponent && [empty year] == NSDateComponentUndefined,
		      [NSString stringWithFormat:@"NSUndefinedDateComponent == NSDateComponentUndefined == %ld, and an "
			@"empty component answers it", (long)NSUndefinedDateComponent]);

		check("the-legacy-wrap-option-is-the-modern-one",
		      NSWrapCalendarComponents == NSCalendarWrapComponents &&
		      NSCalendarWrapComponents == NSCalendarOptionsWrapComponents,
		      @"one option, two spellings, and the modern alias was already declared");
	}

	/* NO RELEASES: a probe compiles under ARC (the library does not), so ownership here is the compiler's. */

	printf("FOUNDATION-CALENDAR-LEGACY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CALENDAR-LEGACY-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CALENDAR-LEGACY DONE\n");
	return failc ? 1 : 0;
}
