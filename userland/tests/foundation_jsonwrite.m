/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_jsonwrite.m — THE PROBE FOR §62.94: the two JSON WRITING options, the last rows of the
 * JSON family, and the two reasons each was smaller than the note that had deferred them.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE DEFAULT ALREADY ESCAPES A SLASH — asserted by TEXT (`http:\/\/a\/b`) and not merely by "it
 *     encoded", because the whole question is what the bytes are. `withoutEscapingSlashes` was
 *     deferred as "a change to the default output rather than an addition", and this check is the
 *     measurement that says otherwise: the escape has been there all along.
 *   * AND THE OPTION TURNS IT OFF, in both directions: the unescaped text appears WITH the flag and
 *     the escaped one without it, and BOTH texts read back to the same string — an escape a reader
 *     cannot undo would make the default wrong rather than merely different.
 *   * THE OPTION CHANGES NOTHING ELSE: a string carrying a quote, a backslash, a tab, a newline and a
 *     control character still escapes every one of them with the flag set, so "unescaped slashes" is
 *     not "no escaping".
 *   * THE FRAGMENT DOOR HAS TWO HALVES AND THE PROBE PINS BOTH: a top-level scalar is REFUSED with no
 *     options (and `+isValidJSONObject:` still answers NO for it — the flag is a second question, not
 *     a change to the first), and ENCODED with `writingFragmentsAllowed`, for a number, a string, a
 *     null and a boolean.
 *   * AND THE FLAG DOES NOT BLESS AN INVALID OBJECT: a date, and a NaN number, are refused with it
 *     set, because the option opens the TOP LEVEL's question and not the nested rules'.
 *   * THE BITS ARE APPLE'S, all four of them, because a name carrying Apple's own value is the only
 *     kind of option that is a fidelity gain rather than a guess.
 *   * THE TWO FLAGS COMBINE, which is the one case the reading side's `json5Allowed` cannot express:
 *     a fragment whose text contains a slash answers `"a/b"` exactly.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-JSONWRITE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-JSONWRITE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* ONE DOOR FOR EVERY WRITE: the text, or nil when the door refuses (it raises for an invalid object,
 * which is this library's contract and therefore the probe's expected path). */
static NSString *fn_write(id object, NSJSONWritingOptions options)
{
	@try {
		NSData *data = [NSJSONSerialization dataWithJSONObject:object options:options error:NULL];

		if (data == nil) {
			return nil;
		}
		return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	} @catch (NSException *e) {
		return nil;
	}
}

static BOOL fn_write_raises(id object, NSJSONWritingOptions options)
{
	return fn_write(object, options) == nil;
}

/* THE OUTPUT IS PARSED BACK WITH THE READING DOOR, so "the escape is real" is a round trip and not a
 * substring hunt alone. */
static id fn_read(NSString *text, NSJSONReadingOptions options)
{
	const char *utf8 = [text UTF8String];
	NSData *data = [NSData dataWithBytes:utf8 length:strlen(utf8)];

	return [NSJSONSerialization JSONObjectWithData:data options:options error:NULL];
}

int main(void)
{
	NSDictionary *urls = @{ @"u" : @"http://a/b" };

	/* 1. THE DEFAULT, BY TEXT AND BY ROUND TRIP. */
	{
		NSString *text = fn_write(urls, 0);
		id back = text != nil ? fn_read(text, 0) : nil;

		check("the-default-escapes-a-slash",
		      text != nil && [text rangeOfString:@"http:\\/\\/a\\/b"].location != NSNotFound &&
		      [text rangeOfString:@"http://a/b"].location == NSNotFound &&
		      back != nil && [[back objectForKey:@"u"] isEqualToString:@"http://a/b"],
		      [NSString stringWithFormat:@"expected the escaped text, got: %@", text]);
	}

	/* 2. THE OPTION TURNS THE ESCAPE OFF — and the reader undoes either spelling. */
	{
		NSString *text = fn_write(urls, NSJSONWritingWithoutEscapingSlashes);
		id back = text != nil ? fn_read(text, 0) : nil;

		check("the-option-stops-escaping-slashes",
		      text != nil && [text rangeOfString:@"http://a/b"].location != NSNotFound &&
		      [text rangeOfString:@"\\/"].location == NSNotFound &&
		      back != nil && [[back objectForKey:@"u"] isEqualToString:@"http://a/b"],
		      [NSString stringWithFormat:@"expected the raw text, got: %@", text]);
	}

	/* 3. AND NOTHING ELSE CHANGES. */
	{
		NSString *awkward = @"q\"b\\s\tt\nn\001";
		NSString *text = fn_write(@[ awkward ], NSJSONWritingWithoutEscapingSlashes);

		check("the-option-changes-nothing-else",
		      text != nil && [text rangeOfString:@"\\\""].location != NSNotFound &&
		      [text rangeOfString:@"\\\\"].location != NSNotFound &&
		      [text rangeOfString:@"\\t"].location != NSNotFound &&
		      [text rangeOfString:@"\\n"].location != NSNotFound &&
		      [text rangeOfString:@"\\u0001"].location != NSNotFound &&
		      [text rangeOfString:awkward].location == NSNotFound,
		      [NSString stringWithFormat:@"a quote, a backslash, a tab, a newline and a control "
						  @"character must all stay escaped: %@", text]);
	}

	/* 4. THE FRAGMENT IS REFUSED BY DEFAULT, AND THE OTHER QUESTION STILL SAYS NO. */
	{
		check("a-top-level-scalar-is-refused-by-default",
		      fn_write_raises(@42, 0) && fn_write_raises(@"hi", 0) &&
		      fn_write_raises([NSNull null], 0) &&
		      ![NSJSONSerialization isValidJSONObject:@42] &&
		      ![NSJSONSerialization isValidJSONObject:@"hi"] &&
		      ![NSJSONSerialization isValidJSONObject:[NSNull null]],
		      @"a bare number, string or null is not a document unless the caller says so, and "
		      @"+isValidJSONObject: keeps answering the option-less question");
	}

	/* 5. THE OPTION WRITES ONE — for all four scalar kinds. */
	{
		NSString *number = fn_write(@42, NSJSONWritingFragmentsAllowed);
		NSString *string = fn_write(@"hi", NSJSONWritingFragmentsAllowed);
		NSString *null = fn_write([NSNull null], NSJSONWritingFragmentsAllowed);
		NSString *boolean = fn_write(@YES, NSJSONWritingFragmentsAllowed);
		id back = number != nil ? fn_read(number, NSJSONReadingFragmentsAllowed) : nil;

		check("the-fragment-option-writes-a-scalar",
		      number != nil && [number isEqualToString:@"42"] &&
		      string != nil && [string isEqualToString:@"\"hi\""] &&
		      null != nil && [null isEqualToString:@"null"] &&
		      boolean != nil && [boolean isEqualToString:@"true"] &&
		      back != nil && [back intValue] == 42,
		      [NSString stringWithFormat:@"number=%@ string=%@ null=%@ boolean=%@", number, string,
						  null, boolean]);
	}

	/* 6. AND IT DOES NOT BLESS AN INVALID OBJECT. */
	{
		check("the-fragment-option-does-not-bless-an-invalid-object",
		      fn_write_raises([NSDate date], NSJSONWritingFragmentsAllowed) &&
		      fn_write_raises(@[ [NSDate date] ], NSJSONWritingFragmentsAllowed) &&
		      fn_write_raises([NSNumber numberWithDouble:NAN], NSJSONWritingFragmentsAllowed) &&
		      fn_write_raises(@[ [NSNumber numberWithDouble:INFINITY] ],
				      NSJSONWritingFragmentsAllowed) &&
		      ![NSJSONSerialization isValidJSONObject:@[ [NSDate date] ]],
		      @"the option opens the TOP LEVEL's question, not the nested rules': a date and a NaN "
		      @"are still not JSON");
	}

	/* 7. THE FOUR BITS ARE APPLE'S. */
	check("the-writing-bit-values-are-apples",
	      NSJSONWritingPrettyPrinted == (1UL << 0) && NSJSONWritingSortedKeys == (1UL << 1) &&
	      NSJSONWritingFragmentsAllowed == (1UL << 2) &&
	      NSJSONWritingWithoutEscapingSlashes == (1UL << 3),
	      @"the four writing option values Apple's own header publishes");

	/* 8. THE TWO FLAGS COMBINE. */
	{
		NSJSONWritingOptions both =
			NSJSONWritingFragmentsAllowed | NSJSONWritingWithoutEscapingSlashes;
		NSString *text = fn_write(@"a/b", both);
		NSString *number = fn_write(@42, both);

		check("the-two-flags-combine",
		      text != nil && [text isEqualToString:@"\"a/b\""] &&
		      number != nil && [number isEqualToString:@"42"],
		      [NSString stringWithFormat:@"expected exactly \"a/b\" and 42, got: %@ and %@", text,
						  number]);
	}

	printf("FOUNDATION-JSONWRITE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-JSONWRITE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-JSONWRITE DONE\n");
	return failc ? 1 : 0;
}
