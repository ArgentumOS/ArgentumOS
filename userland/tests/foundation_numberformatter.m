/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_numberformatter — F13.7c's acceptance.
 * docs/design/foundation-plan.md §10 (the un-refusal program).
 *
 * ONE unit, for the same reason foundation_dateformatter is: this family's claim is DATA, not a
 * cross-translation-unit boundary. It imports only <Foundation/Foundation.h>, which also proves the
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

#import <Foundation/Foundation.h>

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

	{
		/* §63.79: THE FIFTEEN STORED DOORS. What is asserted is what a stored property can get wrong: the value
		 * must SURVIVE its own setter, and the `copy` families must be COPIES — proved by handing in a MUTABLE
		 * and then mutating it, which is the contract Apple declares and the defect this unit fixed beside them. */
		NSNumberFormatter *f = [[NSNumberFormatter alloc] init];
		NSMutableString *symbol = [NSMutableString stringWithString:@"pos"];
		NSAttributedString *attr = [[NSAttributedString alloc] initWithString:@"zero"];
		NSDictionary *attrs = [NSDictionary dictionaryWithObject:@"v" forKey:@"k"];

		[f setAttributedStringForZero:attr];
		[f setAttributedStringForNil:attr];
		[f setAttributedStringForNotANumber:attr];
		[f setTextAttributesForZero:attrs];
		[f setTextAttributesForNegativeValues:attrs];
		[f setTextAttributesForPositiveValues:attrs];
		[f setTextAttributesForNil:attrs];
		[f setTextAttributesForNotANumber:attrs];
		[f setTextAttributesForPositiveInfinity:attrs];
		[f setTextAttributesForNegativeInfinity:attrs];
		[f setPositiveInfinitySymbol:symbol];
		[f setNegativeInfinitySymbol:@"neg"];
		[f setLocalizesFormat:YES];
		[f setPartialStringValidationEnabled:YES];
		[f setFormattingContext:NSFormattingContextBeginningOfSentence];

		[symbol appendString:@"MUTATED"];

		check("numberformatter-stored-text-doors",
		      [[f positiveInfinitySymbol] isEqualToString:@"pos"] &&
		      [[f negativeInfinitySymbol] isEqualToString:@"neg"] &&
		      [[f attributedStringForZero] isEqualToAttributedString:attr] &&
		      [[f attributedStringForNil] isEqualToAttributedString:attr] &&
		      [[f attributedStringForNotANumber] isEqualToAttributedString:attr] &&
		      [[f textAttributesForZero] isEqualToDictionary:attrs] &&
		      [[f textAttributesForNegativeValues] isEqualToDictionary:attrs] &&
		      [[f textAttributesForPositiveValues] isEqualToDictionary:attrs] &&
		      [[f textAttributesForNil] isEqualToDictionary:attrs] &&
		      [[f textAttributesForNotANumber] isEqualToDictionary:attrs] &&
		      [[f textAttributesForPositiveInfinity] isEqualToDictionary:attrs] &&
		      [[f textAttributesForNegativeInfinity] isEqualToDictionary:attrs] &&
		      [f localizesFormat] && [f isPartialStringValidationEnabled] &&
		      [f formattingContext] == NSFormattingContextBeginningOfSentence,
		      [NSString stringWithFormat:@"symbol=%@/%@ zero=%@ ctx=%lu",
			[f positiveInfinitySymbol], [f negativeInfinitySymbol],
			[[f attributedStringForZero] string], (unsigned long)[f formattingContext]]);
	}

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
		/* THE CONFIGURATION DOORS — the properties that set a formatting POLICY. Each is an ICU
		 * attribute written through the door and read back, and the first one is also measured at
		 * the OUTPUT: -alwaysShowsDecimalSeparator forces the decimal point onto an integer. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *text;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setAlwaysShowsDecimalSeparator:YES];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:1234]];
		/* ⚠ THE EXPECTED VALUE IS THE GUEST'S, MEASURED — NOT REASONED, WHICH IS WHAT THE FIRST VERSION DID. ICU
		 * renders the decimal separator and NOTHING after it when alwaysShowsDecimalSeparator is set and no minimum
		 * fraction digit is — which is Apple's own wording for this property ("the separator is always shown, even
		 * when there are no digits after it"). The first version expected "1,234.0" and the guest answered "1,234.";
		 * the agent's report had flagged that expectation as reasoned rather than observed, and the guest settled it.
		 * The FRACTION-DIGIT count is a different property and is deliberately not set here, so this check measures
		 * the separator rule alone. */
		check("nf-always-decimal", text != nil && [text isEqualToString:@"1,234."], text);

		[formatter setAlwaysShowsDecimalSeparator:NO];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setGroupingSize:2];
		[formatter setSecondaryGroupingSize:4];
		check("nf-grouping-size",
		      [formatter groupingSize] == 2 && [formatter secondaryGroupingSize] == 4,
		      [NSString stringWithFormat:@"grouping=%ld secondary=%ld",
			(long)[formatter groupingSize], (long)[formatter secondaryGroupingSize]]);

		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setUsesSignificantDigits:YES];
		[formatter setMinimumSignificantDigits:3];
		[formatter setMaximumSignificantDigits:3];
		text = [formatter stringFromNumber:[NSNumber numberWithInt:1234]];
		/* 1234 has four significant digits; three of them round the last up to 1230. */
		check("nf-significant-digits",
		      [formatter usesSignificantDigits] && [formatter minimumSignificantDigits] == 3 &&
		      [formatter maximumSignificantDigits] == 3 &&
		      text != nil && [text isEqualToString:@"1,230"],
		      [NSString stringWithFormat:@"uses=%d min=%lu max=%lu text=%@",
			(int)[formatter usesSignificantDigits],
			(unsigned long)[formatter minimumSignificantDigits],
			(unsigned long)[formatter maximumSignificantDigits], text]);
	}

	{
		/* ROUNDING INCREMENT goes through ICU's DOUBLE-attribute door, so 0.05 has to survive
		 * without the truncation an int-attribute cast would cause. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumber *increment;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setRoundingIncrement:[NSNumber numberWithDouble:0.05]];
		increment = [formatter roundingIncrement];
		check("nf-rounding-increment",
		      increment != nil && [increment doubleValue] > 0.049 && [increment doubleValue] < 0.051,
		      increment != nil ? [increment stringValue] : @"(nil)");
	}

	{
		/* THE MONETARY AND PER-MILL SYMBOL DOORS, and the two deprecated ALIASES: a value written
		 * through one spelling has to be read back through the other, because that is what makes a
		 * deprecated alias an alias. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *grouping;
		NSString *plain;
		NSString *noGrouping;
		BOOL separatorFlag;
		NSString *perMill;
		NSString *currencyDecimal;
		NSString *currencyGrouping;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setPerMillSymbol:@"pm"];
		perMill = [formatter perMillSymbol];
		[formatter setCurrencyDecimalSeparator:@"."];
		currencyDecimal = [formatter currencyDecimalSeparator];
		[formatter setCurrencyGroupingSeparator:@"'"];
		currencyGrouping = [formatter currencyGroupingSeparator];
		check("nf-symbol-doors",
		      [perMill isEqualToString:@"pm"] && [currencyDecimal isEqualToString:@"."] &&
		      [currencyGrouping isEqualToString:@"'"],
		      [NSString stringWithFormat:@"perMill=%@ curDec=%@ curGrp=%@",
			perMill, currencyDecimal, currencyGrouping]);

		[formatter setThousandSeparator:@"_"];
		grouping = [formatter groupingSeparator];
		plain = [formatter stringFromNumber:[NSNumber numberWithInt:1234567]];
		[formatter setHasThousandSeparators:NO];
		separatorFlag = [formatter usesGroupingSeparator];
		noGrouping = [formatter stringFromNumber:[NSNumber numberWithInt:1234567]];
		check("nf-thousand-alias",
		      [grouping isEqualToString:@"_"] &&
		      plain != nil && [plain isEqualToString:@"1_234_567"] &&
		      separatorFlag == NO &&
		      noGrouping != nil && [noGrouping isEqualToString:@"1234567"],
		      [NSString stringWithFormat:@"grouping=%@ plain=%@ hasThousand=%d off=%@",
			grouping, plain, (int)separatorFlag, noGrouping]);
	}

	{
		/* THE PADDING TRIO — three more ICU doors (the width, the fill character and where the pad
		 * goes relative to the affixes). */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *pad;
		NSNumberFormatterPadPosition position;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setFormatWidth:8];
		[formatter setPaddingCharacter:@"0"];
		[formatter setPaddingPosition:NSNumberFormatterPadAfterPrefix];
		pad = [formatter paddingCharacter];
		position = [formatter paddingPosition];
		check("nf-padding-trio",
		      [formatter formatWidth] == 8 && pad != nil && [pad isEqualToString:@"0"] &&
		      position == NSNumberFormatterPadAfterPrefix,
		      [NSString stringWithFormat:@"width=%lu pad=%@ position=%d",
			(unsigned long)[formatter formatWidth], pad, (int)position]);
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

	{
		/* positiveFormat/negativeFormat ARE THE TWO HALVES OF ONE ICU PATTERN: the decimal pattern
		 * language spells the negative subpattern after a ';'. Both halves are set and read back —
		 * the JUSTIFIED part, because the pattern language round-trips (MEASURED on ICU) — and the
		 * negative subpattern's affixes are confirmed to REACH THE OUTPUT. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSString *pos;
		NSString *neg;
		NSString *out;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setPositiveFormat:@"#,##0.00"];
		[formatter setNegativeFormat:@"(#,##0.00)"];
		pos = [formatter positiveFormat];
		neg = [formatter negativeFormat];
		out = [formatter stringFromNumber:[NSNumber numberWithDouble:-1234.5]];
		/* The recombined pattern "#,##0.00;(#,##0.00)" renders -1234.5 as "(1,234.50)": that exact
		 * string is MEASURED on host ICU but REASONED guest-side, so the check asserts the negative
		 * subpattern's AFFIXES rather than the whole rendering. */
		check("nf-format-halves",
		      pos != nil && neg != nil && [pos isEqualToString:@"#,##0.00"] &&
		      [neg isEqualToString:@"(#,##0.00)"] &&
		      out != nil && [out containsString:@"("] && [out containsString:@")"],
		      [NSString stringWithFormat:@"pos=%@ neg=%@ out=%@", pos, neg, out]);
	}

	{
		/* THE RANGE OVER THE INPUT: Apple's -minimum/-maximum are the lowest/highest number allowed
		 * as INPUT (Apple's own wording), so a parse outside the range is REFUSED and one inside it
		 * is answered. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumber *inRange;
		NSNumber *tooLow;
		NSNumber *tooHigh;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		[formatter setMinimum:[NSNumber numberWithInt:100]];
		[formatter setMaximum:[NSNumber numberWithInt:1000]];
		inRange = [formatter numberFromString:@"500"];
		tooLow = [formatter numberFromString:@"50"];
		tooHigh = [formatter numberFromString:@"5000"];
		check("nf-min-max",
		      inRange != nil && [inRange intValue] == 500 && tooLow == nil && tooHigh == nil,
		      [NSString stringWithFormat:@"in=%@ low=%@ high=%@",
			inRange, tooLow != nil ? [tooLow stringValue] : @"(nil)",
			tooHigh != nil ? [tooHigh stringValue] : @"(nil)"]);
	}

	{
		/* -generatesDecimalNumbers: with it YES the parse answers an NSDecimalNumber (which IS an
		 * NSNumber, so the discriminating test is isKindOfClass:), and with it NO (the default) a
		 * plain NSNumber. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumber *plain;
		NSNumber *decimal;

		[formatter setLocale:en];
		[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
		plain = [formatter numberFromString:@"1234.5"];
		[formatter setGeneratesDecimalNumbers:YES];
		decimal = [formatter numberFromString:@"1234.5"];
		check("nf-generates-decimal",
		      plain != nil && ![plain isKindOfClass:[NSDecimalNumber class]] &&
		      decimal != nil && [decimal isKindOfClass:[NSDecimalNumber class]] &&
		      [decimal doubleValue] > 1234.4 && [decimal doubleValue] < 1234.6,
		      [NSString stringWithFormat:@"plain=%@(%@) decimal=%@(%@)",
			plain, [plain class], decimal, [decimal class]]);
	}

	{
		/* THE FORMATTER BEHAVIOR trio. Apple's modern default is the 10.4 behavior (REASONED — the
		 * documented default, not something a guest can be asked); the class default SEEDS a new
		 * instance through its getter; and a value set on an instance reads back. */
		NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
		NSNumberFormatterBehavior dflt = [NSNumberFormatter defaultFormatterBehavior];
		NSNumberFormatterBehavior got;
		NSNumberFormatter *seeded;
		NSNumberFormatterBehavior fromDefault;

		[formatter setFormatterBehavior:NSNumberFormatterBehavior10_0];
		got = [formatter formatterBehavior];
		/* Change the class default, seed a fresh instance from it, then RESTORE the default so no
		 * later check can observe the change. */
		[NSNumberFormatter setDefaultFormatterBehavior:NSNumberFormatterBehavior10_0];
		seeded = [[NSNumberFormatter alloc] init];
		fromDefault = [seeded formatterBehavior];
		[NSNumberFormatter setDefaultFormatterBehavior:dflt];
		check("nf-behavior",
		      dflt == NSNumberFormatterBehavior10_4 &&
		      got == NSNumberFormatterBehavior10_0 &&
		      fromDefault == NSNumberFormatterBehavior10_0,
		      [NSString stringWithFormat:@"default=%d got=%d seeded=%d",
			(int)dflt, (int)got, (int)fromDefault]);
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
