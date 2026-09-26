/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_scanner — `NSScanner`, the third member of the Pattern Matching family and the one that READS a
 * string in order instead of matching a pattern (docs/design/foundation-plan.md §62.20). ONE unit, importing only
 * <Foundation/Foundation.h>.
 *
 * THE POSITION IS THE SUBJECT OF MOST OF THESE CHECKS, because it is the whole contract: a scan skips the
 * characters it was told to skip BEFORE it looks (check 2), a scan that finds nothing MOVES NOTHING (checks 6, 9,
 * 12), a question about the end does not move it either (check 5), and setting the position is how a caller
 * rescans — which is why the one raise on this class is asserted rather than avoided (check 4).
 *
 * THE NUMERIC CHECKS ASSERT THE GRAMMAR THE HEADER STATES, since Apple publishes none: the sign of the signed
 * doors and the refusal of the unsigned one, an overflowing run being VALID with the position past all of it, an
 * exponent that is not followed by a digit not being consumed, the optional `0x` on hex integers against the
 * REQUIRED one on hex floats, and the two rules `-scanDecimal:` makes (the canonical compaction, and 38
 * significant digits).
 *
 * AND THE LOCALE CHECK IS RELATIONAL RATHER THAN A HARD-CODED COMMA: it asks `NSNumberFormatter` — the class that
 * already reads ICU for this fact — what the separator for a locale IS, and then requires the scanner to agree
 * with it. That is the property the header claims, and it holds whether or not the ICU data on the machine has a
 * German locale in it, which a check that expected `,` would not.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-SCANNER"

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

/* A FRESH SCANNER ON A C STRING, which is what most of these checks want. */
static NSScanner *fn_scanner(const char *text)
{
	/* A LOCAL HOLDS THE NULLABLE RESULT: `+stringWithUTF8String:` is annotated nullable and `+scannerWithString:` is
	 * not, and the guest's probes are compiled -Werror=nullable-to-nonnull-conversion. */
	NSString *string = [NSString stringWithUTF8String:text];

	return [NSScanner scannerWithString:string];
}

/* THE RUN OF DIGITS AND A DECIMAL POINT AT THE POSITION, for the diagnostics below — a value that came back wrong
 * is much easier to read next to the text that produced it. */
static NSString *fn_text(NSScanner *scanner)
{
	NSString *text = [scanner string];
	NSUInteger at = [scanner scanLocation];
	NSUInteger length = [text length];

	if (at >= length) {
		return @"(at the end)";
	}
	return [text substringWithRange:NSMakeRange(at, length - at)];
}

int main(void)
{
	/* ---- THE STRING, THE DEFAULTS, AND THE POSITION ------------------------------------------------- */
	{
		NSString *text = @"  42 and more";
		NSScanner *made = [NSScanner scannerWithString:text];
		NSScanner *initialized = [[NSScanner alloc] initWithString:text];
		NSScanner *empty = fn_scanner("");
		NSCharacterSet *skip = [made charactersToBeSkipped];
		NSCharacterSet *whitespace = [NSCharacterSet whitespaceAndNewlineCharacterSet];

		check("a-scanner-reads-the-string-it-was-given",
		      [[made string] isEqualToString:text] &&
		      [[initialized string] isEqualToString:text] &&
		      [made scanLocation] == 0 && [made caseSensitive] &&
		      [skip isEqualToCharacterSet:whitespace] &&
			![made isAtEnd] && [empty isAtEnd],
		      [[NSString stringWithFormat:@"string=%@ location=%lu caseSensitive=%d atEnd=%d emptyAtEnd=%d",
			[made string], (unsigned long)[made scanLocation], (int)[made caseSensitive],
			(int)[made isAtEnd], (int)[empty isAtEnd]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("  42 and more");
		int value = 0;
		BOOL ok = [scanner scanInt:&value];

		check("whitespace-is-skipped-before-the-element-and-not-during-it",
		      ok && value == 42 && [scanner scanLocation] == 4,
		      [[NSString stringWithFormat:@"ok=%d value=%d location=%lu next=[%@]",
			(int)ok, value, (unsigned long)[scanner scanLocation], fn_text(scanner)] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("  42");
		int value = -1;
		BOOL ok;

		[scanner setCharactersToBeSkipped:[NSCharacterSet characterSetWithCharactersInString:@""]];
		ok = [scanner scanInt:&value];
		check("an-empty-skip-set-stops-at-the-space",
		      !ok && [scanner scanLocation] == 0 && value == -1,
		      [[NSString stringWithFormat:@"ok=%d location=%lu value=%d",
			(int)ok, (unsigned long)[scanner scanLocation], value] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("one two");
		BOOL raised = NO;
		BOOL named = NO;
		BOOL usable = NO;

		[scanner scanUpToString:@" " intoString:NULL];
		[scanner scanString:@" " intoString:NULL];
		[scanner setScanLocation:0];
		usable = [[scanner string] isEqualToString:@"one two"] && [scanner scanLocation] == 0;
		@try {
			[scanner setScanLocation:99];
		} @catch (NSException *e) {
			raised = YES;
			named = [[e name] isEqualToString:NSRangeException];
		}
		check("the-position-is-an-input-and-setting-it-past-the-end-raises",
		      usable && raised && named && [scanner scanLocation] == 0,
		      [[NSString stringWithFormat:@"rewound=%d raised=%d NSRangeException=%d location=%lu",
			(int)usable, (int)raised, (int)named, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("42   ");
		int value = 0;
		BOOL scanned = [scanner scanInt:&value];
		BOOL atEnd = [scanner isAtEnd];
		NSUInteger after = [scanner scanLocation];

		check("the-end-ignores-skipped-characters-and-asking-does-not-move-the-position",
		      scanned && value == 42 && atEnd && after == 2,
		      [[NSString stringWithFormat:@"value=%d atEnd=%d location=%lu (2 means the question moved it)",
			value, (int)atEnd, (unsigned long)after] UTF8String]);
	}

	/* ---- SCANNING SOME OF THE STRING ---------------------------------------------------------------- */
	{
		NSScanner *scanner = fn_scanner("abcdef");
		NSString *taken = nil;
		BOOL first = [scanner scanString:@"abc" intoString:&taken];
		BOOL second;

		scanner = fn_scanner("abcdef");
		first = [scanner scanString:@"abc" intoString:&taken];
		second = [scanner scanString:@"zzz" intoString:&taken];
		check("a-string-scan-advances-only-when-it-matches",
		      first && [taken isEqualToString:@"abc"] && [scanner scanLocation] == 3 && !second &&
		      [scanner scanLocation] == 3,
		      [[NSString stringWithFormat:@"first=%d taken=%@ second=%d location=%lu",
			(int)first, taken, (int)second, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("ABCdef");
		NSString *taken = nil;
		BOOL sensitive = [scanner scanString:@"abc" intoString:&taken];
		NSUInteger afterSensitive = [scanner scanLocation];
		BOOL insensitive;

		[scanner setCaseSensitive:NO];
		insensitive = [scanner scanString:@"abc" intoString:&taken];
		check("case-sensitivity-is-opt-in-and-the-answer-is-the-receivers-text",
		      !sensitive && afterSensitive == 0 && insensitive &&
		      [taken isEqualToString:@"ABC"] && [scanner scanLocation] == 3,
		      [[NSString stringWithFormat:@"sensitive=%d insensitive=%d taken=%@ location=%lu",
			(int)sensitive, (int)insensitive, taken,
			(unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("12ab");
		NSString *taken = nil;
		NSCharacterSet *digitsSet = [NSCharacterSet decimalDigitCharacterSet];
		BOOL digits = [scanner scanCharactersFromSet:digitsSet intoString:&taken];
		BOOL again = [scanner scanCharactersFromSet:digitsSet intoString:&taken];

		check("a-character-run-is-taken-to-its-end-and-then-there-is-nothing",
		      digits && [taken isEqualToString:@"12"] && [scanner scanLocation] == 2 && !again,
		      [[NSString stringWithFormat:@"digits=%d taken=%@ again=%d location=%lu",
			(int)digits, taken, (int)again, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		/* THE CASE APPLE'S OWN PAGE NAMES: a scan for something made of the characters that are skipped finds
		 * nothing, because the skip already ate it. */
		NSScanner *scanner = fn_scanner("   x");
		NSString *taken = nil;
		NSCharacterSet *theSkipSet = [NSCharacterSet whitespaceAndNewlineCharacterSet];
		BOOL ok = [scanner scanCharactersFromSet:theSkipSet intoString:&taken];

		/* THE POSITION IS 3 AND NOT 0, WHICH IS THE SKIP RULE RATHER THAN A FAILED SCAN MOVING: "characters to be
		 * skipped are skipped prior to the scanner examining the target", so the skip happened (the position is at
		 * the `x`) and then the examination found nothing to scan. A scanner that left the position at 0 would be
		 * keeping the skipped characters for the next scan to skip again. */
		check("scanning-for-the-skipped-characters-scans-nothing",
		      !ok && [scanner scanLocation] == 3,
		      [[NSString stringWithFormat:@"ok=%d location=%lu (3 = past the skipped run)",
			(int)ok, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("hello, world");
		NSString *word = nil;
		/* THE SET IS `commaSet` BECAUSE `comma` BELOW IS THE BOOL THE SCAN OF THE COMMA ANSWERS — the compiler
		 * caught the clash, which is worth keeping in mind when naming a variable after the character. */
		NSCharacterSet *commaSet = [NSCharacterSet characterSetWithCharactersInString:@","];
		BOOL up = [scanner scanUpToCharactersFromSet:commaSet intoString:&word];
		BOOL comma;
		NSUInteger atTheStop = [scanner scanLocation];

		comma = [scanner scanString:@"," intoString:NULL];
		check("scan-up-to-characters-stops-on-the-set-and-leaves-it",
		      up && [word isEqualToString:@"hello"] && atTheStop == 5 && comma &&
		      [scanner scanLocation] == 6,
		      [[NSString stringWithFormat:@"up=%d word=%@ comma=%d location=%lu",
			(int)up, word, (int)comma, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("abc");
		NSString *taken = nil;
		NSCharacterSet *comma = [NSCharacterSet characterSetWithCharactersInString:@","];
		BOOL ok = [scanner scanUpToCharactersFromSet:comma intoString:&taken];

		check("scan-up-to-characters-with-no-stop-takes-the-rest-of-the-string",
		      ok && [taken isEqualToString:@"abc"] && [scanner isAtEnd],
		      [[NSString stringWithFormat:@"ok=%d taken=%@ atEnd=%d",
			(int)ok, taken, (int)[scanner isAtEnd]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("a=b");
		NSString *taken = nil;
		BOOL up = [scanner scanUpToString:@"=" intoString:&taken];
		BOOL atTheMatch = ([scanner scanLocation] == 1);
		BOOL equals = [scanner scanString:@"=" intoString:NULL];
		NSScanner *first = fn_scanner("=x");
		NSString *untouched = @"sentinel";
		BOOL refused = [first scanUpToString:@"=" intoString:&untouched];

		check("scan-up-to-a-string-leaves-the-position-at-it-and-refuses-when-it-is-first",
		      up && [taken isEqualToString:@"a"] && atTheMatch && equals &&
		      !refused && [first scanLocation] == 0 &&
		      [untouched isEqualToString:@"sentinel"],
		      [[NSString stringWithFormat:@"up=%d taken=%@ atMatch=%d refused=%d untouched=%@",
			(int)up, taken, (int)atTheMatch, (int)refused, untouched] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("abc");
		NSString *taken = nil;
		BOOL ok = [scanner scanUpToString:@"z" intoString:&taken];

		check("scan-up-to-a-string-that-is-nowhere-takes-the-rest",
		      ok && [taken isEqualToString:@"abc"] && [scanner isAtEnd],
		      [[NSString stringWithFormat:@"ok=%d taken=%@ atEnd=%d",
			(int)ok, taken, (int)[scanner isAtEnd]] UTF8String]);
	}

	/* ---- NUMBERS ------------------------------------------------------------------------------------ */
	{
		NSScanner *negative = fn_scanner("-7 rest");
		NSScanner *positive = fn_scanner("+8 rest");
		NSScanner *space = fn_scanner("  -9 rest");
		NSScanner *letter = fn_scanner("x-7");
		long long a = 0;
		long long b = 0;
		long long c = 0;
		long long d = 0;
		BOOL na = [negative scanLongLong:&a];
		BOOL pb = [positive scanLongLong:&b];
		BOOL sc = [space scanLongLong:&c];
		BOOL ld = [letter scanLongLong:&d];

		check("decimal-integers-take-a-sign-and-refuse-a-letter",
		      na && a == -7 && pb && b == 8 && sc && c == -9 && !ld &&
		      [letter scanLocation] == 0 && [negative scanLocation] == 2,
		      [[NSString stringWithFormat:@"-7=%lld +8=%lld -9=%lld letter=%d location=%lu",
			a, b, c, (int)ld, (unsigned long)[negative scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("123456789012345678901234567890 rest");
		long long value = 0;
		BOOL ok = [scanner scanLongLong:&value];

		check("an-overflowing-integer-is-valid-and-the-position-is-past-all-of-it",
		      ok && [scanner scanLocation] == 30 && value != 0,
		      [[NSString stringWithFormat:@"ok=%d value=%lld location=%lu",
			(int)ok, value, (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *minus = fn_scanner("-5");
		NSScanner *plus = fn_scanner("+5");
		unsigned long long value = 0;
		BOOL refused = [minus scanUnsignedLongLong:&value];
		BOOL taken = [plus scanUnsignedLongLong:&value];

		check("the-unsigned-door-refuses-a-minus-and-takes-a-plus",
		      !refused && [minus scanLocation] == 0 && taken && value == 5 &&
		      [plus scanLocation] == 2,
		      [[NSString stringWithFormat:@"minus=%d plus=%d value=%llu",
			(int)refused, (int)taken, value] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("-12.5e2 tail");
		double value = 0;
		BOOL ok = [scanner scanDouble:&value];
		NSScanner *dangling = fn_scanner("12e");
		double other = 0;
		BOOL scanned = [dangling scanDouble:&other];

		check("doubles-carry-an-exponent-and-an-exponent-without-digits-is-not-taken",
		      ok && value == -1250.0 && [scanner scanLocation] == 7 &&
		      scanned && other == 12.0 && [dangling scanLocation] == 2,
		      [[NSString stringWithFormat:@"value=%g location=%lu dangling=%g danglingLocation=%lu",
			value, (unsigned long)[scanner scanLocation], other,
			(unsigned long)[dangling scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("0xFF tail");
		NSScanner *bare = fn_scanner("FF tail");
		NSScanner *prefixOnly = fn_scanner("0x tail");
		unsigned long long a = 0;
		unsigned long long b = 0;
		unsigned long long c = 0;
		BOOL first = [scanner scanHexLongLong:&a];
		BOOL second = [bare scanHexLongLong:&b];
		BOOL third = [prefixOnly scanHexLongLong:&c];

		check("hex-integers-take-an-optional-prefix-but-need-digits",
		      first && a == 255 && [scanner scanLocation] == 4 &&
		      second && b == 255 && [bare scanLocation] == 2 && !third &&
		      [prefixOnly scanLocation] == 0,
		      [[NSString stringWithFormat:@"0xFF=%llu at=%lu FF=%llu prefixOnly=%d",
			a, (unsigned long)[scanner scanLocation], b, (int)third] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("0x1.8p1 tail");
		NSScanner *bare = fn_scanner("1.8p1 tail");
		double value = 0;
		double other = 0;
		BOOL prefixed = [scanner scanHexDouble:&value];
		BOOL unprefixed = [bare scanHexDouble:&other];

		check("a-hex-float-requires-its-prefix",
		      prefixed && value == 3.0 && [scanner scanLocation] == 7 && !unprefixed &&
		      [bare scanLocation] == 0,
		      [[NSString stringWithFormat:@"0x1.8p1=%g at=%lu 1.8p1=%d",
			value, (unsigned long)[scanner scanLocation], (int)unprefixed] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("12.75 tail");
		NSScanner *trailing = fn_scanner("1.50 tail");
		NSScanner *letter = fn_scanner("x1");
		NSDecimal a;
		NSDecimal b;
		NSDecimal c;
		BOOL first = [scanner scanDecimal:&a];
		BOOL second = [trailing scanDecimal:&b];
		BOOL third = [letter scanDecimal:&c];

		memset(&c, 0, sizeof c);
		check("scan-decimal-answers-the-value-in-this-librarys-canonical-form",
		      first && [NSDecimalString(&a, nil) isEqualToString:@"12.75"] &&
		      second && [NSDecimalString(&b, nil) isEqualToString:@"1.5"] &&
		      !third && [letter scanLocation] == 0 &&
		      [scanner scanLocation] == 5,
		      [[NSString stringWithFormat:@"12.75=%@ 1.50=%@ (compacted) letter=%d at=%lu",
			NSDecimalString(&a, nil), NSDecimalString(&b, nil), (int)third,
			(unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		/* THE 38-DIGIT RULE, FROM BOTH SIDES OF THE POINT: an integer that needs forty digits keeps the first
		 * thirty-eight and raises the exponent, and a fraction that needs forty keeps the first thirty-eight of
		 * them. Both are truncations toward zero, and both are what the header states. */
		char buffer[128];
		NSDecimal big;
		NSDecimal fraction;
		NSScanner *whole;
		NSScanner *point;
		BOOL first;
		BOOL second;
		NSString *expectedWhole = @"1000000000000000000000000000000000000000";

		snprintf(buffer, sizeof buffer, "1%s", "000000000000000000000000000000000000000");
		whole = fn_scanner(buffer);
		first = [whole scanDecimal:&big];
		point = fn_scanner("0.1234567890123456789012345678901234567890");
		second = [point scanDecimal:&fraction];
		check("a-decimal-longer-than-the-mantissa-truncates-toward-zero",
		      first && [NSDecimalString(&big, nil) isEqualToString:expectedWhole] &&
		      second && [NSDecimalString(&fraction, nil)
				isEqualToString:@"0.12345678901234567890123456789012345678"],
		      [[NSString stringWithFormat:@"whole=%@ expected=%@ fraction=%@",
			NSDecimalString(&big, nil), expectedWhole,
			NSDecimalString(&fraction, nil)] UTF8String]);
	}
	{
		/* THE LOCALE, CHECKED AGAINST THE CLASS THAT ALREADY READS ICU FOR IT. The scanner must use the SAME
		 * separator NSNumberFormatter uses for the same locale — and this way the check holds whatever the
		 * machine's ICU data actually contains for de_DE. */
		NSLocale *german = [NSLocale localeWithLocaleIdentifier:@"de_DE"];
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSScanner *none = fn_scanner("3.5");
		NSScanner *localized;
		NSScanner *period;
		NSString *separator;
		NSString *text;
		double plain = 0;
		double value = 0;
		double other = 0;
		int takenThePoint_count = 0;

		if ([none scanDouble:&plain]) {
			takenThePoint_count = 1;
		}

		[formatter setLocale:german];
		separator = [formatter decimalSeparator];
		text = [NSString stringWithFormat:@"3%@5", separator];
		localized = [NSScanner localizedScannerWithString:@"3.5"];
		period = [NSScanner scannerWithString:@"3.5"];
		{
			NSScanner *withLocale = [NSScanner scannerWithString:text];
			NSScanner *pointWithLocale = [NSScanner scannerWithString:@"3.5"];
			BOOL ok;
			BOOL stoppedAtThePoint;
			BOOL agrees;

			/* `+localizedScannerWithString:` uses THIS MACHINE'S locale, so the value it scans is not asserted:
			 * what is asserted is that it has one. The German scanner is where the mechanism shows. */
			[period setLocale:german];
			[withLocale setLocale:german];
			[pointWithLocale setLocale:german];
			ok = [withLocale scanDouble:&value];
			stoppedAtThePoint = [pointWithLocale scanDouble:&other];
			/* THE AGREEMENT IS THE CHECK: with the formatter's separator, 3<sep>5 is 3.5; and when that
			 * separator is not a period, a scanner with the same locale stops AT the period — 3, not 3.5 —
			 * which is what "the locale distinguishes the integer and fractional parts" means. */
			agrees = [separator isEqualToString:@"."] ||
				 (stoppedAtThePoint && other == 3.0 && [pointWithLocale scanLocation] == 1);
			check("a-locale-changes-the-decimal-separator-and-agrees-with-the-formatter",
			      ok && value == 3.5 && takenThePoint_count == 1 && plain == 3.5 && agrees &&
			      [[withLocale locale] isEqual:german] && [localized locale] != nil,
			      [[NSString stringWithFormat:@"separator=[%@] 3%s5=%g location=%lu "
				   @"unlocalized-3.5=%g de-scan-of-3.5=%g at=%lu locale=%@ localized=%@",
				separator, [separator UTF8String], value,
				(unsigned long)[withLocale scanLocation], plain, other,
				(unsigned long)[pointWithLocale scanLocation],
				[[withLocale locale] localeIdentifier],
				[[localized locale] localeIdentifier]] UTF8String]);	/* %@ renders nil as (null) */
		}
		(void)period;
	}

	/* ---- THE OBJECT PROTOCOL, AND THE NULL RESULT --------------------------------------------------- */
	{
		NSScanner *scanner = fn_scanner("one two three");
		NSScanner *copy = [scanner copy];

		[scanner scanUpToString:@" " intoString:NULL];
		check("a-copy-is-independent-of-the-scanner-it-came-from",
		      copy != scanner && [copy scanLocation] == 0 &&
		      [[copy string] isEqualToString:[scanner string]] &&
		      [scanner scanLocation] == 3,
		      [[NSString stringWithFormat:@"copyAt=%lu originalAt=%lu",
			(unsigned long)[copy scanLocation], (unsigned long)[scanner scanLocation]] UTF8String]);
	}
	{
		NSScanner *scanner = fn_scanner("7 x 8 y 9");
		int value = 0;

		/* AND THE SPACE IS NOT SCANNED — IT IS SKIPPED, which is why there is no `scanString:@" "` here: a space
		 * is in the default skip set, so the next scan would eat it before looking and the match would fail. */
		check("every-scan-takes-a-null-result-to-mean-skip-past-it",
		      [scanner scanInt:NULL] && [scanner scanLocation] == 1 &&
		      [scanner scanUpToString:@" " intoString:NULL] && [scanner scanLocation] == 3 &&
		      [scanner scanInt:&value] && value == 8 && [scanner scanLocation] == 5,
		      [[NSString stringWithFormat:@"location=%lu next=[%@] value=%d",
			(unsigned long)[scanner scanLocation], fn_text(scanner), value] UTF8String]);
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
