/*
 * foundation_urlhandle.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSURLHandle` AND ITS CLIENT PROTOCOL (§62.47). Apple deprecated the family at 10.4; §62.24's policy restored it.
 * The sixteen rows the ledger owed were the VOCABULARY — eleven property keys, `NSURLHandleStatus` and its four
 * cases, and the client protocol — and the class is here because a key needs something to key into.
 *
 * WHAT IS MEASURED IS THE MEANING, NOT THE NAMES. A key declared and never filed, a status that never becomes
 * `LoadSucceeded`, a client protocol nobody calls: all three would compile and pass a check that only counted
 * declarations. So this probe loads a real resource THROUGH THE LIBRARY'S OWN TRANSPORT — a registered
 * `NSURLProtocol` answering in process, which is this tree's recipe for an HTTP probe and means no socket, no
 * network, and no test that fails for reasons that are not about the class.
 *
 * ONE PATH, ITS OWN TRANSPORT: nothing here shares a listener or a session with another probe, and the only
 * requests that exist are the ones the handle makes.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <signal.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-URLHANDLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLHANDLE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* --- THE TRANSPORT: IN PROCESS, TWO SCHEMES, ONE FOR SUCCESS AND ONE FOR FAILURE -------------------- */

#define FN_HANDLE_BODY	@"handle-body"

@interface FNProbeTransport : NSURLProtocol
@end

@implementation FNProbeTransport

+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	NSString *scheme = [[[request URL] scheme] lowercaseString];

	return [scheme isEqualToString:@"fnhandle"] || [scheme isEqualToString:@"fnhandlefail"];
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
	return request;
}

- (void)startLoading
{
	NSString *scheme = [[[[self request] URL] scheme] lowercaseString];

	if ([scheme isEqualToString:@"fnhandlefail"]) {
		NSError *error = [NSError errorWithDomain:@"FNProbeTransport" code:7 userInfo:
					[NSDictionary dictionaryWithObject:@"the probe transport refused"
								    forKey:NSLocalizedDescriptionKey]];

		[[self client] URLProtocol:self didFailWithError:error];
		return;
	}

	{
		NSURL *requestURL = [[self request] URL];

		if (requestURL == nil) {
			/* NOTHING TO ANSWER WITH: a request without a URL is not something this transport can be asked, and
			 * answering nothing is the honest reply. */
			return;
		}

		{
			NSData *body = [FN_HANDLE_BODY dataUsingEncoding:NSUTF8StringEncoding];
			NSMutableDictionary *headers = [[NSMutableDictionary alloc] init];
			NSHTTPURLResponse *response;

			[headers setObject:@"fnprobe/1.0" forKey:@"Server"];
			[headers setObject:@"/elsewhere" forKey:@"Location"];
			response = [[NSHTTPURLResponse alloc] initWithURL:requestURL
							      statusCode:200
							     HTTPVersion:@"HTTP/1.1"
							    headerFields:headers];
			[[self client] URLProtocol:self
				   didReceiveResponse:response
				   cacheStoragePolicy:NSURLCacheStorageAllowed];
			[[self client] URLProtocol:self didLoadData:body];
			[[self client] URLProtocolDidFinishLoading:self];
		}
	}
}

- (void)stopLoading
{
}

@end

/* --- A CLIENT THAT RECORDS WHAT IT WAS TOLD, IN ORDER ---------------------------------------------- */

@interface FNProbeClient : NSObject <NSURLHandleClient>
{
	NSMutableArray *_events;
}
- (NSArray *)events;
- (NSUInteger)count;
@end

@implementation FNProbeClient

- (id)init
{
	self = [super init];
	_events = [[NSMutableArray alloc] init];
	return self;
}

- (NSArray *)events { return _events; }
- (NSUInteger)count { return [_events count]; }

- (void)URLHandleResourceDidBeginLoading:(NSURLHandle *)sender
{
	(void)sender;
	[_events addObject:@"begin"];
}

- (void)URLHandleResourceDidFinishLoading:(NSURLHandle *)sender
{
	(void)sender;
	[_events addObject:@"finish"];
}

- (void)URLHandleResourceDidCancelLoading:(NSURLHandle *)sender
{
	(void)sender;
	[_events addObject:@"cancel"];
}

- (void)URLHandle:(NSURLHandle *)sender resourceDataDidBecomeAvailable:(NSData *)newBytes
{
	(void)sender;
	[_events addObject:[NSString stringWithFormat:@"data:%lu", (unsigned long)[newBytes length]]];
}

- (void)URLHandle:(NSURLHandle *)sender resourceDidFailLoadingWithReason:(NSString *)reason
{
	(void)sender;
	[_events addObject:[NSString stringWithFormat:@"fail:%@", reason]];
}

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, SIG_IGN);

	/* REGISTER THE TRANSPORT FIRST — this library's rule, not a habit of this probe. */
	[NSURLProtocol registerClass:[FNProbeTransport class]];

	/* --- THE VOCABULARY ------------------------------------------------------------------------------- */
	{
		NSArray *keys = [NSArray arrayWithObjects:
			NSFTPPropertyActiveTransferModeKey, NSFTPPropertyFileOffsetKey, NSFTPPropertyFTPProxy,
			NSFTPPropertyUserLoginKey, NSFTPPropertyUserPasswordKey,
			NSHTTPPropertyErrorPageDataKey, NSHTTPPropertyHTTPProxy, NSHTTPPropertyRedirectionHeadersKey,
			NSHTTPPropertyServerHTTPVersionKey, NSHTTPPropertyStatusCodeKey, NSHTTPPropertyStatusReasonKey,
			nil];
		NSSet *distinct = [NSSet setWithArray:keys];
		BOOL allNamed = YES;
		NSUInteger i;

		for (i = 0; i < [keys count]; i++) {
			NSString *key = [keys objectAtIndex:i];

			if ([key length] == 0) {
				allNamed = NO;
			}
		}
		check("the-eleven-property-keys-are-declared-and-distinct",
		      [keys count] == 11 && [distinct count] == 11 && allNamed &&
		      NSURLHandleNotLoaded != NSURLHandleLoadInProgress &&
		      NSURLHandleLoadInProgress != NSURLHandleLoadSucceeded &&
		      NSURLHandleLoadSucceeded != NSURLHandleLoadFailed,
		      [NSString stringWithFormat:@"%d key(s), %d distinct, all non-empty; the four statuses are distinct",
			(int)[keys count], (int)[distinct count]]);
	}

	/* --- A HANDLE, ITS URL, ITS STATUS, AND THE REGISTRY ---------------------------------------------- */
	{
		NSURL *url = [NSURL URLWithString:@"fnhandle://probe/resource"];
		NSURLHandle *handle = [[NSURLHandle alloc] initWithURL:url cached:YES];
		NSURLHandle *again = [NSURLHandle cachedHandleForURL:url];

		check("a-handle-answers-its-url-and-starts-not-loaded",
		      handle != nil && [[handle URL] isEqual:url] &&
		      [handle status] == NSURLHandleNotLoaded && [handle failureReason] == nil &&
		      [handle availableResourceData] == nil,
		      @"a fresh handle is NotLoaded, has no failure reason, and has no data");

		check("the-registry-answers-a-class-and-the-cache-the-same-handle",
		      [NSURLHandle canInitWithURL:url] &&
		      [NSURLHandle URLHandleClassForURL:url] == [NSURLHandle class] &&
		      again == handle,
		      @"a scheme this library can fetch is a scheme a handle can serve, and a cached handle is THE handle");

		/* --- THE PROPERTY BAG ------------------------------------------------------------------------- */
		check("a-property-the-caller-writes-comes-back-and-nil-removes-it",
		      [handle writeProperty:@"seven" forKey:NSFTPPropertyUserLoginKey] &&
		      [[handle propertyForKeyIfAvailable:NSFTPPropertyUserLoginKey] isEqual:@"seven"] &&
		      [handle propertyForKeyIfAvailable:NSHTTPPropertyStatusCodeKey] == nil &&
		      [handle writeProperty:nil forKey:NSFTPPropertyUserLoginKey] &&
		      [handle propertyForKeyIfAvailable:NSFTPPropertyUserLoginKey] == nil,
		      @"a written property reads back, an unwritten one is nil rather than a load, and nil removes");

		/* --- THE FOREGROUND LOAD, AND WHAT IT FILES --------------------------------------------------- */
		{
			NSData *data = [handle loadInForeground];
			NSString *text = data != nil
				? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil;
			id code = [handle propertyForKeyIfAvailable:NSHTTPPropertyStatusCodeKey];

			check("the-foreground-load-returns-the-body-through-this-librarys-transport",
			      data != nil && [text isEqual:FN_HANDLE_BODY] &&
			      [handle status] == NSURLHandleLoadSucceeded && [handle failureReason] == nil,
			      [NSString stringWithFormat:@"the load answered %lu byte(s) reading \"%@\" with status %d",
				(unsigned long)[data length], text != nil ? text : @"(nothing)",
				(int)[handle status]]);

			/* THE KEYS, FILED BY THE CLASS RATHER THAN BY THIS PROBE: a restored vocabulary that nothing
			 * fills is a list of names, and this check is where that would show. */
			check("the-response-is-filed-under-apples-keys",
			      code != nil && [code integerValue] == 200 &&
			      [[handle propertyForKeyIfAvailable:NSHTTPPropertyServerHTTPVersionKey]
				containsString:@"fnprobe"] &&
			      [[handle propertyForKeyIfAvailable:NSHTTPPropertyStatusReasonKey] length] > 0 &&
			      [[handle propertyForKeyIfAvailable:NSHTTPPropertyRedirectionHeadersKey] count] == 1,
			      [NSString stringWithFormat:@"status %@, server \"%@\", reason \"%@\", %d redirection header(s)",
				code,
				[handle propertyForKeyIfAvailable:NSHTTPPropertyServerHTTPVersionKey],
				[handle propertyForKeyIfAvailable:NSHTTPPropertyStatusReasonKey],
				(int)[[handle propertyForKeyIfAvailable:NSHTTPPropertyRedirectionHeadersKey] count]]);

			check("available-data-is-what-arrived-and-a-flush-forgets-it",
			      [[handle availableResourceData] isEqual:data],
			      @"the resource data is available after a foreground load");
			[handle flushCachedData];
			check("a-flush-forgets-the-body-and-keeps-the-properties",
			      [handle availableResourceData] == nil &&
			      [handle propertyForKeyIfAvailable:NSHTTPPropertyStatusCodeKey] != nil,
			      @"flushing drops the bytes and keeps what the response said");
		}

		/* --- THE BACKGROUND LOAD, WHICH IS A THREAD AND SAYS SO ---------------------------------------- */
		{
			NSURL *slow = [NSURL URLWithString:@"fnhandle://probe/again"];
			NSURLHandle *background = [[NSURLHandle alloc] initWithURL:slow cached:NO];
			FNProbeClient *client = [[FNProbeClient alloc] init];
			NSTimeInterval deadline = [[NSDate date] timeIntervalSince1970] + 10.0;
			BOOL began = NO, finished = NO;

			[background addClient:client];
			[background beginLoadInBackground];
			began = ([client count] > 0 &&
				 [[[client events] objectAtIndex:0] isEqual:@"begin"]);
			while ([[NSDate date] timeIntervalSince1970] < deadline &&
			       [background status] != NSURLHandleLoadSucceeded) {
				[NSThread sleepForTimeInterval:0.02];
			}
			finished = ([background status] == NSURLHandleLoadSucceeded);
			check("a-background-load-tells-its-client-begin-then-the-bytes-then-finish",
			      began && finished && [client count] == 3 &&
			      [[[client events] objectAtIndex:0] isEqual:@"begin"] &&
			      [[[client events] objectAtIndex:1] isEqual:
				[NSString stringWithFormat:@"data:%lu",
					(unsigned long)[FN_HANDLE_BODY length]]] &&
			      [[[client events] objectAtIndex:2] isEqual:@"finish"],
			      [NSString stringWithFormat:@"the client was told: %@%@",
				[[client events] componentsJoinedByString:@", "],
				finished ? @"" : @" (the load never finished within the budget)"]);

			[background removeClient:client];
		}

		/* --- A FAILED LOAD ANSWERS A REASON ------------------------------------------------------------ */
		{
			NSURL *bad = [NSURL URLWithString:@"fnhandlefail://probe/gone"];
			NSURLHandle *failing = [[NSURLHandle alloc] initWithURL:bad cached:NO];
			NSData *data = [failing loadInForeground];

			check("a-failed-load-answers-no-data-a-failed-status-and-a-reason",
			      data == nil && [failing status] == NSURLHandleLoadFailed &&
			      [failing failureReason] != nil && [[failing failureReason] length] > 0,
			      [NSString stringWithFormat:@"the load answered nil with status %d and the reason \"%@\"",
				(int)[failing status], [failing failureReason]]);
		}

	}

	printf("FOUNDATION-URLHANDLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLHANDLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLHANDLE DONE\n");
	return failc ? 1 : 0;
}
