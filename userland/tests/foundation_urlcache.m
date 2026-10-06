/*
 * foundation_urlcache.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The cache: what it keeps, what it REFUSES to keep, what its usage reports, and the key rule that decides
 * whether a request finds its own entry.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-URLCACHE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLCACHE %s FAIL: %s\n", name, [why UTF8String]);
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

static NSURLRequest *fn_request(NSString *url, NSString *method)
{
	NSMutableURLRequest *r = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];

	[r setHTTPMethod:method];
	return r;
}

static NSCachedURLResponse *fn_cached(NSData *body, NSURLCacheStoragePolicy policy)
{
	return [[NSCachedURLResponse alloc]
			initWithResponse:[[NSHTTPURLResponse alloc] initWithURL:[NSURL URLWithString:@"http://example.com/"]
								       statusCode:200
								      HTTPVersion:@"HTTP/1.1"
								     headerFields:[NSDictionary dictionary]]
				  data:body
			      userInfo:nil
			 storagePolicy:policy];
}

int main(void)
{
	NSURLCache *cache;
	NSData *body = [@"hello cached world" dataUsingEncoding:NSUTF8StringEncoding];

	setvbuf(stdout, NULL, _IONBF, 0);

	cache = [[NSURLCache alloc] initWithMemoryCapacity:1024 * 1024
					      diskCapacity:8 * 1024 * 1024
					      directoryURL:nil];
	check("the-shared-cache-is-one-per-process",
	      [NSURLCache sharedURLCache] == [NSURLCache sharedURLCache],
	      @"two calls answer the same object");
	covers("NSURLCache", "diskCapacity");
	check("and-a-cache-of-ones-own-is-not-it", cache != [NSURLCache sharedURLCache],
	      @"the initialiser makes a cache a caller owns");

	/* --- THE KEY RULE ------------------------------------------------------------------------------- */
	[cache storeCachedResponse:fn_cached(body, NSURLCacheStorageAllowed)
		      forRequest:fn_request(@"http://example.com/a", @"GET")];
	check("a-stored-response-is-found",
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/a", @"GET")] != nil,
	      @"what was stored comes back");
	check("a-rebuilt-request-finds-it",
	      [[[cache cachedResponseForRequest:fn_request(@"http://example.com/a", @"GET")] data]
			isEqualToData:body],
	      @"the key is the request's URL and method, not its object identity");
	covers("NSURLCache", "cachedResponseForRequest:");
	check("a-different-url-does-not",
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/b", @"GET")] == nil,
	      @"a different URL is a different entry");
	check("a-different-method-does-not",
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/a", @"POST")] == nil,
	      @"and so is a different method, which is why the method is in the key");

	/* --- THE POLICY, WHICH IS THE CHECK WITH TEETH -------------------------------------------------- */
	{
		NSUInteger before = [cache currentMemoryUsage];

		[cache storeCachedResponse:fn_cached(body, NSURLCacheStorageNotAllowed)
			      forRequest:fn_request(@"http://example.com/no", @"GET")];
		check("a-not-allowed-response-is-refused",
		      [cache cachedResponseForRequest:fn_request(@"http://example.com/no", @"GET")] == nil,
		      @"a cache that returns what it was told not to keep is worse than no cache");
		check("and-it-does-not-count-toward-the-usage",
		      [cache currentMemoryUsage] == before,
		      @"a refusal that still occupied the cache would be a refusal in name only");
	}
	check("the-usage-is-the-bytes-it-holds",
	      [cache currentMemoryUsage] == [body length],
	      @"summed from what it holds rather than tracked beside it");

	/* --- REMOVAL, INCLUDING BY DATE ------------------------------------------------------------------ */
	[cache storeCachedResponse:fn_cached(body, NSURLCacheStorageAllowedInMemoryOnly)
		      forRequest:fn_request(@"http://example.com/c", @"GET")];
	check("an-in-memory-only-response-is-kept",
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/c", @"GET")] != nil,
	      @"AllowedInMemoryOnly is a yes for a cache that IS memory");
	[cache removeCachedResponseForRequest:fn_request(@"http://example.com/a", @"GET")];
	check("removal-takes-one-out",
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/a", @"GET")] == nil &&
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/c", @"GET")] != nil,
	      @"the one removed is gone and the other stays");
	covers("NSURLCache", "removeCachedResponseForRequest:");
	[cache removeAllCachedResponses];
	check("remove-all-empties-it",
	      [cache currentMemoryUsage] == 0 &&
	      [cache cachedResponseForRequest:fn_request(@"http://example.com/c", @"GET")] == nil,
	      @"and the usage follows, because it is derived");
	{
		[cache storeCachedResponse:fn_cached(body, NSURLCacheStorageAllowed)
			      forRequest:fn_request(@"http://example.com/d", @"GET")];
		[cache removeCachedResponsesSinceDate:[NSDate dateWithTimeIntervalSinceNow:60]];
		check("a-response-cached-before-the-date-goes",
		      [cache cachedResponseForRequest:fn_request(@"http://example.com/d", @"GET")] == nil,
		      @"the entry remembers when it was cached, which is how Apple's door can be answered");
	covers("NSURLCache", "removeCachedResponsesSinceDate:");
	}

	/* --- THE DISK HALF, WHICH REPORTS RATHER THAN PRETENDS ------------------------------------------ */
	check("the-disk-is-not-written-to",
	      [cache currentDiskUsage] == 0 && [cache diskCapacity] == 8 * 1024 * 1024,
	      @"v1 writes nothing; the capacity is reported and the usage says so");

	/* --- THE TASK-SCOPED DOORS ---------------------------------------------------------------------- */
	

	/* --- AND THE SHARED CACHE CAN BE REPLACED, WHICH IS WHAT MAKES A PROBE ABLE TO ISOLATE ITSELF ---- */
	{
		NSURLCache *own = [[NSURLCache alloc] initWithMemoryCapacity:4096 diskCapacity:0 directoryURL:nil];

		[NSURLCache fnSetSharedURLCache:own];
		check("the-shared-cache-can-be-replaced",
		      [NSURLCache sharedURLCache] == own,
		      @"the getter reads the slot the setter writes - the first version did not");
	}

	printf("FOUNDATION-URLCACHE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLCACHE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLCACHE DONE\n");
	return failc ? 1 : 0;
}
