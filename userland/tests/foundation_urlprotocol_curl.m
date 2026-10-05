/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlprotocol_curl, unit of 1 — W7 slice 2c's FIRST HALF: THE BRIDGE.
 * docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c.
 *
 * ONE unit, importing only <Foundation/Foundation.h>, and NO SESSION ANYWHERE: FNCURLURLProtocol is an
 * ordinary NSURLProtocol subclass, so a probe can start one by hand with a client and watch it — which is
 * exactly why the bridge is testable BEFORE the session that will normally drive it. The plan's own
 * ordering says the same thing: "FNCURLURLProtocol … then NSURLSession".
 *
 * IT RECORDS THE ORDER, NOT ONLY THE OUTCOME, because the seam's contract IS a sequence: the response
 * first, then the body in as many pieces as the transport hands over, and then EXACTLY ONE of finished or
 * failed. A probe that only checked "the bytes arrived" would pass for a bridge that reported them in the
 * wrong order, or reported both an ending and a failure.
 *
 * THE FETCHES ARE file:// ON PURPOSE: deterministic, no server, no network, and still a REAL transfer —
 * curl's file protocol is a transfer like any other and it goes through the same code path (a write
 * callback that streams the body, a header callback, a completion). A missing file supplies the failure
 * path, which is the half that a happy path cannot cover.
 *
 * THE FIXTURE IS WRITTEN BY THE PROBE rather than read from the image, so the expected bytes are in this
 * file and the assertion cannot drift from a file somebody else edited.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLPROTOCOL-CURL %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLPROTOCOL-CURL %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

#define FIXTURE_PATH	"/System/Temporary Files/fn_curl_fixture.txt"

/* THE PROBE'S OWN LITERAL URLS GO THROUGH A HELPER, which is slice 1's pattern and its reason:
 * +URLWithString: is NULLABLE by contract (a malformed string answers nil), and a file-scope function's
 * own pointer types carry no nullability annotation, so a literal the probe can see does not need a
 * nullable-to-nonnull conversion spelled out at every call site. */
static NSURL *fn_url(NSString *string)
{
	return [NSURL URLWithString:string];
}
static const char *const fixture_bytes = "the bridge carried these bytes\n";

/* --- THE CLIENT: AN ORDER LOG AND THE BODY ------------------------------------------------------- */

@interface FnCurlClient : NSObject <NSURLProtocolClient>
{
	NSMutableArray *_events;
	NSMutableData *_body;
	NSInteger _status;
	long long _expectedLength;
	BOOL _done;
}
- (NSArray *)events;
- (NSData *)body;
- (NSInteger)status;
- (long long)expectedLength;
- (BOOL)done;
@end

@implementation FnCurlClient

- (id)init
{
	self = [super init];
	if (self != nil) {
		_events = [[NSMutableArray alloc] init];
		_body = [[NSMutableData alloc] init];
		_status = 0;
		_expectedLength = -1;
		_done = NO;
	}
	return self;
}

- (NSArray *)events { return _events; }
- (NSData *)body { return _body; }
- (NSInteger)status { return _status; }
- (long long)expectedLength { return _expectedLength; }
- (BOOL)done { return _done; }

- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy
{
	[_events addObject:@"response"];
	_expectedLength = [response expectedContentLength];
	if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
		_status = [(NSHTTPURLResponse *)response statusCode];
	}
	(void)policy;
}

- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data
{
	[_events addObject:@"data"];
	[_body appendData:data];
}

/* THE AUTHENTICATION DOOR IS IMPLEMENTED BECAUSE THE PROTOCOL REQUIRES IT (§50.3 declared it): this client
 * is handed to the bridge, and a challenge arrives through it the moment a fixture answers 401. No fixture
 * here does, so the answer only has to be a legal one - and it is the DEFAULT handling rather than a
 * credential this probe has no opinion about.
 *
 * ⚠⚠ AND IT ANSWERS THROUGH THE CHALLENGE'S SENDER (§63.158): the door is APPLE'S now and takes no handler,
 * so `-performDefaultHandlingForAuthenticationChallenge:` is the way a client says "I have no opinion" —
 * which is what the handler's PerformDefaultHandling used to say. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	[_events addObject:@"challenge"];
	[[challenge sender] performDefaultHandlingForAuthenticationChallenge:challenge];
}

- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol
{
	[_events addObject:@"finish"];
	_done = YES;
}

- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error
{
	[_events addObject:@"failure"];
	_done = YES;
}

- (void)URLProtocol:(NSURLProtocol *)protocol
    wasRedirectedToRequest:(NSURLRequest *)request
	 redirectResponse:(NSURLResponse *)redirectResponse
{
	[_events addObject:@"redirect"];
	_done = YES;
}

- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse
{
	[_events addObject:@"cached"];
}

@end

/* RUN ONE TRANSFER TO COMPLETION, bounded: the callbacks arrive on the protocol's own thread, so the
 * probe waits for the terminal one rather than assuming it is synchronous. */
static FnCurlClient *fn_run(NSString *urlString)
{
	FnCurlClient *client = [[FnCurlClient alloc] init];
	NSURL *url = [NSURL URLWithString:urlString];
	NSURLRequest *request = [NSURLRequest requestWithURL:url];
	FNCURLURLProtocol *protocol = [[FNCURLURLProtocol alloc] initWithRequest:request
								cachedResponse:nil
								    client:client];
	int waited = 0;

	[protocol startLoading];
	while (![client done] && waited < 100) {	/* 10 s */
		usleep(100000);
		waited++;
	}
	return client;
}

static int fn_write_fixture(void)
{
	FILE *file = fopen(FIXTURE_PATH, "wb");

	if (file == NULL) {
		return 0;
	}
	fwrite(fixture_bytes, 1, strlen(fixture_bytes), file);
	fclose(file);
	return 1;
}

int main(void)
{
	/* --- THE SCHEMES IT CLAIMS ARE THE ONES CURL SPEAKS ----------------------------------------- */
	{
		NSURLRequest *file = [NSURLRequest requestWithURL:fn_url(@"file:///tmp/x")];
		NSURLRequest *http = [NSURLRequest requestWithURL:fn_url(@"http://host/x")];
		NSURLRequest *https = [NSURLRequest requestWithURL:fn_url(@"https://host/x")];
		NSURLRequest *ftp = [NSURLRequest requestWithURL:fn_url(@"ftp://host/x")];

		check("bridge-claims-its-schemes",
		      [FNCURLURLProtocol canInitWithRequest:file] == YES &&
		      [FNCURLURLProtocol canInitWithRequest:http] == YES &&
		      [FNCURLURLProtocol canInitWithRequest:https] == YES &&
		      [FNCURLURLProtocol canInitWithRequest:ftp] == NO,
		      @"file/http/https are curl's own scheme list; anything else is left to another protocol");

		/* AND IT IS A PROTOCOL THE REGISTRY CAN HOLD, which is what makes it a plug-in rather than a
		 * class that happens to look like one. */
		/* THE LIBRARY REGISTERED IT AT LOAD (§62.83), AND THIS PROBE REGISTERS NOTHING - which is what makes the
		 * assertion a fact about the SHIPPED library rather than about this file. It must also NOT unregister:
		 * the transfer further down needs the transport, and unregistering here would take the library's own
		 * registration away with it (the registry holds a class at most once). */
		check("bridge-is-in-the-registry-without-anyone-registering-it",
		      [NSURLProtocol fnProtocolClassForRequest:https] == [FNCURLURLProtocol class] &&
		      [NSURLProtocol fnProtocolClassForRequest:ftp] == Nil,
		      @"the bridge is in NSURLProtocol's registry at load and is found for the schemes it claims");
	}

	/* --- THE HAPPY PATH, AND ITS ORDER ----------------------------------------------------------- */
	{
		NSString *urlString;
		FnCurlClient *client;
		NSArray *events;

		if (!fn_write_fixture()) {
			check("bridge-fetches-a-file-url", 0,
			      @"the fixture could not be written, so there was nothing to fetch");
			check("bridge-reports-response-then-data-then-finish", 0, @"no fixture");
			check("bridge-reports-the-response-length", 0, @"no fixture");
		} else {
			/* THE URL IS BUILT FROM THE PATH, not concatenated: the FSH path has a SPACE in it, so
			 * the URL's spelling is the file rule's business and not this probe's. */
			urlString = [[NSURL fileURLWithPath:@FIXTURE_PATH] absoluteString];
			client = fn_run(urlString);
			events = [client events];

			check("bridge-fetches-a-file-url",
			      [client body] != nil &&
			      [[client body] length] == strlen(fixture_bytes) &&
			      memcmp([[client body] bytes], fixture_bytes, strlen(fixture_bytes)) == 0,
			      [NSString stringWithFormat:@"expected %d bytes, got %d: %@",
				 (int)strlen(fixture_bytes), (int)[[client body] length], events]);

			/* THE ORDER: the response first, the body after it, and exactly ONE ending. */
			{
				int ok = [events count] >= 3 &&
					 [@"response" isEqual:[events objectAtIndex:0]] &&
					 [@"data" isEqual:[events objectAtIndex:1]] &&
					 [[events lastObject] isEqual:@"finish"] &&
					 ![events containsObject:@"failure"];

				/* AND NOTHING AFTER THE ENDING: the log's last element IS the terminal one. */
				check("bridge-reports-response-then-data-then-finish", ok,
				      [NSString stringWithFormat:@"the callback order was %@", events]);
			}

			check("bridge-reports-the-response-length",
			      [client expectedLength] == (long long)strlen(fixture_bytes),
			      [NSString stringWithFormat:@"expectedContentLength was %lld, not %d",
				 [client expectedLength], (int)strlen(fixture_bytes)]);
		}
	}

	/* --- THE FAILURE PATH ------------------------------------------------------------------------ */
	{
		FnCurlClient *client = fn_run(@"file:///System/Temporary%20Files/fn_curl_absent_file.txt");
		NSArray *events = [client events];

		check("bridge-reports-a-missing-file-as-a-failure",
		      [events containsObject:@"failure"] && ![events containsObject:@"finish"],
		      [NSString stringWithFormat:@"a missing file must fail and not finish; the log was %@",
			 events]);
	}

	printf("FOUNDATION-URLPROTOCOL-CURL RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLPROTOCOL-CURL-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLPROTOCOL-CURL DONE\n");
	return failc ? 1 : 0;
}
