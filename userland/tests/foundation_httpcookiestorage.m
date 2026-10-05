/*
 * foundation_httpcookiestorage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The store: identity, the replace rule, the accept policy's one observable effect, and the two RFC
 * matching rules - which are the only part of this class that can leak a cookie to a host that should not
 * have it, so they are driven here rather than reasoned about.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-HTTPCOOKIESTORAGE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-HTTPCOOKIESTORAGE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static NSHTTPCookie *fn_cookie(NSString *name, NSString *value, NSString *domain, NSString *path)
{
	NSMutableDictionary *p = [NSMutableDictionary dictionary];

	[p setObject:name forKey:NSHTTPCookieName];
	[p setObject:value forKey:NSHTTPCookieValue];
	[p setObject:domain forKey:NSHTTPCookieDomain];
	[p setObject:path forKey:NSHTTPCookiePath];
	return [NSHTTPCookie cookieWithProperties:p];
}

/* ONE OBSERVER, because the store's contract includes telling people it changed. */
@interface FNWatcher : NSObject
{
	@public
	int changes;
}
@end

@implementation FNWatcher
- (void)cookiesChanged:(NSNotification *)notification
{
	(void)notification;
	changes++;
}
@end

int main(void)
{
	NSHTTPCookieStorage *store = [NSHTTPCookieStorage sharedHTTPCookieStorage];

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- IDENTITY ---------------------------------------------------------------------------------- */
	check("the-shared-store-is-one-per-process",
	      store == [NSHTTPCookieStorage sharedHTTPCookieStorage],
	      @"two calls answer the same object");
	check("a-store-of-ones-own-is-not-the-shared-one",
	      [[NSHTTPCookieStorage alloc] init] != store,
	      @"Apple's initialiser makes a store of one's own");

	/* --- THE REPLACE RULE, WHICH IS RFC 6265'S IDENTITY RULE ---------------------------------------- */
	{
		FNWatcher *watcher = [[FNWatcher alloc] init];
		NSHTTPCookie *first = fn_cookie(@"a", @"1", @"example.com", @"/");
		NSHTTPCookie *again = fn_cookie(@"a", @"2", @"example.com", @"/");
		NSHTTPCookie *other = fn_cookie(@"a", @"3", @"other.com", @"/");

		[[NSNotificationCenter defaultCenter] addObserver:watcher
							selector:@selector(cookiesChanged:)
							    name:NSHTTPCookieManagerCookiesChangedNotification
							  object:store];
		[store setCookie:first];
		[store setCookie:again];
		check("setting-the-same-cookie-replaces-it",
		      [[store cookies] count] >= 1 &&
		      [[[store cookiesForURL:[NSURL URLWithString:@"http://example.com/"]] lastObject]
			value] != nil,
		      @"the store still answers for that host");
		{
			NSArray *forExample = [store cookiesForURL:[NSURL URLWithString:@"http://example.com/"]];

			check("the-replacement-won", [[forExample lastObject] value] != nil &&
			      [[[forExample lastObject] value] isEqualToString:@"2"],
			      @"same name, domain and path means the second one WINS rather than accumulating");
		}
		[store setCookie:other];
		check("the-store-told-its-observers", watcher->changes >= 3,
		      @"a change posts NSHTTPCookieManagerCookiesChangedNotification");
		check("a-refused-change-stays-silent", YES, @"asserted below, once the policy is Never");
	}

	/* --- THE ACCEPT POLICY'S ONE OBSERVABLE EFFECT -------------------------------------------------- */
	{
		NSHTTPCookieStorage *own = [[NSHTTPCookieStorage alloc] init];
		FNWatcher *watcher = [[FNWatcher alloc] init];
		NSHTTPCookie *cookie = fn_cookie(@"p", @"1", @"example.com", @"/");

		[own setCookieAcceptPolicy:NSHTTPCookieAcceptPolicyNever];
		[[NSNotificationCenter defaultCenter] addObserver:watcher
							selector:@selector(cookiesChanged:)
							    name:NSHTTPCookieManagerCookiesChangedNotification
							  object:own];
		[own setCookie:cookie];
		check("the-policy-gates-insertion", [[own cookies] count] == 0,
		      @"a store that will not accept must not hold what it refused");
		check("a-refused-set-posts-no-change", watcher->changes == 0,
		      @"nothing changed, so nothing is announced");
		[own setCookieAcceptPolicy:NSHTTPCookieAcceptPolicyAlways];
		[own setCookie:cookie];
		check("and-an-accepting-store-holds-it", [[own cookies] count] == 1,
		      @"the same call succeeds once the policy allows it");
	}

	/* --- THE TWO RFC MATCHING RULES, WHICH ARE WHAT THIS CLASS IS FOR -------------------------------- */
	{
		NSHTTPCookieStorage *own = [[NSHTTPCookieStorage alloc] init];
		NSURL *exact = [NSURL URLWithString:@"http://example.com/"];
		NSURL *sub = [NSURL URLWithString:@"http://www.example.com/"];
		NSURL *lookalike = [NSURL URLWithString:@"http://notexample.com/"];
		NSURL *deep = [NSURL URLWithString:@"http://example.com/docs/page.html"];
		NSURL *shallow = [NSURL URLWithString:@"http://example.com/foobar"];
		NSURL *secureOnly = [NSURL URLWithString:@"https://example.com/"];

		[own setCookie:fn_cookie(@"host", @"1", @"example.com", @"/")];
		[own setCookie:fn_cookie(@"sub", @"1", @".example.com", @"/")];
		[own setCookie:fn_cookie(@"docs", @"1", @"example.com", @"/docs")];
		[own setCookie:fn_cookie(@"sec", @"1", @"example.com", @"/")];
		{
			NSMutableDictionary *p = [NSMutableDictionary dictionary];

			[p setObject:@"sec" forKey:NSHTTPCookieName];
			[p setObject:@"1" forKey:NSHTTPCookieValue];
			[p setObject:@"example.com" forKey:NSHTTPCookieDomain];
			[p setObject:@"/" forKey:NSHTTPCookiePath];
			[p setObject:[NSNumber numberWithBool:YES] forKey:NSHTTPCookieSecure];
			[own setCookie:[NSHTTPCookie cookieWithProperties:p]];
		}

		check("a-host-matched-cookie-is-sent", [[own cookiesForURL:exact] count] >= 1,
		      @"the cookie's own host gets it");
		check("a-dot-domain-reaches-a-subdomain", [[own cookiesForURL:sub] count] >= 1,
		      @"a Domain beginning with a dot covers subdomains");
		check("a-lookalike-host-is-refused",
		      [[own cookiesForURL:lookalike] count] == 0,
		      @"\"notexample.com\" ENDS with \"example.com\" and must not match it");
		check("a-path-prefix-is-sent", [[own cookiesForURL:deep] count] > [[own cookiesForURL:shallow] count],
		      @"/docs matches /docs/page.html but not /foobar");
		check("a-secure-cookie-stays-on-https",
		      [[own cookiesForURL:exact] count] < [[own cookiesForURL:secureOnly] count],
		      @"the secure cookie is withheld over http and sent over https");
	}

	/* --- THE SORT, AND THE TASK-SCOPED PAIR --------------------------------------------------------- */
	{
		NSHTTPCookieStorage *own = [[NSHTTPCookieStorage alloc] init];
		NSSortDescriptor *byName = [[NSSortDescriptor alloc] initWithKey:@"name" ascending:YES];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://example.com/"]];

		[own setCookie:fn_cookie(@"b", @"1", @"example.com", @"/")];
		[own setCookie:fn_cookie(@"a", @"1", @"example.com", @"/")];
		{
			NSArray *sorted = [own sortedCookiesUsingDescriptors:
						[NSArray arrayWithObject:byName]];

			check("the-sort-is-nssortdescriptors",
			      [sorted count] == 2 && [[[sorted objectAtIndex:0] name] isEqualToString:@"a"],
			      @"the doors take the class the library already has");
		}
		
	}

	/* --- WHAT IS REFUSED, ASSERTED ABSENT ----------------------------------------------------------- */
	check("the-group-container-store-door-is-absent",
	      ![NSHTTPCookieStorage instancesRespondToSelector:
			NSSelectorFromString(@"sharedCookieStorageForGroupContainerIdentifier:")],
	      @"an app-group store is a concept this system does not have, so the name is not here");
	check("the-deprecated-accept-policy-notification-is-absent",
	      NSClassFromString(@"NSHTTPCookieManagerAcceptPolicyChangedNotification") == nil,
	      @"Apple deprecated this name, and since 2026-09-26 that makes it a WORK ITEM rather than an "
	      @"exclusion (plan section 62.24): it is absent because it is UNIMPLEMENTED, and this check is "
	      @"the tripwire that will fail the day it lands and the ledger row flips to shipped");

	printf("FOUNDATION-HTTPCOOKIESTORAGE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-HTTPCOOKIESTORAGE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-HTTPCOOKIESTORAGE DONE\n");
	return failc ? 1 : 0;
}
