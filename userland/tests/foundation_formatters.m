/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_formatters, unit 1 of 2 — the checks (ARC). docs/design/foundation-plan.md §12.3 W11.
 *
 *   fmt-attributed-default  -attributedStringForObjectValue:withDefaultAttributes: answers nil (Apple's
 *                           documented default, and the reason its return type is forward-declared)
 *   fmt-editing-invokes     -editingStringForObjectValue: INVOKES the string door — proven on a
 *                           subclass, and proven to RAISE on the base, because that is the door it calls
 *   fmt-partial-delegates   the proposed-selected-range form DELEGATES to the three-argument one (our
 *                           choice where Apple publishes no default — the header says so)
 *   fmt-partial-nil-string  a missing in-string is answered NO rather than forwarded through a
 *                           nonnull parameter
 *   fmt-nscoding            NSFormatter conforms to NSCoding (Apple declares it; we did not)
 *   pnc-absent              a fresh object has every component ABSENT, and absent is nil, not @""
 *   pnc-bag                 the seven components, and an unset component is nil rather than empty
 *   pnc-setter-copies       a mutable string handed to a setter is COPIED, so later mutation is not seen
 *   pnc-copy-deep           -copy copies the NESTED representation instead of sharing it, and the two
 *                           objects then diverge independently
 *   pnc-securecoding        NSPersonNameComponents adopts NSSecureCoding and answers YES to
 *                           +supportsSecureCoding
 *   list-join-en            en_US: "Alice, Bob, and Charlie" — the conjunction is the LOCALE's data
 *   list-join-two           two items take the two-item pattern, not the three-item one
 *   list-join-de            de_DE joins with "und", which is the whole point of binding CLDR
 *   list-item-formatter     an itemFormatter renders each item before the list is joined
 *   list-empty              the conjunction of no items is the empty string
 *   list-object-value       -stringForObjectValue: accepts an array and refuses anything else
 *   list-locale-resettable  setting the locale to nil returns the object to the CURRENT locale
 *   iso-round-trip          an ISO 8601 literal survives parse-then-print unchanged
 *   iso-shape               the DEFAULT is InternetDateTime in GMT: the epoch prints as ...Z
 *   iso-date-only           WithFullDate alone prints the date and nothing else
 *   iso-fractional          WithFractionalSeconds adds the field and its dot
 *   iso-space-separator     WithSpaceBetweenDateAndTime joins with a space instead of a T
 *   iso-basic-format        WITHOUT the colon option the time is the BASIC format (123456) — this
 *                           check exists because the one above failed on its first run and was right
 *                           to: the expectation was mine, the library was correct
 *   iso-offset-zone         the same instant in GMT+05:30 is 18:04:56+05:30, colon included
 *   iso-options-compose     the composites ARE ORs of their members, and InternetDateTime is both
 *   iso-refusals            unparseable text, a bare year, TRAILING JUNK and zero fields answer nil
 *   iso-class-and-copy      +stringFromDate:timeZone:formatOptions: and -copy agree with the receiver
 *   dif-range               a two-day range prints BOTH endpoints, once each; the date form and the en
 *                           dash are CLDR's, and Apple's prose example of them is a decade older
 *   dif-collapse            the POINT of the class: on one day the date prints ONCE, and the clock is
 *                           the LOCALE's (this is the check that caught the skeleton asking for "H")
 *   dif-template-locale     one template, two locales: "January 16" and "16. Januar"
 *   dif-no-style            nothing configured has no field to render, so both doors answer nil
 *   dif-interval-object     -stringFromDateInterval: is the same door as -stringFromDate:toDate:
 *   dif-resettable          locale/calendar/timeZone set to nil go back to the CURRENT values
 *   dif-template-copies     a mutable template edited after being set must not change the output
 *   bcf-count-styles        THE SHARP ONE: 1000 bytes is "1 KB" in decimal and "1000 bytes" in binary —
 *                           the same input, two answers, which is what a count style means
 *   bcf-magnitude           the unit walk, one fraction digit, and "1 byte" for a count of one
 *   bcf-units-mask          allowedUnits gates the choice; includesCount/includesUnit each suppress
 *   bcf-zeropad             the option's whole content is the ".0", asserted as a pair
 *   bcf-nonnumeric          zero prints as "Zero KB" (Apple's phrase) or "0 bytes"
 *   bcf-adaptive            the one property Apple publishes no behaviour for: our reading, in the header
 *   bcf-object-value        an NSNumber IS a byte count; anything else answers nil
 *   bcf-measurement-deferred the W12 WORK ITEM as an absence: no NSMeasurement doors, no NSMeasurement
 *   rdf-numeric             the unit choice is OURS: a one-day span is a DAY, not 24 hours
 *   rdf-named               the two styles: "1 day ago" versus "yesterday"
 *   rdf-spellout            SpellOut is what ICU's adopt parameter is for: "two months ago"
 *   rdf-interval-and-components the other two doors, and the two-month span that proves the difference
 *                           is the CALENDAR's — this check is what caught the unset-field sentinel
 *   rdf-capitalization      NSFormattingContext is what capitalises: "yesterday" vs "Yesterday"
 *   rdf-locale              "gestern"
 *   rdf-object-value        a date is formatted relative to NOW; anything else answers nil
 *
 * W11b (NSDateComponentsFormatter) — THE ONE CLASS HERE THAT IS A COMPOSITION: our rules over ICU's data.
 *   dcf-styles              the six styles against Apple's case semantics (Full "the units but not the
 *                           quantities", SpellOut "the units AND quantities", Positional "the POSITION of
 *                           a unit"); exact where ICU's data is stable across the host's and the guest's
 *                           DIFFERENT ICU BUILDS, structural where it is not — and the abbreviated styles'
 *                           SPACE joiner, which is ours and therefore stable
 *   dcf-plural              "1 hour" / "2 hours": plurality is ICU's, not an "s" appended here
 *   dcf-zero-behaviours     the seven zero behaviours against Apple's own definitions — THIS CHECK CAUGHT
 *                           THE DropAll-MASK BUG, where testing "is DropAll set" first (DropAll being the
 *                           OR of the other three) dropped every zero for all four behaviours
 *   dcf-positional-default  the OTHER Default: positional drops leading zeros but PADS trailing ones, so
 *                           one hour is "1:00:00" and not "1"
 *   dcf-positional-clock    Apple's own shape: 1 hour 3 minutes 37 seconds -> "1:03:37"
 *   dcf-unit-rules          maximumUnitCount limits what is displayed; collapsesLargestUnit is our
 *                           documented reading of Apple's unpublished "certain threshold"
 *   dcf-phrases             the two PHRASE options, whose words Apple does not publish: registered as an
 *                           English-affix DEVIATION, and asserted because a deviation must still behave
 *   dcf-date-doors          the date-shaped doors, with the CALENDAR doing the subtraction
 *   dcf-object-and-parse    -stringForObjectValue: takes date information, and -getObjectValue: ANSWERS
 *                           NO because Apple's own page says this class "only implements formatting"
 *   dcf-class-method        +localizedStringFromDateComponents:unitsStyle: is the instance path
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>		/* setenv: the probe's own locale premise — see main()'s note */

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FORMATTERS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FORMATTERS %s FAIL %s\n", name,
		       detail == nil ? "" : [detail UTF8String]);
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT (§9's lesson): a failure prints the strings that were compared. */
static NSString *fn_pair(NSString *a, NSString *b)
{
	return [NSString stringWithFormat:@"got=[%@] want=[%@]", a == nil ? @"(nil)" : a,
		b == nil ? @"(nil)" : b];
}

/*
 * A FORMATTER THAT IMPLEMENTS EXACTLY TWO DOORS, so the NSFormatter base's behaviour is what is being
 * measured and not a subclass's: the string door answers a fixed text, and the THREE-ARGUMENT partial
 * door accepts one string and refuses everything else. The proposed-selected-range door is deliberately
 * NOT overridden — the delegation it is supposed to inherit is the thing under test.
 */
@interface FNProber : NSFormatter
@end

@implementation FNProber

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	(void)object;
	return @"HELLO";
}

- (BOOL)isPartialStringValid:(NSString *)partialString
	    newEditingString:(NSString * _Nullable * _Nullable)newString
	    errorDescription:(NSString * _Nullable * _Nullable)error
{
	(void)newString;
	(void)error;
	return [partialString isEqualToString:@"ok"] ? YES : NO;
}

@end

/* An item formatter: formats an NSNumber as "<n>!", which proves each item went THROUGH it. */
@interface FNItemFormatter : NSFormatter
@end

@implementation FNItemFormatter

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	if (![object isKindOfClass:[NSNumber class]]) {
		return nil;
	}
	return [NSString stringWithFormat:@"%d!", [(NSNumber *)object intValue]];
}

@end

/* HOW MANY TIMES a substring occurs — the measurement the interval checks need, because "the date
 * appears ONCE" is the whole difference between an interval formatter and two joined dates. */
static NSUInteger fn_count(NSString *haystack, NSString *needle)
{
	NSUInteger count = 0;
	NSUInteger start = 0;
	NSUInteger length = [haystack length];
	NSUInteger needleLength = [needle length];

	if (needleLength == 0) {
		return 0;
	}
	while (start + needleLength <= length) {
		NSRange search = NSMakeRange(start, length - start);
		NSRange found = [haystack rangeOfString:needle options:0 range:search];

		if (found.location == NSNotFound) {
			break;
		}
		count++;
		start = found.location + found.length;
	}
	return count;
}

int main(void)
{
	NSFormatter *base = [[NSFormatter alloc] init];
	FNProber *prober = [[FNProber alloc] init];

	/*
	 * THE PROBE STATES ITS OWN PREMISE, AND THAT IS NOT A WORKAROUND — IT IS THE ONE CLASS IN THIS
	 * FAMILY WITH NO WAY TO BE TOLD. `NSDateComponentsFormatter` has NO locale property (Apple's surface
	 * gives it none), so the ambient locale IS its input, resolved through `+[NSLocale currentLocale]`,
	 * which reads LC_ALL then LANG. Every other formatter here is pinned with `setLocale:`; this one can
	 * only be pinned at the environment, so the probe pins it — otherwise a developer's own LANG would
	 * decide whether the checks below read "2 hours" or "2 Stunden".
	 *
	 * AND IT IS WHY THIS WAS FOUND AT ALL: the build's `LANG` was a compiler flag (`-std=c89`, see
	 * mk/00-base.mk), so under `make` this probe read ROOT-locale data ("2 h") while the same binary from
	 * a shell read English ("2 hours"). The build defect is fixed there; this line keeps the probe honest
	 * about the locale it asserts against.
	 */
	setenv("LC_ALL", "en_US.UTF-8", 1);

	/* ---- the NSFormatter gaps W11 found, each against Apple's documented default ---- */
	{
		/* TYPED AS APPLE TYPES IT — NSAttributedString*, the class this library forward-declares
		 * because W10 owns it and the base never mints one. */
		NSAttributedString *attr = [base attributedStringForObjectValue:@"x" withDefaultAttributes:nil];

		check("fmt-attributed-default", attr == nil,
		      @"the base answers nil, which is Apple's documented default");
	}

	{
		/* ON THE SUBclass the editing door must ANSWER THE STRING DOOR'S ANSWER — that is the whole
		 * content of "the default implementation invokes -stringForObjectValue:". */
		NSString *editing = [prober editingStringForObjectValue:@"anything"];
		BOOL baseRaised = NO;

		@try {
			(void)[base editingStringForObjectValue:@"anything"];
		} @catch (NSException *e) {
			(void)e;
			baseRaised = YES;
		}
		/* The base raises BECAUSE the door it invokes is the one that raises: one claim, two
		 * observations, so the check cannot pass by the invokation never happening. */
		check("fmt-editing-invokes",
		      editing != nil && [editing isEqualToString:@"HELLO"] && baseRaised,
		      [NSString stringWithFormat:@"editing=[%@] baseRaised=%d",
			 editing == nil ? @"(nil)" : editing, baseRaised]);
	}

	{
		/* THE DELEGATION. FNProber implements only the three-argument door; if the
		 * proposed-selected-range form did not delegate, both answers would be NO. */
		NSString *yes = @"ok";
		NSString *no = @"nope";
		NSRange whole = NSMakeRange(0, 2);
		BOOL accepted = [prober isPartialStringValid:&yes
					      proposedSelectedRange:NULL
						 originalString:@"o"
					   originalSelectedRange:whole
						errorDescription:NULL];
		BOOL refused = [prober isPartialStringValid:&no
					     proposedSelectedRange:NULL
						originalString:@"nope"
					  originalSelectedRange:whole
					       errorDescription:NULL];
		BOOL baseAnswer = [base isPartialStringValid:&no
					  proposedSelectedRange:NULL
					     originalString:@"nope"
					   originalSelectedRange:whole
						errorDescription:NULL];

		check("fmt-partial-delegates",
		      accepted == YES && refused == NO && baseAnswer == NO,
		      [NSString stringWithFormat:@"accepted=%d refused=%d base=%d (want 1/0/0)",
			 accepted, refused, baseAnswer]);
	}

	{
		/* A MISSING IN-STRING: answered NO here, NOT forwarded — the three-argument door's parameter is
		 * nonnull, so forwarding a nil would pass a value its contract forbids. */
		NSString *missing = nil;
		BOOL answer = [prober isPartialStringValid:&missing
				      proposedSelectedRange:NULL
					 originalString:@"o"
				   originalSelectedRange:NSMakeRange(0, 1)
					errorDescription:NULL];

		check("fmt-partial-nil-string", answer == NO,
		      [NSString stringWithFormat:@"answer=%d (want 0)", answer]);
	}

	{
		BOOL conforms = [NSFormatter conformsToProtocol:@protocol(NSCoding)];

		check("fmt-nscoding", conforms && [NSFormatter conformsToProtocol:@protocol(NSCopying)],
		      [NSString stringWithFormat:@"NSCoding=%d", conforms]);
	}

	/* ---- NSPersonNameComponents ---- */
	{
		NSPersonNameComponents *p = [[NSPersonNameComponents alloc] init];

		/* A DISTINCT NAME: this is the "fresh object" claim, and the check below is the "all seven
		 * round-trip" claim. They shared the name `pnc-bag` until the GUEST case caught it — 49 checks
		 * reported with only 48 distinct names, which the case's result-line check compared against the
		 * probe's own tally and refused. One name per check, per the plan's probe lessons. */
		check("pnc-absent",
		      [p namePrefix] == nil && [p givenName] == nil && [p familyName] == nil &&
		      [p phoneticRepresentation] == nil,
		      @"a fresh object has every component absent, and absent is nil, not @\"\"");

		[p setNamePrefix:@"Dr."];
		[p setGivenName:@"Ada"];
		[p setMiddleName:@"Augusta"];
		[p setFamilyName:@"Lovelace"];
		[p setNameSuffix:@"Jr."];
		[p setNickname:@"Ada"];
		{
			NSPersonNameComponents *phonetic = [[NSPersonNameComponents alloc] init];
			[phonetic setGivenName:@"AY-duh"];
			[p setPhoneticRepresentation:phonetic];
		}

		check("pnc-bag",
		      [[p namePrefix] isEqualToString:@"Dr."] &&
		      [[p givenName] isEqualToString:@"Ada"] &&
		      [[p middleName] isEqualToString:@"Augusta"] &&
		      [[p familyName] isEqualToString:@"Lovelace"] &&
		      [[p nameSuffix] isEqualToString:@"Jr."] &&
		      [[p nickname] isEqualToString:@"Ada"] &&
		      [[[p phoneticRepresentation] givenName] isEqualToString:@"AY-duh"],
		      [NSString stringWithFormat:@"given=[%@] family=[%@] phonetic=[%@]",
			 [p givenName], [p familyName], [[p phoneticRepresentation] givenName]]);
	}

	{
		NSMutableString *mutable = [NSMutableString string];
		NSPersonNameComponents *p = [[NSPersonNameComponents alloc] init];

		[mutable appendString:@"Ada"];
		[p setGivenName:mutable];
		/* THE POINT: the caller keeps its object and edits it. A retained (not copied) setter would
		 * show "AdaMUTATED" here. */
		[mutable appendString:@"MUTATED"];
		check("pnc-setter-copies",
		      [[p givenName] isEqualToString:@"Ada"],
		      fn_pair([p givenName], @"Ada"));
	}

	{
		NSPersonNameComponents *p = [[NSPersonNameComponents alloc] init];
		NSPersonNameComponents *phonetic = [[NSPersonNameComponents alloc] init];
		NSPersonNameComponents *copy;

		[p setGivenName:@"Ada"];
		[p setFamilyName:@"Lovelace"];
		[phonetic setGivenName:@"AY-duh"];
		[p setPhoneticRepresentation:phonetic];
		copy = [p copy];

		/* DIFFERENT OBJECTS at both levels, and the two diverge when one is edited: this is what
		 * "copied" has to mean for a mutable nested value. */
		[copy setGivenName:@"Grace"];
		[copy setPhoneticRepresentation:nil];
		check("pnc-copy-deep",
		      copy != p &&
		      [copy isKindOfClass:[NSPersonNameComponents class]] &&
		      /* THE COMPARISON IS GUARDED, and this check's OWN first run is why: both objects here
		       * had a nil familyName, and `[[copy familyName] isEqualToString:[p familyName]]` is NO for
		       * nil-vs-nil — a message to nil answers NO, so the clause was asking "are they equal"
		       * where it meant "are they the same". The fixture now SETS the family name, so the
		       * comparison is about a value, and the absent component is asserted as absent. */
		      [[copy familyName] isEqualToString:@"Lovelace"] &&
		      [[p familyName] isEqualToString:@"Lovelace"] &&
		      ([copy nickname] == nil) && ([p nickname] == nil) &&
		      [[p givenName] isEqualToString:@"Ada"] &&
		      [[copy givenName] isEqualToString:@"Grace"] &&
		      [[p phoneticRepresentation] givenName] != nil &&
		      [[[p phoneticRepresentation] givenName] isEqualToString:@"AY-duh"] &&
		      [copy phoneticRepresentation] == nil,
		      [NSString stringWithFormat:@"original.given=[%@] copy.given=[%@] original.phonetic=[%@] original.family=[%@]",
			 [p givenName], [copy givenName], [[p phoneticRepresentation] givenName], [p familyName]]);
	}

	{
		BOOL adopts = [NSPersonNameComponents conformsToProtocol:@protocol(NSSecureCoding)];
		BOOL answers = [NSPersonNameComponents supportsSecureCoding];
		BOOL alsoCoding = [NSPersonNameComponents conformsToProtocol:@protocol(NSCoding)];

		check("pnc-securecoding", adopts && answers && alsoCoding,
		      [NSString stringWithFormat:@"NSSecureCoding=%d supports=%d NSCoding=%d",
			 adopts, answers, alsoCoding]);
	}

	/* ---- NSListFormatter, on ICU's CLDR list patterns ---- */
	{
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSString *three;

		[fmt setLocale:en];
		three = [fmt stringFromItems:[NSArray arrayWithObjects:@"Alice", @"Bob", @"Charlie", nil]];
		check("list-join-en",
		      three != nil && [three isEqualToString:@"Alice, Bob, and Charlie"],
		      fn_pair(three, @"Alice, Bob, and Charlie"));
	}

	{
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSString *two;

		[fmt setLocale:en];
		two = [fmt stringFromItems:[NSArray arrayWithObjects:@"Alice", @"Bob", nil]];
		check("list-join-two",
		      two != nil && [two isEqualToString:@"Alice and Bob"],
		      fn_pair(two, @"Alice and Bob"));
	}

	{
		NSLocale *de = [NSLocale localeWithLocaleIdentifier:@"de_DE"];
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSString *german;

		[fmt setLocale:de];
		german = [fmt stringFromItems:[NSArray arrayWithObjects:@"Alice", @"Bob", @"Charlie", nil]];
		[fmt setLocale:en];
		/* The CONJUNCTION is the measurement: same items, same class, different locale. */
		check("list-join-de",
		      german != nil &&
		      [german rangeOfString:@"und"].location != NSNotFound &&
		      ![german isEqualToString:@"Alice, Bob, and Charlie"],
		      german == nil ? @"(nil)" : german);
	}

	{
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		FNItemFormatter *itemFmt = [[FNItemFormatter alloc] init];
		NSString *joined;
		NSNumber *one = [NSNumber numberWithInt:1];
		NSNumber *two = [NSNumber numberWithInt:2];

		[fmt setLocale:en];
		[fmt setItemFormatter:itemFmt];
		joined = [fmt stringFromItems:[NSArray arrayWithObjects:one, two, nil]];
		/* Each item went through the item formatter, so the numbers are rendered as "1!" and "2!"
		 * and the list's own conjunction is still the locale's. */
		check("list-item-formatter",
		      joined != nil &&
		      [joined isEqualToString:@"1! and 2!"],
		      fn_pair(joined, @"1! and 2!"));
	}

	{
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSString *empty = [fmt stringFromItems:[NSArray array]];

		check("list-empty", empty != nil && [empty length] == 0,
		      fn_pair(empty, @""));
	}

	{
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSArray *items = [NSArray arrayWithObjects:@"a", @"b", nil];
		NSString *viaObject;
		NSString *refused;
		NSString *itemsText;

		[fmt setLocale:en];
		viaObject = [fmt stringForObjectValue:items];
		refused = [fmt stringForObjectValue:@"not an array"];
		/* BOUND AND GUARDED, not passed inline: -isEqualToString:'s argument is a nonnull position and
		 * -stringFromItems: answers nullable, which the GUEST's -Werror=nullable-to-nonnull-conversion
		 * rejects (the host does not compile with that flag — §26's lesson, met again). */
		itemsText = [fmt stringFromItems:items];
		check("list-object-value",
		      viaObject != nil && itemsText != nil && [viaObject isEqualToString:itemsText] &&
		      refused == nil,
		      [NSString stringWithFormat:@"viaObject=[%@] refused=%@",
			 viaObject, refused == nil ? @"(nil)" : refused]);
	}

	{
		NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSListFormatter *fmt = [[NSListFormatter alloc] init];
		NSLocale *current;

		[fmt setLocale:en];
		[fmt setLocale:nil];		/* resettable: nil means "the current one" */
		current = [fmt locale];
		/* Compared by IDENTIFIER rather than by -isEqual:: what is under test is that the getter falls
		 * back to the current locale, and a locale's own equality is a different question (this class
		 * stores an identifier, and the current locale need not be the same OBJECT twice). */
		check("list-locale-resettable",
		      current != nil && [current localeIdentifier] != nil &&
		      [[current localeIdentifier] isEqualToString:[[NSLocale currentLocale] localeIdentifier]],
		      [NSString stringWithFormat:@"locale=[%@] current=[%@]",
			 [current localeIdentifier], [[NSLocale currentLocale] localeIdentifier]]);
	}

	/* ---- NSISO8601DateFormatter: options -> pattern, and the grammar is the specification ---- */
	{
		/* THE INPUT IS A LITERAL STRING and the round trip is the check: no epoch constant and no
		 * calendar arithmetic of mine is involved, so a failure here is the ISO 8601 formatter's and
		 * nothing else's. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *parsed = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *again = parsed != nil ? [iso stringFromDate:parsed] : nil;

		check("iso-round-trip",
		      parsed != nil && again != nil && [again isEqualToString:@"2026-09-20T12:34:56Z"],
		      fn_pair(again, @"2026-09-20T12:34:56Z"));
	}

	{
		/* THE DEFAULT IS InternetDateTime AND GMT, so a fresh formatter on the epoch prints the
		 * RFC 3339 shape with a trailing Z. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSString *text = [iso stringFromDate:[NSDate dateWithTimeIntervalSince1970:0]];

		check("iso-shape",
		      text != nil && [text isEqualToString:@"1970-01-01T00:00:00Z"],
		      fn_pair(text, @"1970-01-01T00:00:00Z"));
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *when = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *dateOnly;

		[iso setFormatOptions:NSISO8601DateFormatWithFullDate];
		dateOnly = when != nil ? [iso stringFromDate:when] : nil;
		check("iso-date-only",
		      dateOnly != nil && [dateOnly isEqualToString:@"2026-09-20"],
		      fn_pair(dateOnly, @"2026-09-20"));
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *when = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *fractional;

		[iso setFormatOptions:(NSISO8601DateFormatWithInternetDateTime |
				       NSISO8601DateFormatWithFractionalSeconds)];
		fractional = when != nil ? [iso stringFromDate:when] : nil;
		/* The MILLISECONDS are .000 because the parsed instant had none: what the option adds is the
		 * FIELD and its separator, which is what is asserted. */
		check("iso-fractional",
		      fractional != nil && [fractional isEqualToString:@"2026-09-20T12:34:56.000Z"],
		      fn_pair(fractional, @"2026-09-20T12:34:56.000Z"));
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *when = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *spaced;
		NSString *basic;

		[iso setFormatOptions:(NSISO8601DateFormatWithFullDate |
				       NSISO8601DateFormatWithTime |
				       NSISO8601DateFormatWithColonSeparatorInTime |
				       NSISO8601DateFormatWithSpaceBetweenDateAndTime)];
		spaced = when != nil ? [iso stringFromDate:when] : nil;
		/* NO ZONE OPTION, so this is not RFC 3339 — it is the same fields joined by a SPACE, which is
		 * the whole content of that option, and the reason it cannot be inferred from the others. */
		check("iso-space-separator",
		      spaced != nil && [spaced isEqualToString:@"2026-09-20 12:34:56"],
		      fn_pair(spaced, @"2026-09-20 12:34:56"));

		/* AND THE COLONS ARE THEIR OWN OPTION. THIS CHECK EXISTS BECAUSE THE FIRST RUN OF THE ONE
		 * ABOVE FAILED AND WAS RIGHT TO: it omitted WithColonSeparatorInTime and got
		 * "2026-09-20 123456". That is the ISO 8601 BASIC format, which is what the options asked
		 * for — the expectation was mine and the library was correct, so the measurement became a
		 * check instead of an edit to the library. */
		[iso setFormatOptions:(NSISO8601DateFormatWithFullDate |
				       NSISO8601DateFormatWithTime |
				       NSISO8601DateFormatWithSpaceBetweenDateAndTime)];
		basic = when != nil ? [iso stringFromDate:when] : nil;
		check("iso-basic-format",
		      basic != nil && [basic isEqualToString:@"2026-09-20 123456"],
		      fn_pair(basic, @"2026-09-20 123456"));
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *when = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *offset;
		NSTimeZone *plus0530 = [NSTimeZone timeZoneForSecondsFromGMT:(5 * 3600 + 30 * 60)];

		[iso setTimeZone:plus0530];
		offset = when != nil ? [iso stringFromDate:when] : nil;
		/* THE SAME INSTANT IN A DIFFERENT ZONE: 12:34:56Z is 18:04:56+05:30, and the offset carries a
		 * colon because WithInternetDateTime includes the colon-separator option. */
		check("iso-offset-zone",
		      offset != nil && [offset isEqualToString:@"2026-09-20T18:04:56+05:30"],
		      fn_pair(offset, @"2026-09-20T18:04:56+05:30"));
	}

	{
		/* THE COMPOSITES ARE ORs OF THEIR MEMBERS — asserted, because the bitmask's SHAPE is ours
		 * (D2) and a composite that stopped being an OR would still be *a* value. */
		NSISO8601DateFormatOptions fullDate = NSISO8601DateFormatWithFullDate;
		NSISO8601DateFormatOptions fullTime = NSISO8601DateFormatWithFullTime;
		NSISO8601DateFormatOptions internet = NSISO8601DateFormatWithInternetDateTime;

		check("iso-options-compose",
		      (fullDate & NSISO8601DateFormatWithYear) != 0 &&
		      (fullDate & NSISO8601DateFormatWithMonth) != 0 &&
		      (fullDate & NSISO8601DateFormatWithDay) != 0 &&
		      (fullDate & NSISO8601DateFormatWithDashSeparatorInDate) != 0 &&
		      (fullTime & NSISO8601DateFormatWithTime) != 0 &&
		      (fullTime & NSISO8601DateFormatWithTimeZone) != 0 &&
		      internet == (fullDate | fullTime) &&
		      (internet & NSISO8601DateFormatWithFractionalSeconds) == 0,
		      [NSString stringWithFormat:@"fullDate=%lx fullTime=%lx internet=%lx",
			 (unsigned long)fullDate, (unsigned long)fullTime, (unsigned long)internet]);
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSISO8601DateFormatter *noFields = [[NSISO8601DateFormatter alloc] init];

		[noFields setFormatOptions:0];
		check("iso-refusals",
		      [iso dateFromString:@"not a date"] == nil &&
		      [iso dateFromString:@"1970"] == nil &&
		      /* TRAILING JUNK IS REFUSED: a prefix parse would answer a date the caller never wrote
		       * ("2026-09-20 and then some"), which is why the parser requires that it consumed all. */
		      [iso dateFromString:@"2026-09-20T12:34:56Zjunk"] == nil &&
		      [noFields stringFromDate:[NSDate dateWithTimeIntervalSince1970:0]] == nil,
		      @"unparseable text, a bare year, trailing junk and zero fields all answer nil");
	}

	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *when = [iso dateFromString:@"2026-09-20T12:34:56Z"];
		NSString *viaClass = [NSISO8601DateFormatter stringFromDate:when
								  timeZone:nil
							     formatOptions:NSISO8601DateFormatWithFullDate];
		NSISO8601DateFormatter *copy = [iso copy];
		NSString *viaCopy = copy != nil ? [copy stringFromDate:when] : nil;
		NSString *plain = [iso stringFromDate:when];

		/* `plain` is BOUND so a nonnull argument position is not fed a nullable expression: the guest's
		 * -Werror=nullable-to-nonnull-conversion rejects the inline form. */
		check("iso-class-and-copy",
		      when != nil && plain != nil &&
		      viaClass != nil && [viaClass isEqualToString:@"2026-09-20"] &&
		      viaCopy != nil && [viaCopy isEqualToString:plain],
		      [NSString stringWithFormat:@"viaClass=[%@] viaCopy=[%@]", viaClass, viaCopy]);
	}

	/* ---- NSDateIntervalFormatter: two dates become ONE range ---- */
	{
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *start = [iso dateFromString:@"2015-01-16T12:00:00Z"];
		NSDate *end = [iso dateFromString:@"2015-01-17T12:00:00Z"];
		NSDateIntervalFormatter *dif = [[NSDateIntervalFormatter alloc] init];
		NSString *range;

		[dif setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		[dif setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
		[dif setDateStyle:NSDateIntervalFormatterShortStyle];
		range = [dif stringFromDate:start toDate:end];
		/* APPLE'S OWN EXAMPLE on the class page is "1/16/15 - 1/17/15", and THE MEASUREMENT DISAGREES
		 * WITH IT IN TWO WAYS THAT ARE BOTH CORRECT: under this ICU's CLDR the short date is
		 * "1/16/2015" and the separator is an EN DASH rather than a hyphen. Apple's prose is a decade
		 * old and CLDR's patterns have moved since, so the expectation is the LOCALE DATA's — which is
		 * what this class binds to — and the assertion is that BOTH endpoints appear, once each. */
		check("dif-range",
		      range != nil &&
		      fn_count(range, @"1/16/2015") == 1 &&
		      fn_count(range, @"1/17/2015") == 1,
		      range == nil ? @"(nil)" : range);
	}

	{
		/* THE POINT OF THE CLASS: on ONE day the date is printed ONCE, not twice. Two NSDateFormatter
		 * calls joined by a hyphen would say the date twice; this is what makes the class worth
		 * having, and it is CLDR's interval pattern doing it. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *from = [iso dateFromString:@"2015-01-16T12:00:00Z"];
		NSDate *to = [iso dateFromString:@"2015-01-16T13:00:00Z"];
		NSDateIntervalFormatter *dif = [[NSDateIntervalFormatter alloc] init];
		NSString *range;

		[dif setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		[dif setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
		[dif setDateStyle:NSDateIntervalFormatterShortStyle];
		[dif setTimeStyle:NSDateIntervalFormatterShortStyle];
		range = [dif stringFromDate:from toDate:to];
		check("dif-collapse",
		      range != nil && fn_count(range, @"1/16/2015") == 1 && [range length] > 7 &&
		      /* AND THE CLOCK IS THE LOCALE'S: en_US is a 12-hour locale, so "PM" appearing is the
		       * measurement that caught this file's first version asking ICU for "H" (hour 0-23) and
		       * thereby forcing a 24-hour clock on every locale. One assertion, one real bug. */
		      [range rangeOfString:@"PM"].location != NSNotFound,
		      range == nil ? @"(nil)" : range);
	}

	{
		/* A TEMPLATE REPLACES BOTH STYLES: the fields are named, the locale arranges them. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *start = [iso dateFromString:@"2015-01-16T12:00:00Z"];
		NSDate *end = [iso dateFromString:@"2015-01-17T12:00:00Z"];
		NSDateIntervalFormatter *dif = [[NSDateIntervalFormatter alloc] init];
		NSString *english;
		NSString *german;

		[dif setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
		[dif setDateTemplate:@"MMMMd"];
		[dif setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		english = [dif stringFromDate:start toDate:end];
		[dif setLocale:[NSLocale localeWithLocaleIdentifier:@"de_DE"]];
		german = [dif stringFromDate:start toDate:end];
		/* SAME FIELDS, TWO LOCALES: the month's NAME is the locale's, the same claim
		 * foundation_dateformatter's "März"/"March" check makes one class over. */
		check("dif-template-locale",
		      english != nil && [english rangeOfString:@"January 16"].location != NSNotFound &&
		      german != nil && [german rangeOfString:@"Januar"].location != NSNotFound,
		      [NSString stringWithFormat:@"en=[%@] de=[%@]",
			 english == nil ? @"(nil)" : english, german == nil ? @"(nil)" : german]);
	}

	{
		/* NOTHING CONFIGURED ANSWERS NIL — our documented choice where Apple publishes no default, and
		 * the same decision NSISO8601DateFormatter makes for zero options. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *start = [iso dateFromString:@"2015-01-16T12:00:00Z"];
		NSDate *end = [iso dateFromString:@"2015-01-17T12:00:00Z"];
		NSDateIntervalFormatter *bare = [[NSDateIntervalFormatter alloc] init];
		NSDateIntervalFormatter *de = [[NSDateIntervalFormatter alloc] init];
		NSString *wanted;
		NSString *viaObject;
		NSDateInterval *interval;

		check("dif-no-style",
		      [bare stringFromDate:start toDate:end] == nil &&
		      [bare dateStyle] == NSDateIntervalFormatterNoStyle &&
		      [bare timeStyle] == NSDateIntervalFormatterNoStyle,
		      @"a formatter with no style and no template has no field to render");

		/* THE TWO DOORS ARE ONE DOOR: the interval object must produce what the date pair does. */
		[de setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		[de setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
		[de setDateStyle:NSDateIntervalFormatterShortStyle];
		wanted = [de stringFromDate:start toDate:end];
		interval = [[NSDateInterval alloc] initWithStartDate:start endDate:end];
		viaObject = [de stringFromDateInterval:interval];
		check("dif-interval-object",
		      wanted != nil && viaObject != nil && [viaObject isEqualToString:wanted],
		      [NSString stringWithFormat:@"pair=[%@] object=[%@]",
			 wanted == nil ? @"(nil)" : wanted, viaObject == nil ? @"(nil)" : viaObject]);

		/* THE RESETTABLE PROPERTIES: nil goes back to the DEFAULT, so the getters keep answering a
		 * value rather than nil. */
		[de setLocale:nil];
		[de setCalendar:nil];
		[de setTimeZone:nil];
		check("dif-resettable",
		      [de locale] != nil && [de calendar] != nil && [de timeZone] != nil &&
		      [[[de locale] localeIdentifier] isEqualToString:
			  [[NSLocale currentLocale] localeIdentifier]],
		      [NSString stringWithFormat:@"locale=[%@]", [[de locale] localeIdentifier]]);

		/* THE TEMPLATE IS COPIED: a caller that edits its own mutable string afterwards must not
		 * change what the formatter renders. */
		{
			NSMutableString *mutable = [NSMutableString string];
			NSString *before;
			NSString *after;

			[mutable appendString:@"yyyyMMdd"];
			[de setDateTemplate:mutable];
			before = [de stringFromDate:start toDate:end];
			[mutable appendString:@"HHmm"];
			after = [de stringFromDate:start toDate:end];
			check("dif-template-copies",
			      before != nil && after != nil && [after isEqualToString:before],
			      [NSString stringWithFormat:@"before=[%@] after=[%@]",
				 before == nil ? @"(nil)" : before, after == nil ? @"(nil)" : after]);
		}
	}

	/* ---- NSByteCountFormatter: the ONE class in W11 whose arithmetic is ours ---- */
	{
		/* THE TWO STYLES ON THE SAME INPUT is the sharpest assertion available, and it is Apple's own
		 * documented rule: "Decimal: 1000 bytes → 1 KB", "Binary: 1024 bytes → 1 KB", with File and
		 * Memory being those two by Apple's own statement for the platform-specific pair. */
		NSString *decimal = [NSByteCountFormatter stringFromByteCount:1000
								 countStyle:NSByteCountFormatterCountStyleDecimal];
		NSString *binary = [NSByteCountFormatter stringFromByteCount:1024
								countStyle:NSByteCountFormatterCountStyleBinary];
		NSString *binary1000 = [NSByteCountFormatter stringFromByteCount:1000
								    countStyle:NSByteCountFormatterCountStyleBinary];
		NSString *file = [NSByteCountFormatter stringFromByteCount:1000
							      countStyle:NSByteCountFormatterCountStyleFile];
		NSString *memory = [NSByteCountFormatter stringFromByteCount:1024
								countStyle:NSByteCountFormatterCountStyleMemory];

		check("bcf-count-styles",
		      decimal != nil && [decimal isEqualToString:@"1 KB"] &&
		      binary != nil && [binary isEqualToString:@"1 KB"] &&
		      file != nil && [file isEqualToString:@"1 KB"] &&
		      memory != nil && [memory isEqualToString:@"1 KB"] &&
		      /* 1000 bytes is 1 KB in DECIMAL and still 1000 whole bytes in BINARY: the same input,
		       * two answers, which is what the style MEANS. */
		      binary1000 != nil && [binary1000 isEqualToString:@"1000 bytes"],
		      [NSString stringWithFormat:@"decimal(1000)=[%@] binary(1024)=[%@] binary(1000)=[%@] "
			 "file(1000)=[%@] memory(1024)=[%@]",
			 decimal == nil ? @"(nil)" : decimal, binary == nil ? @"(nil)" : binary,
			 binary1000 == nil ? @"(nil)" : binary1000, file == nil ? @"(nil)" : file,
			 memory == nil ? @"(nil)" : memory]);
	}

	{
		/* THE MAGNITUDE WALK, AND THE ONE FRACTION DIGIT. */
		NSString *oneKB = [NSByteCountFormatter stringFromByteCount:1100
								 countStyle:NSByteCountFormatterCountStyleFile];
		NSString *oneMB = [NSByteCountFormatter stringFromByteCount:1234567
								 countStyle:NSByteCountFormatterCountStyleFile];
		NSString *binaryMB = [NSByteCountFormatter stringFromByteCount:1048576
								    countStyle:NSByteCountFormatterCountStyleBinary];
		NSString *singular = [NSByteCountFormatter stringFromByteCount:1
								    countStyle:NSByteCountFormatterCountStyleFile];

		check("bcf-magnitude",
		      oneKB != nil && [oneKB isEqualToString:@"1.1 KB"] &&
		      oneMB != nil && [oneMB isEqualToString:@"1.2 MB"] &&
		      binaryMB != nil && [binaryMB isEqualToString:@"1 MB"] &&
		      /* ONE BYTE IS "byte", AND ENGLISH IS THE ONLY REASON. */
		      singular != nil && [singular isEqualToString:@"1 byte"],
		      [NSString stringWithFormat:@"1100=[%@] 1234567=[%@] 1048576bin=[%@] 1=[%@]",
			 oneKB == nil ? @"(nil)" : oneKB, oneMB == nil ? @"(nil)" : oneMB,
			 binaryMB == nil ? @"(nil)" : binaryMB, singular == nil ? @"(nil)" : singular]);
	}

	{
		/* THE UNITS MASK GATES the choice: "Specifying any units explicitly causes just those units to
		 * be used in showing the number." */
		NSByteCountFormatter *bcf = [[NSByteCountFormatter alloc] init];
		NSString *masked;
		NSString *noUnit;
		NSString *noCount;

		[bcf setCountStyle:NSByteCountFormatterCountStyleFile];
		[bcf setAllowedUnits:NSByteCountFormatterUseMB];
		masked = [bcf stringFromByteCount:2000000];
		[bcf setIncludesUnit:NO];
		noUnit = [bcf stringFromByteCount:2000000];
		[bcf setIncludesUnit:YES];
		[bcf setIncludesCount:NO];
		noCount = [bcf stringFromByteCount:2000000];
		check("bcf-units-mask",
		      masked != nil && [masked isEqualToString:@"2 MB"] &&
		      noUnit != nil && [noUnit isEqualToString:@"2"] &&
		      noCount != nil && [noCount isEqualToString:@"MB"],
		      [NSString stringWithFormat:@"masked=[%@] noUnit=[%@] noCount=[%@]",
			 masked == nil ? @"(nil)" : masked, noUnit == nil ? @"(nil)" : noUnit,
			 noCount == nil ? @"(nil)" : noCount]);
	}

	{
		NSByteCountFormatter *bcf = [[NSByteCountFormatter alloc] init];
		NSString *plain;
		NSString *padded;

		[bcf setCountStyle:NSByteCountFormatterCountStyleFile];
		plain = [bcf stringFromByteCount:1000000];
		[bcf setZeroPadsFractionDigits:YES];
		padded = [bcf stringFromByteCount:1000000];
		/* THE OPTION'S WHOLE CONTENT IS THE ".0", which is why it is asserted as a PAIR: the same
		 * number with and without the padding. */
		check("bcf-zeropad",
		      plain != nil && [plain isEqualToString:@"1 MB"] &&
		      padded != nil && [padded isEqualToString:@"1.0 MB"],
		      [NSString stringWithFormat:@"plain=[%@] padded=[%@]",
			 plain == nil ? @"(nil)" : plain, padded == nil ? @"(nil)" : padded]);
	}

	{
		NSByteCountFormatter *bcf = [[NSByteCountFormatter alloc] init];
		NSString *natural;
		NSString *literal;

		[bcf setCountStyle:NSByteCountFormatterCountStyleFile];
		natural = [bcf stringFromByteCount:0];
		[bcf setAllowsNonnumericFormatting:NO];
		literal = [bcf stringFromByteCount:0];
		/* APPLE'S PHRASE IS "Zero KB" — see the implementation's note on why the unit is CHOSEN rather
		 * than computed for a zero count. */
		check("bcf-nonnumeric",
		      natural != nil && [natural isEqualToString:@"Zero KB"] &&
		      literal != nil && [literal isEqualToString:@"0 bytes"],
		      [NSString stringWithFormat:@"natural=[%@] literal=[%@]",
			 natural == nil ? @"(nil)" : natural, literal == nil ? @"(nil)" : literal]);
	}

	{
		/* ADAPTIVE, the one property whose behaviour Apple does not publish: our reading (stated in the
		 * header) is that it lets the MAGNITUDE win over the mask. */
		NSByteCountFormatter *bcf = [[NSByteCountFormatter alloc] init];
		NSString *gated;
		NSString *adapted;

		[bcf setCountStyle:NSByteCountFormatterCountStyleFile];
		[bcf setAllowedUnits:NSByteCountFormatterUseBytes];
		gated = [bcf stringFromByteCount:5000];
		[bcf setAdaptive:YES];
		adapted = [bcf stringFromByteCount:5000];
		check("bcf-adaptive",
		      gated != nil && [gated isEqualToString:@"5000 bytes"] &&
		      adapted != nil && [adapted isEqualToString:@"5 KB"] &&
		      [bcf isAdaptive],
		      [NSString stringWithFormat:@"gated=[%@] adapted=[%@]",
			 gated == nil ? @"(nil)" : gated, adapted == nil ? @"(nil)" : adapted]);
	}

	{
		/* NSFormatter's door: an NSNumber IS a byte count; anything else is not our kind of value. */
		NSByteCountFormatter *bcf = [[NSByteCountFormatter alloc] init];
		NSNumber *number = [NSNumber numberWithLongLong:1000];
		NSString *viaObject;
		NSString *refused;
		NSString *reference;

		[bcf setCountStyle:NSByteCountFormatterCountStyleFile];
		viaObject = [bcf stringForObjectValue:number];
		refused = [bcf stringForObjectValue:@"1000"];
		/* BOUND, for the same nonnull-argument reason the other two sites record. */
		reference = [bcf stringFromByteCount:1000];
		check("bcf-object-value",
		      viaObject != nil && reference != nil && [viaObject isEqualToString:reference] &&
		      viaObject != nil && [viaObject isEqualToString:@"1 KB"] &&
		      refused == nil,
		      [NSString stringWithFormat:@"viaObject=[%@] refused=%@",
			 viaObject == nil ? @"(nil)" : viaObject, refused == nil ? @"(nil)" : refused]);
	}

	{
		/*
		 * THE W12 WORK ITEM, ASSERTED AS AN ABSENCE rather than described as a boundary (§11's rule, and
		 * §11.2's lesson that an absence assertion is a fact about the TREE). The two
		 * NSMeasurement-taking members are the only W11 surface this unit does not land: NSMeasurement
		 * and the NSUnit* family are W12's, so those rows stay ledger-open and this check names them —
		 * which is what keeps the excluded list a to-do instead of a claim.
		 */
		BOOL classLevel = [NSByteCountFormatter
			respondsToSelector:sel_registerName("stringFromMeasurement:countStyle:")];
		BOOL instanceLevel = [NSByteCountFormatter
			respondsToSelector:sel_registerName("stringFromMeasurement:")];
		BOOL measurementAbsent = objc_getClass("NSMeasurement") == NULL;

		check("bcf-measurement-deferred",
		      !classLevel && !instanceLevel && measurementAbsent,
		      [NSString stringWithFormat:@"+stringFromMeasurement:countStyle:=%d "
			 "-stringFromMeasurement:=%d NSMeasurement absent=%d (want 0/0/1 until W12)",
			 classLevel, instanceLevel, measurementAbsent]);
	}

	/* ---- NSRelativeDateTimeFormatter: ICU names the span, we choose WHICH span ---- */
	{
		/* The reference dates come from NSISO8601DateFormatter, so no epoch arithmetic of mine is in play
		 * and the SPAN is exact. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *now = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *past = [iso dateFromString:@"2026-09-19T12:00:00Z"];
		NSDate *future = [iso dateFromString:@"2026-09-21T12:00:00Z"];
		NSDate *later = [iso dateFromString:@"2026-09-20T15:00:00Z"];
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *ago;
		NSString *ahead;
		NSString *hours;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		ago = [rdf localizedStringForDate:past relativeToDate:now];
		ahead = [rdf localizedStringForDate:future relativeToDate:now];
		hours = [rdf localizedStringForDate:later relativeToDate:now];
		/* THE UNIT CHOICE IS OURS (the header states the rule): a one-day span is a DAY and not 24
		 * hours, which IS the difference between this class and a duration formatter. */
		check("rdf-numeric",
		      ago != nil && [ago isEqualToString:@"1 day ago"] &&
		      ahead != nil && [ahead isEqualToString:@"in 1 day"] &&
		      hours != nil && [hours isEqualToString:@"in 3 hours"],
		      [NSString stringWithFormat:@"past=[%@] future=[%@] later=[%@]",
			 ago == nil ? @"(nil)" : ago, ahead == nil ? @"(nil)" : ahead,
			 hours == nil ? @"(nil)" : hours]);
	}

	{
		/* NAMED vs NUMERIC — Apple's two styles, quoted in the header: the same span, an idiom or
		 * arithmetic. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *now = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *past = [iso dateFromString:@"2026-09-19T12:00:00Z"];
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *numeric;
		NSString *named;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		numeric = [rdf localizedStringForDate:past relativeToDate:now];
		[rdf setDateTimeStyle:NSRelativeDateTimeFormatterStyleNamed];
		named = [rdf localizedStringForDate:past relativeToDate:now];
		check("rdf-named",
		      numeric != nil && [numeric isEqualToString:@"1 day ago"] &&
		      named != nil && [named isEqualToString:@"yesterday"] &&
		      [rdf dateTimeStyle] == NSRelativeDateTimeFormatterStyleNamed,
		      [NSString stringWithFormat:@"numeric=[%@] named=[%@]",
			 numeric == nil ? @"(nil)" : numeric, named == nil ? @"(nil)" : named]);
	}

	{
		/* THE UNITS STYLE IS THE NUMBER'S SPELLING, and SpellOut is what ICU's ADOPT parameter is for. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *now = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *twoMonths = [iso dateFromString:@"2026-07-20T12:00:00Z"];
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *full;
		NSString *spelled;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		full = [rdf localizedStringForDate:twoMonths relativeToDate:now];
		[rdf setUnitsStyle:NSRelativeDateTimeFormatterUnitsStyleSpellOut];
		spelled = [rdf localizedStringForDate:twoMonths relativeToDate:now];
		/* A TWO-MONTH SPAN IS TWO MONTHS and not 61 days: the difference is the shipped NSCalendar's. */
		check("rdf-spellout",
		      full != nil && [full isEqualToString:@"2 months ago"] &&
		      spelled != nil && [spelled isEqualToString:@"two months ago"],
		      [NSString stringWithFormat:@"full=[%@] spelled=[%@]",
			 full == nil ? @"(nil)" : full, spelled == nil ? @"(nil)" : spelled]);
	}

	{
		/* THE OTHER TWO DOORS: a time interval, and a components bag. */
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSDateComponents *days = [[NSDateComponents alloc] init];
		NSDateComponents *weeks = [[NSDateComponents alloc] init];
		NSString *interval;
		NSString *byDays;
		NSString *byWeeks;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		interval = [rdf localizedStringFromTimeInterval:-3600.0];
		[days setDay:-3];
		byDays = [rdf localizedStringFromDateComponents:days];
		[weeks setWeekOfYear:-2];
		byWeeks = [rdf localizedStringFromDateComponents:weeks];
		/* WEEK IS REACHABLE THROUGH THIS DOOR AND NOT THROUGH THE DATE ONE — the deliberate asymmetry
		 * the implementation's comment records. */
		check("rdf-interval-and-components",
		      interval != nil && [interval isEqualToString:@"1 hour ago"] &&
		      byDays != nil && [byDays isEqualToString:@"3 days ago"] &&
		      byWeeks != nil && [byWeeks isEqualToString:@"2 weeks ago"],
		      [NSString stringWithFormat:@"-3600=[%@] day:-3=[%@] week:-2=[%@]",
			 interval == nil ? @"(nil)" : interval, byDays == nil ? @"(nil)" : byDays,
			 byWeeks == nil ? @"(nil)" : byWeeks]);
	}

	{
		/* WHERE THE TEXT APPEARS IS WHAT CAPITALISES IT — ICU's capitalization context, which is what
		 * NSFormattingContext is FOR. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *now = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *past = [iso dateFromString:@"2026-09-19T12:00:00Z"];
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *plain;
		NSString *start;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		[rdf setDateTimeStyle:NSRelativeDateTimeFormatterStyleNamed];
		plain = [rdf localizedStringForDate:past relativeToDate:now];
		[rdf setFormattingContext:NSFormattingContextBeginningOfSentence];
		start = [rdf localizedStringForDate:past relativeToDate:now];
		check("rdf-capitalization",
		      plain != nil && [plain isEqualToString:@"yesterday"] &&
		      start != nil && [start isEqualToString:@"Yesterday"],
		      [NSString stringWithFormat:@"plain=[%@] start=[%@]",
			 plain == nil ? @"(nil)" : plain, start == nil ? @"(nil)" : start]);
	}

	{
		/* THE LOCALE IS THE DATA: the same span in German. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *now = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *past = [iso dateFromString:@"2026-09-19T12:00:00Z"];
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *german;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"de_DE"]];
		[rdf setDateTimeStyle:NSRelativeDateTimeFormatterStyleNamed];
		german = [rdf localizedStringForDate:past relativeToDate:now];
		check("rdf-locale", german != nil && [german isEqualToString:@"gestern"],
		      german == nil ? @"(nil)" : german);
	}

	{
		/* NSFormatter's door: a DATE is formatted relative to NOW; anything else is not our kind. */
		NSRelativeDateTimeFormatter *rdf = [[NSRelativeDateTimeFormatter alloc] init];
		NSString *viaObject;
		NSString *refused;

		[rdf setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US"]];
		viaObject = [rdf stringForObjectValue:[NSDate date]];
		refused = [rdf stringForObjectValue:@"yesterday"];
		check("rdf-object-value",
		      viaObject != nil && [viaObject length] > 0 && refused == nil,
		      [NSString stringWithFormat:@"viaObject=[%@] refused=%@",
			 viaObject == nil ? @"(nil)" : viaObject, refused == nil ? @"(nil)" : refused]);
	}

	/* ---- NSDateComponentsFormatter: OUR RULES over ICU'S DATA (W11b, plan §29/§29.1) ---- */
	{
		/* THE SIX STYLES, against Apple's own published semantics for each case: Full "spells out the
		 * UNITS but not the quantities", SpellOut "the units AND quantities", Short "a shortened
		 * spelling", Brief "shorter than Short", Abbreviated "the most abbreviated", and Positional
		 * "uses the POSITION of a unit … to identify its value". */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *full, *shortForm, *brief, *abbrev, *spelled, *positional;

		[c setHour:2];
		[c setMinute:3];
		[dcf setAllowedUnits:(NSCalendarUnitHour | NSCalendarUnitMinute)];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropAll];
		full = [dcf stringFromDateComponents:c];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleShort];
		shortForm = [dcf stringFromDateComponents:c];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleBrief];
		brief = [dcf stringFromDateComponents:c];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleAbbreviated];
		abbrev = [dcf stringFromDateComponents:c];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleSpellOut];
		spelled = [dcf stringFromDateComponents:c];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStylePositional];
		positional = [dcf stringFromDateComponents:c];

		/* THE ASSERTIONS SPLIT BY WHAT IS STABLE, because the HOST AND THE GUEST LINK DIFFERENT ICU
		 * BUILDS: the host tier links the system ICU through pkg-config, while the guest runs the pinned
		 * ICU 76.1 in .build/icu-prefix. A FULL unit name and a spelled number are stable across those
		 * ("2 hours", "two"); a SHORT or NARROW spelling is ICU's data and may be "2 hr" or "2 hrs"
		 * depending on the build. So the styles whose semantics are the whole claim are asserted exactly,
		 * and the abbreviated ones are asserted STRUCTURALLY — including the one thing about them that is
		 * ours and therefore stable: the SPACE joiner. */
		check("dcf-styles",
		      full != nil && [full isEqualToString:@"2 hours, 3 minutes"] &&
		      spelled != nil && [spelled isEqualToString:@"two hours, three minutes"] &&
		      positional != nil && [positional isEqualToString:@"2:03"] &&
		      shortForm != nil &&
		      [shortForm rangeOfString:@"2"].location != NSNotFound &&
		      [shortForm rangeOfString:@"3"].location != NSNotFound &&
		      [shortForm rangeOfString:@"hr"].location != NSNotFound &&
		      [shortForm rangeOfString:@"min"].location != NSNotFound &&
		      brief != nil && abbrev != nil && [brief isEqualToString:abbrev] &&
		      /* THE ABBREVIATED STYLES JOIN WITH A SPACE, not a comma: Apple's own worked examples give
		       * "1h 0m 30s". This is where that was measured. */
		      [brief rangeOfString:@","].location == NSNotFound &&
		      [brief rangeOfString:@" "].location != NSNotFound &&
		      [brief rangeOfString:@"2h"].location != NSNotFound &&
		      [brief rangeOfString:@"3"].location != NSNotFound,
		      [NSString stringWithFormat:@"full=[%@] short=[%@] brief=[%@] abbrev=[%@] spelled=[%@] "
			 "positional=[%@]",
			 full == nil ? @"(nil)" : full, shortForm == nil ? @"(nil)" : shortForm,
			 brief == nil ? @"(nil)" : brief, abbrev == nil ? @"(nil)" : abbrev,
			 spelled == nil ? @"(nil)" : spelled, positional == nil ? @"(nil)" : positional]);
	}

	{
		/* PLURALITY COMES WITH THE DATA: "1 hour" and "2 hours" are ICU's, not an "s" appended here. */
		NSDateComponents *one = [[NSDateComponents alloc] init];
		NSDateComponents *two = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *singular, *plural;

		[one setHour:1];
		[two setHour:2];
		[dcf setAllowedUnits:NSCalendarUnitHour];
		singular = [dcf stringFromDateComponents:one];
		plural = [dcf stringFromDateComponents:two];
		check("dcf-plural",
		      singular != nil && [singular isEqualToString:@"1 hour"] &&
		      plural != nil && [plural isEqualToString:@"2 hours"],
		      [NSString stringWithFormat:@"1=[%@] 2=[%@]",
			 singular == nil ? @"(nil)" : singular, plural == nil ? @"(nil)" : plural]);
	}

	{
		/*
		 * THE SEVEN ZERO BEHAVIOURS, against Apple's own definitions and worked examples. The fixture is
		 * Apple's shape — days, hours, minutes and seconds allowed, a zero at each end and a real value in
		 * the middle — and it is expressed in the FULL style, whose unit names are stable across the two
		 * ICU builds the host and the guest link ("0 days, 1 hour, 0 minutes, 30 seconds"); the
		 * ABBREVIATED spellings are ICU data that differs between those builds, so they are not what this
		 * check asserts. (The abbreviated form's SPACE joiner is asserted separately, in dcf-styles.)
		 */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *none, *leading, *middle, *trailing, *all, *deflt;

		[c setDay:0];
		[c setHour:1];
		[c setMinute:0];
		[c setSecond:30];
		[dcf setAllowedUnits:(NSCalendarUnitDay | NSCalendarUnitHour |
				      NSCalendarUnitMinute | NSCalendarUnitSecond)];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleFull];

		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorNone];
		none = [dcf stringFromDateComponents:c];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropLeading];
		leading = [dcf stringFromDateComponents:c];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropMiddle];
		middle = [dcf stringFromDateComponents:c];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropTrailing];
		trailing = [dcf stringFromDateComponents:c];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropAll];
		all = [dcf stringFromDateComponents:c];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDefault];
		deflt = [dcf stringFromDateComponents:c];

		check("dcf-zero-behaviours",
		      /* None: "prevents the dropping of zero values or adding of zeroes for padding". */
		      none != nil && [none isEqualToString:@"0 days, 1 hour, 0 minutes, 30 seconds"] &&
		      /* DropLeading: "units whose values are 0 are dropped starting at the beginning of the
		       * sequence … until a non-zero value is encountered" — the MIDDLE zero STAYS. THIS IS THE
		       * CHECK THAT CAUGHT THE DropAll-MASK BUG, where all four behaviours dropped everything. */
		      leading != nil && [leading isEqualToString:@"1 hour, 0 minutes, 30 seconds"] &&
		      /* DropMiddle: "dropped from anywhere in the middle of a sequence" — the ends stay. */
		      middle != nil && [middle isEqualToString:@"0 days, 1 hour, 30 seconds"] &&
		      /* DropTrailing: nothing here is trailing-zero (30 seconds is last), so this equals None. */
		      trailing != nil && [trailing isEqualToString:@"0 days, 1 hour, 0 minutes, 30 seconds"] &&
		      /* DropAll: "drops all units whose values are 0" — Apple's own example is one hour shown as
		       * "1h"; here one hour and thirty seconds. */
		      all != nil && [all isEqualToString:@"1 hour, 30 seconds"] &&
		      /* Default: OUR reading for the non-positional styles — Apple's sentence describes only the
		       * positional case (drop leading, pad middle and trailing). */
		      deflt != nil && [deflt isEqualToString:@"1 hour, 0 minutes, 30 seconds"],
		      [NSString stringWithFormat:@"none=[%@] lead=[%@] middle=[%@] trail=[%@] all=[%@] "
			 "default=[%@]",
			 none == nil ? @"(nil)" : none, leading == nil ? @"(nil)" : leading,
			 middle == nil ? @"(nil)" : middle, trailing == nil ? @"(nil)" : trailing,
			 all == nil ? @"(nil)" : all, deflt == nil ? @"(nil)" : deflt]);
	}

	{
		/* DEFAULT IN THE POSITIONAL FORM IS DIFFERENT, AND APPLE SAYS SO: it "drops leading zeroes but
		 * PADS middle and trailing values with zeros as needed", so one hour is "1:00:00" and not "1".
		 * Asserted as its own check because the two Default readings are the fork the header records. */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *clock;

		[c setDay:0];
		[c setHour:1];
		[c setMinute:0];
		[c setSecond:0];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStylePositional];
		[dcf setAllowedUnits:(NSCalendarUnitDay | NSCalendarUnitHour |
				      NSCalendarUnitMinute | NSCalendarUnitSecond)];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDefault];
		clock = [dcf stringFromDateComponents:c];
		check("dcf-positional-default",
		      clock != nil && [clock isEqualToString:@"1:00:00"],
		      clock == nil ? @"(nil)" : clock);
	}

	{
		/* THE POSITIONAL FORM IS OURS (ICU's numeric duration skeleton is a syntax error here), and
		 * Apple's own doc example shape is 1 hour 3 minutes 37 seconds -> "1:03:37". */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *clock;

		[c setHour:1];
		[c setMinute:3];
		[c setSecond:37];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStylePositional];
		[dcf setAllowedUnits:(NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropAll];
		clock = [dcf stringFromDateComponents:c];
		check("dcf-positional-clock",
		      clock != nil && [clock isEqualToString:@"1:03:37"],
		      clock == nil ? @"(nil)" : clock);
	}

	{
		/* THE UNIT RULES: maximumUnitCount limits what is DISPLAYED, and loses the SMALLER units. */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *limited;
		NSString *collapsed;

		[c setDay:2];
		[c setHour:3];
		[c setMinute:4];
		[c setSecond:5];
		[dcf setAllowedUnits:(NSCalendarUnitDay | NSCalendarUnitHour |
				      NSCalendarUnitMinute | NSCalendarUnitSecond)];
		[dcf setMaximumUnitCount:2];
		limited = [dcf stringFromDateComponents:c];

		/* COLLAPSING: Apple publishes the effect ("collapse the largest unit into smaller units") and not
		 * the threshold, so this is the reading the header states — the largest unit is folded into the
		 * next smaller ALLOWED one, here 1 hour 30 minutes -> 90 minutes. */
		{
			NSDateComponents *hc = [[NSDateComponents alloc] init];
			NSDateComponentsFormatter *h = [[NSDateComponentsFormatter alloc] init];

			[hc setHour:1];
			[hc setMinute:30];
			[h setAllowedUnits:(NSCalendarUnitHour | NSCalendarUnitMinute)];
			[h setCollapsesLargestUnit:YES];
			[h setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropAll];
			collapsed = [h stringFromDateComponents:hc];
		}
		check("dcf-unit-rules",
		      limited != nil && [limited isEqualToString:@"2 days, 3 hours"] &&
		      collapsed != nil && [collapsed isEqualToString:@"90 minutes"],
		      [NSString stringWithFormat:@"maxCount2=[%@] collapsed=[%@]",
			 limited == nil ? @"(nil)" : limited, collapsed == nil ? @"(nil)" : collapsed]);
	}

	{
		/* THE PHRASE OPTIONS — the two members whose WORDS Apple does not publish, implemented with
		 * English affixes and REGISTERED as a deviation in the header. Asserted anyway: a documented
		 * deviation still has to behave. */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *plain, *about, *left;

		[c setHour:2];
		[dcf setAllowedUnits:NSCalendarUnitHour];
		plain = [dcf stringFromDateComponents:c];
		[dcf setIncludesApproximationPhrase:YES];
		about = [dcf stringFromDateComponents:c];
		[dcf setIncludesApproximationPhrase:NO];
		[dcf setIncludesTimeRemainingPhrase:YES];
		left = [dcf stringFromDateComponents:c];
		check("dcf-phrases",
		      plain != nil && [plain isEqualToString:@"2 hours"] &&
		      about != nil && [about isEqualToString:@"about 2 hours"] &&
		      left != nil && [left isEqualToString:@"2 hours left"],
		      [NSString stringWithFormat:@"plain=[%@] about=[%@] left=[%@]",
			 plain == nil ? @"(nil)" : plain, about == nil ? @"(nil)" : about,
			 left == nil ? @"(nil)" : left]);
	}

	{
		/* THE DATE-SHAPED DOORS, and the CALENDAR doing the subtraction: 2.5 hours apart is "2 hours, 30
		 * minutes" and not "2 hours, 30 minutes and 0 seconds" of approximation. The dates come from the
		 * ISO formatter, so no epoch arithmetic of mine is involved. */
		NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
		NSDate *start = [iso dateFromString:@"2026-09-20T12:00:00Z"];
		NSDate *end = [iso dateFromString:@"2026-09-20T14:30:00Z"];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *span;
		NSString *interval;

		[dcf setAllowedUnits:(NSCalendarUnitHour | NSCalendarUnitMinute)];
		[dcf setZeroFormattingBehavior:NSDateComponentsFormatterZeroFormattingBehaviorDropAll];
		span = [dcf stringFromDate:start toDate:end];
		interval = [dcf stringFromTimeInterval:3600.0];
		check("dcf-date-doors",
		      span != nil && [span isEqualToString:@"2 hours, 30 minutes"] &&
		      interval != nil && [interval isEqualToString:@"1 hour"],
		      [NSString stringWithFormat:@"span=[%@] interval(+3600)=[%@]",
			 span == nil ? @"(nil)" : span, interval == nil ? @"(nil)" : interval]);
	}

	{
		/* THE TWO DOORS APPLE'S OWN PAGES DEFINE: -stringForObjectValue: takes date information, and
		 * -getObjectValue: ANSWERS NO because this class "only implements formatting, not parsing". */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *viaObject;
		NSString *refused;
		id out = nil;
		NSString *error = nil;
		BOOL parsed;

		[c setHour:1];
		[dcf setAllowedUnits:NSCalendarUnitHour];
		viaObject = [dcf stringForObjectValue:c];
		refused = [dcf stringForObjectValue:@"1 hour"];
		parsed = [dcf getObjectValue:&out forString:@"1 hour" errorDescription:&error];
		check("dcf-object-and-parse",
		      viaObject != nil && [viaObject isEqualToString:@"1 hour"] && refused == nil &&
		      parsed == NO && out == nil,
		      [NSString stringWithFormat:@"viaObject=[%@] refused=%@ parsed=%d",
			 viaObject == nil ? @"(nil)" : viaObject,
			 refused == nil ? @"(nil)" : refused, parsed]);
	}

	{
		/* THE CLASS SHORTCUT is the instance path with a style, which is all Apple says it is. */
		NSDateComponents *c = [[NSDateComponents alloc] init];
		NSDateComponentsFormatter *dcf = [[NSDateComponentsFormatter alloc] init];
		NSString *viaClass;
		NSString *viaInstance;

		[c setHour:2];
		[dcf setAllowedUnits:NSCalendarUnitHour];
		[dcf setUnitsStyle:NSDateComponentsFormatterUnitsStyleSpellOut];
		viaInstance = [dcf stringFromDateComponents:c];
		viaClass = [NSDateComponentsFormatter localizedStringFromDateComponents:c
									 unitsStyle:NSDateComponentsFormatterUnitsStyleSpellOut];
		check("dcf-class-method",
		      viaClass != nil && viaInstance != nil && [viaClass isEqualToString:viaInstance] &&
		      [viaClass isEqualToString:@"two hours"],
		      [NSString stringWithFormat:@"viaClass=[%@] viaInstance=[%@]",
			 viaClass == nil ? @"(nil)" : viaClass, viaInstance == nil ? @"(nil)" : viaInstance]);
	}

	printf("FOUNDATION-FORMATTERS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FORMATTERS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-FORMATTERS DONE\n");
	return failc ? 1 : 0;
}
