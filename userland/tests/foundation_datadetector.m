/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_datadetector — `NSDataDetector`'s three detectors and the two types it REFUSES, with a reason
 * (docs/design/foundation-plan.md §62.19). ONE unit, importing only <Foundation/Foundation.h>.
 *
 * EVERY CHECK BUILDS ITS OWN DETECTOR, asking only for the kind it is about. That is not tidiness: two
 * detectors can claim the same text (`2026-02-30` is an eight-digit hyphenated run as well as a date that does
 * not exist), and a check whose subject is the DATE detector must not be able to answer differently because the
 * PHONE detector is also switched on. A check whose own precondition other checks can change measures its
 * neighbours.
 *
 * WHAT THIS PROBE ASSERTS ABOUT THE DATES IS READ BACK THROUGH NSCALENDAR rather than compared with a string:
 * the result carries an NSDate, and the fact worth checking is the DAY the calendar reports - which is also
 * what makes the `February 30` check meaningful, since that is the calendar's own refusal and not a rule this
 * library wrote down twice.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-DATADETECTOR"

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf(PREFIX " %s ok\n", name);
	} else {
		failc++;
		printf(PREFIX " %s FAIL %s\n", name, detail ? detail : "");
	}
}

static NSTextCheckingTypes FN_ALL = NSTextCheckingTypeDate | NSTextCheckingTypeLink |
				    NSTextCheckingTypePhoneNumber;

/* THE DAY, HOUR AND MINUTE A RESULT'S DATE NAMES, read in the same calendar and zone the detector used. */
static BOOL fn_components_of(NSDate *date, NSInteger *year, NSInteger *month, NSInteger *day,
			    NSInteger *hour, NSInteger *minute)
{
	NSCalendar *calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
	NSTimeZone *zone = [NSTimeZone systemTimeZone];
	NSDateComponents *parts;

	if (calendar == nil) {
		return NO;
	}
	[calendar setTimeZone:zone];
	parts = [calendar components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay |
				      NSCalendarUnitHour | NSCalendarUnitMinute)
			     fromDate:date];
	if (parts == nil) {
		return NO;
	}
	*year = [parts year];
	*month = [parts month];
	*day = [parts day];
	*hour = [parts hour];
	*minute = [parts minute];
	return YES;
}

int main(void)
{
	NSDataDetector *links = nil;
	NSDataDetector *dates = nil;
	NSDataDetector *phones = nil;
	NSDataDetector *all = nil;
	NSError *error = nil;

	/* ---- THE REFUSALS, WHICH ARE THE CONTRACT ---------------------------------------------------- */
	{
		NSDataDetector *wrong =
			[NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeSpelling error:&error];

		check("a-type-no-detector-makes-is-refused-with-an-error",
		      wrong == nil && error != nil &&
		      [[error domain] isEqualToString:NSDataDetectorErrorDomain] &&
		      [error code] == NSDataDetectorTypeNotADataDetectorCode,
		      [[NSString stringWithFormat:@"detector=%s domain=%@ code=%ld",
			wrong == nil ? "nil" : "NOT NIL", [error domain], (long)[error code]] UTF8String]);
	}
	{
		NSError *addressError = nil;
		NSError *transitError = nil;
		NSDataDetector *address =
			[NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeAddress error:&addressError];
		NSDataDetector *transit =
			[NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeTransitInformation
							error:&transitError];

		check("address-and-transit-refuse-by-name",
		      address == nil && transit == nil && addressError != nil && transitError != nil &&
		      [addressError code] == NSDataDetectorTypeNeedsSubstrateCode &&
		      [transitError code] == NSDataDetectorTypeNeedsSubstrateCode &&
		      [[addressError localizedDescription] length] > 0,
		      [[NSString stringWithFormat:@"address=%s code=%ld transit=%s code=%ld",
			address == nil ? "nil" : "NOT NIL", (long)[addressError code],
			transit == nil ? "nil" : "NOT NIL", (long)[transitError code]] UTF8String]);
	}
	{
		NSError *zeroError = nil;
		NSDataDetector *none =
			[NSDataDetector dataDetectorWithTypes:0 error:&zeroError];

		check("asking-for-no-type-is-refused", none == nil && zeroError != nil,
		      "a detector asked for nothing has nothing to do");
	}

	links = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeLink error:NULL];
	dates = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeDate error:NULL];
	phones = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypePhoneNumber error:NULL];
	all = [NSDataDetector dataDetectorWithTypes:FN_ALL error:NULL];
	check("the-three-supported-types-are-accepted",
	      links != nil && dates != nil && phones != nil && all != nil &&
	      [links checkingTypes] == NSTextCheckingTypeLink &&
	      [all checkingTypes] == FN_ALL,
	      [[NSString stringWithFormat:@"links=%s dates=%s phones=%s types=0x%llx",
		links != nil ? "yes" : "no", dates != nil ? "yes" : "no",
		phones != nil ? "yes" : "no", (unsigned long long)[all checkingTypes]] UTF8String]);

	/* ---- LINKS ----------------------------------------------------------------------------------- */
	{
		NSString *text = @"see https://example.com/a/b?q=1 now";
		NSArray *found = [links matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;
		NSRange range = match != nil ? [match range] : NSMakeRange(0, 0);
		NSString *piece = match != nil ? [text substringWithRange:range] : @"(none)";

		check("a-link-is-found-with-its-range-and-its-url",
		      match != nil && [match resultType] == NSTextCheckingTypeLink &&
		      [piece isEqualToString:@"https://example.com/a/b?q=1"] &&
		      [[[match URL] absoluteString] isEqualToString:@"https://example.com/a/b?q=1"],
		      [[NSString stringWithFormat:@"found=%lu piece=[%@] url=%@",
			(unsigned long)[found count], piece, [[match URL] absoluteString]] UTF8String]);
	}
	{
		NSString *text = @"visit www.example.com today";
		NSArray *found = [links matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;

		check("a-bare-host-is-a-link-and-the-url-gets-a-scheme",
		      match != nil &&
		      [[[match URL] absoluteString] isEqualToString:@"http://www.example.com"],
		      [[NSString stringWithFormat:@"url=%@", [[match URL] absoluteString]] UTF8String]);
	}
	{
		NSString *text = @"go to http://example.com. Then stop";
		NSArray *found = [links matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;

		check("trailing-punctuation-is-not-part-of-the-link",
		      match != nil &&
		      [[text substringWithRange:[match range]] isEqualToString:@"http://example.com"],
		      [[NSString stringWithFormat:@"piece=[%@]",
			match != nil ? [text substringWithRange:[match range]] : @"(none)"] UTF8String]);
	}
	{
		NSString *text = @"http://en.example.org/wiki/Foo_(bar) and (see http://x.example/)";
		NSArray *found = [links matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *first = [found count] >= 1 ? [found objectAtIndex:0] : nil;
		NSTextCheckingResult *second = [found count] >= 2 ? [found objectAtIndex:1] : nil;

		check("a-balanced-parenthesis-stays-and-an-unbalanced-one-does-not",
		      [found count] == 2 && first != nil && second != nil &&
		      [[text substringWithRange:[first range]]
			isEqualToString:@"http://en.example.org/wiki/Foo_(bar)"] &&
		      [[text substringWithRange:[second range]] isEqualToString:@"http://x.example/"],
		      [[NSString stringWithFormat:@"found=%lu first=[%@] second=[%@]",
			(unsigned long)[found count],
			first != nil ? [text substringWithRange:[first range]] : @"(none)",
			second != nil ? [text substringWithRange:[second range]] : @"(none)"]
			UTF8String]);
	}

	/* ---- DATES ----------------------------------------------------------------------------------- */
	{
		NSString *text = @"we met on 2026-09-26 at last";
		NSArray *found = [dates matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;
		NSInteger y = 0;
		NSInteger m = 0;
		NSInteger d = 0;
		NSInteger h = 0;
		NSInteger mi = 0;
		BOOL read = match != nil && [match date] != nil &&
			    fn_components_of([match date], &y, &m, &d, &h, &mi);

		check("an-iso-date-is-found-and-its-day-is-the-one-written",
		      read && [match resultType] == NSTextCheckingTypeDate &&
		      [[text substringWithRange:[match range]] isEqualToString:@"2026-09-26"] &&
		      y == 2026 && m == 9 && d == 26 && h == 0 && mi == 0 &&
		      [match timeZone] != nil,
		      [[NSString stringWithFormat:@"count=%lu date=%s read=%@",
			(unsigned long)[found count], [match date] != nil ? "yes" : "no",
			read ? [NSString stringWithFormat:@"%ld-%02ld-%02ld %02ld:%02ld",
				(long)y, (long)m, (long)d, (long)h, (long)mi] : @"(none)"] UTF8String]);
	}
	{
		NSString *monthName = @"due Sep 26, 2026 sharp";
		NSString *dayFirst = @"due 26 September 2026 sharp";
		NSString *slashed = @"due 9/26/2026 sharp";
		NSArray *a = [dates matchesInString:monthName options:0 range:NSMakeRange(0, [monthName length])];
		NSArray *b = [dates matchesInString:dayFirst options:0 range:NSMakeRange(0, [dayFirst length])];
		NSArray *c = [dates matchesInString:slashed options:0 range:NSMakeRange(0, [slashed length])];
		NSInteger y = 0;
		NSInteger m = 0;
		NSInteger d = 0;
		NSInteger h = 0;
		NSInteger mi = 0;
		BOOL read = NO;
		BOOL pieces = [a count] == 1 && [b count] == 1 && [c count] == 1 &&
			      [[monthName substringWithRange:[[a objectAtIndex:0] range]]
				isEqualToString:@"Sep 26, 2026"] &&
			      [[dayFirst substringWithRange:[[b objectAtIndex:0] range]]
				isEqualToString:@"26 September 2026"] &&
			      [[slashed substringWithRange:[[c objectAtIndex:0] range]]
				isEqualToString:@"9/26/2026"];

		read = pieces && fn_components_of([[c objectAtIndex:0] date], &y, &m, &d, &h, &mi);
		check("the-three-other-date-forms-all-name-the-same-day",
		      pieces &&
		      fn_components_of([[a objectAtIndex:0] date], &y, &m, &d, &h, &mi) &&
		      y == 2026 && m == 9 && d == 26 &&
		      fn_components_of([[b objectAtIndex:0] date], &y, &m, &d, &h, &mi) &&
		      y == 2026 && m == 9 && d == 26 &&
		      fn_components_of([[c objectAtIndex:0] date], &y, &m, &d, &h, &mi) &&
		      y == 2026 && m == 9 && d == 26,
		      [[NSString stringWithFormat:@"counts=%lu/%lu/%lu read=%@",
			(unsigned long)[a count], (unsigned long)[b count], (unsigned long)[c count],
			read ? [NSString stringWithFormat:@"%ld-%02ld-%02ld", (long)y, (long)m, (long)d]
			     : @"(none)"] UTF8String]);
	}
	{
		NSString *text = @"not a date: 2026-02-30";
		NSError *docError = NULL;
		NSDataDetector *monthNames =
			[NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeDate error:&docError];
		NSArray *found = [monthNames matchesInString:text options:0 range:NSMakeRange(0, [text length])];

		check("a-day-the-calendar-does-not-have-is-not-a-date",
		      monthNames != nil && [found count] == 0,
		      [[NSString stringWithFormat:@"found=%lu (February 30 must not be a date)",
			(unsigned long)[found count]] UTF8String]);
	}
	{
		NSString *text = @"meet at 2026-09-26 14:30 please";
		NSArray *found = [dates matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;
		NSInteger y = 0;
		NSInteger m = 0;
		NSInteger d = 0;
		NSInteger h = 0;
		NSInteger mi = 0;
		BOOL read = match != nil && fn_components_of([match date], &y, &m, &d, &h, &mi);

		check("a-date-and-a-time-joined-by-a-space-are-one-result",
		      read && y == 2026 && m == 9 && d == 26 && h == 14 && mi == 30 &&
		      [[text substringWithRange:[match range]] isEqualToString:@"2026-09-26 14:30"],
		      [[NSString stringWithFormat:@"count=%lu piece=[%@] read=%@",
			(unsigned long)[found count],
			match != nil ? [text substringWithRange:[match range]] : @"(none)",
			read ? [NSString stringWithFormat:@"%02ld:%02ld", (long)h, (long)mi] : @"(none)"]
			UTF8String]);
	}
	{
		NSString *text = @"we left at 7:05 pm sharp";
		NSArray *found = [dates matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		NSTextCheckingResult *match = [found count] == 1 ? [found objectAtIndex:0] : nil;
		NSCalendar *calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
		NSInteger y = 0;
		NSInteger m = 0;
		NSInteger d = 0;
		NSInteger h = 0;
		NSInteger mi = 0;
		NSInteger ty = 0;
		NSInteger tm = 0;
		NSInteger td = 0;
		NSInteger th = 0;
		NSInteger tmi = 0;
		BOOL read = NO;
		BOOL today = NO;

		{
			/* A LOCAL, because `+systemTimeZone` is annotated nullable and `-setTimeZone:` is not. */
			NSTimeZone *zone = [NSTimeZone systemTimeZone];

			[calendar setTimeZone:zone];
		}
		if (match != nil) {
			read = fn_components_of([match date], &y, &m, &d, &h, &mi);
		}
		today = fn_components_of([NSDate date], &ty, &tm, &td, &th, &tmi);
		check("a-time-on-its-own-is-today-at-that-time",
		      read && today && h == 19 && mi == 5 && y == ty && m == tm && d == td &&
		      [[text substringWithRange:[match range]] isEqualToString:@"7:05 pm"],
		      [[NSString stringWithFormat:@"piece=[%@] read=%@ today=%ld-%02ld-%02ld",
			match != nil ? [text substringWithRange:[match range]] : @"(none)",
			read ? [NSString stringWithFormat:@"%02ld:%02ld", (long)h, (long)mi] : @"(none)",
			(long)ty, (long)tm, (long)td] UTF8String]);
	}

	/* ---- PHONE NUMBERS --------------------------------------------------------------------------- */
	{
		NSString *international = @"ring +44 20 7946 0958 today";
		NSString *national = @"ring (555) 123-4567 today";
		NSArray *a = [phones matchesInString:international options:0
					       range:NSMakeRange(0, [international length])];
		NSArray *b = [phones matchesInString:national options:0
					       range:NSMakeRange(0, [national length])];

		check("a-phone-number-is-found-in-both-international-shapes",
		      [a count] == 1 && [b count] == 1 &&
		      [[international substringWithRange:[[a objectAtIndex:0] range]]
			isEqualToString:@"+44 20 7946 0958"] &&
		      [[national substringWithRange:[[b objectAtIndex:0] range]]
			isEqualToString:@"(555) 123-4567"] &&
		      [[[a objectAtIndex:0] phoneNumber] isEqualToString:@"+44 20 7946 0958"],
		      [[NSString stringWithFormat:@"counts=%lu/%lu first=[%@] second=[%@]",
			(unsigned long)[a count], (unsigned long)[b count],
			[a count] > 0 ? [international substringWithRange:[[a objectAtIndex:0] range]] : @"(none)",
			[b count] > 0 ? [national substringWithRange:[[b objectAtIndex:0] range]] : @"(none)"]
			UTF8String]);
	}
	{
		/* THE RULE THAT KEEPS AN INTEGER FROM BEING A PHONE NUMBER, asserted from both sides: eight digits with
		 * no separator is not one, a seven-digit bare run is not one, and fourteen digits is. */
		NSString *bare = @"order 20260926 shipped";
		NSString *seven = @"code 1234567 here";
		NSString *longRun = @"id 12345678901234 here";
		NSArray *a = [phones matchesInString:bare options:0 range:NSMakeRange(0, [bare length])];
		NSArray *b = [phones matchesInString:seven options:0 range:NSMakeRange(0, [seven length])];
		NSArray *c = [phones matchesInString:longRun options:0
					       range:NSMakeRange(0, [longRun length])];

		check("a-bare-digit-run-is-not-a-phone-number",
		      [a count] == 0 && [b count] == 0 && [c count] == 1,
		      [[NSString stringWithFormat:@"eight=%lu seven=%lu fourteen=%lu",
			(unsigned long)[a count], (unsigned long)[b count], (unsigned long)[c count]]
			UTF8String]);
	}
	{
		NSString *text = @"on 2026-09-26 we met";
		NSArray *found = [all matchesInString:text options:0 range:NSMakeRange(0, [text length])];

		check("one-span-is-claimed-once-and-the-date-wins-it",
		      [found count] == 1 &&
		      [[found objectAtIndex:0] resultType] == NSTextCheckingTypeDate,
		      [[NSString stringWithFormat:@"count=%lu type=0x%llx",
			(unsigned long)[found count],
			(unsigned long)([found count] > 0 ? [[found objectAtIndex:0] resultType] : 0)]
			UTF8String]);
	}
	{
		/* A DATE INSIDE A URL IS PART OF THE LINK, so it is claimed once: the link starts earlier and the date
		 * that overlaps it is dropped. This is the case that would otherwise answer two results for one span of
		 * text - the link and the date it contains. */
		NSString *inside = @"http://example.com/2026-09-26 is the date";
		NSArray *found = [all matchesInString:inside options:0
						range:NSMakeRange(0, [inside length])];

		check("a-date-inside-a-link-is-part-of-the-link",
		      [found count] == 1 &&
		      [[found objectAtIndex:0] resultType] == NSTextCheckingTypeLink &&
		      [[inside substringWithRange:[[found objectAtIndex:0] range]]
			isEqualToString:@"http://example.com/2026-09-26"],
		      [[NSString stringWithFormat:@"count=%lu type=0x%llx",
			(unsigned long)[found count],
			(unsigned long)([found count] > 0 ? [[found objectAtIndex:0] resultType] : 0)]
			UTF8String]);
	}
	{
		NSString *text = @"see http://a.example/ on 2026-09-26";
		NSArray *found = [all matchesInString:text options:0 range:NSMakeRange(0, [text length])];
		BOOL ordered = YES;
		BOOL disjoint = YES;
		NSUInteger i;

		for (i = 0; i < [found count]; i++) {
			NSRange range = [[found objectAtIndex:i] range];

			if (i > 0 && range.location < NSMaxRange([[found objectAtIndex:i - 1] range])) {
				disjoint = NO;
			}
			if (i > 0 && range.location < [[found objectAtIndex:i - 1] range].location) {
				ordered = NO;
			}
		}
		check("the-answer-is-in-document-order-and-never-overlaps",
		      [found count] == 2 && ordered && disjoint &&
		      [[found objectAtIndex:0] resultType] == NSTextCheckingTypeLink &&
		      [[found objectAtIndex:1] resultType] == NSTextCheckingTypeDate &&
		      /* ASSERTED BY WHAT THE RANGE COVERS, not by a hand-counted offset: the number would be a second
		       * place for the text to be written down, and the first version of this check had it wrong while
		       * the detector was right. */
		      [[text substringWithRange:[[found objectAtIndex:1] range]]
			isEqualToString:@"2026-09-26"],
		      [[NSString stringWithFormat:@"count=%lu ordered=%d disjoint=%d second=[%@]",
			(unsigned long)[found count], (int)ordered, (int)disjoint,
			[found count] > 1 ? [text substringWithRange:[[found objectAtIndex:1] range]]
					  : @"(none)"] UTF8String]);
	}
	{
		NSString *text = @"x 2026-09-26 y";
		NSArray *outside = [dates matchesInString:text options:0 range:NSMakeRange(0, 2)];
		NSArray *inside = [dates matchesInString:text options:0 range:NSMakeRange(2, 10)];

		check("the-range-argument-bounds-the-search",
		      [outside count] == 0 && [inside count] == 1 &&
		      [[inside objectAtIndex:0] range].location == 2,
		      [[NSString stringWithFormat:@"outside=%lu inside=%lu at=%ld",
			(unsigned long)[outside count], (unsigned long)[inside count],
			[inside count] > 0 ? (long)[[inside objectAtIndex:0] range].location : -1]
			UTF8String]);
	}

	/* ---- IT IS A SUBCLASS OF NSRegularExpression, BUT NOT A PATTERN -------------------------------- */
	{
		NSString *text = @"a 2026-09-26 b";
		__block NSUInteger seen = 0;
		NSUInteger counted;

		[dates enumerateMatchesInString:text options:0 range:NSMakeRange(0, [text length])
				     usingBlock:^(NSTextCheckingResult *result, NSMatchingFlags flags,
						  BOOL *stop) {
			(void)flags;
			(void)stop;
			if ([result resultType] == NSTextCheckingTypeDate) {
				seen++;
			}
		}];
		counted = [dates numberOfMatchesInString:text options:0
						   range:NSMakeRange(0, [text length])];
		check("the-parents-matching-doors-work-and-the-block-enumerator-too",
		      seen == 1 && counted == 1 && [dates firstMatchInString:text options:0
							       range:NSMakeRange(0, [text length])] != nil &&
		      [dates rangeOfFirstMatchInString:text options:0
						 range:NSMakeRange(0, [text length])].location == 2,
		      [[NSString stringWithFormat:@"block=%lu number=%lu",
			(unsigned long)seen, (unsigned long)counted] UTF8String]);
		check("a-detector-is-not-a-pattern",
		      [dates pattern] == nil && [dates numberOfCaptureGroups] == 0,
		      [[NSString stringWithFormat:@"pattern=%@ groups=%lu",
			[dates pattern], (unsigned long)[dates numberOfCaptureGroups]] UTF8String]);
	}
	{
		NSDataDetector *same = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeDate error:NULL];
		id copied = [dates copy];
		BOOL identity = copied == dates;
		BOOL equal = same != nil && [dates isEqual:same];
		BOOL hashed = equal && [dates hash] == [same hash];
		BOOL different = ![dates isEqual:links] && ![dates isEqual:all];

		check("a-detector-is-immutable-and-compares-by-its-types",
		      identity && equal && hashed && different,
		      [[NSString stringWithFormat:@"identity=%d equal=%d hash=%d different=%d",
			(int)identity, (int)equal, (int)hashed, (int)different] UTF8String]);
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
