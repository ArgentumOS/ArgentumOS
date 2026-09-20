/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_regex, unit of 1 — F13.16's acceptance for NSRegularExpression.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>.
 *
 * THE THREE CHECKS THAT EARN THEIR PLACE:
 *   regex-utf16-ranges        the engine counts BYTES and Cocoa counts UTF-16 units, so the ranges
 *                             are asserted by taking the SUBSTRING they point at — an ASCII-only
 *                             test would have passed with no conversion at all;
 *   regex-empty-match-advances a pattern that can match nothing matches at EVERY position, and a
 *                             loop that did not advance would hang rather than fail: 4 matches on
 *                             "bab" is the measurement;
 *   regex-options-report-what-took-effect  a caller who asks for an option POSIX cannot express is
 *                             told, by -options, that it is not in force.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-REGEX %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-REGEX %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* THE SUBSTRING A RANGE POINTS AT, which is how a range is checked when the units matter. */
static NSString *fn_text(NSString *string, NSRange range)
{
	if (range.location == NSNotFound || range.location + range.length > [string length]) {
		return @"(out of range)";
	}
	return [string substringWithRange:range];
}

int main(void)
{
	{
		NSError *error = nil;
		NSRegularExpression *good = [NSRegularExpression regularExpressionWithPattern:@"(\\w+)@(\\w+)"
										      options:0
											error:NULL];
		NSRegularExpression *bad = [NSRegularExpression regularExpressionWithPattern:@"(unclosed"
										     options:0
										       error:&error];

		check("regex-compiles-and-refuses",
		      good != nil && [good numberOfCaptureGroups] == 2 &&
		      [[good pattern] length] > 0 &&
		      bad == nil && error != nil && [[error localizedDescription] length] > 0,
		      [NSString stringWithFormat:@"captures=%lu badRefused=%d error=%@",
			(unsigned long)(good != nil ? [good numberOfCaptureGroups] : 0),
			(int)(bad == nil), error != nil ? [error localizedDescription] : @"(none)"]);
	}

	{
		NSRegularExpression *digits = [NSRegularExpression regularExpressionWithPattern:@"[0-9]+"
										       options:0
											 error:NULL];
		NSString *text = @"a1b22c333";
		NSArray *matches = [digits matchesInString:text options:0
						     range:NSMakeRange(0, [text length])];

		check("regex-finds-all-matches",
		      [matches count] == 3 &&
		      fn_text(text, [[matches objectAtIndex:0] range]) != nil &&
		      [fn_text(text, [[matches objectAtIndex:0] range]) isEqualToString:@"1"] &&
		      [fn_text(text, [[matches objectAtIndex:1] range]) isEqualToString:@"22"] &&
		      [fn_text(text, [[matches objectAtIndex:2] range]) isEqualToString:@"333"] &&
		      [[matches objectAtIndex:1] numberOfRanges] == 1,
		      [NSString stringWithFormat:@"count=%lu [%@] [%@] [%@]",
			(unsigned long)[matches count],
			fn_text(text, [[matches objectAtIndex:0] range]),
			fn_text(text, [[matches objectAtIndex:1] range]),
			fn_text(text, [[matches objectAtIndex:2] range])]);
	}

	{
		NSRegularExpression *pair = [NSRegularExpression regularExpressionWithPattern:@"([a-z]+)@([a-z]+)"
										     options:0
										       error:NULL];
		NSString *text = @"user@host";
		NSTextCheckingResult *first = [pair firstMatchInString:text options:0
								 range:NSMakeRange(0, [text length])];

		check("regex-capture-groups",
		      first != nil && [first numberOfRanges] == 3 &&
		      [fn_text(text, [first range]) isEqualToString:@"user@host"] &&
		      [fn_text(text, [first rangeAtIndex:1]) isEqualToString:@"user"] &&
		      [fn_text(text, [first rangeAtIndex:2]) isEqualToString:@"host"] &&
		      [pair numberOfMatchesInString:text options:0
					      range:NSMakeRange(0, [text length])] == 1,
		      [NSString stringWithFormat:@"whole=%@ g1=%@ g2=%@",
			fn_text(text, [first range]), fn_text(text, [first rangeAtIndex:1]),
			fn_text(text, [first rangeAtIndex:2])]);
	}

	{
		NSRegularExpression *plain = [NSRegularExpression regularExpressionWithPattern:@"abc"
										     options:0
										       error:NULL];
		NSRegularExpression *folded = [NSRegularExpression regularExpressionWithPattern:@"abc"
										      options:NSRegularExpressionCaseInsensitive
											error:NULL];
		NSString *text = @"ABC";

		check("regex-case-option",
		      [[plain matchesInString:text options:0 range:NSMakeRange(0, 3)] count] == 0 &&
		      [[folded matchesInString:text options:0 range:NSMakeRange(0, 3)] count] == 1,
		      [NSString stringWithFormat:@"plain=%lu folded=%lu",
			(unsigned long)[[plain matchesInString:text options:0
							 range:NSMakeRange(0, 3)] count],
			(unsigned long)[[folded matchesInString:text options:0
							  range:NSMakeRange(0, 3)] count]]);
	}

	{
		/* A NON-ASCII STRING IS THE ONLY WAY TO SEE THE CONVERSION: the byte offsets the engine
		 * answers with are not the UTF-16 indices Cocoa's ranges are measured in, so a SUBSTRING
		 * taken at the reported range is what proves the mapping. */
		NSRegularExpression *words = [NSRegularExpression regularExpressionWithPattern:@"[^ ]+"
										      options:0
											error:NULL];
		NSString *text = @"héllo wörld";
		NSArray *matches = [words matchesInString:text options:0
						    range:NSMakeRange(0, [text length])];

		check("regex-utf16-ranges",
		      [matches count] == 2 &&
		      [fn_text(text, [[matches objectAtIndex:0] range]) isEqualToString:@"héllo"] &&
		      [fn_text(text, [[matches objectAtIndex:1] range]) isEqualToString:@"wörld"],
		      [NSString stringWithFormat:@"count=%lu [%@] [%@]", (unsigned long)[matches count],
			fn_text(text, [[matches objectAtIndex:0] range]),
			fn_text(text, [[matches objectAtIndex:1] range])]);
	}

	{
		NSRegularExpression *pair = [NSRegularExpression regularExpressionWithPattern:@"([0-9]+)-([0-9]+)"
										     options:0
										       error:NULL];
		NSString *text = @"range 12-34 end";
		NSString *replaced = [pair stringByReplacingMatchesInString:text
								   options:0
								     range:NSMakeRange(0, [text length])
							      withTemplate:@"$2/$1"];

		check("regex-replace-with-template",
		      [replaced isEqualToString:@"range 34/12 end"],
		      [NSString stringWithFormat:@"replaced=%@", replaced]);
	}

	{
		NSRegularExpression *anchored = [NSRegularExpression regularExpressionWithPattern:@"^b"
											 options:NSRegularExpressionAnchorsMatchLines
											   error:NULL];
		NSRegularExpression *flat = [NSRegularExpression regularExpressionWithPattern:@"^b"
										options:0
										  error:NULL];
		NSString *text = @"a\nb";

		check("regex-anchors-option",
		      [[anchored matchesInString:text options:0 range:NSMakeRange(0, 3)] count] == 1 &&
		      [[flat matchesInString:text options:0 range:NSMakeRange(0, 3)] count] == 0,
		      [NSString stringWithFormat:@"anchored=%lu flat=%lu",
			(unsigned long)[[anchored matchesInString:text options:0
							    range:NSMakeRange(0, 3)] count],
			(unsigned long)[[flat matchesInString:text options:0
							range:NSMakeRange(0, 3)] count]]);
	}

	{
		/* THE HONESTY CLAIM: an option POSIX has no spelling for is not silently claimed. */
		NSRegularExpression *asked = [NSRegularExpression
			regularExpressionWithPattern:@"a.b"
					     options:(NSRegularExpressionAllowCommentsAndWhitespace |
						      NSRegularExpressionCaseInsensitive |
						      NSRegularExpressionAnchorsMatchLines)
					       error:NULL];

		check("regex-options-report-what-took-effect",
		      asked != nil &&
		      ([asked options] & NSRegularExpressionAllowCommentsAndWhitespace) == 0 &&
		      ([asked options] & NSRegularExpressionCaseInsensitive) != 0 &&
		      ([asked options] & NSRegularExpressionAnchorsMatchLines) != 0,
		      [NSString stringWithFormat:@"options=%lu", (unsigned long)[asked options]]);
	}

	{
		/* A PATTERN THAT CAN MATCH NOTHING matches at every position; a loop that did not advance
		 * would hang here rather than fail, so the COUNT is the measurement. */
		NSRegularExpression *optional = [NSRegularExpression regularExpressionWithPattern:@"a*"
											 options:0
											   error:NULL];
		NSString *text = @"bab";
		NSArray *matches = [optional matchesInString:text options:0
						       range:NSMakeRange(0, [text length])];

		check("regex-empty-match-advances",
		      [matches count] == 4 && [text length] == 3,
		      [NSString stringWithFormat:@"count=%lu", (unsigned long)[matches count]]);
	}

	printf("FOUNDATION-REGEX RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-REGEX-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-REGEX DONE\n");
	return failc ? 1 : 0;
}
