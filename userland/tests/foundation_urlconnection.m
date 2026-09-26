/*
 * foundation_urlconnection.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSURLConnection and its two data-side protocols — the first payment on §62.24's work list, and the
 * acceptance for the facade docs/design/foundation-plan.md §62.25 records.
 *
 * SERVER-FREE BY DESIGN, AND THAT IS A DECISION RATHER THAN A LIMITATION. Three of these checks need a
 * real exchange, and `file://` supplies one: the loading system speaks it (the bridge's own code says a
 * file transfer "dials nothing"), the guest always has a filesystem, and a test tier that needed a
 * listening server would be a test tier that fails for reasons that are not about this class. So the
 * round trips below are REAL TRANSFERS through the whole stack - NSURLConnection -> NSURLSession ->
 * NSURLProtocol -> the bridge - with nothing to start first.
 *
 * THE ONE DOOR THAT CANNOT BE REACHED THAT WAY IS THE REDIRECT, WHICH NEEDS A 3xx. It is therefore
 * tested DIRECTLY, through the runtime: the translation is a pure function of (delegate, proposed
 * request, response) and it is exercised here with three delegates - one that returns a request, one
 * that returns nil ("do not follow") and none at all. That is the whole rule, and it is tested the way
 * the rule is stated rather than through a server that would have to be trusted to produce a 302.
 *
 * WHAT IS ASSERTED ABSENT IS AS IMPORTANT AS WHAT IS ASSERTED PRESENT (§11.2): the refused doors are
 * named in the class's `excluded` inventory below and checked one by one, because a door that is
 * declared and never called is worse than an absent one.
 */
#import <Foundation/Foundation.h>
#import <Foundation/FNCURLURLProtocol.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-URLCONNECTION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLCONNECTION %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE NULLABLE-FACTORY IDIOM THIS TIER REQUIRES (the same one foundation_defaults.m records): the probes
 * compile with -Werror=nullable-to-nonnull-conversion, so a nullable factory's result goes through an
 * `id` local before it reaches a non-null parameter. EVERY URL in this probe is built here. */
static NSURL *fn_url(NSString *string)
{
	id url = [NSURL URLWithString:string];

	return url;
}

static NSURL *fn_fileURL(NSString *path)
{
	id url = [NSURL fileURLWithPath:path];

	return url;
}

static NSURLRequest *fn_request(NSString *string)
{
	return [NSURLRequest requestWithURL:fn_url(string)];
}

/* AN OPTIONAL METHOD'S PRESENCE IS ASKED OF THE PROTOCOL OBJECT, not of `-respondsToSelector:`, which
 * answers about a CLASS. `protocol_getMethodDescription` with isRequiredMethod=NO is the only door that
 * sees an `@optional` member, and an empty `types` is how "not declared" reads. */
static BOOL fn_protocol_has(Protocol *proto, const char *sel)
{
	struct objc_method_description d;

	if (proto == NULL) {
		return NO;
	}
	d = protocol_getMethodDescription(proto, sel_registerName(sel),
					  NO, YES);
	return d.types != NULL;
}

/* --- THE DELEGATES THE ROUND TRIPS USE ------------------------------------------------------------- */

/* WHAT THE DELEGATE SAW, IN ORDER. `order` is a bit string so the SEQUENCE is a check rather than three
 * independent ones: a response after the ending is as wrong as a missing response. */
@interface FNConnRecorder : NSObject <NSURLConnectionDataDelegate>
{
	@public
	NSMutableData *body;
	NSURLResponse *response;
	int responseCount;
	int dataCount;
	int finishCount;
	int failCount;
	NSString *order;
	BOOL done;
}

@end

@implementation FNConnRecorder

/* THE SEQUENCE IS BUILT LAZILY, AND THAT IS NOT A STYLE CHOICE: `order` starts nil, and a message to nil
 * ANSWERS nil - so appending to it leaves it nil forever and the check that reads it sees (null). That is
 * exactly what this probe's first run did. */
- (void)fnAppend:(NSString *)letter
{
	order = (order == nil) ? letter : [order stringByAppendingString:letter];
}

- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)aResponse
{
	(void)connection;
	responseCount++;
	response = aResponse;
	[self fnAppend:@"R"];
}

- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data
{
	(void)connection;
	dataCount++;
	if (body == nil) {
		body = [NSMutableData data];
	}
	[body appendData:data];
	[self fnAppend:@"D"];
}

- (void)connectionDidFinishLoading:(NSURLConnection *)connection
{
	(void)connection;
	finishCount++;
	done = YES;
	[self fnAppend:@"F"];
}

- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error
{
	(void)connection;
	(void)error;
	failCount++;
	done = YES;
	[self fnAppend:@"E"];
}

@end

/* THE REDIRECT THREE-STEP FIXTURE. `mode` says what the delegate should answer, and the check reads what
 * the connection's completion handler was given. */
@interface FNRedirectAnswerer : NSObject <NSURLConnectionDataDelegate>
{
	@public
	int mode;		/* 0 = return the SAME request (follow), 1 = return nil (do not follow),
				 * 2 = return a DIFFERENT request */
	NSURLRequest *returned;
	BOOL asked;
}

@end

@implementation FNRedirectAnswerer

- (NSURLRequest *)connection:(NSURLConnection *)connection
	      willSendRequest:(NSURLRequest *)request
	   redirectResponse:(NSURLResponse *)response
{
	(void)connection;
	(void)response;
	asked = YES;
	if (mode == 1) {
		return nil;
	}
	if (mode == 2) {
		returned = [NSURLRequest requestWithURL:
				fn_url(@"http://example.invalid/elsewhere")];
		return returned;
	}
	returned = request;
	return request;
}

@end

/* A DELEGATE THAT IMPLEMENTS NOTHING, so "a delegate that does not implement a door is not asked" is a
 * check about a real object rather than about a nil one. */
/* IT ADOPTS THE BASE PROTOCOL ONLY, which is also the check that the base protocol is adoptable on its
 * own - the arrangement Apple documents (the data protocol refines it, and a delegate may implement just
 * the failure door). */
@interface FNEmptyConnDelegate : NSObject <NSURLConnectionDelegate>
@end
@implementation FNEmptyConnDelegate
@end

/* WHERE THE ROUND TRIPS READ AND WRITE. The guest's temporary directory is this library's own
 * NSTemporaryDirectory (plan §11.6: it is hardcoded, and a probe that used TMPDIR would be testing the
 * environment instead). */
static NSString *fn_temp_path(NSString *name)
{
	return [NSTemporaryDirectory() stringByAppendingPathComponent:name];
}

static NSURL *fn_write_fixture(NSString *name, NSData *bytes)
{
	NSString *path = fn_temp_path(name);

	[bytes writeToFile:path atomically:YES];
	return fn_fileURL(path);
}

/* THE POLL, AND WHY THE PROBE CAN HAVE ONE AT ALL: this class's delegates are called on the loading
 * system's THREAD (the deviation NSURLConnection.h documents - the run loop has no door to hand work to
 * another thread, so Apple's "the thread you started it on" cannot be honoured). That means the main
 * thread here is free to WAIT, and a wait is all this needs. */
static BOOL fn_wait_for(FNConnRecorder *r, int milliseconds)
{
	int slept = 0;

	/* THE GRANULARITY IS THE GUEST'S, NOT THIS FILE'S: a 2 ms sleep is rounded up hard by this system's
	 * clock, so a ten-thousand-iteration loop is minutes rather than twenty seconds - measured, and the
	 * reason this loop sleeps in 20 ms steps and counts them against a wall-clock budget. */
	while (!r->done && slept < milliseconds) {
		usleep(20000);
		slept += 20;
	}
	return r->done;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* REGISTER THE TRANSPORT FIRST - THIS LIBRARY'S RULE, NOT A HABIT OF THIS PROBE. NSURLProtocol ships
	 * an EMPTY registry and Apple's own instruction is to fill it before any loading starts; every sibling
	 * probe that performs a real exchange does this (foundation_cachehooks, foundation_authloop). It is
	 * also what MAKES +canHandleRequest: answer YES below: that class asks the registry, so before this
	 * line the honest answer for an http URL is NO. */
	[NSURLProtocol registerClass:[FNCURLURLProtocol class]];

	/* --- 1. THE CLASS AND ITS PROTOCOLS ------------------------------------------------------------ */
	{
		Class cls = objc_getClass("NSURLConnection");
		Protocol *base = objc_getProtocol("NSURLConnectionDelegate");
		Protocol *data = objc_getProtocol("NSURLConnectionDataDelegate");

		check("connection-class-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] &&
		      [cls canHandleRequest:[NSURLRequest requestWithURL:
				fn_url(@"http://example.invalid/")]],
		      @"NSURLConnection exists, is an NSObject, and answers -canHandleRequest:");

		check("delegate-protocols-declared",
		      base != NULL && data != NULL && protocol_conformsToProtocol(data, base),
		      @"NSURLConnectionDelegate and NSURLConnectionDataDelegate exist, and the data protocol "
		      @"refines the base one (Apple's arrangement)");
	}

	/* --- 2. THE SCHEME QUESTION IS ASKED OF THE LOADING SYSTEM ------------------------------------ */
	{
		NSURLRequest *http = [NSURLRequest requestWithURL:
			fn_url(@"http://example.invalid/")];
		NSURLRequest *nonsense = [NSURLRequest requestWithURL:
			fn_url(@"zzz-nothing-speaks-this://host/x")];
		BOOL httpOK = [NSURLConnection canHandleRequest:http];
		BOOL nonsenseOK = [NSURLConnection canHandleRequest:nonsense];

		check("can-handle-request-asks-the-registry",
		      httpOK && !nonsenseOK,
		      [NSString stringWithFormat:@"http is handled (%d) and an unknown scheme is not (%d) - "
			@"the answer comes from the protocol registry, not a second list of schemes",
			(int)httpOK, (int)nonsenseOK]);
	}

	/* --- 3. A REAL TRANSFER, SYNCHRONOUSLY -------------------------------------------------------- */
	{
		const char *bytes = "the synchronous door, through the whole stack\n";
		NSData *fixture = [NSData dataWithBytes:bytes length:strlen(bytes)];
		NSURL *url = fn_write_fixture(@"fnconn-sync.txt", fixture);
		NSURLResponse *response = nil;
		NSError *error = nil;
		NSData *got = [NSURLConnection sendSynchronousRequest:[NSURLRequest requestWithURL:url]
						  returningResponse:&response
							      error:&error];

		check("synchronous-round-trip",
		      got != nil && [got isEqualToData:fixture] && response != nil && error == nil &&
		      [response expectedContentLength] == (long long)[fixture length],
		      [NSString stringWithFormat:@"the bytes came back (%d of %d), the response says %d, "
			@"error=%@", (int)[got length], (int)[fixture length],
			(int)(response ? [response expectedContentLength] : -1), error]);

		/* AND THE OUT-PARAMETERS ARE OPTIONAL, which Apple's signature promises and a NULL caller
		 * relies on. A request for a file that is not there is the failure path: the data is nil and
		 * THE ERROR IS THE ONLY REPORT - no exception, no half-filled response. */
		{
			NSData *missing = [NSURLConnection sendSynchronousRequest:
				[NSURLRequest requestWithURL:
					fn_fileURL(fn_temp_path(@"fnconn-not-here.txt"))]
						   returningResponse:NULL
							       error:NULL];
			NSURLResponse *response2 = nil;
			NSError *error2 = nil;

			[NSURLConnection sendSynchronousRequest:
				[NSURLRequest requestWithURL:
					fn_fileURL(fn_temp_path(@"fnconn-not-here.txt"))]
					   returningResponse:&response2
						       error:&error2];

			check("synchronous-failure-is-reported-not-raised",
			      missing == nil && error2 != nil,
			      [NSString stringWithFormat:@"a missing file answers nil (also with NULL "
				@"out-parameters) and reports through the error door: %@", error2]);
		}
	}

	/* --- 4. THE SAME TRANSFER, ASYNCHRONOUSLY, AND THE ORDER OF THE CALLS -------------------------- */
	{
		const char *bytes = "asynchronous, and streamed in chunks as they arrive\n";
		NSData *fixture = [NSData dataWithBytes:bytes length:strlen(bytes)];
		NSURL *url = fn_write_fixture(@"fnconn-async.txt", fixture);
		FNConnRecorder *rec = [[FNConnRecorder alloc] init];
		NSURLConnection *conn = [NSURLConnection connectionWithRequest:
						[NSURLRequest requestWithURL:url]
					delegate:rec];
		BOOL ended = fn_wait_for(rec, 20000);
		NSData *body = rec->body;
		NSString *order = rec->order;
		NSURLRequest *original = [conn originalRequest];
		NSURLRequest *current = [conn currentRequest];

		check("asynchronous-streaming-round-trip",
		      ended && body != nil && [body isEqualToData:fixture] &&
		      rec->responseCount == 1 && rec->dataCount >= 1 && rec->finishCount == 1 &&
		      rec->failCount == 0,
		      [NSString stringWithFormat:@"orders=%@ responses=%d chunks=%d finishes=%d fails=%d "
			@"bytes=%d", order, rec->responseCount, rec->dataCount, rec->finishCount,
			rec->failCount, (int)[body length]]);

		check("the-calls-arrive-in-order",
		      [order isEqualToString:@"RDF"],
		      [NSString stringWithFormat:@"response, then data, then the ending: %@", order]);

		check("original-and-current-request",
		      original != nil && [original isEqual:[NSURLRequest requestWithURL:url]] &&
		      current != nil && [current isEqual:original],
		      @"with no redirect the two requests agree, and both are what was asked for");

	}

	/* --- 5. THE REDIRECT DOOR, WHICH IS THE ONLY TRANSLATION WITH A RULE IN IT ---------------------- */
	{
		/* THE CONNECTION NEED NOT BE STARTED: the redirect translation is a pure function of the
		 * delegate, the response and the proposed request, so it is exercised on an idle object. THE
		 * CAST IS THE POINT - this is the session's delegate door, asked through the runtime with a
		 * block argument, which is exactly how the session will ask it. */
		SEL sel = NSSelectorFromString(@"URLSession:task:willPerformHTTPRedirection:newRequest:"
					       @"completionHandler:");
		typedef void (*Fn)(id, SEL, NSURLSession *, NSURLSessionTask *, NSURLResponse *,
				   NSURLRequest *, void (^)(NSURLRequest *));
		NSURLRequest *proposed = [NSURLRequest requestWithURL:
			fn_url(@"http://example.invalid/next")];
		NSURLResponse *threeOhTwo = [[NSURLResponse alloc]
			initWithURL:fn_url(@"http://example.invalid/")
			   MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil];
		int mode;

		for (mode = 0; mode <= 2; mode++) {
			FNRedirectAnswerer *answerer = [[FNRedirectAnswerer alloc] init];
			NSURLConnection *conn = [NSURLConnection connectionWithRequest:
							[NSURLRequest requestWithURL:
								fn_url(@"file:///dev/null")]
						delegate:(id)answerer];
			__block NSURLRequest *given = nil;
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			if (present) {
				answerer->mode = mode;
				((Fn)objc_msgSend)(conn, sel, nil, nil, threeOhTwo, proposed,
						   ^(NSURLRequest *next) { given = next; });
			}

			if (mode == 0) {
				check("redirect-door-follows-what-the-delegate-returns",
				      present && answerer->asked && given == proposed,
				      @"the delegate's return value IS the session's answer: the same request comes "
				      @"back and the redirect is followed");
			} else if (mode == 1) {
				check("redirect-door-nil-means-do-not-follow",
				      present && answerer->asked && given == nil,
				      @"nil from the delegate reaches the session as nil, which is 'do not follow'");
			} else {
				check("redirect-door-passes-a-different-request",
				      present && answerer->asked && given == answerer->returned &&
				      ![given isEqual:proposed],
				      @"a different request travels through unchanged - nothing here rewrites it");
			}
		}

		/* AND A DELEGATE THAT IMPLEMENTS NO SUCH DOOR IS NOT ASKED, and the transfer follows: the
		 * proposed request is handed straight through. */
		{
			FNEmptyConnDelegate *empty = [[FNEmptyConnDelegate alloc] init];
			NSURLConnection *conn = [NSURLConnection connectionWithRequest:
							[NSURLRequest requestWithURL:
								fn_url(@"file:///dev/null")]
						delegate:(id)empty];
			__block NSURLRequest *given = nil;
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, nil, threeOhTwo, proposed,
						   ^(NSURLRequest *next) { given = next; });
			}
			check("redirect-door-with-no-delegate-door-follows",
			      present && given == proposed,
			      @"a delegate that does not implement the door is not asked, and the redirect is "
			      @"followed - Apple's default, inherited rather than re-invented");
		}
	}

	/* --- 6. WHAT IS REFUSED, ASSERTED ABSENT ------------------------------------------------------- */
	{
		/* EACH OF THESE HAS A GROUND IN THE HEADER, and the grounds are the register's kind (ii): a
		 * dependency this system does not have. The class-level doors are absent on the CLASS; the
		 * delegate doors are absent from the PROTOCOL OBJECTS, which is where an optional member lives. */
		Class cls = objc_getClass("NSURLConnection");
		Protocol *base = objc_getProtocol("NSURLConnectionDelegate");
		Protocol *data = objc_getProtocol("NSURLConnectionDataDelegate");
		/* the run-loop scheduling pair: there is no door to hand work to a run loop here */
		BOOL runloopPair = ![cls instancesRespondToSelector:
					NSSelectorFromString(@"scheduleInRunLoop:forMode:")] &&
				   ![cls instancesRespondToSelector:
					NSSelectorFromString(@"unscheduleFromRunLoop:forMode:")];
		/* the authentication doors: a challenge cannot be answered without -sender */
		BOOL authDoors = !fn_protocol_has(base,
					"connection:willSendRequestForAuthenticationChallenge:") &&
				 !fn_protocol_has(base, "connection:didReceiveAuthenticationChallenge:") &&
				 !fn_protocol_has(base, "connection:didCancelAuthenticationChallenge:") &&
				 !fn_protocol_has(base, "connection:canAuthenticateAgainstProtectionSpace:") &&
				 !fn_protocol_has(base, "connectionShouldUseCredentialStorage:");
		/* and the three whose dependency is the session's own shape */
		BOOL dataDoors = !fn_protocol_has(data, "connection:needNewBodyStream:") &&
				 !fn_protocol_has(data,
					"connection:didSendBodyData:totalBytesWritten:totalBytesExpectedToWrite:") &&
				 !fn_protocol_has(data, "connection:willCacheResponse:");

		check("refused-doors-are-absent",
		      cls != Nil && base != NULL && data != NULL && runloopPair && authDoors && dataDoors,
		      [NSString stringWithFormat:@"the inventory: run-loop pair absent=%d, the five "
			@"authentication doors absent=%d, the three session-shape doors absent=%d",
			(int)runloopPair, (int)authDoors, (int)dataDoors]);

		/* THE DOWNLOAD HALF IS THE NEXT SLICE, AND ITS ABSENCE IS A ROW THE LEDGER TRACKS rather than a
		 * silent gap: NSURLConnectionDownloadDelegate is `open` in the surface file. */
		check("download-protocol-is-the-next-slice",
		      objc_getProtocol("NSURLConnectionDownloadDelegate") == NULL,
		      @"NSURLConnectionDownloadDelegate is not declared (slice 2), which the ledger records "
		      @"as an open row rather than a refusal");
	}

	/* --- 7. THE DOORS THAT DO SHIP, ON THE CLASS AND THE PROTOCOLS ------------------------------- */
	{
		Protocol *data = objc_getProtocol("NSURLConnectionDataDelegate");
		Class cls = objc_getClass("NSURLConnection");

		check("the-declared-surface-is-what-ships",
		      [cls respondsToSelector:@selector(connectionWithRequest:delegate:)] &&
		      [cls respondsToSelector:@selector(sendSynchronousRequest:returningResponse:error:)] &&
		      [cls respondsToSelector:@selector(canHandleRequest:)] &&
		      [cls instancesRespondToSelector:@selector(start)] &&
		      [cls instancesRespondToSelector:@selector(cancel)] &&
		      [cls instancesRespondToSelector:@selector(setDelegateQueue:)] &&
		      fn_protocol_has(data, "connection:willSendRequest:redirectResponse:") &&
		      fn_protocol_has(data, "connection:didReceiveResponse:") &&
		      fn_protocol_has(data, "connection:didReceiveData:") &&
		      fn_protocol_has(data, "connectionDidFinishLoading:") &&
		      fn_protocol_has(objc_getProtocol("NSURLConnectionDelegate"),
				      "connection:didFailWithError:"),
		      @"every door this slice ships is reachable - the class doors on the class, the delegate "
		      @"doors on the protocols (the failure door on the BASE protocol, as Apple puts it)");
	}

	printf("FOUNDATION-URLCONNECTION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLCONNECTION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLCONNECTION DONE\n");
	return failc ? 1 : 0;
}
