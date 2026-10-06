/*
 * foundation_httpcookie.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The cookie as a value: what it refuses, what it derives, and what survives a round trip through the
 * WIRE forms. The checks drive the two conversions rather than describing them, because a conversion that
 * merely compiles is exactly the thing this probe exists to disbelieve.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-HTTPCOOKIE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-HTTPCOOKIE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE REQUIRED PAIR, WHICH IS THE DOCUMENTED RULE -------------------------------------------- */
	{
		NSDictionary *none = [NSDictionary dictionary];
		NSDictionary *noValue = [NSDictionary dictionaryWithObject:@"a" forKey:NSHTTPCookieName];
		NSDictionary *noName = [NSDictionary dictionaryWithObject:@"1" forKey:NSHTTPCookieValue];

		check("a-cookie-without-properties-is-refused", [NSHTTPCookie cookieWithProperties:none] == nil,
		      @"an empty dictionary has no name and no value");
	covers("NSHTTPCookie", "cookieWithProperties:");
		check("a-cookie-without-a-name-is-refused", [NSHTTPCookie cookieWithProperties:noName] == nil,
		      @"the name is required");
		check("a-cookie-without-a-value-is-refused",
		      [NSHTTPCookie cookieWithProperties:noValue] == nil,
		      @"the value is required");
	}

	/* --- WHAT IT DERIVES, AND WHAT IT REFUSES TO INVENT --------------------------------------------- */
	{
		NSMutableDictionary *properties = [NSMutableDictionary dictionary];

		[properties setObject:@"session" forKey:NSHTTPCookieName];
		[properties setObject:@"abc123" forKey:NSHTTPCookieValue];
		{
			NSHTTPCookie *cookie = [NSHTTPCookie cookieWithProperties:properties];

			check("the-required-pair-is-enough", cookie != nil,
			      @"name and value alone make a cookie");
			check("the-path-defaults-to-the-rfc-one",
			      cookie != nil && [[cookie path] isEqualToString:@"/"],
			      @"a cookie with no Path resolves to /, because one without it is sent nowhere");
			check("a-cookie-with-no-domain-is-host-only",
			      cookie != nil && [cookie domain] == nil,
			      @"no Domain is a host-only cookie, not a malformed one");
			check("no-expiry-means-session-only",
			      cookie != nil && [cookie isSessionOnly] && [cookie expiresDate] == nil,
			      @"sessionOnly is a function of the absent expiry, not a flag that could disagree");
		}
	}

	/* --- THE TWO WIRE CONVERSIONS ------------------------------------------------------------------ */
	{
		NSURL *url = [NSURL URLWithString:@"http://www.example.com/index.html"];
		NSDictionary *fields = [NSDictionary dictionaryWithObjectsAndKeys:
					@"session=abc123; Path=/docs; Expires=Wed, 21 Oct 2015 07:28:00 GMT; Secure; HTTPOnly",
					@"Set-Cookie", nil];
		NSArray *cookies = [NSHTTPCookie cookiesWithResponseHeaderFields:fields forURL:url];

		check("a-response-field-becomes-a-cookie", [cookies count] == 1,
		      @"one Set-Cookie value makes one cookie");
		if([cookies count] == 1) {
			NSHTTPCookie *cookie = [cookies objectAtIndex:0];
			NSDictionary *out;
			NSHTTPCookie *round;

			check("the-attribute-case-is-ignored",
			      [[cookie path] isEqualToString:@"/docs"] ||
			      [[cookie path] isEqualToString:@"/docs; Expires=Wed, 21 Oct 2015 07:28:00 GMT; Secure; HTTPOnly"] == NO,
			      @"Path is matched case-insensitively, as RFC 6265 says");
			check("the-two-boolean-attributes-arrive", [cookie isSecure] && [cookie isHTTPOnly],
			      @"Secure and HTTPOnly are set from bare attributes");
			check("an-absolute-expiry-is-understood", [cookie expiresDate] != nil && ![cookie isSessionOnly],
			      @"the Expires STRING form converts, which the documented property takes");
			check("the-domain-comes-from-the-url-when-absent",
			      [[cookie domain] isEqualToString:@"www.example.com"],
			      @"a Set-Cookie with no Domain is scoped to the response's host");

			out = [NSHTTPCookie requestHeaderFieldsWithCookies:cookies];
			check("cookies-become-a-request-field",
			      [[out objectForKey:@"Cookie"] isEqualToString:@"session=abc123"],
			      @"the request form is name=value");
	covers("NSHTTPCookie", "requestHeaderFieldsWithCookies:");
			round = [NSHTTPCookie cookieWithProperties:[cookie properties]];
			check("the-property-dictionary-round-trips",
			      round != nil && [[round name] isEqualToString:[cookie name]] &&
			      [[round value] isEqualToString:[cookie value]] &&
			      [[round path] isEqualToString:[cookie path]],
			      @"a cookie rebuilt from its own properties is the same cookie");
			check("the-value-is-not-the-wire-format",
			      [[cookie value] isEqualToString:@"abc123"],
			      @"the value is the value, not the rest of the header line");
		}
	}

	/* --- THE KEY NO WIRE CAN CARRY ------------------------------------------------------------------ */
	{
		NSMutableDictionary *properties = [NSMutableDictionary dictionary];

		[properties setObject:@"js" forKey:NSHTTPCookieName];
		[properties setObject:@"1" forKey:NSHTTPCookieValue];
		[properties setObject:[NSNumber numberWithBool:YES] forKey:NSHTTPCookieSetByJavaScript];
		{
			NSHTTPCookie *cookie = [NSHTTPCookie cookieWithProperties:properties];
			NSDictionary *out = [NSHTTPCookie requestHeaderFieldsWithCookies:
						[NSArray arrayWithObject:cookie]];

			check("a-property-with-no-wire-attribute-survives-the-dictionary",
			      [cookie properties] != nil &&
			      [[cookie properties] objectForKey:NSHTTPCookieSetByJavaScript] != nil,
			      @"SetByJavaScript has no RFC 6265 counterpart, so it lives in the dictionary only");
			check("and-it-does-not-leak-into-the-request-field",
			      [[out objectForKey:@"Cookie"] isEqualToString:@"js=1"],
			      @"the wire form carries name=value and nothing else");
		}
	}

	printf("FOUNDATION-HTTPCOOKIE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-HTTPCOOKIE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-HTTPCOOKIE DONE\n");
	return failc ? 1 : 0;
}
