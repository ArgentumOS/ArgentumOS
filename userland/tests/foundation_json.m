/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_json.m — THE PROBE FOR §62.93: the JSON reading options Apple publishes and this library
 * refused, AND the two grammars they depend on finally being distinguishable.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE STRICT PATH IS STRICT. It accepted a missing comma (`[1 2]`), a trailing comma and `+1`, so
 *     JSON5's rules could not have been observed at all - a probe that only tested JSON5 would have
 *     passed against the old parser for the wrong reason. Both directions are asserted here: what
 *     strict JSON refuses, and that the same text parses under `json5Allowed`.
 *   * THE STRICT NUMBERS ARE RFC 8259'S, by VALUE and not merely by "it parsed": `1e3` is 1000 and
 *     `-2.5E-2` is -0.025, so a reader that accepted the text and lost the exponent fails.
 *   * EACH JSON5 FEATURE IS ONE CHECK AND EACH IS TESTED BY ITS OWN ANSWER: comments, both quote
 *     characters, the four extra escapes, the line continuation, the any-character escape, the three
 *     number additions, the trailing comma, the unquoted key.
 *   * THE FEATURE-FLAG PATTERN IS PINNED: a strict document parses to an EQUAL object with and
 *     without `json5Allowed` - the flag is additive, not a second dialect that changes the old one.
 *   * `topLevelDictionaryAssumed` IS CHECKED FOR WHAT IT ADDS **AND FOR WHAT IT DOES NOT**: a document
 *     that begins with `{` or `[` parses exactly as it did with or without the option, which is the
 *     boundary the header states.
 *   * THE TWO BOUNDARIES THE HEADER ADMITS ARE ASSERTED RATHER THAN ASSUMED: an unquoted key is ASCII
 *     plus \u escapes (a NON-ASCII key is refused by name), and the JSON5 blank-space set is the one
 *     implemented (a document separated by U+00A0 and U+2028 parses under JSON5 and fails apart).
 *   * AND THE DEPRECATED SPELLING CARRIES THE SAME BIT, with a fragment parsed through it, because a
 *     name that equals the right constant but does not behave like it would pass a value-only check.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-JSON %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-JSON %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* ONE DOOR FOR EVERY PARSE, so a check reads as "this text, this option, this answer". */
static id fn_json_parse(const char *text, NSJSONReadingOptions options)
{
	NSData *data = [NSData dataWithBytes:text length:strlen(text)];

	return [NSJSONSerialization JSONObjectWithData:data options:options error:NULL];
}

static BOOL fn_json_refused(const char *text, NSJSONReadingOptions options)
{
	return fn_json_parse(text, options) == nil;
}

static BOOL fn_json_is_integer(id object, long long value)
{
	return [object isKindOfClass:[NSNumber class]] && [object longLongValue] == value;
}

static BOOL fn_json_is_double(id object, double value)
{
	return [object isKindOfClass:[NSNumber class]] && [object doubleValue] == value;
}

int main(void)
{
	/* 1. THE STRICT NUMBERS, BY VALUE. */
	{
		id numbers = fn_json_parse("[0,-1,1.5,1e3,-2.5E-2,100]", 0);
		NSArray *want = @[ @0, @-1, @1.5, @1000.0, @-0.025, @100 ];
		NSMutableArray *wrong = [NSMutableArray array];
		unsigned long i;

		for (i = 0; numbers != nil && i < [want count]; i++) {
			id got = [numbers objectAtIndex:i];
			id expect = [want objectAtIndex:i];

			if (![[got description] isEqualToString:[expect description]]) {
				[wrong addObject:[NSString stringWithFormat:@"%lu: %@ != %@", i, got, expect]];
			}
		}
		check("strict-numbers-are-rfc-8259s",
		      numbers != nil && [numbers count] == 6 && [wrong count] == 0 &&
		      fn_json_refused("[+1]", 0) && fn_json_refused("[01]", 0) &&
		      fn_json_refused("[.5]", 0) && fn_json_refused("[5.]", 0) &&
		      fn_json_refused("[0x1F]", 0) && fn_json_refused("[Infinity]", 0) &&
		      fn_json_refused("[NaN]", 0) && fn_json_refused("[1e]", 0) &&
		      fn_json_refused("[1e+]", 0),
		      [NSString stringWithFormat:@"values that came back wrong: %@; and the NINE spellings "
						  @"JSON does not have must all be refused", wrong]);
	}

	/* 2. THE MEMBER LISTS, where the old parser was lenient. */
	{
		id array = fn_json_parse("[1,2]", 0);
		id dictionary = fn_json_parse("{\"a\":1,\"b\":2}", 0);

		check("strict-member-lists-need-their-commas",
		      array != nil && [array count] == 2 && dictionary != nil && [dictionary count] == 2 &&
		      fn_json_parse("[]", 0) != nil && fn_json_parse("{}", 0) != nil &&
		      fn_json_refused("[1 2]", 0) && fn_json_refused("{\"a\":1\"b\":2}", 0) &&
		      fn_json_refused("[1,]", 0) && fn_json_refused("{\"a\":1,}", 0),
		      @"a missing comma and a trailing comma are both outside JSON, and the old parser "
		      @"accepted them, which is what made JSON5's trailing comma untestable");
	}

	/* 3. THE OTHER JSON5 SPELLINGS, REFUSED WHERE THEY ARE NOT JSON. */
	{
		check("strict-leaves-the-other-json5-spellings-alone",
		      fn_json_refused("['a']", 0) && fn_json_refused("[\"a\" 'b']", 0) &&
		      fn_json_refused("[\"\\q\"]", 0) && fn_json_refused("[\"\\x41\"]", 0) &&
		      fn_json_refused("[/*c*/1]", 0) && fn_json_refused("[1]//tail", 0) &&
		      fn_json_refused("[1,/*c*/2]", 0) &&
		      fn_json_refused("[\302\2401]", 0) &&		/* U+00A0 between the brackets */
		      fn_json_refused("[\342\200\2501]", 0),		/* U+2028 */
		      @"JSON has one quote character, no comments, no \\x, no \\q and four blank space "
		      @"characters");
	}

	/* 4. COMMENTS (JSON5). */
	{
		id array = fn_json_parse("/* leading */ [1, 2] // trailing", NSJSONReadingJSON5Allowed);
		id inside = fn_json_parse("[1, /* two */ 2]", NSJSONReadingJSON5Allowed);

		check("json5-comments-are-skipped",
		      array != nil && [array count] == 2 && inside != nil && [inside count] == 2 &&
		      fn_json_refused("[1, /* here", NSJSONReadingJSON5Allowed) &&
		      fn_json_refused("[1 / 2]", NSJSONReadingJSON5Allowed),
		      @"both comment forms are JSON5's, an unclosed one is an error rather than a silent end, "
		      @"and a solitary slash is not blank space");
	}

	/* 5. THE TWO QUOTE CHARACTERS, AND THE ESCAPES JSON5 ADDS. */
	{
		id single = fn_json_parse("['a']", NSJSONReadingJSON5Allowed);
		id escapedQuote = fn_json_parse("['\\'']", NSJSONReadingJSON5Allowed);
		id hex = fn_json_parse("[\"\\x41\"]", NSJSONReadingJSON5Allowed);
		id vertical = fn_json_parse("[\"\\v\"]", NSJSONReadingJSON5Allowed);
		id nul = fn_json_parse("[\"\\0\"]", NSJSONReadingJSON5Allowed);
		id continued = fn_json_parse("[\"a\\\nb\"]", NSJSONReadingJSON5Allowed);
		id anyEscape = fn_json_parse("[\"\\q\"]", NSJSONReadingJSON5Allowed);

		check("json5-strings-come-in-two-quotes-and-more-escapes",
		      single != nil && [[single objectAtIndex:0] isEqualToString:@"a"] &&
		      escapedQuote != nil && [[escapedQuote objectAtIndex:0] isEqualToString:@"'"] &&
		      hex != nil && [[hex objectAtIndex:0] isEqualToString:@"A"] &&
		      vertical != nil && [[vertical objectAtIndex:0] length] == 1 &&
		      [[vertical objectAtIndex:0] characterAtIndex:0] == 11 &&
		      nul != nil && [[nul objectAtIndex:0] length] == 1 &&
		      [[nul objectAtIndex:0] characterAtIndex:0] == 0 &&
		      continued != nil && [[continued objectAtIndex:0] isEqualToString:@"ab"] &&
		      anyEscape != nil && [[anyEscape objectAtIndex:0] isEqualToString:@"q"],
		      @"a single-quoted string, an escaped quote, \\x41 as A, \\v, \\0, a line continuation "
		      @"that REMOVES the newline, and \\q as q");
	}

	/* 6. THE NUMBERS JSON5 ADDS, BY VALUE. */
	{
		id numbers = fn_json_parse("[+1,.5,5.,0x1F,Infinity,-Infinity,NaN]",
					   NSJSONReadingJSON5Allowed);

		check("json5-numbers",
		      numbers != nil && [numbers count] == 7 &&
		      fn_json_is_integer([numbers objectAtIndex:0], 1) &&
		      fn_json_is_double([numbers objectAtIndex:1], 0.5) &&
		      fn_json_is_double([numbers objectAtIndex:2], 5.0) &&
		      fn_json_is_double([numbers objectAtIndex:3], 31.0) &&
		      fn_json_is_double([numbers objectAtIndex:4], HUGE_VAL) &&
		      fn_json_is_double([numbers objectAtIndex:5], -HUGE_VAL) &&
		      [[numbers objectAtIndex:6] doubleValue] != [[numbers objectAtIndex:6] doubleValue],
		      @"a leading +, a leading dot, a trailing dot, hexadecimal, both infinities and NaN");
	}

	/* 7. THE TRAILING COMMA - which is why the strict comma rule above had to be fixed first. */
	{
		id array = fn_json_parse("[1,2,]", NSJSONReadingJSON5Allowed);
		id dictionary = fn_json_parse("{a:1,}", NSJSONReadingJSON5Allowed);

		check("json5-member-lists-may-end-in-a-comma",
		      array != nil && [array count] == 2 && dictionary != nil && [dictionary count] == 1 &&
		      fn_json_refused("[1 2]", NSJSONReadingJSON5Allowed) &&
		      fn_json_refused("[1,,2]", NSJSONReadingJSON5Allowed),
		      @"a trailing comma is legal in JSON5 and a MISSING one still is not, in either grammar");
	}

	/* 8. THE UNQUOTED KEY, AND ITS STATED BOUNDARY. */
	{
		id keys = fn_json_parse("{a:1, $b:2, _c:3, a1:4}", NSJSONReadingJSON5Allowed);
		id quoted = fn_json_parse("{'q':1}", NSJSONReadingJSON5Allowed);
		id escaped = fn_json_parse("{\\u0061:1}", NSJSONReadingJSON5Allowed);

		check("json5-keys-may-be-unquoted",
		      keys != nil && [keys count] == 4 &&
		      fn_json_is_integer([keys objectForKey:@"a"], 1) &&
		      fn_json_is_integer([keys objectForKey:@"$b"], 2) &&
		      fn_json_is_integer([keys objectForKey:@"_c"], 3) &&
		      fn_json_is_integer([keys objectForKey:@"a1"], 4) &&
		      quoted != nil && [quoted count] == 1 &&
		      escaped != nil && [escaped count] == 1 &&
		      fn_json_is_integer([escaped objectForKey:@"a"], 1) &&
		      fn_json_refused("{\303\251:1}", NSJSONReadingJSON5Allowed) &&	/* a non-ASCII letter */
		      fn_json_refused("{1a:1}", NSJSONReadingJSON5Allowed),
		      @"an identifier name, an escaped one, a single-quoted one - and the STATED boundary: a "
		      @"key whose letter is not ASCII is refused, because this library has no ES5 identifier "
		      @"table and guessing one would be inventing a grammar");
	}

	/* 9. THE FLAG IS ADDITIVE: a strict document answers the same with and without it. */
	{
		const char *text = "{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true,\"e\":null}";
		id strict = fn_json_parse(text, 0);
		id relaxed = fn_json_parse(text, NSJSONReadingJSON5Allowed);

		check("json5-leaves-a-strict-document-alone",
		      strict != nil && relaxed != nil && [strict isEqual:relaxed],
		      @"json5Allowed turns deviations ON rather than switching to another dialect");
	}

	/* 10. THE BLANK SPACE SET, which is JSON5's rather than ASCII's. */
	{
		/* U+00A0, then U+2028, then U+FEFF: one of each of the three UTF-8 shapes. */
		const char *text = "[\302\2401\342\200\250,\357\273\2772]";
		id spaced = fn_json_parse(text, NSJSONReadingJSON5Allowed);

		check("json5-blank-space-is-not-only-ascii",
		      spaced != nil && [spaced count] == 2 &&
		      fn_json_is_integer([spaced objectAtIndex:0], 1) &&
		      fn_json_is_integer([spaced objectAtIndex:1], 2) &&
		      fn_json_refused(text, 0),
		      @"the no-break space, the line separator and the byte order mark are JSON5 blank "
		      @"space and are not JSON's");
	}

	/* 11. THE TOP-LEVEL DICTIONARY FORM. */
	{
		id assumed = fn_json_parse("\"a\": 1, \"b\": 2", NSJSONReadingTopLevelDictionaryAssumed);
		id insideArray = fn_json_parse("[1,2]",
					       NSJSONReadingTopLevelDictionaryAssumed | NSJSONReadingJSON5Allowed);

		check("the-assumed-top-level-dictionary-form",
		      assumed != nil && [assumed isKindOfClass:[NSDictionary class]] &&
		      [assumed count] == 2 && fn_json_is_integer([assumed objectForKey:@"a"], 1) &&
		      fn_json_is_integer([assumed objectForKey:@"b"], 2) &&
		      fn_json_refused("\"a\": 1, \"b\": 2", 0) &&
		      insideArray != nil && [insideArray isKindOfClass:[NSArray class]],
		      @"the document IS the body when the option says so, and without the option it is not a "
		      @"document at all");
	}

	/* 12. WHAT THE OPTION DOES NOT DO: only ADD a form. */
	{
		id braced = fn_json_parse("{\"a\":1}", NSJSONReadingTopLevelDictionaryAssumed);
		id array = fn_json_parse("[1,2]", NSJSONReadingTopLevelDictionaryAssumed);
		id fragment = fn_json_parse("1", NSJSONReadingTopLevelDictionaryAssumed);

		check("the-assumed-form-only-adds",
		      braced != nil && [braced isKindOfClass:[NSDictionary class]] && [braced count] == 1 &&
		      array != nil && [array isKindOfClass:[NSArray class]] && [array count] == 2 &&
		      fragment == nil,
		      @"a document that begins with a brace or a bracket parses exactly as it did - the "
		      @"option adds the brace-less dictionary and does not touch the rest, and a bare scalar "
		      @"is still gated by fragmentsAllowed");
	}

	/* 13. THE DEPRECATED SPELLING: the same bit, AND the same behaviour. */
	{
		id fragment = fn_json_parse("42", NSJSONReadingAllowFragments);

		check("the-deprecated-allow-fragments-spelling-carries-the-same-bit",
		      NSJSONReadingAllowFragments == NSJSONReadingFragmentsAllowed &&
		      NSJSONReadingJSON5Allowed == (1UL << 3) &&
		      NSJSONReadingTopLevelDictionaryAssumed == (1UL << 4) &&
		      fragment != nil && fn_json_is_integer(fragment, 42) &&
		      fn_json_refused("42", 0),
		      @"Apple's deprecated name carries its modern bit, the two new bits are Apple's, and "
		      @"the old spelling really opens the door it names");
	}

	printf("FOUNDATION-JSON RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-JSON-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-JSON DONE\n");
	return failc ? 1 : 0;
}
