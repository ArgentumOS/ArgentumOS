/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNCURLURLProtocol.m — libcurl driven behind the seam. The design and the reasoning are in
 * FNCURLURLProtocol.h.
 */
#import <Foundation/FNCURLURLProtocol.h>
#import <Foundation/NSURLProtectionSpace.h>
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSURLAuthenticationChallenge.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLCache.h>
/* THE RECORD THE PRIVATE DOOR BELOW CARRIES, AND ITS INTERNAL WRITER CATEGORY (§52): what the transport
 * measured is reported as the PUBLIC record, so the seam learns nothing about curl. */
#import <Foundation/NSURLSessionTaskMetrics.h>
#import <Foundation/NSData.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSError.h>
#import <Foundation/NSAutoreleasePool.h>

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* THE TRANSFER'S OWN STATE, on the loading thread's stack for as long as the transfer runs. It carries
 * the curl handle because the HEADER CALLBACK needs to ask curl for the response code while it is
 * running, which is the one documented thing a callback may do with the easy handle it was handed. */
typedef struct FNCurlTransfer {
	FNCURLURLProtocol *protocol;	/* not retained: the loading thread retains it for the lock's life */
	CURL *curl;
	NSMutableData *headerBytes;	/* accumulated until the blank line that ends the header block */
	int responded;			/* didReceiveResponse has been reported */
	int redirected;			/* a redirect was reported: the transfer ends here, by design */
	int retry;			/* a 401 was answered with a credential: re-issue ONCE */
	int attempt;			/* how many times a challenge has been decided: the guard */
	int cancelledChallenge;		/* the client cancelled the challenge */
	NSURLCredential *credential;	/* the credential the client handed back, or nil */
	volatile int stopped;		/* -stopLoading was called from another thread */
} FNCurlTransfer;

/* THE PRIVATE DOORS THE C CALLBACKS REACH THE OBJECT THROUGH: a C function cannot message an object it
 * only has as a `void *`, and the reporting has to happen at the moment the callback sees the fact. */
@interface FNCURLURLProtocol (FNCurlReporting)
- (void)fnReportResponseWithStatus:(long)status headers:(NSDictionary *)headers;
/* AND THE MEASUREMENTS GO UP THROUGH ONE (§52): the record is built WHERE THE NUMBERS ARE - on the handle,
 * before curl_easy_cleanup - and then reported through the client's first-party door, which a client is not
 * required to implement. */
- (void)fnReportMetrics:(NSURLSessionTaskTransactionMetrics *)metrics;
/* THE CLIENT IS ASKED ABOUT A CHALLENGE THROUGH THIS, because the C header callback cannot message it. The
 * door is the client protocol's, and the client answers through the handler - synchronously, by contract. */
- (void)fnAskClientForCredential:(NSURLAuthenticationChallenge *)challenge
	       completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
					   NSURLCredential *credential))completionHandler;
- (void)fnReportRedirectToURL:(NSString *)location status:(long)status headers:(NSDictionary *)headers;
- (void)fnReportFailure:(NSError *)error;
@end

/* PARSE A HEADER BLOCK into a dictionary, one "Name: value" per line, names as the SERVER spelled them
 * (the case rules are the consumers' business, exactly as they are for a request's fields). A line
 * without a colon is skipped rather than guessed at, and a status line is skipped by the same test. */
static NSDictionary *fn_curl_header_fields(NSData *bytes)
{
	NSString *text = [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
	NSMutableDictionary *fields;
	NSString *line;
	NSArray *lines;
	NSUInteger i;

	if (text == nil) {
		return nil;
	}
	fields = [[NSMutableDictionary alloc] init];
	lines = [text componentsSeparatedByString:@"\n"];
	for (i = 0; i < [lines count]; i++) {
		NSRange colon;
		NSString *name, *value;

		line = [lines objectAtIndex:i];
		if ([line hasSuffix:@"\r"]) {
			line = [line substringToIndex:[line length] - 1];
		}
		colon = [line rangeOfString:@":"];
		if (colon.location == NSNotFound || colon.location == 0) {
			continue;	/* the status line, a continuation, or nothing at all */
		}
		name = [line substringToIndex:colon.location];
		value = [line substringFromIndex:colon.location + 1];
		while ([value hasPrefix:@" "]) {
			value = [value substringFromIndex:1];
		}
		if ([name length] > 0) {
			[fields setObject:value forKey:name];
		}
	}
	[text release];
	return [fields autorelease];
}

/* THE BODY ARRIVES HERE, AND IT IS FORWARDED AS IT ARRIVES: a chunk a caller can act on is worth more
 * than an accumulated buffer, and it is the seam's contract. Returning 0 tells curl to abort, which is
 * how -stopLoading takes effect. */
static size_t fn_curl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FNCurlTransfer *transfer = (FNCurlTransfer *)userdata;
	size_t bytes = size * nmemb;
	id <NSURLProtocolClient> client;

	if (transfer->stopped) {
		return 0;
	}
	client = [transfer->protocol client];
	if (client == nil) {
		return bytes;	/* nobody is listening: drain rather than abort */
	}
	[client URLProtocol:transfer->protocol
		didLoadData:[NSData dataWithBytes:ptr length:bytes]];
	return bytes;
}

/* THE HEADER BLOCK ARRIVES HERE, LINE BY LINE, AND THE BLANK LINE IS THE MOMENT THE RESPONSE EXISTS. */
/* A PROTECTION SPACE OUT OF A WWW-Authenticate HEADER, which is where the realm and the method actually are:
 * `Basic realm="Restricted"` is the whole of what a server tells a client it will accept. An UNKNOWN scheme
 * becomes NSURLAuthenticationMethodDefault rather than a refusal, because a server asking with a scheme this
 * library does not know is still a server asking. */
static NSURLProtectionSpace *fn_protection_space_from_challenge(NSString *header, NSURL *url)
{
	NSString *scheme = header;
	NSString *realm = nil;
	NSString *method = NSURLAuthenticationMethodDefault;
	NSRange space = [header rangeOfString:@" "];
	NSRange realmRange;

	if (space.location != NSNotFound) {
		scheme = [header substringToIndex:space.location];
	}
	realmRange = [header rangeOfString:@"realm=\""];
	if (realmRange.location != NSNotFound) {
		NSUInteger from = realmRange.location + realmRange.length;
		NSRange closing = [header rangeOfString:@"\"" options:0
					   range:NSMakeRange(from, [header length] - from)];

		if (closing.location != NSNotFound) {
			realm = [header substringWithRange:NSMakeRange(from, closing.location - from)];
		}
	}
	if ([scheme caseInsensitiveCompare:@"Basic"] == NSOrderedSame) {
		method = NSURLAuthenticationMethodHTTPBasic;
	} else if ([scheme caseInsensitiveCompare:@"Digest"] == NSOrderedSame) {
		method = NSURLAuthenticationMethodHTTPDigest;
	} else if ([scheme caseInsensitiveCompare:@"NTLM"] == NSOrderedSame) {
		method = NSURLAuthenticationMethodNTLM;
	} else if ([scheme caseInsensitiveCompare:@"Negotiate"] == NSOrderedSame) {
		method = NSURLAuthenticationMethodNegotiate;
	}
	return [[[NSURLProtectionSpace alloc] initWithHost:[url host]
						     port:[[url port] integerValue]
						 protocol:[url scheme]
						    realm:realm
					   authenticationMethod:method] autorelease];
}

static size_t fn_curl_header(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FNCurlTransfer *transfer = (FNCurlTransfer *)userdata;
	size_t bytes = size * nmemb;
	long status = 0;
	int blank = (bytes == 2 && ptr[0] == '\r' && ptr[1] == '\n') ||
		    (bytes == 1 && ptr[0] == '\n');

	if (transfer->stopped) {
		return 0;
	}
	if (!blank) {
		[transfer->headerBytes appendBytes:ptr length:bytes];
		return bytes;
	}

	/* THE END OF THE HEADER BLOCK: the response is complete and complete means reportable. */
	curl_easy_getinfo(transfer->curl, CURLINFO_RESPONSE_CODE, &status);
	{
		NSDictionary *fields = fn_curl_header_fields(transfer->headerBytes);
		NSString *location = [fields objectForKey:@"Location"];

		/* A REDIRECT IS REPORTED RATHER THAN FOLLOWED (decision 2 in the header): a 3xx that NAMES a
		 * destination is the seam's redirect callback, and the transfer stops there. */
		if (location != nil &&
		    (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
			transfer->redirected = 1;
			[transfer->protocol fnReportRedirectToURL:location status:status headers:fields];
			return 0;	/* abort: the caller decides what happens next */
		}
		/* A 401 IS A RESPONSE TOO, and it is where authentication is decided. The client is asked THROUGH
		 * THE DOOR, synchronously - the same wait the response disposition takes - and if it hands back a
		 * credential the transfer is ABORTED here and RE-ISSUED once by the caller, which is the redirect
		 * pattern exactly. The attempt count IS the guard: a server that always answers 401 must not spin,
		 * and the loop below runs twice at most. */
		if (status == 401 && transfer->retry == 0 && transfer->attempt == 0 &&
		    [fields objectForKey:@"WWW-Authenticate"] != nil &&
		    [[fields objectForKey:@"WWW-Authenticate"] rangeOfString:@"Basic"
				   options:NSCaseInsensitiveSearch].location != NSNotFound) {
			__block NSInteger disposition = -1;
			__block NSURLCredential *credential = nil;
			NSURLProtectionSpace *space = fn_protection_space_from_challenge(
				[fields objectForKey:@"WWW-Authenticate"], [[transfer->protocol request] URL]);

			transfer->attempt = 1;
			if (space != nil) {
				NSURLAuthenticationChallenge *challenge = [[NSURLAuthenticationChallenge alloc]
					initWithProtectionSpace:space
					     proposedCredential:nil
					   previousFailureCount:0
						failureResponse:nil
						      error:nil
						     sender:nil];

				[transfer->protocol fnAskClientForCredential:challenge
					completionHandler:^(NSURLSessionAuthChallengeDisposition chosen,
							    NSURLCredential *given) {
					disposition = (NSInteger)chosen;
					credential = [given retain];
				}];
				[challenge release];
			}
			if (disposition == NSURLSessionAuthChallengeUseCredential && credential != nil) {
				transfer->credential = credential;	/* kept for the re-issue */
				transfer->retry = 1;
				return 0;	/* abort: the caller re-issues with the credential */
			}
			[credential release];
			if (disposition == NSURLSessionAuthChallengeCancelAuthenticationChallenge) {
				transfer->cancelledChallenge = 1;
				return 0;
			}
			/* Every other answer leaves the 401 AS THE RESPONSE, which is the honest outcome: the server
			 * asked, the client declined to answer, and what it gets is what the server said. */
		}
		[transfer->protocol fnReportResponseWithStatus:status headers:fields];
	}
	return bytes;
}

static size_t fn_curl_header_discard(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	return size * nmemb;
}

/* THE RECORD OF ONE ATTEMPT (§52), BUILT WHERE THE MEASUREMENTS ARE AND NOWHERE ELSE: this is the only
 * place in the library holding the curl handle, and every field below is read FROM IT before
 * curl_easy_cleanup. Three rules decide what is filled and what is left:
 *
 *   * CURL REPORTS DURATIONS, NOT INSTANTS, so the attempt's start - captured immediately before
 *     curl_easy_perform - is the base every date is added to;
 *   * A PHASE WHOSE DURATION IS ZERO DID NOT HAPPEN, so both of its markers stay NIL rather than collapsing
 *     onto the start (the record's own rule: nil is not zero, and a DNS pair filled with zeros would claim a
 *     lookup that never occurred);
 *   * A FIELD WITH NO SOURCE IS LEFT AND SAID SO: localAddress and the domain-resolution protocol are not
 *     reported by the transport at all, and countOfRequestHeaderBytesSent has no source - a count has no nil
 *     to say "not reported" with, so it stays 0 and the header documents it.
 *
 * AND THE FOUR NETWORK BOOLEANS ARE ANSWERED FROM THE PLATFORM rather than from the transfer: cellular,
 * expensive, constrained and multipath describe the NETWORK A MACHINE IS ON, and this system is on none of
 * those - which is a true answer and a checkable one. */
static NSURLSessionTaskTransactionMetrics *fn_metrics_for_transfer(FNCurlTransfer *transfer,
								  NSDate *started)
{
	NSURLSessionTaskTransactionMetrics *metrics = [[NSURLSessionTaskTransactionMetrics alloc] init];
	NSURLRequest *request = [transfer->protocol request];
	NSURLResponse *response = nil;
	NSDictionary *fields = nil;
	NSDate *connectStart = started;
	BOOL hasRemote = NO;
	double elapsed = 0.0;
	long status = 0;
	long value = 0;
	char *text = NULL;
	curl_off_t count = 0;

	/* THE ANSWER'S HEAD, from the bytes this attempt received: the same source the client was told from, so
	 * a record and a response cannot disagree. No status at all (a file transfer, or a failure before the
	 * head arrived) leaves the response nil - which is Apple's own answer for a transaction that never got
	 * one. */
	if ([transfer->headerBytes length] > 0) {
		fields = fn_curl_header_fields(transfer->headerBytes);
	}
	if (fields != nil &&
	    curl_easy_getinfo(transfer->curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status > 0) {
		response = [[[NSHTTPURLResponse alloc] initWithURL:[request URL]
							statusCode:status
						       HTTPVersion:nil
						      headerFields:fields] autorelease];
	}

	[metrics fnSetRequest:request response:response];
	[metrics fnSetFetchStartDate:started];

	if (curl_easy_getinfo(transfer->curl, CURLINFO_NAMELOOKUP_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		/* THE LOOKUP'S START IS THE TRANSACTION'S OWN, because the lookup is the first phase and the
		 * transport reports only how long it took. */
		[metrics fnSetDomainLookupStartDate:started];
		connectStart = [started dateByAddingTimeInterval:elapsed];
		[metrics fnSetDomainLookupEndDate:connectStart];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_CONNECT_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		[metrics fnSetConnectStartDate:connectStart];
		[metrics fnSetConnectEndDate:[started dateByAddingTimeInterval:elapsed]];
	}
	/* THE SECURE CONNECTION'S END ONLY, and that is §50.1's measurement rather than an omission: curl
	 * reports the handshake finishing and never when it started, so the start marker STAYS NIL. */
	if (curl_easy_getinfo(transfer->curl, CURLINFO_APPCONNECT_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		[metrics fnSetSecureConnectionEndDate:[started dateByAddingTimeInterval:elapsed]];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_PRETRANSFER_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		[metrics fnSetRequestStartDate:[started dateByAddingTimeInterval:elapsed]];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_STARTTRANSFER_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		NSDate *firstByte = [started dateByAddingTimeInterval:elapsed];

		/* ONE MEASUREMENT IN TWO FIELDS: the instant the first byte arrived is when the request finished
		 * going out AND when the answer began, which is true of a request-response exchange and is said
		 * here rather than invented twice. */
		[metrics fnSetRequestEndDate:firstByte];
		[metrics fnSetResponseStartDate:firstByte];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_TOTAL_TIME, &elapsed) == CURLE_OK && elapsed > 0.0) {
		[metrics fnSetResponseEndDate:[started dateByAddingTimeInterval:elapsed]];
	}

	if (curl_easy_getinfo(transfer->curl, CURLINFO_PRIMARY_IP, &text) == CURLE_OK && text != NULL) {
		[metrics fnSetRemoteAddress:[NSString stringWithUTF8String:text]];
		hasRemote = YES;
	}
	if (hasRemote &&
	    curl_easy_getinfo(transfer->curl, CURLINFO_NUM_CONNECTS, &value) == CURLE_OK) {
		/* A TRANSFER THAT DIALED NOTHING REUSED ITS CONNECTION - and the test is guarded by a remote
		 * address, because a `file://` transfer dials nothing either and is not a reused connection. */
		[metrics fnSetReusedConnection:value == 0];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_HTTP_VERSION, &value) == CURLE_OK) {
		switch (value) {
		case CURL_HTTP_VERSION_1_0:
			[metrics fnSetNetworkProtocolName:@"http/1.0"];
			break;
		case CURL_HTTP_VERSION_1_1:
			[metrics fnSetNetworkProtocolName:@"http/1.1"];
			break;
		case CURL_HTTP_VERSION_2_0:
		case CURL_HTTP_VERSION_2TLS:
			[metrics fnSetNetworkProtocolName:@"h2"];
			break;
		case CURL_HTTP_VERSION_3:
			[metrics fnSetNetworkProtocolName:@"h3"];
			break;
		default:
			break;	/* NONE, or a version this mapping does not name: nil rather than a guess */
		}
	}

	if (curl_easy_getinfo(transfer->curl, CURLINFO_HEADER_SIZE, &value) == CURLE_OK) {
		[metrics fnSetCountOfResponseHeaderBytesReceived:(NSInteger)value];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_SIZE_DOWNLOAD_T, &count) == CURLE_OK) {
		/* NOTHING IS DECODED SEPARATELY, so the two response-body counts are the same number - §50.1's
		 * rule for the request's pair, and saying so beats inventing a distinction. */
		[metrics fnSetCountOfResponseBodyBytesReceived:(NSInteger)count];
		[metrics fnSetCountOfResponseBodyBytesAfterDecoding:(NSInteger)count];
	}
	if (curl_easy_getinfo(transfer->curl, CURLINFO_SIZE_UPLOAD_T, &count) == CURLE_OK) {
		[metrics fnSetCountOfRequestBodyBytesSent:(NSInteger)count];
		[metrics fnSetCountOfRequestBodyBytesBeforeEncoding:(NSInteger)count];
	}

	[metrics fnSetCellular:NO];
	[metrics fnSetExpensive:NO];
	[metrics fnSetConstrained:NO];
	[metrics fnSetMultipath:NO];
	[metrics fnSetResourceFetchType:NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad];

	return metrics;
}

@implementation FNCURLURLProtocol

/* THE THREE SCHEMES CURL ON THIS SYSTEM ACTUALLY SPEAKS, and nothing else: `curl --version` answers
 * `Protocols: file http https`, which tools/curl-build.sh asserts, so claiming a fourth would promise a
 * transfer that cannot happen. */
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	NSString *scheme = [[request URL] scheme];

	return [scheme isEqualToString:@"file"] ||
	       [scheme isEqualToString:@"http"] ||
	       [scheme isEqualToString:@"https"];
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
	return request;
}

/* --- THE TRANSFER, ON ITS OWN THREAD ------------------------------------------------------------- */

- (void)startLoading
{
	[NSThread detachNewThreadSelector:@selector(fnPerformTransfer)
				 toTarget:self
			       withObject:nil];
}

/* STOPS AT THE NEXT CHUNK (the header's refusal, stated there): a cross-thread curl_easy call would be
 * undefined behaviour, so the flag the write callback reads is the mechanism. */
- (void)stopLoading
{
	if (_transfer != NULL) {
		((FNCurlTransfer *)_transfer)->stopped = 1;
	}
	/* AND A CACHE HIT HAS NO TRANSFER TO REACH, which is how a cancelled response still delivered its body:
	 * the disposition door cancels the task, -stopLoading is called, and the hit path below carried on
	 * because the flag it would have read did not exist yet. This one does. */
	_hitStopped = 1;
}

- (void)fnPerformTransfer
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	FNCurlTransfer transfer;
	CURL *curl;
	CURLcode result;
	NSURLRequest *request = [self request];
	NSString *urlString = [[request URL] absoluteString];
	struct curl_slist *headerList = NULL;
	int failed = 0;

	memset(&transfer, 0, sizeof(transfer));
	transfer.protocol = self;
	transfer.headerBytes = [[NSMutableData alloc] init];

	/* THE CACHE IS ASKED BEFORE THE NETWORK, AND A HIT DOES NOT DIAL OUT AT ALL - which is the entire point
	 * of a cache and the easiest half of it to get wrong: a hit that still opens a connection costs what it
	 * was supposed to save. The stored response goes to the client through the SAME doors a live one would,
	 * so a delegate cannot tell a hit from a miss except by the order it sees them in. */
	{
		NSCachedURLResponse *cached = [[NSURLCache sharedURLCache] cachedResponseForRequest:request];

		if(cached != nil) {
			/* THE RESPONSE GOES THROUGH THE SAME DOOR A LIVE ONE DOES, which is where a delegate gets to
			 * CANCEL it - and THEN THE HIT MUST STOP, exactly as the live path stops when its write callback
			 * sees the flag. Without this the cancel was honoured by the task and ignored by the protocol, so
			 * a cancelled response delivered its body anyway. */
			[_client URLProtocol:self
			    didReceiveResponse:[cached response]
			     cacheStoragePolicy:[cached storagePolicy]];
			/* THE IVAR, NOT A FIELD OF THIS STRUCT: -stopLoading runs on ANOTHER thread with no transfer in
			 * hand, so the flag it can write is the object's - and reading a struct field nothing writes is
			 * exactly the mistake that made the first version of this fix do nothing (and the same shape as
			 * the cache's own setter, for the third time in this session). */
			if(_hitStopped) {
				[transfer.headerBytes release];
				[pool release];
				return;
			}
			[_client URLProtocol:self didLoadData:[cached data]];
			[_client URLProtocolDidFinishLoading:self];
			[transfer.headerBytes release];
			[pool release];
			return;
		}
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		[_client URLProtocol:self didFailWithError:
			[NSError errorWithDomain:@"FNCURLURLProtocol"
					    code:1
					userInfo:nil]];
		[transfer.headerBytes release];
		[pool release];
		return;
	}
	transfer.curl = curl;
	curl_easy_setopt(curl, CURLOPT_URL, urlString != nil ? [urlString UTF8String] : "");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fn_curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, fn_curl_header);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
	/* THE REDIRECT DECISION (decision 2): curl must NOT chase it, because reporting it is this seam's. */
	/* THE REQUEST'S OWN TIMEOUT IS APPLIED, AND IT WAS NOT: NSURLRequest carries a timeoutInterval (60
	 * seconds by default) and curl was given no timeout at all, so a transfer to a server that never answers
	 * waited FOREVER - found by a probe that posts to a receiver which accepts and says nothing. A
	 * non-positive interval is left alone, because for curl that means no timeout. */
	if ([[self request] timeoutInterval] > 0.0) {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
				 (long)([[self request] timeoutInterval] * 1000.0));
	}
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	/* No SIGALRM-based timeouts: this is a library thread inside a process that may have its own. */
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	/* THE REQUEST'S OWN METHOD, HEADERS AND BODY, because a bridge that only GETs is a bridge that
	 * silently drops half of what a request can say. */
	{
		NSString *method = [request HTTPMethod];
		NSDictionary *fields = [request allHTTPHeaderFields];
		NSArray *names;
		NSUInteger i;

		if (method != nil && ![method isEqualToString:@"GET"]) {
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, [method UTF8String]);
		}
		if (fields != nil) {
			names = [fields allKeys];
			for (i = 0; i < [names count]; i++) {
				NSString *name = [names objectAtIndex:i];
				NSString *line = [NSString stringWithFormat:@"%@: %@",
						  name, [fields objectForKey:name]];
				headerList = curl_slist_append(headerList, [line UTF8String]);
			}
		}
		if (headerList != NULL) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
		}
		if ([request HTTPBody] != nil) {
			/* THE BYTES ARE HANDED OVER AS BYTES, AND THE FIRST VERSION DID NOT: it round-tripped the body
			 * through -initWithData:encoding:NSUTF8StringEncoding and sent the STRING's bytes, so a body
			 * that is not valid UTF-8 made that initializer answer nil and THE BODY WAS DROPPED - not
			 * corrupted, OMITTED, with no error and a server that could only report a request with nothing
			 * in it. A body is bytes; only a caller that knows better should call it text. */
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, [[request HTTPBody] bytes]);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
					 (long)[[request HTTPBody] length]);
		}
	}

	/* PUBLISHED BEFORE THE TRANSFER RUNS, so -stopLoading can reach it (and cleared after, so a stop
	 * that arrives after the transfer has ended does nothing rather than touching a dead struct). */
	/* THE RE-ISSUE POINT, and the whole loop is two lines plus a guard: a 401 answered with a credential
	 * sets the flag from inside the header callback (which aborts the transfer), and control comes back
	 * here. THE ATTEMPT COUNT IS THE GUARD - the flag is only ever set when transfer->attempt was 0, so the
	 * second pass cannot set it again, and a server that always answers 401 does not spin. */
retry_transfer:
	if (transfer.credential != nil) {
		curl_easy_setopt(curl, CURLOPT_USERNAME, [[transfer.credential user] UTF8String]);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, [[transfer.credential password] UTF8String]);
		/* CURLAUTH_BASIC AND NOT CURLAUTH_ANY, AND THAT IS THE WHOLE OF THIS BUG: with ANY, curl WAITS FOR A
		 * 401 before it will put credentials on the wire - so the re-issued request went out bare, the probe's
		 * server answered 200 without ever asking again, and the credential never travelled. The challenge
		 * TOLD this bridge the scheme (that is what WWW-Authenticate is for), the transport has already
		 * decided, and the credential a caller hands over is a user and a password - which is exactly what
		 * BASIC carries and nothing else here does. So it goes out pre-emptively, once, by name. */
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
	}
	/* THE INSTANT THIS ATTEMPT BEGINS (§52): curl reports DURATIONS and no instants, so this capture is the
	 * base of every date in the record - and it is taken PER ATTEMPT, because a re-issued 401 is a SECOND
	 * transaction rather than a longer first one. */
	{
		NSDate *attemptStarted = [NSDate date];

		_transfer = &transfer;
		result = curl_easy_perform(curl);
		_transfer = NULL;
		/* AND WHAT IT COST GOES UP BEFORE ITS OUTCOME IS DECIDED, which is the order Apple delivers in:
		 * the record describes the exchange, and whether the exchange then failed or finished is the
		 * outgoing report's business rather than the metrics'. */
		[self fnReportMetrics:fn_metrics_for_transfer(&transfer, attemptStarted)];
	}
	if (transfer.retry && transfer.credential != nil) {
		transfer.retry = 0;
		transfer.responded = 0;
		goto retry_transfer;
	}

	if (transfer.cancelledChallenge) {
		/* THE CLIENT CANCELLED THE CHALLENGE: that is a failure of the task, not a response to hand back. */
		/* THE CODE IS THE TRANSPORT'S OWN, because the NSURLError* constant mass is NOT SHIPPED YET (it is
		 * its own ledger row and its own slice): naming a constant this library does not declare would
		 * either invent a value or fail to build, and CURLE_ABORTED_BY_CALLBACK is exactly what happened -
		 * the transfer was aborted because the client said no. */
		[_client URLProtocol:self didFailWithError:
			[NSError errorWithDomain:@"FNCURLURLProtocol"
					    code:(NSInteger)CURLE_ABORTED_BY_CALLBACK
					userInfo:nil]];
	} else if (transfer.stopped) {
		/* THE CALLER STOPPED IT: no finish and no failure, because neither happened. */
	} else if (transfer.redirected) {
		/* ALREADY REPORTED, and the transfer ended there on purpose. */
	} else if (result != CURLE_OK) {
		failed = 1;
		[_client URLProtocol:self didFailWithError:
			[NSError errorWithDomain:@"FNCURLURLProtocol"
					    code:(NSInteger)result
					userInfo:nil]];
	} else {
		[_client URLProtocolDidFinishLoading:self];
	}
	(void)failed;

	if (headerList != NULL) {
		curl_slist_free_all(headerList);
	}
	curl_easy_cleanup(curl);
	[transfer.headerBytes release];
	[pool release];
}

/* THE MEASUREMENTS GO UP THROUGH THIS, AND A CLIENT THAT DOES NOT IMPLEMENT THE DOOR IS SIMPLY NOT TOLD
 * (§52). The record was built from the handle; what is left here is the RULE every first-party door in this
 * library keeps - no door means nothing is reported, and it is not a failure. */
- (void)fnReportMetrics:(NSURLSessionTaskTransactionMetrics *)metrics
{
	if (_client == nil ||
	    ![_client respondsToSelector:@selector(URLProtocol:fnDidCollectMetrics:)]) {
		return;
	}
	[_client URLProtocol:self fnDidCollectMetrics:metrics];
}

/* --- THE PRIVATE REPORTING DOORS ----------------------------------------------------------------- */

- (void)fnAskClientForCredential:(NSURLAuthenticationChallenge *)challenge
	       completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
					   NSURLCredential *))completionHandler
{
	if (_client == nil ||
	    ![_client respondsToSelector:
		@selector(URLProtocol:didReceiveAuthenticationChallenge:completionHandler:)]) {
		/* NO DOOR MEANS NO OPINION, WITHOUT WAITING - the rule every door in this library keeps. */
		completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
		return;
	}
	[_client URLProtocol:self
	    didReceiveAuthenticationChallenge:challenge
		    completionHandler:completionHandler];
}

- (void)fnReportResponseWithStatus:(long)status headers:(NSDictionary *)headers
{
	if (_client == nil) {
		return;
	}
	NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:[[self request] URL]
								  statusCode:status
								 HTTPVersion:nil
								headerFields:headers];

	/* THE ADVICE IS "DO NOT STORE" IN v1, AND THAT IS A DECISION RATHER THAN A DEFAULT: there is no
	 * cache in this library yet (NSURLCache is its own slice), so a policy of "allowed" would be a
	 * promise nothing keeps. Saying NotAllowed is the honest answer until a store exists. */
	[_client URLProtocol:self
	    didReceiveResponse:response
	     cacheStoragePolicy:NSURLCacheStorageNotAllowed];
	[response release];
}

- (void)fnReportRedirectToURL:(NSString *)location status:(long)status headers:(NSDictionary *)headers
{
	NSURL *target;
	NSMutableURLRequest *next;
	NSHTTPURLResponse *response;

	if (_client == nil) {
		return;
	}
	target = [NSURL URLWithString:location];
	if (target == nil) {
		return;
	}
	next = [NSMutableURLRequest requestWithURL:target];
	response = [[NSHTTPURLResponse alloc] initWithURL:[[self request] URL]
					       statusCode:status
					      HTTPVersion:nil
					     headerFields:headers];
	[_client URLProtocol:self wasRedirectedToRequest:next redirectResponse:response];
	[response release];
}

- (void)fnReportFailure:(NSError *)error
{
	if (_client != nil) {
		[_client URLProtocol:self didFailWithError:error];
	}
}

@end
