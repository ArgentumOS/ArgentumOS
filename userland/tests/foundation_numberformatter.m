/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_numberformatter — F13.7c's acceptance.
 * docs/design/foundation-plan.md §10 (the un-refusal program).
 *
 * ONE unit, for the same reason foundation_dateformatter is: this family's claim is DATA, not a
 * cross-translation-unit boundary. It imports only <foundation/Foundation.h>, which also proves the
 * umbrella exports the new class.
 *
 * WHAT IT MEASURES, and why none of it can come from this file:
 *   nf-de / nf-en        THE SAME NUMBER IN TWO LOCALES: 1234567.89 is "1.234.567,89" in de_DE and
 *                        "1,234,567.89" in en_US — the separators are data, and one constant
 *                        cannot be both;
 *   nf-currency-locale   the CURRENCY style puts the locale's own symbol and placement in: the
 *                        dollar one starts with "$", the German one carries "€";
 *   nf-spellout / nf-ordinal  spell-out ("forty-two") and ordinals ("3rd") are ICU formats this
 *                        library could not have written from rules at all;
 *   nf-percent / nf-scientific  the percent and scientific styles;
 *   nf-parse / nf-round-trip   parsing text in the locale's conventions, and the round trip;
 *   nf-int64-exact       A LARGE INTEGER SURVIVES: 9007199254740993 is 2^53+1, which a double
 *                        cannot represent, so a formatter that went through one would print
 *                        9,007,199,254,740,992 — this check is what keeps the 64-bit door honest;
 *   nf-symbols-custom    a symbol SET on the formatter reaches the output;
 *   nf-allowsfloats-off  with -setAllowsFloats:NO a fraction is refused, not rounded away;
 *   nf-zero-and-nil      the two symbols that are OURS, because ICU has no zero-VALUE or no-value
 *                        symbol;
 *   nf-formatter-door    NSNumberFormatter answers the NSFormatter door as well as its own;
 *   nf-copy-independent  a copy is independent of its original.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

/* The detail is NULLABLE: every answer under test comes back through a nullable door. */
static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NUMBERFORMATTER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NUMBERFORMATTER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	NSLocale *en = [NSLocale localeWithLocaleIdentifier:@"en_US"];
	NSLocale *de = [NSLocale localeWithLocaleIdentifier:@"de_DE"];
	NSNumber *value = [NSNumber numberWithDouble:1234567.89];

	{
		/* ONE NUMBER, TWO LOCALES. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *deText;
		NSString *enText;

		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setLocale:de];
		deText = [formatter stringFromNumber:value];
		[formatter setLocale:en];
		enText = [formatter stringFromNumber:value];
		check("nf-de", deText != nil && [deText isEqualToString:@"1.234.567,89"], deText);
		check("nf-en", enText != nil && [enText isEqualToString:@"1,234,567.89"], enText);
	}

	{
		/* THE CURRENCY STYLE, where the symbol, its placement and the fraction digits are all
		 * the locale's business. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *deText;
		NSString *enText;

		[formatter setNumberStyle:NSNumberFormatterCurrencyStyle];
		[formatter setLocale:en];
		enText = [formatter stringFromNumber:value];
		[formatter setLocale:de];
		deText = [formatter stringFromNumber:value];
		check("nf-currency-locale",
		      enText != nil && deText != nil &&
		      [enText hasPrefix:@"$"] && [deText containsString:@"€"],
		      [NSString stringWithFormat:@"en=%@ de=%@", enText, deText]);
	}

	{
		/* FORMATS THAT ARE RULES OF LANGUAGE, not arithmetic. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterSpellOutStyle];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:42]];
		check("nf-spellout", text != nil && [text isEqualToString:@"forty-two"], text);

		[formatter setNumberStyle:NSNumberFormatterOrdinalStyle];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:3]];
		check("nf-ordinal", text != nil && [text isEqualToString:@"3rd"], text);
	}

	{
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterPercentStyle];
		text = [formatter stringFromNumber:[NSNumber numberWithDouble:0.25]];
		check("nf-percent", text != nil && [text isEqualToString:@"25%"], text);

		[formatter setNumberStyle:NSNumberFormatterScientificStyle];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:1234]];
		check("nf-scientific", text != nil && [text containsString:@"E"], text);
	}

	{
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumber *back;

		[formatter setLocale:de];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		back = [formatter numberFromString:@"1.234.567,89"];
		check("nf-parse",
		      back != nil && [back doubleValue] > 1234567.88
		      && [back doubleValue] < 1234567.90,
		      back != nil ? [back stringValue] : @"(nil)");

		{
			NSString *text = [formatter stringFromNumber:value];
			NSNumber *again = nil;

			if (text != nil) {
				again = [formatter numberFromString:text];
			}
			check("nf-round-trip",
			      again != nil && [again doubleValue] > 1234567.88
			      && [again doubleValue] < 1234567.90,
			      again != nil ? [again stringValue] : @"(nil)");
		}
	}

	{
		/* 2^53 + 1. A DOUBLE CANNOT HOLD IT: the nearest double is 9007199254740992, so a
		 * formatter that routed this through one would print ...992 — which is exactly the
		 * quiet wrongness the 64-bit door exists to prevent. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		text = [formatter stringFromNumber:
				[NSNumber numberWithLongLong:9007199254740993LL]];
		check("nf-int64-exact",
		      text != nil && [text isEqualToString:@"9,007,199,254,740,993"], text);
	}

	{
		/* A SYMBOL SET ON THE FORMATTER REACHES THE OUTPUT — that is what makes the symbols
		 * settable rather than decorative. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setGroupingSeparator:@"_"];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:1234567]];
		check("nf-symbols-custom", text != nil && [text isEqualToString:@"1_234_567"], text);
	}

	{
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumber *refused;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setAllowsFloats:NO];
		refused = [formatter numberFromString:@"12.5"];
		check("nf-allowsfloats-off", refused == nil,
		      refused != nil ? [refused stringValue] : @"(nil)");
	}

	{
		/* THE TWO SYMBOLS THAT ARE OURS: ICU has a NaN symbol but no symbol for a zero VALUE,
		 * and none for "no value at all". */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *zero;
		NSString *nothing;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setZeroSymbol:@"none"];
		[formatter setNilSymbol:@"no value"];
		zero = [formatter stringFromNumber:[NSNumber numberWithInt:0]];
		/* THROUGH -stringForObjectValue:, WHICH IS THE NULLABLE DOOR: -stringFromNumber: takes a
		 * NONNULL number in Cocoa (and here), so passing nil to it is undefined and warns; the
		 * documented way to reach nilSymbol is the NSFormatter door, which handles nil by design. */
		nothing = [formatter stringForObjectValue:nil];
		check("nf-zero-and-nil",
		      zero != nil && nothing != nil && [zero isEqualToString:@"none"] &&
		      [nothing isEqualToString:@"no value"],
		      [NSString stringWithFormat:@"zero=%@ nil=%@", zero, nothing]);
	}

	{
		/* THE NSFormatter DOOR, implemented rather than inherited: a number in, text out. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		text = [formatter stringForObjectValue:[NSNumber numberWithInt:1234]];
		check("nf-formatter-door", text != nil && [text isEqualToString:@"1,234"], text);
	}

	{
		NSNumberFormatter *original = [[NSNumberFormatter alloc] init];
		NSNumberFormatter *copy;
		NSString *before;
		NSString *after;

		[original setLocale:en];
		[original setNumberStyle:NSNumberFormatterDecimalStyle];
		before = [original stringFromNumber:value];
		copy = [original copy];
		[copy setGroupingSeparator:@"_"];
		after = [original stringFromNumber:value];
		check("nf-copy-independent",
		      before != nil && after != nil && [before isEqualToString:after] &&
		      [[copy stringFromNumber:value] isEqualToString:@"1_234_567.89"],
		      [NSString stringWithFormat:
				@"before=%@ after=%@ copy=%@ copyStyle=%d copyFormat=%@ copyPlain=%@",
				before, after, [copy stringFromNumber:value],
				(int)[copy numberStyle], [copy format],
				[copy stringFromNumber:[NSNumber numberWithInt:1234]]]);
	}

	printf("FOUNDATION-NUMBERFORMATTER RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-NUMBERFORMATTER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-NUMBERFORMATTER DONE\n");
	return failc ? 1 : 0;
}
