/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlcomponents, unit of 1 — F13.15's acceptance for NSURLComponents.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE CHECKS THAT EARN THEIR PLACE ARE THE LAST THREE, because their expected values come from
 * RFC 3986 ITSELF — §5.4's table of reference resolutions — and not from anything this file could
 * have been written to match. A resolution algorithm that agrees with the document that defines it
 * on the document's own examples is a resolution algorithm, which is a stronger claim than any
 * number of round trips.
 *
 * WHAT THE REST MEASURES:
 *   components-parse-the-field  one URL into its eight fields, including the two accessors that
 *                               differ: -path DECODES and -percentEncodedPath does not;
 *   components-render           the string it was given comes back, and an edited field changes it;
 *   components-query-items      "a=1&b=2&flag" as three items, and back again;
 *   components-ipv6-and-port    a literal host with a port, where a colon inside the brackets is
 *                               not a port separator;
 *   components-url-and-copy     -URL, equality, and a copy that is not the same object;
 *   url-relative-door           NSURL's +URLWithString:relativeToURL: — F8 refused it by name, and
 *                               it is here because the resolution it needs now exists.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLCOMPONENTS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLCOMPONENTS %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* THE RESOLUTION, as one call, so the RFC's table can be read as a table. */
static NSString *fn_resolve(NSString *reference, NSString *base)
{
	NSURL *resolved = [NSURL URLWithString:reference relativeToURL:[NSURL URLWithString:base]];

	return resolved != nil ? [resolved absoluteString] : @"(nil)";
}

int main(void)
{
	{
		NSURLComponents *components = [NSURLComponents componentsWithString:
			@"https://user:pw@example.com:8080/a/b?x=1&y=2#frag"];

		check("components-parse-the-field",
		      components != nil &&
		      [[components scheme] isEqualToString:@"https"] &&
		      [[components user] isEqualToString:@"user"] &&
		      [[components password] isEqualToString:@"pw"] &&
		      [[components host] isEqualToString:@"example.com"] &&
		      [[components port] intValue] == 8080 &&
		      [[components path] isEqualToString:@"/a/b"] &&
		      [[components query] isEqualToString:@"x=1&y=2"] &&
		      [[components fragment] isEqualToString:@"frag"],
		      [NSString stringWithFormat:@"%@ / %@ / %@ / %@ / %@ / %@ / %@ / %@",
			[components scheme], [components user], [components password],
			[components host], [components port], [components path], [components query],
			[components fragment]]);
	}

	{
		/* THE TWO ACCESSORS THAT DIFFER, which is the whole reason a components object exists. */
		NSURLComponents *components = [NSURLComponents componentsWithString:
			@"http://example.com/a%20b/c%2Fd?q=a%20b"];

		check("components-decodes-and-keeps",
		      components != nil &&
		      [[components percentEncodedPath] isEqualToString:@"/a%20b/c%2Fd"] &&
		      [[components path] isEqualToString:@"/a b/c/d"] &&
		      [[components percentEncodedQuery] isEqualToString:@"q=a%20b"] &&
		      [[components query] isEqualToString:@"q=a b"],
		      [NSString stringWithFormat:@"raw=%@ decoded=%@",
			[components percentEncodedPath], [components path]]);
	}

	{
		NSURLComponents *components = [NSURLComponents componentsWithString:@"http://a/b?q=1#f"];
		NSString *before = [components string];

		[components setHost:@"elsewhere.example"];
		[components setPort:[NSNumber numberWithInt:8443]];
		check("components-render",
		      before != nil && [before isEqualToString:@"http://a/b?q=1#f"] &&
		      [[components string] isEqualToString:@"http://elsewhere.example:8443/b?q=1#f"],
		      [NSString stringWithFormat:@"before=%@ after=%@", before, [components string]]);
	}

	{
		NSURLComponents *components = [NSURLComponents componentsWithString:@"http://a/b?a=1&b=2&flag"];
		NSArray *items = [components queryItems];

		check("components-query-items",
		      items != nil && [items count] == 3 &&
		      [[[items objectAtIndex:0] name] isEqualToString:@"a"] &&
		      [[[items objectAtIndex:0] value] isEqualToString:@"1"] &&
		      [[[items objectAtIndex:2] name] isEqualToString:@"flag"] &&
		      [[items objectAtIndex:2] value] == nil,
		      [NSString stringWithFormat:@"items=%@", items]);
	}

	{
		NSURLComponents *components = [NSURLComponents componentsWithString:
			@"http://[2001:db8::1]:8080/x"];

		check("components-ipv6-and-port",
		      components != nil &&
		      [[components host] isEqualToString:@"[2001:db8::1]"] &&
		      [[components port] intValue] == 8080 &&
		      [[components path] isEqualToString:@"/x"],
		      [NSString stringWithFormat:@"host=%@ port=%@ path=%@", [components host],
			[components port], [components path]]);
	}

	{
		NSURLComponents *components = [NSURLComponents componentsWithString:@"http://a/b"];
		NSURLComponents *same = [NSURLComponents componentsWithString:@"http://a/b"];
		NSURLComponents *copy = [components copy];

		check("components-url-and-copy",
		      [components URL] != nil &&
		      [[[components URL] absoluteString] isEqualToString:@"http://a/b"] &&
		      [components isEqual:same] && copy != components && [copy isEqual:components],
		      [NSString stringWithFormat:@"url=%@", [components URL]]);
	}

	{
		/* RFC 3986 §5.4.1, the NORMAL examples, with the base the document uses. */
		NSString *base = @"http://a/b/c/d;p?q";
		struct { const char *reference; const char *expected; } rows[] = {
			{ "g:h", "g:h" },
			{ "g", "http://a/b/c/g" },
			{ "./g", "http://a/b/c/g" },
			{ "g/", "http://a/b/c/g/" },
			{ "/g", "http://a/g" },
			{ "?y", "http://a/b/c/d;p?y" },
			{ "g?y", "http://a/b/c/g?y" },
			{ "#s", "http://a/b/c/d;p?q#s" },
			{ ";x", "http://a/b/c/;x" },
			{ "", "http://a/b/c/d;p?q" }
		};
		int good = 1;
		NSMutableString *detail = [NSMutableString string];
		NSUInteger i;

		for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
			NSString *got = fn_resolve([NSString stringWithUTF8String:rows[i].reference], base);
			NSString *want = [NSString stringWithUTF8String:rows[i].expected];

			if (![got isEqualToString:want]) {
				good = 0;
				[detail appendFormat:@" [%s -> %@ want %@]", rows[i].reference, got, want];
			}
		}
		check("rfc3986-normal-examples", good,
		      good ? @"all ten rows" : detail);
	}

	{
		/* RFC 3986 §5.4.2, which is where dot-segment removal is really exercised. */
		NSString *base = @"http://a/b/c/d;p?q";
		struct { const char *reference; const char *expected; } rows[] = {
			{ "./../g", "http://a/b/g" },
			{ "../..", "http://a/" },
			{ "../../g", "http://a/g" },
			{ "../../../g", "http://a/g" },
			{ "../g/", "http://a/b/g/" },
			{ "g.", "http://a/b/c/g." },
			{ ".g", "http://a/b/c/.g" },
			{ "..g", "http://a/b/c/..g" }
		};
		int good = 1;
		NSMutableString *detail = [NSMutableString string];
		NSUInteger i;

		for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
			NSString *got = fn_resolve([NSString stringWithUTF8String:rows[i].reference], base);
			NSString *want = [NSString stringWithUTF8String:rows[i].expected];

			if (![got isEqualToString:want]) {
				good = 0;
				[detail appendFormat:@" [%s -> %@ want %@]", rows[i].reference, got, want];
			}
		}
		check("rfc3986-abnormal-examples", good,
		      good ? @"all eight rows" : detail);
	}

	{
		NSURL *relative = [NSURL URLWithString:@"../g" relativeToURL:
					[NSURL URLWithString:@"http://a/b/c/d;p?q"]];
		NSURL *plain = [NSURL URLWithString:@"http://a/b/c/d;p?q"];

		check("url-relative-door",
		      relative != nil &&
		      [[relative absoluteString] isEqualToString:@"http://a/b/g"] &&
		      plain != nil,
		      [NSString stringWithFormat:@"resolved=%@ base=%@",
			relative != nil ? [relative absoluteString] : @"(nil)", plain]);
	}

	printf("FOUNDATION-URLCOMPONENTS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-URLCOMPONENTS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLCOMPONENTS DONE\n");
	return failc ? 1 : 0;
}
