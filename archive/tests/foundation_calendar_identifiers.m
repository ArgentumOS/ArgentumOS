/*
 * foundation_calendar_identifiers.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LEGACY CALENDAR IDENTIFIERS (§62.49). Apple deprecated `NSGregorianCalendar` and its ten siblings at 10.9 in
 * favour of the `NSCalendarIdentifier*` spelling, and §62.24's policy puts them back.
 *
 * THE PROPERTY THIS PROBE EXISTS FOR IS IDENTITY, NOT EQUALITY. A calendar identifier is a key: a program that
 * hands `NSGregorianCalendar` to `+[NSCalendar calendarWithIdentifier:]` and another that hands the modern name must
 * reach the SAME calendar, and a restored constant built as a second string with the same characters would be a
 * different object doing the same job by accident. So the first check is a POINTER COMPARISON, and the rest measure
 * that the alias carries meaning rather than merely existing: the calendar it builds answers the identifier it was
 * asked for, and two neighbouring names really are two calendars.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-CALENDAR-IDS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CALENDAR-IDS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- ELEVEN NAMES, EACH THE SAME OBJECT AS THE IDENTIFIER IT NAMES --------------------------------- */
	{
		struct { NSString *legacy; NSString *modern; const char *name; } pairs[] = {
			{ NSGregorianCalendar, NSCalendarIdentifierGregorian, "NSGregorianCalendar" },
			{ NSBuddhistCalendar, NSCalendarIdentifierBuddhist, "NSBuddhistCalendar" },
			{ NSChineseCalendar, NSCalendarIdentifierChinese, "NSChineseCalendar" },
			{ NSHebrewCalendar, NSCalendarIdentifierHebrew, "NSHebrewCalendar" },
			{ NSIndianCalendar, NSCalendarIdentifierIndian, "NSIndianCalendar" },
			{ NSIslamicCalendar, NSCalendarIdentifierIslamic, "NSIslamicCalendar" },
			{ NSIslamicCivilCalendar, NSCalendarIdentifierIslamicCivil, "NSIslamicCivilCalendar" },
			{ NSISO8601Calendar, NSCalendarIdentifierISO8601, "NSISO8601Calendar" },
			{ NSJapaneseCalendar, NSCalendarIdentifierJapanese, "NSJapaneseCalendar" },
			{ NSPersianCalendar, NSCalendarIdentifierPersian, "NSPersianCalendar" },
			{ NSRepublicOfChinaCalendar, NSCalendarIdentifierRepublicOfChina,
			  "NSRepublicOfChinaCalendar" }
		};
		int count = (int)(sizeof(pairs) / sizeof(pairs[0]));
		int i, same = 0, distinct = 0;
		NSSet *values;
		NSMutableArray *all = [NSMutableArray array];

		for (i = 0; i < count; i++) {
			if (pairs[i].legacy == pairs[i].modern) {
				same++;
			}
			[all addObject:pairs[i].legacy];
		}
		values = [NSSet setWithArray:all];
		distinct = (int)[values count];

		check("each-legacy-name-is-the-same-object-as-its-modern-identifier",
		      same == count && same == 11,
		      [NSString stringWithFormat:@"%d of %d legacy names are the IDENTICAL object as the modern "
			@"identifier", same, count]);
		check("the-eleven-names-are-eleven-calendars",
		      distinct == 11,
		      [NSString stringWithFormat:@"%d distinct identifier(s) among the eleven names", distinct]);
	}

	/* --- AND A LEGACY NAME DOES THE JOB THE MODERN NAME DOES -------------------------------------------- */
	{
		/* THE ROUND TRIP THROUGH `-calendarIdentifier` WOULD BE THE OBVIOUS CHECK AND THIS LIBRARY DOES NOT
		 * DECLARE THAT DOOR, so the property is measured through BEHAVIOUR instead: a calendar built from the
		 * legacy name and one built from the modern name answer the SAME date the same way. That is the thing a
		 * caller actually wanted, and it needs no accessor this library has not got. */
		NSDateComponents *when = [[NSDateComponents alloc] init];
		NSCalendar *fromLegacy = [[NSCalendar alloc] initWithCalendarIdentifier:NSGregorianCalendar];
		NSCalendar *fromModern = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
		NSDateComponents *gotLegacy;
		NSDateComponents *gotModern;
		NSDate *date;

		[when setYear:2026];
		[when setMonth:1];
		[when setDay:15];
		date = [fromModern dateFromComponents:when];
		gotLegacy = [fromLegacy components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay)
					  fromDate:date];
		gotModern = [fromModern components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay)
					  fromDate:date];

		check("a-legacy-name-does-the-job-the-modern-name-does",
		      fromLegacy != nil && date != nil &&
		      [gotLegacy year] == [gotModern year] && [gotLegacy month] == [gotModern month] &&
		      [gotLegacy day] == [gotModern day] &&
		      [gotLegacy year] == 2026 && [gotLegacy month] == 1 && [gotLegacy day] == 15,
		      [NSString stringWithFormat:@"through the legacy name: %d-%d-%d; through the modern name: %d-%d-%d",
			(int)[gotLegacy year], (int)[gotLegacy month], (int)[gotLegacy day],
			(int)[gotModern year], (int)[gotModern month], (int)[gotModern day]]);
	}

	/* --- THE CONTROL: TWO NAMES THAT MUST NOT BE THE SAME CALENDAR ------------------------------------- */
	{
		NSDateComponents *when = [[NSDateComponents alloc] init];
		NSCalendar *gregorian = [[NSCalendar alloc] initWithCalendarIdentifier:NSGregorianCalendar];
		NSCalendar *buddhist = [[NSCalendar alloc] initWithCalendarIdentifier:NSBuddhistCalendar];
		NSDate *date;
		NSDateComponents *inGregorian;
		NSDateComponents *inBuddhist;

		[when setYear:2026];
		[when setMonth:1];
		[when setDay:15];
		date = [gregorian dateFromComponents:when];
		inGregorian = [gregorian components:NSCalendarUnitEra | NSCalendarUnitYear fromDate:date];
		inBuddhist = [buddhist components:NSCalendarUnitEra | NSCalendarUnitYear fromDate:date];

		/* IF EVERY LEGACY NAME COLLAPSED INTO ONE CALENDAR, THE THREE CHECKS ABOVE WOULD STILL PASS: the same
		 * object is the same object. THIS ONE WOULD NOT. The Buddhist era is 2569 where the Gregorian is 2026,
		 * so an alias that lost its meaning shows up here as two equal answers. */
		check("two-neighbouring-names-are-two-calendars",
		      [inGregorian era] != [inBuddhist era] &&
		      [inGregorian year] != [inBuddhist year] &&
		      [inGregorian year] == 2026 && [inBuddhist year] > 2500,
		      [NSString stringWithFormat:@"the same date is era %d year %d in NSGregorianCalendar and era %d "
			@"year %d in NSBuddhistCalendar", (int)[inGregorian era], (int)[inGregorian year],
			(int)[inBuddhist era], (int)[inBuddhist year]]);
	}

	printf("FOUNDATION-CALENDAR-IDS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CALENDAR-IDS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CALENDAR-IDS DONE\n");
	return failc ? 1 : 0;
}
