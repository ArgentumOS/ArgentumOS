/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNCURLURLProtocol.m — libcurl driven behind the seam. The design and the reasoning are in
 * FNCURLURLProtocol.h.
 */
#import <Foundation/FNCURLURLProtocol.h>
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
	volatile int stopped;		/* -stopLoading was called from another thread */
} FNCurlTransfer;

/* THE PRIVATE DOORS THE C CALLBACKS REACH THE OBJECT THROUGH: a C function cannot message an object it
 * only has as a `void *`, and the reporting has to happen at the moment the callback sees the fact. */
@interface FNCURLURLProtocol (FNCurlReporting)
- (void)fnReportResponseWithStatus:(long)status headers:(NSDictionary *)headers;
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
		[transfer->protocol fnReportResponseWithStatus:status headers:fields];
	}
	return bytes;
}

static size_t fn_curl_header_discard(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	return size * nmemb;
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
	_transfer = &transfer;
	result = curl_easy_perform(curl);
	_transfer = NULL;

	if (transfer.stopped) {
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

/* --- THE PRIVATE REPORTING DOORS ----------------------------------------------------------------- */

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
