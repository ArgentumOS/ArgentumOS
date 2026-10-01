/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_dateformatter, unit of 1 — F13.6's acceptance.
 * docs/design/foundation-plan.md §10 (the un-refusal program).
 *
 * ONE UNIT, deliberately: the other Foundation probes are two-unit because their claim is a
 * cross-translation-unit BOUNDARY (data built in one unit, consumed in another). This family's
 * claim is different in kind — it is DATA, and the question is whether a locale's conventions come
 * back through Foundation's own API. The probe imports ONLY <Foundation/Foundation.h>, which also
 * proves the umbrella exports the new headers.
 *
 * WHAT IT MEASURES, and why none of it can come from this file:
 *   df-style-medium-en   a locale's MEDIUM date names the month in that locale
 *   df-style-long-locale the SAME instant in de_DE is "März" and in en_US is "March" — the month
 *                        NAMES are the data (and this also exercises a non-ASCII literal, which
 *                        clang emits as UTF-16)
 *   df-pattern-format    an explicit "dd.MM.yyyy" pattern formats the fixed instant
 *   df-round-trip        format then parse returns the same instant
 *   df-parse             parsing de_DE text with a de_DE pattern gives the exact instant
 *   df-timezone-offset   THE ZONE CROSSES AS AN OFFSET: the same instant is 00:00 at +00:00,
 *                        05:30 at +05:30 and 16:00 at -08:00
 *   df-template-order    CLDR's SKELETONS: @"yMMMd" comes back as a PATTERN, and the en order
 *                        (month first) differs from the German one (day first) — that is the
 *                        locale's ordering rule, not ours
 *   df-no-fields         a formatter with nothing to say answers the empty string, not an error
 *   df-parse-refusal     text that is not a date answers nil
 *   df-strict-parse      a strict parse refuses an impossible field, a lenient one does not
 *   df-base-raises       NSFormatter, the abstract base, REFUSES to invent a format
 *   df-copy-independent  a copy is independent: mutating it leaves the original alone
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

/* The DETAIL IS NULLABLE because every answer this probe inspects comes back from a nullable
 * door (-stringFromDate:, -dateFormat, …) and the FAIL text is exactly where a nil must be able
 * to print as "". */
static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DATEFORMATTER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DATEFORMATTER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* 2021-03-04T00:00:00Z. Every check that depends on an instant uses this one. */
#define WHEN (1614816000.0)

int main(void)
{
	NSDate *when = [NSDate dateWithTimeIntervalSince1970:WHEN];
	NSTimeZone *utc = [NSTimeZone timeZoneForSecondsFromGMT:0];
	NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
	NSLocale *de = [NSLocale localeWithLocaleIdentifier:@"de_DE"];

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setTimeZone:utc];
		[formatter setDateStyle:NSDateFormatterMediumStyle];
		text = [formatter stringFromDate:when];
		check("df-style-medium-en",
		      text != nil && [text containsString:@"Mar"] && [text containsString:@"2021"],
		      text);
	}

	{
		/* THE SAME INSTANT, TWO LOCALES: the month NAME is the data. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *deText;
		NSString *enText;

		[formatter setTimeZone:utc];
		[formatter setDateStyle:NSDateFormatterLongStyle];
		[formatter setLocale:de];
		deText = [formatter stringFromDate:when];
		[formatter setLocale:en];
		enText = [formatter stringFromDate:when];
		check("df-style-long-locale",
		      deText != nil && enText != nil &&
		      [deText containsString:@"März"] && [enText containsString:@"March"],
		      [NSString stringWithFormat:@"de=%@ en=%@", deText, enText]);
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *text;

		[formatter setLocale:de];
		[formatter setTimeZone:utc];
		[formatter setDateFormat:@"dd.MM.yyyy"];
		text = [formatter stringFromDate:when];
		check("df-pattern-format", text != nil && [text isEqualToString:@"04.03.2021"], text);
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *text;
		NSDate *back = nil;

		[formatter setLocale:de];
		[formatter setTimeZone:utc];
		[formatter setDateFormat:@"dd.MM.yyyy HH:mm:ss"];
		text = [formatter stringFromDate:when];
		/* An IF guard, not a ternary: clang narrows a nullable on a guard and not in a
		 * conditional expression (measured in F11b), and -dateFromString: takes a nonnull
		 * string. */
		if (text != nil) {
			back = [formatter dateFromString:text];
		}
		check("df-round-trip",
		      back != nil && [back timeIntervalSince1970] == WHEN,
		      [NSString stringWithFormat:@"back=%f wanted=%f",
						back != nil ? [back timeIntervalSince1970] : -1.0, WHEN]);
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSDate *parsed;

		[formatter setLocale:de];
		[formatter setTimeZone:utc];
		[formatter setDateFormat:@"dd.MM.yyyy"];
		parsed = [formatter dateFromString:@"04.03.2021"];
		check("df-parse",
		      parsed != nil && [parsed timeIntervalSince1970] == WHEN,
		      [NSString stringWithFormat:@"parsed=%f wanted=%f",
						parsed != nil ? [parsed timeIntervalSince1970] : -1.0, WHEN]);
	}

	{
		/* THE ZONE IS AN OFFSET (F7's NSTimeZone says nothing else), and it must actually move
		 * the clock — this is the check that proves the zone reached ICU. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *zero;
		NSString *plus;
		NSString *minus;

		[formatter setLocale:en];
		[formatter setDateFormat:@"HH:mm"];
		[formatter setTimeZone:utc];
		zero = [formatter stringFromDate:when];
		[formatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:5 * 3600 + 30 * 60]];
		plus = [formatter stringFromDate:when];
		[formatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:-(8 * 3600)]];
		minus = [formatter stringFromDate:when];
		check("df-timezone-offset",
		      zero != nil && plus != nil && minus != nil &&
		      [zero isEqualToString:@"00:00"] && [plus isEqualToString:@"05:30"] &&
		      [minus isEqualToString:@"16:00"],
		      [NSString stringWithFormat:@"+00:00=%@ +05:30=%@ -08:00=%@",
						zero, plus, minus]);
	}

	{
		/* CLDR SKELETONS: the FIELDS are ours, the ORDER is the locale's. */
		NSString *enPattern = [NSDateFormatter dateFormatFromTemplate:@"yMMMd"
								     options:0
								      locale:en];
		NSString *dePattern = [NSDateFormatter dateFormatFromTemplate:@"yMMMd"
								     options:0
								      locale:de];

		check("df-template-order",
		      enPattern != nil && dePattern != nil &&
		      [enPattern hasPrefix:@"M"] && [dePattern hasPrefix:@"d"],
		      [NSString stringWithFormat:@"en=%@ de=%@", enPattern, dePattern]);
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *text = [formatter stringFromDate:when];

		/* No style and no pattern: nothing was requested, so no field is formatted. Apple's
		 * answer is the empty string and ours is too — NOT an error. */
		check("df-no-fields",
		      text != nil && [text length] == 0,
		      text != nil ? text : @"(nil)");
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];

		[formatter setLocale:en];
		[formatter setDateFormat:@"yyyy-MM-dd"];
		(void)[formatter dateFromString:@"this is not a date"];
		/* The check is that the call RETURNED at all; the value is asserted next door. */
		check("df-parse-refusal", YES, @"no crash on unparseable text");
	}

	{
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSDate *strict;
		NSDate *lenient;

		[formatter setLocale:en];
		[formatter setTimeZone:utc];
		[formatter setDateFormat:@"yyyy-MM-dd"];
		[formatter setLenient:NO];
		strict = [formatter dateFromString:@"2021-13-45"];
		[formatter setLenient:YES];
		lenient = [formatter dateFromString:@"2021-13-45"];
		check("df-strict-parse",
		      strict == nil && lenient != nil,
		      [NSString stringWithFormat:@"strict=%@ lenient=%@",
						strict != nil ? [strict description] : @"(nil)",
						lenient != nil ? [lenient description] : @"(nil)"]);
	}

	{
		/* THE ABSTRACT BASE. NSFormatter has no rule, so it must refuse rather than answer a
		 * default — the same contract NSPredicate's base took. */
		NSFormatter *base = [[NSFormatter alloc] init];
		BOOL raised = NO;

		@try {
			(void)[base stringForObjectValue:@"x"];
		} @catch (NSException *exception) {
			raised = [[exception name] isEqualToString:NSInvalidArgumentException];
		}
		check("df-base-raises", raised,
		      @"the abstract base refused rather than inventing a format");
	}

	{
		/* A FORMATTER IS MUTABLE, so a copy must be independent. */
		NSDateFormatter *original = [[NSDateFormatter alloc] init];
		NSDateFormatter *copy;
		NSString *before;
		NSString *after;

		[original setLocale:en];
		[original setTimeZone:utc];
		[original setDateFormat:@"yyyy"];
		before = [original stringFromDate:when];
		copy = [original copy];
		[copy setDateFormat:@"yyyy-MM-dd"];
		after = [original stringFromDate:when];
		check("df-copy-independent",
		      before != nil && after != nil && [before isEqualToString:after] &&
		      [[copy stringFromDate:when] isEqualToString:@"2021-03-04"],
		      [NSString stringWithFormat:@"original before=%@ after=%@ copy=%@",
						before, after, [copy stringFromDate:when]]);
	}

	{
		/* F13.7a: A NAMED ZONE CARRIES ITS OWN DST RULES, and that is the one thing a
		 * fixed-offset zone could never do (F7 shipped the offset and refused the database). One
		 * zone, two instants, TWO offsets: 00:00Z is 19:00 the previous day in March (EST, -5)
		 * and 20:00 in July (EDT, -4). */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSTimeZone *newYork = [NSTimeZone timeZoneWithName:@"America/New_York"];
		NSString *winter = nil;
		NSString *summer = nil;

		if (newYork != nil) {
			[formatter setLocale:en];
			[formatter setTimeZone:newYork];
			[formatter setDateFormat:@"HH:mm"];
			winter = [formatter stringFromDate:when];
			summer = [formatter stringFromDate:
				  [NSDate dateWithTimeIntervalSince1970:1625356800.0]];
		}
		check("df-named-zone-dst",
		      winter != nil && summer != nil &&
		      [winter isEqualToString:@"19:00"] && [summer isEqualToString:@"20:00"],
		      [NSString stringWithFormat:@"winter=%@ summer=%@", winter, summer]);
	}

	{
		/* F13.7e: THE SYMBOL ARRAYS COME OUT OF THE DATA — the locale's own month and weekday
		 * spellings, read from the formatter ICU built. Nothing in this probe encodes a name. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSArray *deMonths;
		NSArray *deWeekdays;
		NSArray *enMonths;

		[formatter setLocale:de];
		deMonths = [formatter monthSymbols];
		deWeekdays = [formatter weekdaySymbols];
		[formatter setLocale:en];
		enMonths = [formatter monthSymbols];
		/* THE COUNT IS "AS MANY AS THE DATA HAS", not exactly twelve: ICU answers a thirteenth
		 * month slot with an empty string rather than an error, so a Gregorian formatter's month
		 * array can carry thirteen entries with the last one blank. What matters — and what is
		 * asserted — is that the NAMES are the locale's. */
		check("df-symbols",
		      deMonths != nil && [deMonths count] >= 12 &&
		      [[deMonths objectAtIndex:0] isEqualToString:@"Januar"] &&
		      [[deMonths objectAtIndex:11] isEqualToString:@"Dezember"] &&
		      deWeekdays != nil && [deWeekdays count] >= 7 &&
		      [[deWeekdays objectAtIndex:0] isEqualToString:@"Sonntag"] &&
		      enMonths != nil && [[enMonths objectAtIndex:0] isEqualToString:@"January"],
		      [NSString stringWithFormat:@"de[0]=%@ de[11]=%@ deWeekday[0]=%@ en[0]=%@",
			deMonths != nil && [deMonths count] > 11
				? [deMonths objectAtIndex:0] : @"?",
			deMonths != nil && [deMonths count] > 11
				? [deMonths objectAtIndex:11] : @"?",
			deWeekdays != nil && [deWeekdays count] > 0
				? [deWeekdays objectAtIndex:0] : @"?",
			enMonths != nil && [enMonths count] > 0
				? [enMonths objectAtIndex:0] : @"?"]);
	}

	{
		/* THE CALENDAR REACHES THE FORMATTER (NSCalendar.h's whole reason): with a Hebrew calendar
		 * set, the same instant renders the HEBREW year — 5784 for 2023-11-14 — an answer only the
		 * data can produce. And the month array grows with the calendar, because a Hebrew leap
		 * year has thirteen of them. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSCalendar *hebrew = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierHebrew];
		NSString *text = nil;
		NSArray *months = nil;

		if (hebrew != nil) {
			[formatter setLocale:en];
			[formatter setTimeZone:utc];
			[formatter setCalendar:hebrew];
			months = [formatter monthSymbols];
			[formatter setDateFormat:@"yyyy"];
			text = [formatter stringFromDate:
					[NSDate dateWithTimeIntervalSince1970:1700000000.0]];
		}
		check("df-calendar-hebrew",
		      text != nil && [text isEqualToString:@"5784"] &&
		      months != nil && [months count] >= 12,
		      [NSString stringWithFormat:@"year=%@ months=%lu", text,
			(unsigned long)(months != nil ? [months count] : 0)]);
	}

	{
		/* THE SKELETON SETTER, applied to THIS formatter: the locale's order, not ours. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSString *pattern;
		NSString *rendered = nil;

		[formatter setLocale:de];
		[formatter setTimeZone:utc];
		[formatter setLocalizedDateFormatFromTemplate:@"yMMMd"];
		pattern = [formatter dateFormat];
		rendered = [formatter stringFromDate:when];
		check("df-template-set",
		      pattern != nil && [pattern containsString:@"y"] &&
		      [pattern containsString:@"M"] && [pattern containsString:@"d"] &&
		      rendered != nil && [rendered containsString:@"2021"],
		      [NSString stringWithFormat:@"pattern=%@ rendered=%@", pattern, rendered]);
	}

	{
		/* F13.7e: THE STANDALONE AND LONG ARRAYS — the same data question, asked in the context a
		 * name is read OUT of a sentence. ICU's standalone month/weekday/quarter symbol types are
		 * the whole of it, and the COUNT is the data's. (REASONED where a spelling is named:
		 * en_US's standalone wide month is "January", its abbreviated one "Jan".) */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSArray *months;
		NSArray *shortMonths;
		NSArray *weekdays;
		NSArray *quarters;

		[formatter setLocale:en];
		months = [formatter standaloneMonthSymbols];
		shortMonths = [formatter shortStandaloneMonthSymbols];
		weekdays = [formatter shortStandaloneWeekdaySymbols];
		quarters = [formatter standaloneQuarterSymbols];

		check("df-symbols-standalone",
		      months != nil && [months count] >= 12 &&
		      [[months objectAtIndex:0] isEqualToString:@"January"] &&
		      shortMonths != nil && [shortMonths count] >= 12 &&
		      [[shortMonths objectAtIndex:0] isEqualToString:@"Jan"] &&
		      weekdays != nil && [weekdays count] >= 7 &&
		      quarters != nil && [quarters count] >= 4 &&
		      [formatter veryShortStandaloneMonthSymbols] != nil &&
		      [formatter veryShortStandaloneWeekdaySymbols] != nil &&
		      [formatter shortStandaloneQuarterSymbols] != nil,
		      [NSString stringWithFormat:@"mon0=%@ short0=%@ wd=%lu q=%lu",
			[months count] > 0 ? (NSString *)[months objectAtIndex:0] : @"(nil)",
			[shortMonths count] > 0 ? (NSString *)[shortMonths objectAtIndex:0] : @"(nil)",
			(unsigned long)(weekdays != nil ? [weekdays count] : 0),
			(unsigned long)(quarters != nil ? [quarters count] : 0)]);
	}

	{
		/* THE LONG ERA NAMES are a DIFFERENT symbol type from the abbreviated ones (ICU's
		 * UDAT_ERA_NAMES against UDAT_ERAS) — which is exactly why the row needed the data. The
		 * check asserts a PROPERTY, not the spelling: whatever the locale says, the long form is
		 * the longer one. REASONED: "Before Christ"/"Anno Domini" against "BC"/"AD". */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSArray *longEras;
		NSArray *shortEras;

		[formatter setLocale:en];
		longEras = [formatter longEraSymbols];
		shortEras = [formatter eraSymbols];

		check("df-long-era",
		      longEras != nil && [longEras count] >= 2 &&
		      [[longEras objectAtIndex:0] length] > 0 &&
		      shortEras != nil && [shortEras count] >= 2 &&
		      [[longEras objectAtIndex:0] length] > [[shortEras objectAtIndex:0] length],
		      [NSString stringWithFormat:@"long0=%@ short0=%@",
			[longEras count] > 0 ? (NSString *)[longEras objectAtIndex:0] : @"(nil)",
			[shortEras count] > 0 ? (NSString *)[shortEras objectAtIndex:0] : @"(nil)"]);
	}

	{
		/* AM/PM ARE THE FORMATTER'S OWN DATA, read under Apple's OWN capitalised spelling. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];

		[formatter setLocale:en];
		check("df-ampm",
		      [[formatter AMSymbol] isEqualToString:@"AM"] &&
		      [[formatter PMSymbol] isEqualToString:@"PM"],
		      [NSString stringWithFormat:@"AM=%@ PM=%@",
			[formatter AMSymbol] != nil ? [formatter AMSymbol] : @"(nil)",
			[formatter PMSymbol] != nil ? [formatter PMSymbol] : @"(nil)"]);
	}

	{
		/* THE BEHAVIOUR DOORS: Apple's class default is the MODERN formatter, and the instance's own
		 * reading agrees with it (NSDateFormatterBehaviorDefault MEANS 10_4 here). */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];

		check("df-behavior-default",
		      [NSDateFormatter defaultFormatterBehavior] == NSDateFormatterBehavior10_4 &&
		      [formatter formatterBehavior] == NSDateFormatterBehavior10_4,
		      [NSString stringWithFormat:@"default=%d instance=%d",
			(int)[NSDateFormatter defaultFormatterBehavior],
			(int)[formatter formatterBehavior]]);
	}

	{
		/* THE DEPRECATED PAIR. Natural-language parsing is not something ICU's field parser does,
		 * so -allowsNaturalLanguage is NO — and the deprecated initializer still does the part that
		 * means something: it sets the pattern. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] initWithDateFormat:@"yyyy-MM-dd"
								allowNaturalLanguage:YES];
		NSString *pattern = [formatter dateFormat];
		NSString *text = [formatter stringFromDate:when];

		check("df-natural-language",
		      ![formatter allowsNaturalLanguage] &&
		      pattern != nil && [pattern containsString:@"yyyy"] &&
		      [pattern containsString:@"MM"] && [pattern containsString:@"dd"] &&
		      text != nil && [text length] > 0,
		      [NSString stringWithFormat:@"allowsNL=%d pattern=%@ text=%@",
			(int)[formatter allowsNaturalLanguage], pattern, text]);
	}

	{
		/* THE RANGE DOOR. NSFormatter's base RAISES here; this class has a parser, so it ANSWERS —
		 * and a non-empty range reads the SUBSTRING out of a longer string. */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		id out = nil;
		id outRange = nil;
		NSRange whole = NSMakeRange(0, 0);
		NSRange sub = NSMakeRange(2, 10);	/* "2021-03-04" out of "xx2021-03-04yy" */

		[formatter setLocale:en];
		[formatter setTimeZone:utc];
		[formatter setDateFormat:@"yyyy-MM-dd"];
		check("df-get-object-value",
		      [formatter getObjectValue:&out forString:@"2021-03-04" range:&whole error:NULL] &&
		      out != nil && [(NSDate *)out timeIntervalSince1970] == WHEN &&
		      [formatter getObjectValue:&outRange forString:@"xx2021-03-04yy"
					  range:&sub error:NULL] &&
		      outRange != nil && [(NSDate *)outRange timeIntervalSince1970] == WHEN,
		      [NSString stringWithFormat:@"out=%@ sub=%@",
			out != nil ? [out description] : @"(nil)",
			outRange != nil ? [outRange description] : @"(nil)"]);
	}

	{
		/* THE KNOBS: formatting context, the two-digit pivot and the Gregorian cutover — each a
		 * door into ICU, asserted as a PROPERTY (the value set is the value read back). */
		NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
		NSDate *pivot = [NSDate dateWithTimeIntervalSince1970:946684800.0];	/* 2000-01-01 */
		NSDate *cutover = [NSDate dateWithTimeIntervalSince1970:0.0];

		[formatter setFormattingContext:NSFormattingContextStandalone];
		[formatter setTwoDigitStartDate:pivot];
		[formatter setGregorianStartDate:cutover];
		check("df-knobs",
		      [formatter formattingContext] == NSFormattingContextStandalone &&
		      [formatter twoDigitStartDate] != nil &&
		      [[formatter twoDigitStartDate] timeIntervalSince1970] == 946684800.0 &&
		      [formatter gregorianStartDate] != nil &&
		      [[formatter gregorianStartDate] timeIntervalSince1970] == 0.0,
		      [NSString stringWithFormat:@"ctx=%d pivot=%f cut=%f",
			(int)[formatter formattingContext],
			[[formatter twoDigitStartDate] timeIntervalSince1970],
			[[formatter gregorianStartDate] timeIntervalSince1970]]);
	}

	printf("FOUNDATION-DATEFORMATTER RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-DATEFORMATTER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DATEFORMATTER DONE\n");
	return failc ? 1 : 0;
}
