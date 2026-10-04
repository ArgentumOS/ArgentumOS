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

/* AND THE SAME QUESTION ABOUT A REQUIRED DOOR, which needs its own helper rather than a third argument on the
 * one above: the two are different claims about a protocol, and a call site that has to remember which is
 * which is a call site that will get it wrong (the first version of the session-protocol check did exactly
 * that, and the compiler refused it). */
static BOOL fn_protocol_requires(Protocol *proto, const char *sel)
{
	struct objc_method_description d;

	if (proto == NULL) {
		return NO;
	}
	d = protocol_getMethodDescription(proto, sel_registerName(sel),
					  YES, YES);
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

/* THE REDIRECT THREE-STEP FIXTURE. `mode` says what the delegate should answer, and the checks read what
 * the connection did with it (§63.154: it is asked through the class's OWN door now, so the observable
 * facts are the delegate's `asked` flag and the connection's `currentRequest`). */
@interface FNRedirectAnswerer : NSObject <NSURLConnectionDataDelegate>
{
	@public
	int mode;		/* 0 = return the SAME request (follow), 1 = return nil (do not follow),
				 * 2 = return a DIFFERENT request */
	NSURL *different;	/* what mode 2 answers a request for; SET BY THE PROBE, because a URL built
				 * here would have to be a second copy of the probe's fixture rules */
	NSURLRequest *returned;
	BOOL asked;
	BOOL delivered;		/* `-connection:didReceiveResponse:` was called: what "do not follow"
				 * DELIVERS, since the 3xx is then the answer */
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
		returned = [NSURLRequest requestWithURL:different];
		return returned;
	}
	returned = request;
	return request;
}

- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)response
{
	(void)connection;
	(void)response;
	delivered = YES;
}

@end

/* A DELEGATE THAT IMPLEMENTS NOTHING, so "a delegate that does not implement a door is not asked" is a
 * check about a real object rather than about a nil one. */
/* IT ADOPTS THE BASE PROTOCOL ONLY, which is also the check that the base protocol is adoptable on its
 * own - the arrangement Apple documents (the data protocol refines it, and a delegate may implement just
 * the failure door). */
/* THE AUTHENTICATION FIXTURES, AND THEY EXIST IN PAIRS ON PURPOSE (§62.28 has four precedence branches,
 * and each needs a delegate shaped for it):
 *   * A SENDER THAT RECORDS, so "the delegate answered through the sender" is observable rather than
 *     assumed - and it adopts Apple's protocol, which is also what registers that protocol's metadata in
 *     this binary (the measurement foundation_url.m records about unadopted protocols). **AND SINCE §63.158
 *     IT COUNTS EVERY ANSWER, not just the credentialed one**: with the door's completion handler gone, the
 *     SENDER is the only answer path, so "this class answered nothing" is a fact only a recorder can hold.
 *   * A DELEGATE IMPLEMENTING ALL THREE DOORS, which is how "the modern door SUPERSEDES the deprecated
 *     pair" becomes a check about an object that could have answered either way - parameterised by what the
 *     gate answers, so the "no means no authentication" leg uses the same shape.
 */
@interface FNSenderRecorder : NSObject <NSURLAuthenticationChallengeSender>
{
	@public
	int answers;		/* EVERY answer, whichever door of the protocol delivered it */
	int useCredentialCalls;
	NSURLCredential *credential;
	NSURLAuthenticationChallenge *challenge;
	NSString *user;
}

@end

@implementation FNSenderRecorder

- (void)useCredential:(NSURLCredential *)aCredential
forAuthenticationChallenge:(NSURLAuthenticationChallenge *)aChallenge
{
	answers++;
	useCredentialCalls++;
	credential = aCredential;
	challenge = aChallenge;
	user = [aCredential user];
}

- (void)continueWithoutCredentialForAuthenticationChallenge:(NSURLAuthenticationChallenge *)aChallenge
{
	(void)aChallenge;
	answers++;
}

- (void)cancelAuthenticationChallenge:(NSURLAuthenticationChallenge *)aChallenge
{
	(void)aChallenge;
	answers++;
}

@end

@interface FNAuthDelegate : NSObject <NSURLConnectionDelegate>
{
	@public
	int modernCalls;
	int gateCalls;
	int deprecatedCalls;
	BOOL gateAnswer;
}

@end

@implementation FNAuthDelegate

- (BOOL)connection:(NSURLConnection *)connection
canAuthenticateAgainstProtectionSpace:(NSURLProtectionSpace *)space
{
	(void)connection;
	(void)space;
	gateCalls++;
	return gateAnswer;
}

/* THE MODERN DOOR, WHICH SUPERSEDES THE TWO BELOW IT: a delegate implementing it must be the only one
 * asked, and this fixture implements all three so that "the only one asked" is a measurement. */
- (void)connection:(NSURLConnection *)connection
willSendRequestForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)connection;
	modernCalls++;
	[self fnAnswerThroughTheSender:challenge];
}

- (void)connection:(NSURLConnection *)connection
didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)connection;
	deprecatedCalls++;
	[self fnAnswerThroughTheSender:challenge];
}

/* THE OLDER WAY OF ANSWERING, AND THE ONLY ONE A CONNECTION DELEGATE HAS: this protocol has no completion
 * handler, so the answer goes back through the challenge's sender (which the TRANSPORT put there, §62.27).
 * A delegate that returns from a door without answering leaves the transfer waiting - which is why the
 * header says an implementing delegate MUST answer. */
- (void)fnAnswerThroughTheSender:(NSURLAuthenticationChallenge *)challenge
{
	id <NSURLAuthenticationChallengeSender> sender = [challenge sender];

	if(sender != nil) {
		[sender useCredential:[NSURLCredential credentialWithUser:@"kyle"
								 password:@"secret"
							      persistence:NSURLCredentialPersistenceNone]
		forAuthenticationChallenge:challenge];
	}
}

@end

/* THE DEPRECATED-PAIR DELEGATE: THE SAME TWO DOORS AND NO MODERN ONE, which is the ONLY shape that reaches
 * them - the tests below were first written against the all-three delegate above, and they FAILED, exactly
 * as the precedence rule says they should. A CLASS rather than a flag: `-respondsToSelector:` is a property
 * of the class, so "this delegate does not implement the modern door" has to be a different class and not an
 * instance with a switch turned off. THE FAILURE WAS THE CHECK'S, NOT THE CODE'S, and it is kept in words
 * because that is the more useful half of the pair.
 */
@interface FNDeprecatedAuthDelegate : NSObject <NSURLConnectionDelegate>
{
	@public
	int gateCalls;
	int deprecatedCalls;
	BOOL gateAnswer;
}

@end

@implementation FNDeprecatedAuthDelegate

- (BOOL)connection:(NSURLConnection *)connection
canAuthenticateAgainstProtectionSpace:(NSURLProtectionSpace *)space
{
	(void)connection;
	(void)space;
	gateCalls++;
	return gateAnswer;
}

- (void)connection:(NSURLConnection *)connection
didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	id <NSURLAuthenticationChallengeSender> sender = [challenge sender];

	(void)connection;
	deprecatedCalls++;
	if(sender != nil) {
		[sender useCredential:[NSURLCredential credentialWithUser:@"kyle"
								 password:@"secret"
							      persistence:NSURLCredentialPersistenceNone]
		forAuthenticationChallenge:challenge];
	}
}

@end

/* ⚠⚠ THE SESSION'S DOWNLOAD-PROTOCOL FIXTURE IS GONE (§63.159), and it is worth saying why it existed: it
 * adopted `NSURLSessionDownloadDelegate` FOR A MEASUREMENT RATHER THAN FOR BEHAVIOUR, because
 * `objc_getProtocol` answers NULL for a protocol the calling binary never adopts. The protocol it registered
 * left with the session family, so there is nothing left to make exist. */

/* THE RE-SENDER (§62.35): it hands back a stream when asked, and records the ask - so the translation is a
 * measurement rather than a claim. */
@interface FNBodyStreamAnswerer : NSObject <NSURLConnectionDataDelegate>
{
	@public
	int asks;
	NSInputStream *stream;
}

@end

@implementation FNBodyStreamAnswerer

- (NSInputStream *)connection:(NSURLConnection *)connection needNewBodyStream:(NSURLRequest *)request
{
	(void)connection;
	(void)request;
	asks++;
	return stream;
}

@end

/* THE CACHE-DECIDER FOR A CONNECTION (§62.33): it answers what the test tells it to, so the translation's two
 * branches - the delegate's answer, and the proposal when there is no such door - are both measurable. */
@interface FNCacheAnswerer : NSObject <NSURLConnectionDataDelegate>
{
	@public
	int asks;
	BOOL keep;
}

@end

@implementation FNCacheAnswerer

- (NSCachedURLResponse *)connection:(NSURLConnection *)connection
		  willCacheResponse:(NSCachedURLResponse *)cachedResponse
{
	(void)connection;
	asks++;
	return keep ? cachedResponse : nil;
}

@end

@interface FNEmptyConnDelegate : NSObject <NSURLConnectionDelegate>
@end
@implementation FNEmptyConnDelegate
@end

/* THE DOWNLOAD DELEGATE, AND IT ADOPTS BOTH PROTOCOLS ON PURPOSE: the dispatch rule ("a delegate that
 * implements -connectionDidFinishDownloading:destinationURL: downloads") must be shown to WIN over the
 * data doors, not merely to exist. It therefore implements the data doors as well and COUNTS their calls,
 * and it implements `-connection:didWriteData:…` - a door this library refuses - so that "never called" is
 * a fact about a real implementation rather than about a missing one. */
@interface FNDownloadRecorder : NSObject <NSURLConnectionDownloadDelegate, NSURLConnectionDataDelegate>
{
	@public
	NSURL *destination;
	int finishCount;
	int failCount;
	int dataDoorCalls;
	int responseDoorCalls;
	int writeProgressCalls;
	long long lastBytesWritten;
	long long lastTotalWritten;
	long long lastExpectedTotal;
	BOOL done;
}

@end

@implementation FNDownloadRecorder

- (void)connectionDidFinishDownloading:(NSURLConnection *)connection destinationURL:(NSURL *)aURL
{
	(void)connection;
	destination = aURL;
	finishCount++;
	done = YES;
}

- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error
{
	(void)connection;
	(void)error;
	failCount++;
	done = YES;
}

/* THE DOORS BELOW MUST NEVER BE CALLED, which is what makes them worth implementing here. */
- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data
{
	(void)connection;
	(void)data;
	dataDoorCalls++;
}

- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)response
{
	(void)connection;
	(void)response;
	responseDoorCalls++;
}

/* A DOWNLOAD DOOR SINCE §62.29, SO IT IS EXPECTED TO FIRE - and it records the three numbers so the check
 * can assert they are about THIS transfer rather than merely that a call happened. */
- (void)connection:(NSURLConnection *)connection
	 didWriteData:(long long)bytesWritten
    totalBytesWritten:(long long)totalBytesWritten
    expectedTotalBytes:(long long)expectedTotalBytes
{
	(void)connection;
	writeProgressCalls++;
	lastBytesWritten = bytesWritten;
	lastTotalWritten = totalBytesWritten;
	lastExpectedTotal = expectedTotalBytes;
}

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
static BOOL fn_wait_flag(BOOL *flag, int milliseconds)
{
	int slept = 0;

	/* THE GRANULARITY IS THE GUEST'S, NOT THIS FILE'S: a 2 ms sleep is rounded up hard by this system's
	 * clock, so a ten-thousand-iteration loop is minutes rather than twenty seconds - measured, and the
	 * reason this loop sleeps in 20 ms steps and counts them against a wall-clock budget. */
	while (!*flag && slept < milliseconds) {
		usleep(20000);
		slept += 20;
	}
	return *flag;
}

static BOOL fn_wait_for(FNConnRecorder *r, int milliseconds)
{
	return fn_wait_flag(&r->done, milliseconds);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* NO REGISTRATION HERE ANY MORE. This probe used to fill an EMPTY registry first, because `NSURLProtocol`
	 * shipped empty and Apple's instruction is to register before any loading starts. Since §62.83 the LIBRARY
	 * registers its own transport at load, so this line is gone - and the `can-handle-request-asks-the-registry`
	 * check below is now the gate for THAT: it asks whether http is handled, and the honest answer is YES only if
	 * the library's own registration happened. */

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


	/* --- 5. THE REDIRECT DOOR, ON THIS CLASS'S OWN DOOR ---------------------------------------------
	 *
	 * THE TRANSLATION IS A PURE FUNCTION OF (delegate, response, proposed request) PLUS THE ONE ACT IT
	 * PERFORMS — FOLLOWING is this class's own step, because the seam REPORTS the 3xx and stops (that is
	 * its header's decision 2). So the connection is asked through THE DOOR THE SEAM WOULD USE,
	 * `-URLProtocol:wasRedirectedToRequest:redirectResponse:` (NSURLProtocolClient), and the checks read the
	 * delegate's own record and the connection's `currentRequest` — the request it says it is running.
	 *
	 * IT CONNECTS TO NOTHING: every URL is a local fixture, so a mode that FOLLOWS starts a real transfer
	 * that succeeds at once rather than one waiting for a server that is not there.
	 */
	{
		SEL sel = @selector(URLProtocol:wasRedirectedToRequest:redirectResponse:);
		typedef void (*Fn)(id, SEL, NSURLProtocol *, NSURLRequest *, NSURLResponse *);
		const char *redirectBytes = "the request a redirect was followed to\n";
		NSData *redirectFixture = [NSData dataWithBytes:redirectBytes length:strlen(redirectBytes)];
		NSURL *nextURL = fn_write_fixture(@"fnconn-redirect-target.txt", redirectFixture);
		NSURL *otherURL = fn_write_fixture(@"fnconn-redirect-other.txt", redirectFixture);
		NSURLRequest *original = [NSURLRequest requestWithURL:fn_url(@"file:///dev/null")];
		NSURLRequest *proposed = [NSURLRequest requestWithURL:nextURL];
		NSURLResponse *threeOhTwo = [[NSURLResponse alloc]
			initWithURL:fn_url(@"file:///dev/null")
			   MIMEType:@"text/html"
		     expectedContentLength:0
			 textEncodingName:nil];
		int mode;

		/* THE THREE ANSWERS A DELEGATE CAN GIVE, ONE PER MODE. Mode 0 answers the PROPOSED request, which
		 * MEANS follow: the observable fact is `currentRequest` moving onto it. */
		for (mode = 0; mode <= 2; mode++) {
			FNRedirectAnswerer *answerer = [[FNRedirectAnswerer alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:original
									 delegate:(id)answerer
								 startImmediately:NO];
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			answerer->mode = mode;
			answerer->different = otherURL;
			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, proposed, threeOhTwo);
			}

			if (mode == 0) {
				check("redirect-door-follows-what-the-delegate-returns",
				      present && answerer->asked &&
				      [[conn currentRequest] isEqual:proposed],
				      [NSString stringWithFormat:@"present=%d asked=%d current=%@", (int)present,
					       (int)answerer->asked, [[conn currentRequest] URL]]);
			} else if (mode == 1) {
				/* NIL MEANS DO NOT FOLLOW, and then the 3xx IS the answer: the delegate hears it as a
				 * response, and the connection is STILL RUNNING THE ORIGINAL REQUEST — `currentRequest` is
				 * "what this connection is running", so a refused redirect does not move it. */
				check("redirect-door-nil-means-do-not-follow",
				      present && answerer->asked && answerer->delivered &&
				      [[conn currentRequest] isEqual:original],
				      [NSString stringWithFormat:@"asked=%d delivered=%@ current=%@",
					       (int)answerer->asked, answerer->delivered ? @"yes" : @"no",
					       [[conn currentRequest] URL]]);
			} else {
				/* A DIFFERENT REQUEST IS RUN AS IT STANDS: nothing here rewrites what a caller's delegate
				 * answered. */
				check("redirect-door-passes-a-different-request",
				      present && answerer->asked &&
				      [[conn currentRequest] isEqual:[NSURLRequest requestWithURL:otherURL]],
				      [NSString stringWithFormat:@"asked=%d current=%@", (int)answerer->asked,
					       [[conn currentRequest] URL]]);
			}
			/* NO `release`: this probe is compiled under -fobjc-arc, so the strong locals above are ARC's —
			 * and an explicit release is a COMPILE ERROR here, which is the trap the tree already records
			 * (the library is MRC, every probe is ARC, and ownership must be written out). */
		}

		/* AND A DELEGATE THAT IMPLEMENTS NO SUCH DOOR IS NOT ASKED, and the transfer follows: the proposed
		 * request is what the connection runs. */
		{
			FNEmptyConnDelegate *empty = [[FNEmptyConnDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:original
									 delegate:(id)empty
								 startImmediately:NO];

			if (conn != nil && [conn respondsToSelector:sel]) {
				((Fn)objc_msgSend)(conn, sel, nil, proposed, threeOhTwo);
			}
			check("redirect-door-with-no-delegate-door-follows",
			      [[conn currentRequest] isEqual:proposed],
			      [NSString stringWithFormat:@"current=%@", [[conn currentRequest] URL]]);
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
		/* THE TWO AUTHENTICATION DOORS THAT REMAIN REFUSED - the other three SHIP, and §62.28 is what
		 * changed that: `-sender` landed (§62.27), so a connection delegate can answer a challenge. */
		BOOL authDoors = !fn_protocol_has(base, "connectionShouldUseCredentialStorage:") &&
				 !fn_protocol_has(base, "connection:didCancelAuthenticationChallenge:");
		/* ⚠⚠ AND THE "THREE SESSION-SHAPE DOORS" THAT USED TO BE ASSERTED ABSENT HERE ARE NOT REFUSED AT ALL
		 * (§63.156): this tree declared them on the DOWNLOAD protocol, APPLE declares them on the DATA
		 * protocol, and the tree's own ledger said so — three `open` rows — while this check denied the
		 * placement. They have MOVED to where Apple has them, so the refusal list here is what is really
		 * refused, and the placement is asserted by its own check below. */
		check("refused-doors-are-absent",
		      cls != Nil && base != NULL && data != NULL && runloopPair && authDoors,
		      [NSString stringWithFormat:@"the inventory: run-loop pair absent=%d, the two "
			@"still-refused authentication doors absent=%d, and both of this class's protocols "
			@"resolve=%d",
			(int)runloopPair, (int)authDoors, (int)(cls != Nil && base != NULL && data != NULL)]);

		/* THE PLACEMENT, ASSERTED RATHER THAN DENIED: Apple declares the body-stream, the upload-progress
		 * and the cache-decision doors on `NSURLConnectionDataDelegate`, and this tree had them one protocol
		 * down — which is why those three rows sat `open` in the ledger for two sessions.
		 *
		 * THE ASSERTION IS TWO-SIDED ON PURPOSE: each door must be ON the data protocol AND OFF the download
		 * protocol, because either half alone is satisfied by the misplacement this corrects — "present
		 * somewhere" was already true, and "absent from the download protocol" becomes true the moment
		 * somebody deletes one instead of moving it. */
		{
			Protocol *dl = objc_getProtocol("NSURLConnectionDownloadDelegate");
			BOOL onData = fn_protocol_has(data, "connection:needNewBodyStream:") &&
				      fn_protocol_has(data, "connection:didSendBodyData:totalBytesWritten:"
						      "totalBytesExpectedToWrite:") &&
				      fn_protocol_has(data, "connection:willCacheResponse:");
			BOOL offDownload = dl != NULL &&
					   !fn_protocol_has(dl, "connection:needNewBodyStream:") &&
					   !fn_protocol_has(dl, "connection:didSendBodyData:totalBytesWritten:"
							   "totalBytesExpectedToWrite:") &&
					   !fn_protocol_has(dl, "connection:willCacheResponse:");

			check("the-data-protocol-carries-the-three-doors-apple-declares",
			      onData && offDownload,
			      [NSString stringWithFormat:@"on the DATA protocol=%d (needNewBodyStream, "
				@"didSendBodyData, willCacheResponse), off the DOWNLOAD protocol=%d",
				(int)onData, (int)offDownload]);
		}

		/* THE DOWNLOAD PROTOCOL, WHICH SLICE 2 DECLARED: asserted PRESENT now, and asserted where its
		 * metadata exists - the fixture above adopts it, which is what registers it. */
		check("download-protocol-declared",
		      objc_getProtocol("NSURLConnectionDownloadDelegate") != NULL &&
		      fn_protocol_has(objc_getProtocol("NSURLConnectionDownloadDelegate"),
				      "connectionDidFinishDownloading:destinationURL:"),
		      @"NSURLConnectionDownloadDelegate is declared, with the door that MEANS a download");
	}

	/* --- 8. THE DOWNLOAD PATH, END TO END, AND THE PROTOCOL'S ONE REFUSAL --------------------------- */
	{
		Protocol *dl = objc_getProtocol("NSURLConnectionDownloadDelegate");
		const char *downloadBytes = "the download door, and the file it leaves behind\n";
		NSData *downloadFixture = [NSData dataWithBytes:downloadBytes length:strlen(downloadBytes)];
		NSURL *downloadURL = fn_write_fixture(@"fnconn-download.txt", downloadFixture);
		FNDownloadRecorder *rec = [[FNDownloadRecorder alloc] init];
		NSURLConnection *conn = [NSURLConnection connectionWithRequest:
						[NSURLRequest requestWithURL:downloadURL]
					delegate:rec];
		BOOL ended = fn_wait_flag(&rec->done, 20000);
		NSData *landed = nil;

		(void)conn;
		if (rec->destination != nil) {
			landed = [NSData dataWithContentsOfFile:[rec->destination path]];
		}

		/* THE ROUND TRIP IS THE CHECK THAT MATTERS: the delegate is handed a FILE holding the body, and a
		 * caller could keep it — which is what the door MEANS. §63.154: this class writes that file itself
		 * now (§63.145 step 3 put the file writing where 10.2 had it, and the seam hands over chunks), so
		 * this check is the acceptance for code no other probe reaches. */
		check("download-round-trip",
		      ended && rec->finishCount == 1 && rec->failCount == 0 &&
		      landed != nil && [landed isEqualToData:downloadFixture],
		      [NSString stringWithFormat:@"ended=%d finishes=%d fails=%d landed=%d bytes=%d",
			(int)ended, rec->finishCount, rec->failCount, (int)(landed != nil),
			(int)[landed length]]);

		/* AND THE DISPATCH RULE HOLDS: a delegate that implements the data doors TOO is not fed by them,
		 * because a download's contract is the FILE. */
		check("the-data-doors-are-not-used-for-a-download",
		      rec->responseDoorCalls == 0 && rec->dataDoorCalls == 0,
		      [NSString stringWithFormat:@"responses=%d data=%d", rec->responseDoorCalls,
			rec->dataDoorCalls]);

		/* AND THE PROGRESS DOOR IS HEARD, WITH THIS TRANSFER'S OWN TOTALS: the numbers are this class's own
		 * count of what it has accumulated for the file, so the last report has to equal the fixture. */
		check("the-download-progress-door-is-reported",
		      rec->writeProgressCalls >= 1 &&
		      rec->lastTotalWritten == (long long)[downloadFixture length],
		      [NSString stringWithFormat:@"calls=%d lastTotal=%ld expected=%ld fixture=%d",
			rec->writeProgressCalls, (long)rec->lastTotalWritten,
			(long)rec->lastExpectedTotal, (int)[downloadFixture length]]);

		/* THE CONNECTION'S RESUME DOOR IS ABSENT, AND THAT REFUSAL IS STRUCTURAL: an `NSURLConnection` cannot
		 * be GIVEN resume data — the class has no initialiser that takes it — so a door that announces a
		 * resume would be announcing something that cannot happen.
		 *
		 * ⚠⚠ AND THE SECOND HALF OF THIS CHECK WENT WITH THE SESSION FAMILY (§63.159): it used to assert that
		 * the SESSION had the resume API, which is what made the absence a property of THIS CLASS rather than
		 * of the loading system. THERE IS NO SESSION TO ASK ANY MORE, and that is the stronger form of the same
		 * statement — nothing in this library can resume, so the door could not be implemented even if it were
		 * declared. */
		check("download-refusals-are-absent",
		      !fn_protocol_has(dl,
			"connectionDidResumeDownloading:totalBytesWritten:expectedTotalBytes:"),
		      @"the CONNECTION's resume door is absent (§62.31): the refusal is structural - a "
		      @"connection cannot be given resume data - not a missing dependency");
	}

	/* --- 9. THE AUTHENTICATION TRANSLATION, ON THIS CLASS'S OWN DOOR ---------------------------------
	 *
	 * THE PRECEDENCE IS APPLE'S AND IT IS THE WHOLE SUBJECT: the MODERN door supersedes the deprecated
	 * pair, the GATE is asked next, and no door at all means the default WITHOUT WAITING. The connection is
	 * asked through the door the BRIDGE uses — APPLE'S `-URLProtocol:didReceiveAuthenticationChallenge:`,
	 * which takes no handler since §63.158 — and every check reads what the delegate recorded plus what the
	 * SENDER recorded, because THE SENDER IS THE ONLY ANSWER PATH NOW: a challenge's sender is the
	 * transport's own thunk, so an answer from the delegate reaches the transport and there is exactly one
	 * answer to count. "This class answered nothing" is a ZERO in that counter.
	 */
	{
		SEL sel = @selector(URLProtocol:didReceiveAuthenticationChallenge:);
		typedef void (*Fn)(id, SEL, NSURLProtocol *, NSURLAuthenticationChallenge *);
		NSURLProtectionSpace *space = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.invalid" port:80 protocol:@"http" realm:@"Probe"
	    authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSURLRequest *request = [NSURLRequest requestWithURL:fn_url(@"file:///dev/null")];

		/* THE MODERN DOOR SUPERSEDES THE PAIR, AND THIS DELEGATE IMPLEMENTS ALL THREE — so the check is
		 * about one that COULD have been asked twice. THE SENDER IS WHERE THE ANSWER LANDS: a connection
		 * delegate has no completion handler, so the credential goes back through `[challenge sender]`. */
		{
			FNSenderRecorder *sender = [[FNSenderRecorder alloc] init];
			FNAuthDelegate *auth = [[FNAuthDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)auth
								 startImmediately:NO];
			NSURLAuthenticationChallenge *challenge = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space proposedCredential:nil previousFailureCount:0
					 failureResponse:nil error:nil sender:sender];
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, challenge);
			}
			/* ⚠ THE ANSWER IS THE SENDER'S, AND IT IS COUNTED THERE (§63.158): the door takes no handler any
			 * more, so "this class did not answer the challenge itself" is no longer a separate thing to
			 * assert — an answer can only have come from the DELEGATE, through the sender the probe supplied.
			 * ONE answer is therefore the whole of the contract this check is about. */
			check("the-modern-door-supersedes-the-deprecated-pair",
			      present && auth->modernCalls == 1 && auth->gateCalls == 0 &&
			      auth->deprecatedCalls == 0 && sender->useCredentialCalls == 1 &&
			      sender->answers == 1,
			      [NSString stringWithFormat:@"modern=%d gate=%d deprecated=%d senderCredential=%d "
				@"senderAnswers=%d, and the only answer is the delegate's",
				auth->modernCalls, auth->gateCalls, auth->deprecatedCalls,
				sender->useCredentialCalls, sender->answers]);
		}

		/* THE DEPRECATED PAIR WHEN THE MODERN DOOR IS ABSENT: THE GATE FIRST, then the challenge — Apple's
		 * order — and both answered through the sender, exactly as the modern door is. */
		{
			FNSenderRecorder *sender = [[FNSenderRecorder alloc] init];
			FNDeprecatedAuthDelegate *auth = [[FNDeprecatedAuthDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)auth
								 startImmediately:NO];
			NSURLAuthenticationChallenge *challenge = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space proposedCredential:nil previousFailureCount:0
					 failureResponse:nil error:nil sender:sender];
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			auth->gateAnswer = YES;
			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, challenge);
			}
			check("the-deprecated-pair-is-asked-gate-first",
			      present && auth->gateCalls == 1 && auth->deprecatedCalls == 1 &&
			      sender->useCredentialCalls == 1 && sender->answers == 1,
			      [NSString stringWithFormat:@"gate=%d deprecated=%d senderCredential=%d "
				@"senderAnswers=%d", auth->gateCalls, auth->deprecatedCalls,
				sender->useCredentialCalls, sender->answers]);
		}

		/* AND A `NO` FROM THE GATE MEANS "DO NOT AUTHENTICATE": the transfer continues WITHOUT credentials,
		 * which is the transport's default handling — and the challenge door is not asked, because the gate
		 * has already answered. **THE ASSERTION IS A COUNT OF ZERO**, which is now the only way to say it:
		 * with no handler to leave unanswered, "nothing answered this challenge" IS the default. */
		{
			FNSenderRecorder *sender = [[FNSenderRecorder alloc] init];
			FNDeprecatedAuthDelegate *auth = [[FNDeprecatedAuthDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)auth
								 startImmediately:NO];
			NSURLAuthenticationChallenge *challenge = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space proposedCredential:nil previousFailureCount:0
					 failureResponse:nil error:nil sender:sender];
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			auth->gateAnswer = NO;
			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, challenge);
			}
			check("a-no-from-the-gate-means-no-authentication",
			      present && auth->gateCalls == 1 && auth->deprecatedCalls == 0 &&
			      sender->answers == 0 && sender->useCredentialCalls == 0,
			      [NSString stringWithFormat:@"gate=%d deprecated=%d senderAnswers=%d "
				@"senderCredential=%d, and NOBODY answered the challenge",
				auth->gateCalls, auth->deprecatedCalls, sender->answers,
				sender->useCredentialCalls]);
		}

		/* AND NO AUTH DOOR AT ALL MEANS THE DEFAULT, WITHOUT WAITING — the rule every door in this library
		 * keeps, and the reason a delegate that cares about none of this is never blocked on. The sender is
		 * supplied so that "the class answered nothing" is OBSERVED rather than assumed. */
		{
			FNSenderRecorder *sender = [[FNSenderRecorder alloc] init];
			FNEmptyConnDelegate *empty = [[FNEmptyConnDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)empty
								 startImmediately:NO];
			NSURLAuthenticationChallenge *challenge = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space proposedCredential:nil previousFailureCount:0
					 failureResponse:nil error:nil sender:sender];
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			if (present) {
				((Fn)objc_msgSend)(conn, sel, nil, challenge);
			}
			check("no-auth-door-means-the-default-without-waiting",
			      present && sender->answers == 0,
			      [NSString stringWithFormat:@"senderAnswers=%d", sender->answers]);
		}
	}

	/* --- 10. THE RE-SEND'S BODY, THROUGH THE SEAM'S FIRST-PARTY DOOR ---------------------------------
	 *
	 * THIS IS THE ONE ATTEMPT NO OTHER DOOR CAN REACH (§62.36): the TRANSPORT re-issues a 401 itself
	 * (`goto retry_transfer:` in the bridge), so the connection never sees the second attempt and no
	 * delegate door can be asked from there. The bridge therefore asks its client through
	 * `-URLProtocol:fnNewBodyStreamForReSend:`, and this class asks its delegate — whose answer, INCLUDING
	 * nil, goes straight back, because Apple's contract for a replacement is "a new, UNOPENED stream" and a
	 * delegate that has none says so by answering nothing.
	 */
	{
		SEL sel = @selector(URLProtocol:fnNewBodyStreamForReSend:);
		typedef NSInputStream *(*Fn)(id, SEL, NSURLProtocol *, NSURLRequest *);
		const char *freshBytes = "a fresh body, unopened and unstoppable\n";
		NSData *freshData = [NSData dataWithBytes:freshBytes length:strlen(freshBytes)];
		NSURLRequest *request = [NSURLRequest requestWithURL:fn_url(@"file:///dev/null")];

		{
			FNBodyStreamAnswerer *answerer = [[FNBodyStreamAnswerer alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)answerer
								 startImmediately:NO];
			NSInputStream *asked = nil;
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			answerer->stream = [NSInputStream inputStreamWithData:freshData];
			if (present) {
				asked = ((Fn)objc_msgSend)(conn, sel, nil, request);
			}
			check("the-re-sends-body-comes-from-the-delegate",
			      present && answerer->asks == 1 && asked == answerer->stream,
			      [NSString stringWithFormat:@"asks=%d answeredTheDelegatesStream=%d",
				answerer->asks, (int)(asked == answerer->stream)]);
		}

		/* AND A DELEGATE THAT IMPLEMENTS NO SUCH DOOR ANSWERS NOTHING RATHER THAN BEING BLOCKED ON: nil is
		 * what the transport re-runs with, which is the failure its own header describes. */
		{
			FNEmptyConnDelegate *empty = [[FNEmptyConnDelegate alloc] init];
			NSURLConnection *conn = [[NSURLConnection alloc] initWithRequest:request
									 delegate:(id)empty
								 startImmediately:NO];
			NSInputStream *asked = nil;
			BOOL present = (conn != nil) && [conn respondsToSelector:sel];

			if (present) {
				asked = ((Fn)objc_msgSend)(conn, sel, nil, request);
			}
			check("the-re-sends-body-with-no-delegate-door-is-nil",
			      present && asked == nil,
			      [NSString stringWithFormat:@"answeredNil=%d", (int)(asked == nil)]);
		}
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
