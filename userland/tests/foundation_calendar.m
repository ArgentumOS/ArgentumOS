/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_calendar, unit 2 of 2 — the checks (ARC).
 * docs/design/foundation-plan.md, F7.
 *
 *   tz-offset             a fixed offset, its name, and that it has no DST
 *   calendar-convert      absolute time -> fields, against known dates
 *   calendar-roundtrip    fields -> absolute time -> the same fields
 *   calendar-add-months   THE CLAMP: 31 January + 1 month is the last day of
 *                         February, 28 or 29 depending on the year
 *   calendar-add-units    day/year arithmetic, including across a month end
 *   calendar-ranges       days in a month, months in a year, hours in a day
 *   calendar-weeks        the WEEK RULE, and the year that owns week 1
 *   calendar-timezone     the same instant in two zones is two local dates
 *   calendar-refusals     what is ABSENT is absent: no tz database, no parser
 *   cross-tu              objects built in the other unit behave locally
 */

#import "foundation_calendar.h"
#include <stdio.h>
#include <string.h>
#import <objc/runtime.h>
#import <Foundation/NSDateFormatter.h>	/* F13.6: the family this probe used to call refused */

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CALENDAR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CALENDAR %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT, not a description of it: a failure that
 * says "y=2026 m=3 d=31" is diagnosable, one that says "the clamp is wrong" is
 * not. (The Foundation plan's §9 records this lesson from an earlier probe; it
 * was earned again here — calendar-ranges' first failure said nothing usable.) */
static const char *fn_why(NSDateComponents *c)
{
	static char why[96];

	snprintf(why, sizeof why, "y=%ld m=%ld d=%ld %02ld:%02ld:%02ld",
		 (long)[c year], (long)[c month], (long)[c day],
		 (long)[c hour], (long)[c minute], (long)[c second]);
	return why;
}

static const char *fn_why2(NSDateComponents *a, NSDateComponents *b)
{
	static char why[160];

	snprintf(why, sizeof why, "first: y=%ld m=%ld d=%ld | second: y=%ld m=%ld d=%ld",
		 (long)[a year], (long)[a month], (long)[a day],
		 (long)[b year], (long)[b month], (long)[b day]);
	return why;
}

int main(void)
{
	NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
	NSCalendarUnit ymd = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay;
	NSCalendarUnit hms = NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond;
	NSCalendarUnit all = ymd | hms;

	{
		NSTimeZone *ist = [NSTimeZone timeZoneForSecondsFromGMT:19800];
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
		NSTimeZone *same = [NSTimeZone timeZoneForSecondsFromGMT:19800];
		NSTimeZone *west = [NSTimeZone timeZoneForSecondsFromGMT:-18000];
		NSString *name = [ist name];
		NSString *westName = [west name];

		check("tz-offset",
		      [ist secondsFromGMT] == 19800 &&
		      [utc secondsFromGMT] == 0 &&
		      [west secondsFromGMT] == -18000 &&
		      [name isEqualToString:@"GMT+0530"] &&
		      [westName isEqualToString:@"GMT-0500"] &&
		      [[NSTimeZone systemTimeZone] secondsFromGMT] == 0 &&
		      ![[NSTimeZone systemTimeZone] isDaylightSavingTime] &&
		      [ist isEqual:same] && ![ist isEqual:west] &&
		      [ist hash] == [same hash],
		      "an offset, a rendered name, no DST, and equality by offset");
	}

	{
		/* 1970-01-01 00:00:00 UTC was a THURSDAY, and with firstWeekday = Sunday
		 * that is day 5. */
		NSDate *epoch = [NSDate dateWithTimeIntervalSince1970:0];
		NSDateComponents *c = [cal components:all | NSCalendarUnitWeekday
					    | NSCalendarUnitEra | NSCalendarUnitQuarter
					      fromDate:epoch];

		check("calendar-convert",
		      [c era] == 1 && [c year] == 1970 && [c month] == 1 && [c day] == 1 &&
		      [c hour] == 0 && [c minute] == 0 && [c second] == 0 &&
		      [c weekday] == 5 && [c quarter] == 1,
		      fn_why(c));
	}

	{
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDate *made;
		NSDateComponents *back;

		[c setYear:2026];
		[c setMonth:9];
		[c setDay:17];
		[c setHour:13];
		[c setMinute:45];
		[c setSecond:30];
		made = [cal dateFromComponents:c];
		back = [cal components:all fromDate:made];

		check("calendar-roundtrip",
		      [back year] == 2026 && [back month] == 9 && [back day] == 17 &&
		      [back hour] == 13 && [back minute] == 45 && [back second] == 30 &&
		      [c isEqual:back] &&
		      [[c description] length] > 0 &&
		      [c isValidDateInCalendar:cal],
		      "components -> date -> components is the same fields");
	}

	{
		NSDateComponents *one = [[NSDateComponents alloc] init];
		NSDateComponents *jan31 = [[NSDateComponents alloc] init];
		NSDate *base, *plusOne, *leapBase, *leapPlus;
		NSDateComponents *got;

		[one setMonth:1];
		[jan31 setYear:2026];
		[jan31 setMonth:1];
		[jan31 setDay:31];
		base = [cal dateFromComponents:jan31];
		plusOne = [cal dateByAddingComponents:one toDate:base options:NSCalendarOptionsNone];
		got = [cal components:ymd fromDate:plusOne];

		/* A leap year clamps to the 29th instead. */
		[jan31 setYear:2024];
		leapBase = [cal dateFromComponents:jan31];
		leapPlus = [cal dateByAddingComponents:one toDate:leapBase
						options:NSCalendarOptionsNone];

		check("calendar-add-months",
		      [got year] == 2026 && [got month] == 2 && [got day] == 28 &&
		      [[cal components:ymd fromDate:leapPlus] day] == 29,
		      fn_why2(got, [cal components:ymd fromDate:leapPlus]));
	}

	{
		NSDateComponents *aug31 = [[NSDateComponents alloc] init];
		NSDate *d, *plusDay, *plusYear;
		NSDateComponents *leapDay = [[NSDateComponents alloc] init];

		[aug31 setYear:2026];
		[aug31 setMonth:8];
		[aug31 setDay:31];
		d = [cal dateFromComponents:aug31];
		plusDay = [cal dateByAddingUnit:NSCalendarUnitDay value:1 toDate:d
					options:NSCalendarOptionsNone];
		plusYear = [cal dateByAddingUnit:NSCalendarUnitYear value:1 toDate:d
					 options:NSCalendarOptionsNone];

		[leapDay setYear:2024];
		[leapDay setMonth:2];
		[leapDay setDay:29];

		/* The nullable constructors are BOUND and GUARDED inside the && chain, so a
		 * nil FAILS the check rather than being passed on as if it were a date —
		 * which is exactly what the conversion flag asks for. */
		NSDate *leapDate = [cal dateFromComponents:leapDay];
		NSDate *leapPlusYear = leapDate == nil ? nil
			: [cal dateByAddingUnit:NSCalendarUnitYear value:1 toDate:leapDate
					options:NSCalendarOptionsNone];
		NSDateComponents *leapBack = leapPlusYear == nil ? nil
			: [cal components:ymd fromDate:leapPlusYear];

		check("calendar-add-units",
		      [[cal components:ymd fromDate:plusDay] month] == 9 &&
		      [[cal components:ymd fromDate:plusDay] day] == 1 &&
		      [[cal components:ymd fromDate:plusYear] year] == 2027 &&
		      leapBack != nil && [leapBack day] == 28,
		      fn_why2([cal components:ymd fromDate:plusDay], leapBack));
	}

	{
		NSDateComponents *feb26 = [[NSDateComponents alloc] init];
		NSDateComponents *feb24 = [[NSDateComponents alloc] init];
		NSDate *d26, *d24, *start = nil;
		double interval = 0;
		BOOL hasStart;

		[feb26 setYear:2026];
		[feb26 setMonth:2];
		[feb26 setDay:10];
		[feb24 setYear:2024];
		[feb24 setMonth:2];
		[feb24 setDay:10];
		d26 = [cal dateFromComponents:feb26];
		d24 = [cal dateFromComponents:feb24];
		/* The MONTH's start, not the day's: asking for a DAY gives that day's own
		 * midnight, so an "is it the 1st" assertion belongs to the month. */
		hasStart = [cal rangeOfUnit:NSCalendarUnitMonth
				  startDate:&start
				   interval:&interval
				    forDate:d26];

		check("calendar-ranges",
		      [cal rangeOfUnit:NSCalendarUnitDay inUnit:NSCalendarUnitMonth
			       forDate:d26].length == 28 &&
		      [cal rangeOfUnit:NSCalendarUnitDay inUnit:NSCalendarUnitMonth
			       forDate:d24].length == 29 &&
		      [cal rangeOfUnit:NSCalendarUnitMonth inUnit:NSCalendarUnitYear
			       forDate:d26].length == 12 &&
		      [cal rangeOfUnit:NSCalendarUnitHour inUnit:NSCalendarUnitDay
			       forDate:d26].length == 24 &&
		      hasStart && start != nil && interval == 28 * 86400.0 &&
		      [[cal components:ymd fromDate:start] day] == 1,
		      fn_why2([cal components:ymd fromDate:start],
			      [cal components:ymd fromDate:d26]));
	}

	{
		/* ISO's week settings: weeks start on Monday and week 1 is the first
		 * week with at least four days of the new year. By that rule
		 * 2026-01-01 (a Thursday) is in week 1 of 2026, and 2025-12-29 is the
		 * Monday that week begins on — the same week, and the rollover case. */
		NSDateComponents *jan1 = [[NSDateComponents alloc] init];
		NSDateComponents *dec29 = [[NSDateComponents alloc] init];
		NSDate *a, *b;
		NSDateComponents *ca, *cb;

		[cal setFirstWeekday:2];
		[cal setMinimumDaysInFirstWeek:4];
		[jan1 setYear:2026];
		[jan1 setMonth:1];
		[jan1 setDay:1];
		[dec29 setYear:2025];
		[dec29 setMonth:12];
		[dec29 setDay:29];
		a = [cal dateFromComponents:jan1];
		b = [cal dateFromComponents:dec29];
		ca = [cal components:NSCalendarUnitWeekOfYear | NSCalendarUnitYearForWeekOfYear
			    | NSCalendarUnitWeekOfMonth | NSCalendarUnitWeekday
			      fromDate:a];
		cb = [cal components:NSCalendarUnitWeekOfYear | NSCalendarUnitYearForWeekOfYear
			      fromDate:b];

		check("calendar-weeks",
		      [ca weekOfYear] == 1 && [ca yearForWeekOfYear] == 2026 &&
		      /* THURSDAY IS 5, and this expectation CHANGED when the calendars moved to ICU
		       * (F13.7b). The libc path ROTATED weekday by firstWeekday — with a Monday-start
		       * week it called Thursday 4 — and Cocoa numbers weekday ABSOLUTELY, 1 = Sunday,
		       * which is what ICU's UCAL_DAY_OF_WEEK is. The old number was the bug. */
		      [ca weekday] == 5 && [ca weekOfMonth] == 1 &&
		      [cb weekOfYear] == 1 && [cb yearForWeekOfYear] == 2026 &&
		      [cal firstWeekday] == 2 && [cal minimumDaysInFirstWeek] == 4,
		      [[NSString stringWithFormat:
				@"jan1 woy=%ld woyYear=%ld weekday=%ld wom=%ld | dec29 woy=%ld woyYear=%ld",
				(long)[ca weekOfYear], (long)[ca yearForWeekOfYear],
				(long)[ca weekday], (long)[ca weekOfMonth],
				(long)[cb weekOfYear], (long)[cb yearForWeekOfYear]] UTF8String]);
		[cal setFirstWeekday:1];
		[cal setMinimumDaysInFirstWeek:1];
	}

	{
		/* F13.7e: THE FIELD-WISE DIFFERENCE — the method F7 refused BY NAME, because its option
		 * semantics are a table of cases. ICU has the table, and its answer is a WALK rather than
		 * a division: from 31 January to 1 March the month takes the walk to the clamped 28
		 * February, and the day is what is left. So the pair is {months 1, days 1}, not "1.03
		 * months" — and not {0, 29} either, which is what a subtraction would give. */
		NSDateComponents *start = [[NSDateComponents alloc] init];
		NSDateComponents *end = [[NSDateComponents alloc] init];
		NSDate *from;
		NSDate *to;
		NSDateComponents *difference = nil;

		[start setYear:2026];
		[start setMonth:1];
		[start setDay:31];
		[end setYear:2026];
		[end setMonth:3];
		[end setDay:1];
		from = [cal dateFromComponents:start];
		to = [cal dateFromComponents:end];
		if (from != nil && to != nil) {
			difference = [cal components:NSCalendarUnitMonth | NSCalendarUnitDay
					    fromDate:from
					      toDate:to
					     options:NSCalendarOptionsNone];
		}
		check("calendar-difference",
		      difference != nil && [difference month] == 1 && [difference day] == 1,
		      [[NSString stringWithFormat:@"months=%ld days=%ld",
				(long)(difference != nil ? [difference month] : -1),
				(long)(difference != nil ? [difference day] : -1)] UTF8String]);
	}

	{
		NSTimeZone *ist = [NSTimeZone timeZoneForSecondsFromGMT:19800];
		NSDate *epoch = [NSDate dateWithTimeIntervalSince1970:0];
		NSDateComponents *c;

		[cal setTimeZone:ist];
		c = [cal components:all fromDate:epoch];

		check("calendar-timezone",
		      [c hour] == 5 && [c minute] == 30 && [c day] == 1 &&
		      [[cal timeZone] isEqualToTimeZone:ist] &&
		      [[cal description] length] > 0 &&
		      [[cal copy] isEqualToCalendar:cal],
		      "the same instant is 05:30 in +05:30, and the zone is the calendar's");
		{
			/* Bound and guarded: -setTimeZone: RAISES on nil (a calendar always
			 * has a zone), so an inline nullable would be a crash, not a warning. */
			NSTimeZone *utcZone = [NSTimeZone timeZoneForSecondsFromGMT:0];

			if (utcZone != nil) {
				[cal setTimeZone:utcZone];
			}
		}
	}

	{
		/* THE REFUSALS, asserted rather than described — and note what is NO LONGER here. This
		 * block used to be described as "no tz database, no parser, no formatter"; the
		 * parser/formatter family came back in F13.6 (it had been refused for the DATA it
		 * needed, and ICU is now a dependency), so the check after this one asserts it
		 * PRESENT. What this block still tests is what is still refused: the zone database by
		 * NAME and the non-Gregorian calendars, which are F13.7's own slice. (The
		 * `-dateFromString:` assertion is on NSCalendar, which has no parser in Cocoa either
		 * — it is a fact about NSCalendar, not about the formatter, and it stays.) */
		SEL abbrevSel = sel_registerName("abbreviationDictionary");
		SEL dateFromStringSel = sel_registerName("dateFromString:");
		NSCalendar *gregorian = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];

		check("calendar-refusals",
		      ![NSTimeZone respondsToSelector:abbrevSel] &&
		      ![cal respondsToSelector:dateFromStringSel] &&
		      gregorian != nil,
		      "the CURATED abbreviation map is still refused, NSCalendar still has no parser, "
		      "and the Gregorian calendar constructs");
	}

	{
		/* F13.7b: THE NON-GREGORIAN CALENDARS ARE HERE. The identifiers used to answer nil; now
		 * they are ICU's, and each answers its OWN year for the same instant — which no constant
		 * in this file could produce. 2023-11-14 (1700000000) falls in Hebrew 5784, Islamic 1445
		 * and Buddhist 2566. */
		NSCalendar *hebrew = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierHebrew];
		NSCalendar *islamic = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierIslamic];
		NSCalendar *buddhist = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierBuddhist];
		NSCalendar *iso = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierISO8601];
		NSDate *when = [NSDate dateWithTimeIntervalSince1970:1700000000.0];
		NSInteger hebrewYear = hebrew != nil
			? [[hebrew components:NSCalendarUnitYear fromDate:when] year] : 0;
		NSInteger islamicYear = islamic != nil
			? [[islamic components:NSCalendarUnitYear fromDate:when] year] : 0;
		NSInteger buddhistYear = buddhist != nil
			? [[buddhist components:NSCalendarUnitYear fromDate:when] year] : 0;

		check("calendar-non-gregorian",
		      hebrew != nil && islamic != nil && buddhist != nil && iso != nil &&
		      hebrewYear == 5784 && islamicYear == 1445 && buddhistYear == 2566,
		      [[NSString stringWithFormat:@"hebrew=%ld islamic=%ld buddhist=%ld",
				(long)hebrewYear, (long)islamicYear, (long)buddhistYear] UTF8String]);
	}

	{
		/* F13.7a: THE ZONE DATABASE IS HERE, asserted POSITIVELY — the same pair rule the
		 * formatter family got in F13.6. `+timeZoneWithName:` and `+knownTimeZoneNames` are no
		 * longer refused (F13.7a read them out of ICU), and the DST PAIR below is the
		 * measurement that could not exist before: ONE zone, TWO offsets. */
		NSTimeZone *newYork = [NSTimeZone timeZoneWithName:@"America/New_York"];
		NSArray *known = [NSTimeZone knownTimeZoneNames];
		NSDate *winter = [NSDate dateWithTimeIntervalSince1970:1614816000.0];	/* 2021-03-04 */
		NSDate *summer = [NSDate dateWithTimeIntervalSince1970:1625356800.0];	/* 2021-07-04 */
		NSInteger winterOffset = newYork != nil ? [newYork secondsFromGMTForDate:winter] : 0;
		NSInteger summerOffset = newYork != nil ? [newYork secondsFromGMTForDate:summer] : 0;

		check("tz-names-present",
		      newYork != nil && [[newYork name] isEqualToString:@"America/New_York"] &&
		      known != nil && [known count] > 100 &&
		      winterOffset == -(5 * 3600) && summerOffset == -(4 * 3600) &&
		      [newYork isDaylightSavingTimeForDate:summer] &&
		      ![newYork isDaylightSavingTimeForDate:winter],
		      [[NSString stringWithFormat:@"est=%ld edt=%ld known=%lu dstSummer=%d",
					(long)winterOffset, (long)summerOffset,
					(unsigned long)(known != nil ? [known count] : 0),
					(int)(newYork != nil &&
					      [newYork isDaylightSavingTimeForDate:summer])] UTF8String]);
	}

	{
		/* THE FAMILY THAT WAS REFUSED FOR DATA IS HERE (F13.6), asserted POSITIVELY so this
		 * probe reads as a pair: what is still refused IS refused, and what came back is
		 * back. The year is formatted through the locale's data, not through a constant. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *text;

		[formatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
		[formatter setDateFormat:@"yyyy"];
		text = [formatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:1614816000.0]];
		check("calendar-formatter-present",
		      text != nil && [text isEqualToString:@"2021"],
		      text != nil ? [text UTF8String] : "(nil)");
	}

	{
		/* Objects from the OTHER unit are ordinary objects: they compare and
		 * convert exactly as locally built ones do. */
		NSDate *theirs = foundation_calendar_date();
		NSCalendar *theirCal = foundation_calendar_calendar();
		NSTimeZone *theirZone = foundation_calendar_zone();
		NSDateComponents *c = [theirCal components:all fromDate:theirs];
		/* A LOCALLY-BUILT TWIN, so the comparison is about the UNIT and not about a zone — and
		 * the difference from `cal` below is the other half of the claim. */
		NSCalendar *twin = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];

		check("cross-tu",
		      [theirCal isEqualToCalendar:twin] &&
		      /* AND THE ZONE IS PART OF A CALENDAR'S IDENTITY: `cal` was set to the FIXED-offset
		       * UTC zone (whose name is "GMT"), the twin sits in the NAMED zone the system
		       * reports, and Cocoa compares zones by NAME — so these are two different
		       * calendars even though the offsets agree. That was already Apple's rule; F13.7a
		       * is what made it true here (before the database, both names were "GMT"). */
		      ![theirCal isEqualToCalendar:cal] &&
		      [theirZone secondsFromGMT] == 19800 &&
		      [c year] == 2023 && [c month] == 11 && [c day] == 14 &&
		      [c hour] == 22 && [c minute] == 13 && [c second] == 20,
		      /* THE DETAIL CARRIES THE MEASUREMENT, one number per clause: an assertion this
		       * long is otherwise a single bit, and a bit is what cost a round trip here. The
		       * ZONE NAMES are in it because the equality above turns on them. */
		      [[NSString stringWithFormat:
				@"twinEqual=%d calDiffers=%d twinZone=%@ calZone=%@ zoneOffset=%ld "
				 "fields=%ld-%02ld-%02ld %02ld:%02ld:%02ld",
				(int)[theirCal isEqualToCalendar:twin],
				(int)![theirCal isEqualToCalendar:cal],
				[[twin timeZone] name], [[cal timeZone] name],
				(long)[theirZone secondsFromGMT],
				(long)[c year], (long)[c month], (long)[c day],
				(long)[c hour], (long)[c minute], (long)[c second]] UTF8String]);
	}

	{
		/* THREE PROPERTIES, EACH A PLACE THE VOCABULARY COULD LIE: Apple's option spelling must be the SAME BIT
		 * as the house name it aliases (or a caller using Apple's name would ask for nothing), the newly
		 * declared unit must be Apple's own bit (1 << 8, the one the existing list skipped), and an
		 * identifier must be the lowercase WIRE STRING the initialiser accepts. */
		check("calendar-alias-and-wire-values",
		      NSCalendarWrapComponents == NSCalendarOptionsWrapComponents &&
		      NSCalendarSearchBackwards == NSCalendarOptionsSearchBackwards &&
		      NSCalendarUnitDayOfYear == (1UL << 8) &&
		      [NSCalendarIdentifierBangla isEqualToString:@"bangla"] &&
		      [NSCalendarDayChangedNotification isEqualToString:@"NSCalendarDayChangedNotification"],
		      "the aliases equal their house names, DayOfYear is Apple's bit, and an identifier is its wire string");
	}

	{
		/* F13.7c: EXTRACTION AND CONSTRUCTION. A fresh calendar in a FIXED zone and an explicit
		 * locale, so every number is a fact about the calendar and not about the host's TZ or LANG. */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSDate *epoch = [NSDate dateWithTimeIntervalSince1970:0];
		NSDate *when = [NSDate dateWithTimeIntervalSince1970:1700000000.0];	/* 2023-11-14T22:13:20Z */
		NSInteger era = 0, year = 0, month = 0, day = 0;
		NSInteger hour = 0, minute = 0, second = 0, nano = 0;
		NSTimeZone *ist = [NSTimeZone timeZoneForSecondsFromGMT:19800];
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSDateComponents *inZone;

		[c2 setTimeZone:utc];
		[c2 setLocale:en];
		[c2 getEra:&era year:&year month:&month day:&day fromDate:epoch];
		[c2 getHour:&hour minute:&minute second:&second nanosecond:&nano fromDate:when];
		inZone = [c2 componentsInTimeZone:ist fromDate:epoch];

		check("calendar-extraction",
		      [[c2 component:NSCalendarUnitDay fromDate:epoch] integerValue] == 1 &&
		      [[c2 component:NSCalendarUnitWeekday fromDate:epoch] integerValue] == 5 &&
		      [[c2 component:NSCalendarUnitYear fromDate:when] integerValue] == 2023 &&
		      [c2 component:NSCalendarUnitCalendar fromDate:epoch] == nil &&
		      era == 1 && year == 1970 && month == 1 && day == 1 &&
		      hour == 22 && minute == 13 && second == 20 && nano == 0 &&
		      [inZone hour] == 5 && [inZone minute] == 30,
		      [[NSString stringWithFormat:
				@"compDay=%ld compWd=%ld era=%ld y=%ld m=%ld d=%ld %02ld:%02ld:%02ld ns=%ld zone=%02ld:%02ld",
			(long)[[c2 component:NSCalendarUnitDay fromDate:epoch] integerValue],
			(long)[[c2 component:NSCalendarUnitWeekday fromDate:epoch] integerValue],
			(long)era, (long)year, (long)month, (long)day,
			(long)hour, (long)minute, (long)second, (long)nano,
			(long)[inZone hour], (long)[inZone minute]] UTF8String]);
	}

	{
		/* -dateWithEra:yearForWeekOfYear:weekOfYear:weekday:… — ISO week 1 of 2026 is Monday
		 * 2025-12-29, resolved by ICU's UCAL_YEAR_WOY / UCAL_WEEK_OF_YEAR / UCAL_DAY_OF_WEEK. */
		NSCalendar *iso = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierISO8601];
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
		NSDate *d;
		NSDateComponents *got;

		[iso setTimeZone:utc];
		[iso setFirstWeekday:2];		/* Monday */
		[iso setMinimumDaysInFirstWeek:4];	/* ISO's week-1 rule */
		d = [iso dateWithEra:1 yearForWeekOfYear:2026 weekOfYear:1 weekday:2
				 hour:0 minute:0 second:0 nanosecond:0];
		got = [iso components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay)
			       fromDate:d];

		check("calendar-date-with-week",
		      d != nil && [got year] == 2025 && [got month] == 12 && [got day] == 29,
		      [[NSString stringWithFormat:@"week1of2026 -> %ld-%02ld-%02ld",
			(long)[got year], (long)[got month], (long)[got day]] UTF8String]);
	}

	{
		/* RANGES, ORDINALITY, START OF DAY. The limits come from the calendar itself. */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
		NSDateComponents *feb = [[NSDateComponents alloc] init];
		NSDate *d, *sod;

		[c2 setTimeZone:utc];
		[feb setYear:2026]; [feb setMonth:2]; [feb setDay:10];
		[feb setHour:18]; [feb setMinute:45]; [feb setSecond:30];
		d = [c2 dateFromComponents:feb];
		sod = [c2 startOfDayForDate:d];
		check("calendar-range-and-ordinality",
		      [c2 maximumRangeOfUnit:NSCalendarUnitDay].location == 1 &&
		      [c2 maximumRangeOfUnit:NSCalendarUnitDay].length == 31 &&
		      [c2 minimumRangeOfUnit:NSCalendarUnitDay].location == 1 &&
		      [c2 minimumRangeOfUnit:NSCalendarUnitDay].length == 28 &&
		      [c2 maximumRangeOfUnit:NSCalendarUnitMonth].location == 1 &&
		      [c2 maximumRangeOfUnit:NSCalendarUnitMonth].length == 12 &&
		      [c2 ordinalityOfUnit:NSCalendarUnitDay inUnit:NSCalendarUnitMonth forDate:d] == 10 &&
		      [c2 ordinalityOfUnit:NSCalendarUnitMonth inUnit:NSCalendarUnitYear forDate:d] == 2 &&
		      sod != nil &&
		      [[c2 component:NSCalendarUnitHour fromDate:sod] integerValue] == 0 &&
		      [[c2 component:NSCalendarUnitMinute fromDate:sod] integerValue] == 0,
		      [[NSString stringWithFormat:@"maxDay=%lu minDayLen=%lu ordDay=%lu ordMon=%lu sod=%02ld:%02ld",
			(unsigned long)[c2 maximumRangeOfUnit:NSCalendarUnitDay].length,
			(unsigned long)[c2 minimumRangeOfUnit:NSCalendarUnitDay].length,
			(unsigned long)[c2 ordinalityOfUnit:NSCalendarUnitDay inUnit:NSCalendarUnitMonth forDate:d],
			(unsigned long)[c2 ordinalityOfUnit:NSCalendarUnitMonth inUnit:NSCalendarUnitYear forDate:d],
			(long)[[c2 component:NSCalendarUnitHour fromDate:sod] integerValue],
			(long)[[c2 component:NSCalendarUnitMinute fromDate:sod] integerValue]] UTF8String]);
	}

	{
		/* SETTING AND GRANULARITY. ucal_set keeps the day; granularity truncates then compares. */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSDateComponents *b1 = [[NSDateComponents alloc] init];
		NSDateComponents *b2 = [[NSDateComponents alloc] init];
		NSDate *base, *set, *later, *daySet;
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];

		[c2 setTimeZone:utc];
		[b1 setYear:2023]; [b1 setMonth:11]; [b1 setDay:14];
		[b1 setHour:22]; [b1 setMinute:13]; [b1 setSecond:20];
		base = [c2 dateFromComponents:b1];
		set = [c2 dateBySettingHour:9 minute:30 second:0 ofDate:base options:NSCalendarOptionsNone];
		[b2 setYear:2023]; [b2 setMonth:11]; [b2 setDay:14];
		[b2 setHour:23]; [b2 setMinute:0]; [b2 setSecond:0];
		later = [c2 dateFromComponents:b2];
		daySet = [c2 dateBySettingUnit:NSCalendarUnitDay value:20 ofDate:base options:NSCalendarOptionsNone];

		check("calendar-setting-and-granularity",
		      set != nil &&
		      [[c2 component:NSCalendarUnitHour fromDate:set] integerValue] == 9 &&
		      [[c2 component:NSCalendarUnitMinute fromDate:set] integerValue] == 30 &&
		      [[c2 component:NSCalendarUnitDay fromDate:set] integerValue] == 14 &&
		      [[c2 component:NSCalendarUnitDay fromDate:daySet] integerValue] == 20 &&
		      [[c2 component:NSCalendarUnitMonth fromDate:daySet] integerValue] == 11 &&
		      [c2 isDate:base equalToDate:later toUnitGranularity:NSCalendarUnitDay] &&
		      ![c2 isDate:base equalToDate:later toUnitGranularity:NSCalendarUnitHour] &&
		      [c2 compareDate:base toDate:later toUnitGranularity:NSCalendarUnitDay] == NSOrderedSame &&
		      [c2 compareDate:base toDate:later toUnitGranularity:NSCalendarUnitMinute] == NSOrderedAscending,
		      [[NSString stringWithFormat:@"setH=%ld setM=%ld setD=%ld unitDay=%ld",
			(long)[[c2 component:NSCalendarUnitHour fromDate:set] integerValue],
			(long)[[c2 component:NSCalendarUnitMinute fromDate:set] integerValue],
			(long)[[c2 component:NSCalendarUnitDay fromDate:set] integerValue],
			(long)[[c2 component:NSCalendarUnitDay fromDate:daySet] integerValue]] UTF8String]);
	}

	{
		/* TODAY and THE WEEKEND. 2024-01-06 is a Saturday and 2024-01-03 a Wednesday, both at 12:00Z;
		 * the calendar is pinned to GMT so "today" and the weekend boundary are host-independent. */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSDate *now = [NSDate date];
		NSDateComponents *s = [[NSDateComponents alloc] init];
		NSDate *sat, *wed, *ws = nil;
		double wi = 0;
		BOOL haveWs;
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];

		[c2 setTimeZone:utc];
		[s setYear:2024]; [s setMonth:1]; [s setDay:6]; [s setHour:12];
		sat = [c2 dateFromComponents:s];
		[s setDay:3];
		wed = [c2 dateFromComponents:s];
		haveWs = [c2 rangeOfWeekendStartDate:&ws interval:&wi containingDate:sat];

		check("calendar-today-and-weekend",
		      [c2 isDateInToday:now] &&
		      ![c2 isDateInToday:[now dateByAddingTimeInterval:-10 * 86400.0]] &&
		      [c2 isDateInWeekend:sat] && ![c2 isDateInWeekend:wed] &&
		      haveWs && ws != nil && ((long)(wi / 86400.0)) == 2 &&
		      [[c2 component:NSCalendarUnitDay fromDate:ws] integerValue] == 6 &&
		      [[c2 component:NSCalendarUnitHour fromDate:ws] integerValue] == 0,
		      [[NSString stringWithFormat:@"today=%d wkendSat=%d wsDay=%ld wi=%.0f",
			(int)[c2 isDateInToday:now], (int)[c2 isDateInWeekend:sat],
			(long)[[c2 component:NSCalendarUnitDay fromDate:ws] integerValue], wi] UTF8String]);
	}

	{
		/* SYMBOLS AND IDENTITY (F13.7c): the LOCALE's names, read from ICU against the calendar's
		 * own keyword. */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSArray *wd, *mon, *eras;
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];

		[c2 setLocale:en];
		wd = [c2 weekdaySymbols];
		mon = [c2 monthSymbols];
		eras = [c2 eraSymbols];

		check("calendar-symbols",
		      [[c2 calendarIdentifier] isEqualToString:NSCalendarIdentifierGregorian] &&
		      [c2 locale] != nil &&
		      [NSCalendar autoupdatingCurrentCalendar] != nil &&
		      [c2 AMSymbol] != nil && [[c2 AMSymbol] isEqualToString:@"AM"] &&
		      [[c2 PMSymbol] isEqualToString:@"PM"] &&
		      wd != nil && [wd count] == 7 &&
		      [[wd objectAtIndex:0] isEqualToString:@"Sunday"] &&
		      [[wd objectAtIndex:1] isEqualToString:@"Monday"] &&
		      mon != nil && [mon count] >= 12 && [[mon objectAtIndex:0] isEqualToString:@"January"] &&
		      eras != nil && [eras count] == 2 && [[eras objectAtIndex:0] isEqualToString:@"BC"] &&
		      [[c2 longEraSymbols] count] == 2 &&
		      [[c2 shortWeekdaySymbols] count] == 7 &&
		      [[c2 quarterSymbols] count] == 4 &&
		      [[c2 standaloneMonthSymbols] count] >= 12,
		      [[NSString stringWithFormat:@"wd0=%@ mon0=%@ am=%@ eras0=%@ id=%@",
			[wd count] > 0 ? (NSString *)[wd objectAtIndex:0] : @"(none)",
			[mon count] > 0 ? (NSString *)[mon objectAtIndex:0] : @"(none)",
			[c2 AMSymbol] != nil ? [c2 AMSymbol] : @"(nil)",
			[eras count] > 0 ? (NSString *)[eras objectAtIndex:0] : @"(none)",
			[c2 calendarIdentifier]] UTF8String]);
	}

	{
		/* MATCHING and THE COMPONENT DIFFERENCE (F13.7c). */
		NSCalendar *c2 = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
		NSDateComponents *d1 = [[NSDateComponents alloc] init];
		NSDateComponents *match = [[NSDateComponents alloc] init];
		NSDateComponents *nomatch = [[NSDateComponents alloc] init];
		NSDateComponents *e1 = [[NSDateComponents alloc] init];
		NSDateComponents *e2 = [[NSDateComponents alloc] init];
		NSDate *d;
		NSDateComponents *delta;
		NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];

		[c2 setTimeZone:utc];
		[d1 setYear:2026]; [d1 setMonth:9]; [d1 setDay:17];
		[match setMonth:9]; [match setDay:17];
		[nomatch setMonth:10];
		d = [c2 dateFromComponents:d1];
		[e1 setYear:2026]; [e1 setMonth:1]; [e1 setDay:31];
		[e2 setYear:2026]; [e2 setMonth:3]; [e2 setDay:1];
		delta = [c2 components:(NSCalendarUnitMonth | NSCalendarUnitDay)
		       fromDateComponents:e1 toDateComponents:e2 options:NSCalendarOptionsNone];

		check("calendar-matches-and-comp-diff",
		      [c2 date:d matchesComponents:match] &&
		      ![c2 date:d matchesComponents:nomatch] &&
		      [delta month] == 1 && [delta day] == 1,
		      [[NSString stringWithFormat:@"match=%d nomatch=%d deltaM=%ld deltaD=%ld",
			(int)[c2 date:d matchesComponents:match],
			(int)[c2 date:d matchesComponents:nomatch],
			(long)[delta month], (long)[delta day]] UTF8String]);
	}

	printf("FOUNDATION-CALENDAR RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-CALENDAR-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CALENDAR DONE\n");
	return failc ? 1 : 0;
}
