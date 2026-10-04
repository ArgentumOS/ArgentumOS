/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlrequest, unit of 1 — W7 slice 1's acceptance for NSURLRequest, NSMutableURLRequest,
 * NSURLResponse and NSHTTPURLResponse. docs/design/foundation-plan.md §46.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THIS IS A VALUE UNIT AND ITS PROBE IS A VALUE PROBE: no socket, no session and no transport anywhere.
 * What it asserts is the pair's contract — the defaults the two convenience doors answer, the mutability
 * boundary (a mutable request's -copy is an immutable snapshot later mutation cannot reach), the two
 * HTTP header doors' DIFFERENT rules (set replaces, add appends), the case-insensitivity RFC 9110
 * requires, the enums' values (which are OURS under D2), and the one piece of derived arithmetic in the
 * slice: a response's MIME type, charset and expected length come from the headers the caller passed.
 *
 * THE LAST TWO CHECKS ARE THE ONES THAT EARN THEIR PLACE:
 *   http-response-status-phrase  RFC 9110 §15's own rows, so the expected values come from the document
 *                                that defines them rather than from anything this file could be written
 *                                to match — and a code outside the registry must answer nil;
 *   url-request-api-inventory    the audited Cocoa inventory: every selector this unit owes EXISTS and
 *                                every one it REFUSES is ABSENT, so the inventory cannot drift from code.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLREQUEST %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLREQUEST %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* The headers a response is built from, as the caller would hand them over. */
static NSDictionary *fn_headers(void)
{
	NSMutableDictionary *headers = [[NSMutableDictionary alloc] init];

	[headers setObject:@"text/html; charset=utf-8" forKey:@"Content-Type"];
	[headers setObject:@"42" forKey:@"Content-Length"];
	[headers setObject:@"example" forKey:@"Server"];
	return headers;
}

/* A URL the probe KNOWS is valid, returned NONNULL so it can be handed to a nonnull door: every URL
 * here is a literal, so a nil would be a broken probe and the check that reads it would fail anyway. */
static NSURL * _Nonnull fn_url(NSString *string)
{
	return (NSURL * _Nonnull)[NSURL URLWithString:string];
}

static NSURL * _Nonnull fn_file_url(NSString *path)
{
	return (NSURL * _Nonnull)[NSURL fileURLWithPath:path];
}

int main(void)
{
	NSURL *url = fn_url(@"https://example.com/a/b?x=1#frag");

	{
		/* The two convenience doors' DEFAULTS, pinned. */
		NSURLRequest *request = [NSURLRequest requestWithURL:url];

		check("request-defaults",
		      request != nil &&
		      [[request URL] isEqual:url] &&
		      [request cachePolicy] == NSURLRequestUseProtocolCachePolicy &&
		      [request timeoutInterval] == 60.0,
		      [NSString stringWithFormat:@"%@ policy=%d timeout=%g",
			[request URL], (int)[request cachePolicy], [request timeoutInterval]]);
	}

	{
		NSURLRequest *request = [NSURLRequest requestWithURL:url
						cachePolicy:NSURLRequestReturnCacheDataDontLoad
					    timeoutInterval:12.5];
		NSURLRequest *other = [[NSURLRequest alloc] initWithURL:url
							   cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
						       timeoutInterval:3.0];

		check("request-designated-initializer",
		      [request cachePolicy] == NSURLRequestReturnCacheDataDontLoad &&
		      [request timeoutInterval] == 12.5 &&
		      [other cachePolicy] == NSURLRequestReloadIgnoringLocalCacheData &&
		      [other timeoutInterval] == 3.0,
		      [NSString stringWithFormat:@"%d/%g %d/%g",
			(int)[request cachePolicy], [request timeoutInterval],
			(int)[other cachePolicy], [other timeoutInterval]]);
	}

	{
		/* The mutable initializer is the SAME designated initializer, so a mutable is reachable through
		 * it and is still a kind of NSURLRequest. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];

		check("request-mutable-initializer-is-mutable",
		      request != nil &&
		      [request isKindOfClass:[NSURLRequest class]] &&
		      [request isKindOfClass:[NSMutableURLRequest class]] &&
		      [request respondsToSelector:sel_registerName("setHTTPMethod:")],
		      [NSString stringWithFormat:@"class=%@", [request class]]);
	}

	{
		/* THE HTTP DEFAULTS: GET, cookies handled, no pipelining, cellular allowed, Default service
		 * type, Developer attribution, and no body or headers yet. */
		NSURLRequest *request = [NSURLRequest requestWithURL:url];

		check("request-http-defaults",
		      [[request HTTPMethod] isEqualToString:@"GET"] &&
		      [request allHTTPHeaderFields] == nil &&
		      [request HTTPBody] == nil &&
		      [request HTTPBodyStream] == nil &&
		      [request HTTPShouldHandleCookies] == YES &&
		      [request HTTPShouldUsePipelining] == NO &&
		      [request allowsCellularAccess] == YES &&
		      [request networkServiceType] == NSURLNetworkServiceTypeDefault &&
		      [request attribution] == NSURLRequestAttributionDeveloper,
		      [NSString stringWithFormat:@"method=%@ cookies=%d pipeline=%d cellular=%d type=%d attr=%d",
			[request HTTPMethod], (int)[request HTTPShouldHandleCookies],
			(int)[request HTTPShouldUsePipelining], (int)[request allowsCellularAccess],
			(int)[request networkServiceType], (int)[request attribution]]);
	}

	{
		/* EVERY SETTER, read back. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];
		NSURL *main = fn_url(@"https://example.com/a/b");
		NSData *body = [NSData dataWithBytes:"hello" length:5];

		[request setHTTPMethod:@"POST"];
		[request setCachePolicy:NSURLRequestReloadRevalidatingCacheData];
		[request setTimeoutInterval:9.0];
		[request setMainDocumentURL:main];
		[request setNetworkServiceType:NSURLNetworkServiceTypeBackground];
		[request setAttribution:NSURLRequestAttributionUser];
		[request setHTTPBody:body];
		[request setHTTPShouldHandleCookies:NO];
		[request setHTTPShouldUsePipelining:YES];
		[request setAllowsCellularAccess:NO];

		check("mutable-setters-round-trip",
		      [[request HTTPMethod] isEqualToString:@"POST"] &&
		      [request cachePolicy] == NSURLRequestReloadRevalidatingCacheData &&
		      [request timeoutInterval] == 9.0 &&
		      [[request mainDocumentURL] isEqual:main] &&
		      [request networkServiceType] == NSURLNetworkServiceTypeBackground &&
		      [request attribution] == NSURLRequestAttributionUser &&
		      [request HTTPBody] == body &&
		      [request HTTPShouldHandleCookies] == NO &&
		      [request HTTPShouldUsePipelining] == YES &&
		      [request allowsCellularAccess] == NO,
		      [NSString stringWithFormat:@"method=%@ policy=%d body=%p",
			[request HTTPMethod], (int)[request cachePolicy], (void *)[request HTTPBody]]);
	}

	{
		/* THE FIELD NAME IS CASE-INSENSITIVE (RFC 9110 §5.1), and the STORED spelling is the caller's. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];

		[request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
		check("headers-are-case-insensitive",
		      [[request valueForHTTPHeaderField:@"content-type"]
			isEqualToString:@"application/json"] &&
		      [[request valueForHTTPHeaderField:@"CONTENT-TYPE"]
			isEqualToString:@"application/json"] &&
		      [[request allHTTPHeaderFields] count] == 1 &&
		      [[request allHTTPHeaderFields] objectForKey:@"Content-Type"] != nil,
		      [NSString stringWithFormat:@"lookup=%@ raw=%@",
			[request valueForHTTPHeaderField:@"content-type"],
			[request allHTTPHeaderFields]]);
	}

	{
		/* ADD APPENDS, with ", " (RFC 9110 §5.2's list rule). */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];

		[request addValue:@"a" forHTTPHeaderField:@"X-Thing"];
		[request addValue:@"b" forHTTPHeaderField:@"x-thing"];
		check("add-value-appends",
		      [[request valueForHTTPHeaderField:@"X-Thing"] isEqualToString:@"a, b"] &&
		      [[request allHTTPHeaderFields] count] == 1,
		      [NSString stringWithFormat:@"value=%@ count=%d",
			[request valueForHTTPHeaderField:@"X-Thing"],
			(int)[[request allHTTPHeaderFields] count]]);
	}

	{
		/* SET REPLACES — the other door's rule, and the one they differ in. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];

		[request setValue:@"a" forHTTPHeaderField:@"X-Thing"];
		[request setValue:@"b" forHTTPHeaderField:@"X-Thing"];
		[request setValue:@"c" forHTTPHeaderField:@"Another"];
		check("set-value-replaces",
		      [[request valueForHTTPHeaderField:@"X-Thing"] isEqualToString:@"b"] &&
		      [[request allHTTPHeaderFields] count] == 2,
		      [NSString stringWithFormat:@"value=%@ count=%d",
			[request valueForHTTPHeaderField:@"X-Thing"],
			(int)[[request allHTTPHeaderFields] count]]);
	}

	{
		/* A SNAPSHOT HANDED OUT EARLIER DOES NOT CHANGE when the request is mutated — the reason the
		 * storage is an immutable copy rather than a live mutable dictionary. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];
		NSDictionary *before;

		[request setValue:@"one" forHTTPHeaderField:@"X-Thing"];
		before = [request allHTTPHeaderFields];
		[request setValue:@"two" forHTTPHeaderField:@"X-Thing"];
		[request setValue:@"new" forHTTPHeaderField:@"X-Other"];

		check("header-snapshot-is-stable",
		      [before count] == 1 &&
		      [[before objectForKey:@"X-Thing"] isEqualToString:@"one"],
		      [NSString stringWithFormat:@"before=%@ after=%@", before,
			[request allHTTPHeaderFields]]);
	}

	{
		/* EQUALITY AND THE HASH MOVE TOGETHER: equal requests are equal and hash equally; one changed
		 * field makes them unequal. */
		NSURLRequest *a = [NSURLRequest requestWithURL:url];
		NSURLRequest *b = [NSURLRequest requestWithURL:url];
		NSMutableURLRequest *c = [NSMutableURLRequest requestWithURL:url];

		[c setHTTPMethod:@"POST"];
		check("request-equality-and-hash",
		      [a isEqual:b] && [a hash] == [b hash] &&
		      ![a isEqual:c] &&
		      ![a isEqual:@"not a request"] &&
		      [a isEqual:a],
		      [NSString stringWithFormat:@"a=b:%d a=c:%d hash=%d/%d",
			(int)[a isEqual:b], (int)[a isEqual:c],
			(int)[a hash], (int)[b hash]]);
	}

	{
		/* THE MUTABILITY BOUNDARY: a mutable's -copy is an IMMUTABLE snapshot, and later mutation of the
		 * original cannot reach it. */
		NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url];
		NSURLRequest *snapshot;

		[request setHTTPMethod:@"PATCH"];
		snapshot = [request copy];
		[request setHTTPMethod:@"DELETE"];
		[request setValue:@"x" forHTTPHeaderField:@"X-Late"];

		check("copy-of-a-mutable-is-an-immutable-snapshot",
		      [snapshot isKindOfClass:[NSMutableURLRequest class]] == NO &&
		      [snapshot isKindOfClass:[NSURLRequest class]] &&
		      [[snapshot HTTPMethod] isEqualToString:@"PATCH"] &&
		      [snapshot valueForHTTPHeaderField:@"X-Late"] == nil &&
		      [[request HTTPMethod] isEqualToString:@"DELETE"],
		      [NSString stringWithFormat:@"snapshot=%@ method=%@", [snapshot HTTPMethod],
			[request HTTPMethod]]);
	}

	{
		/* -mutableCopy IS INDEPENDENT: equal at the moment of copying, and separate afterwards. */
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
		NSMutableURLRequest *copy = [request mutableCopy];

		[copy setHTTPMethod:@"PUT"];
		check("mutable-copy-is-independent",
		      copy != request &&
		      [copy isKindOfClass:[NSMutableURLRequest class]] &&
		      [[copy HTTPMethod] isEqualToString:@"PUT"] &&
		      [[request HTTPMethod] isEqualToString:@"GET"] &&
		      ![request isEqual:copy],
		      [NSString stringWithFormat:@"copy=%@ original=%@", [copy HTTPMethod],
			[request HTTPMethod]]);
	}

	{
		/* THE RESPONSE'S FOUR FACTS, and the UNKNOWN length when none is given. */
		NSURL *file = fn_file_url(@"/System/Shared/tests/foundation_urlrequest");
		NSURLResponse *response = [[NSURLResponse alloc] initWithURL:file
								   MIMEType:@"text/plain"
							expectedContentLength:(NSInteger)NSURLResponseUnknownLength
							   textEncodingName:@"utf-8"];

		check("response-properties",
		      response != nil &&
		      [[response URL] isEqual:file] &&
		      [[response MIMEType] isEqualToString:@"text/plain"] &&
		      [response expectedContentLength] == NSURLResponseUnknownLength &&
		      [[response textEncodingName] isEqualToString:@"utf-8"],
		      [NSString stringWithFormat:@"%@ mime=%@ length=%d enc=%@",
			[response URL], [response MIMEType],
			(int)[response expectedContentLength], [response textEncodingName]]);
	}

	{
		/* THE SUGGESTED FILENAME IS A RULE: the last path component, or "Unknown" when there is none. */
		NSURLResponse *named = [[NSURLResponse alloc]
					initWithURL:fn_file_url(@"/System/Temporary Files/thing.txt")
					   MIMEType:nil expectedContentLength:0 textEncodingName:nil];
		NSURLResponse *root = [[NSURLResponse alloc]
					initWithURL:fn_url(@"http://example.com/")
					   MIMEType:nil expectedContentLength:0 textEncodingName:nil];
		NSURLResponse *deep = [[NSURLResponse alloc]
					initWithURL:fn_url(@"http://example.com/a/b.txt")
					   MIMEType:nil expectedContentLength:0 textEncodingName:nil];

		check("response-suggested-filename",
		      [[named suggestedFilename] isEqualToString:@"thing.txt"] &&
		      [[root suggestedFilename] isEqualToString:@"Unknown"] &&
		      [[deep suggestedFilename] isEqualToString:@"b.txt"],
		      [NSString stringWithFormat:@"%@ / %@ / %@", [named suggestedFilename],
			[root suggestedFilename], [deep suggestedFilename]]);
	}

	{
		/* THE DERIVED HALF: MIME type, charset and length come from the HEADERS the caller passed. */
		NSURL *target = fn_url(@"http://example.com/page");
		NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:target
									 statusCode:404
									HTTPVersion:@"HTTP/1.1"
								       headerFields:fn_headers()];

		check("http-response-derives-from-headers",
		      response != nil &&
		      [response statusCode] == 404 &&
		      [[response MIMEType] isEqualToString:@"text/html"] &&
		      [[response textEncodingName] isEqualToString:@"utf-8"] &&
		      [response expectedContentLength] == 42 &&
		      [[response valueForHTTPHeaderField:@"content-length"] isEqualToString:@"42"] &&
		      [[response allHeaderFields] count] == 3 &&
		      [response isKindOfClass:[NSURLResponse class]],
		      [NSString stringWithFormat:@"status=%d mime=%@ enc=%@ length=%d",
			(int)[response statusCode], [response MIMEType], [response textEncodingName],
			(int)[response expectedContentLength]]);
	}

	{
		/* A RESPONSE WITH NO Content-Length ANSWERS THE UNKNOWN SENTINEL, and the two unknown doors agree. */
		NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc]
					initWithURL:fn_url(@"http://example.com/x")
					 statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:nil];

		check("response-unknown-length",
		      (long long)NSURLResponseUnknownLength == -1LL &&
		      [response expectedContentLength] == -1 &&
		      [[response allHeaderFields] count] == 0 &&
		      [response MIMEType] == nil &&
		      [response textEncodingName] == nil,
		      [NSString stringWithFormat:@"length=%d headers=%@",
			(int)[response expectedContentLength], [response allHeaderFields]]);
	}

	{
		/* RFC 9110 §15's phrase registry, the document's own rows — and NIL for a code it does not define. */
		NSString *ok = [NSHTTPURLResponse localizedStringForStatusCode:200];
		NSString *missing = [NSHTTPURLResponse localizedStringForStatusCode:404];
		NSString *server = [NSHTTPURLResponse localizedStringForStatusCode:500];
		NSString *teapot = [NSHTTPURLResponse localizedStringForStatusCode:418];

		check("http-response-status-phrase",
		      ok != nil && [ok isEqualToString:@"OK"] &&
		      missing != nil && [missing isEqualToString:@"Not Found"] &&
		      server != nil && [server isEqualToString:@"Internal Server Error"] &&
		      teapot != nil && [teapot isEqualToString:@"I'm a teapot"] &&
		      [NSHTTPURLResponse localizedStringForStatusCode:599] == nil,
		      [NSString stringWithFormat:@"200=%@ 404=%@ 500=%@ 418=%@ 599=%@",
			ok, missing, server, teapot,
			[NSHTTPURLResponse localizedStringForStatusCode:599]]);
	}

	{
		/* THE ENUM VALUES ARE OURS (D2), so they are PINNED here rather than assumed — including the
		 * documented alias and the HOLE the …VoIP case left at 1. THAT HOLE IS AN OWED ROW, NOT A
		 * STRIKE: it used to be struck as deprecated, and §62.24 retired that ground (2026-09-26), so
		 * the value stays pinned and the name is a work item. */
		check("url-request-enum-values",
		      NSURLRequestUseProtocolCachePolicy == 0 &&
		      NSURLRequestReloadIgnoringLocalCacheData == 1 &&
		      NSURLRequestReturnCacheDataElseLoad == 2 &&
		      NSURLRequestReturnCacheDataDontLoad == 3 &&
		      NSURLRequestReloadIgnoringLocalAndRemoteCacheData == 4 &&
		      NSURLRequestReloadRevalidatingCacheData == 5 &&
		      NSURLRequestReloadIgnoringCacheData == NSURLRequestReloadIgnoringLocalCacheData &&
		      NSURLNetworkServiceTypeDefault == 0 &&
		      NSURLNetworkServiceTypeVideo == 2 &&
		      NSURLNetworkServiceTypeBackground == 3 &&
		      NSURLNetworkServiceTypeVoice == 4 &&
		      NSURLNetworkServiceTypeAVStreaming == 5 &&
		      NSURLNetworkServiceTypeResponsiveAV == 6 &&
		      NSURLNetworkServiceTypeResponsiveData == 7 &&
		      NSURLNetworkServiceTypeCallSignaling == 8 &&
		      NSURLRequestAttributionDeveloper == 0 &&
		      NSURLRequestAttributionUser == 1,
		      [NSString stringWithFormat:@"cache=%d..%d type=%d attr=%d",
			(int)NSURLRequestUseProtocolCachePolicy,
			(int)NSURLRequestReloadRevalidatingCacheData,
			(int)NSURLNetworkServiceTypeCallSignaling,
			(int)NSURLRequestAttributionUser]);
	}

	{
		/*
		 * THE AUDITED COCOA INVENTORY. Every selector this unit OWES must exist, and every one it
		 * REFUSES must be ABSENT — so adding one to the library without moving it here fails the check,
		 * and implementing a refused door silently fails it too.
		 */
		static const char *requestClassSelectors[] = {
			"requestWithURL:", "requestWithURL:cachePolicy:timeoutInterval:", NULL
		};
		static const char *requestSelectors[] = {
			"initWithURL:", "initWithURL:cachePolicy:timeoutInterval:",
			"URL", "cachePolicy", "timeoutInterval", "mainDocumentURL",
			"networkServiceType", "attribution", "HTTPMethod", "allHTTPHeaderFields",
			"HTTPBody", "HTTPBodyStream", "HTTPShouldHandleCookies",
			"HTTPShouldUsePipelining", "allowsCellularAccess", "valueForHTTPHeaderField:",
			"copy", "mutableCopy", NULL
		};
		static const char *mutableSelectors[] = {
			"setURL:", "setCachePolicy:", "setTimeoutInterval:", "setMainDocumentURL:",
			"setNetworkServiceType:", "setAttribution:", "setHTTPMethod:",
			"setAllHTTPHeaderFields:", "setHTTPBody:", "setHTTPBodyStream:",
			"setHTTPShouldHandleCookies:", "setHTTPShouldUsePipelining:",
			"setAllowsCellularAccess:", "setValue:forHTTPHeaderField:",
			"addValue:forHTTPHeaderField:", "copy", "mutableCopy", NULL
		};
		static const char *responseSelectors[] = {
			"initWithURL:MIMEType:expectedContentLength:textEncodingName:",
			"URL", "MIMEType", "expectedContentLength", "textEncodingName",
			"suggestedFilename", "copy", NULL
		};
		static const char *httpResponseClassSelectors[] = {
			"localizedStringForStatusCode:", NULL
		};
		static const char *httpResponseSelectors[] = {
			"initWithURL:statusCode:HTTPVersion:headerFields:",
			"statusCode", "allHeaderFields", "valueForHTTPHeaderField:", NULL
		};
		/*
		 * REFUSED, AND ASSERTED ABSENT: the transport doors (a request here is a DESCRIPTION), the two
		 * coder doors (an archive form Apple does not publish), and the certificate doors Apple
		 * DEPRECATED - which §11.5 struck until 2026-09-26, and which §62.24 makes OWED: they are absent
		 * because they are unimplemented, and this array is the distance to zero for them too.
		 */
		static const char *excluded[] = {
			"start", "resume", "loadRequest:",
			"initWithCoder:", "encodeWithCoder:",
			"allowsAnyHTTPSCertificateForHost:", "setAllowsAnyHTTPSCertificate:forHost:",
			NULL
		};
		NSURLRequest *request = [NSURLRequest requestWithURL:url];
		NSMutableURLRequest *mutable = [[NSMutableURLRequest alloc] initWithURL:url];
		NSURLResponse *response = [[NSURLResponse alloc] initWithURL:url MIMEType:nil
							     expectedContentLength:0 textEncodingName:nil];
		NSHTTPURLResponse *http = [[NSHTTPURLResponse alloc] initWithURL:url statusCode:200
									 HTTPVersion:nil headerFields:nil];
		int complete = 1;
		int i;

		for (i = 0; requestClassSelectors[i] != NULL; i++) {
			if (![NSURLRequest respondsToSelector:sel_registerName(requestClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing +%s\n", requestClassSelectors[i]);
			}
		}
		for (i = 0; requestSelectors[i] != NULL; i++) {
			if (![request respondsToSelector:sel_registerName(requestSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing -%s\n", requestSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; responseSelectors[i] != NULL; i++) {
			if (![response respondsToSelector:sel_registerName(responseSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing -%s (response)\n", responseSelectors[i]);
			}
		}
		for (i = 0; httpResponseClassSelectors[i] != NULL; i++) {
			if (![NSHTTPURLResponse respondsToSelector:sel_registerName(httpResponseClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing +%s (http response)\n",
				       httpResponseClassSelectors[i]);
			}
		}
		for (i = 0; httpResponseSelectors[i] != NULL; i++) {
			if (![http respondsToSelector:sel_registerName(httpResponseSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST missing -%s (http response)\n",
				       httpResponseSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([request respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-URLREQUEST present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("url-request-api-inventory", complete,
		      @"the audited Cocoa inventory: every owed selector exists, and nothing listed as excluded does");
	}


	{
		/* §63.192: THE DEFAULTS ARE THE CONTRACT. (The URL goes into a LOCAL first: `+URLWithString:` is
		 * `_Nullable` and the request doors are not, and this probe compiles under
		 * -Werror,-nullable-to-nonnull-conversion.) */
		NSURL *url = [NSURL URLWithString:@"https://example.com/"];
		NSURLRequest *fresh = [NSURLRequest requestWithURL:url];

		check("urlrequest-network-policy-defaults",
		      url != nil && [fresh allowsConstrainedNetworkAccess] && [fresh allowsExpensiveNetworkAccess] &&
		      [fresh allowsUltraConstrainedNetworkAccess] && ![fresh requiresDNSSECValidation] &&
		      ![fresh assumesHTTP3Capable] && ![fresh allowsPersistentDNS] &&
		      [fresh cookiePartitionIdentifier] == nil,
		      [NSString stringWithFormat:@"allows=%d%d%d policies=%d%d%d cookie=%@",
			[fresh allowsConstrainedNetworkAccess], [fresh allowsExpensiveNetworkAccess],
			[fresh allowsUltraConstrainedNetworkAccess], [fresh requiresDNSSECValidation],
			[fresh assumesHTTP3Capable], [fresh allowsPersistentDNS], [fresh cookiePartitionIdentifier]]);
	}
	{
		/* THE ROUND TRIP THROUGH THE MUTABLE CLASS, INCLUDING THE `copy` SEMANTICS of the identifier: a
		 * MUTABLE string handed in must not change under the request afterwards. */
		NSURL *url = [NSURL URLWithString:@"https://example.com/"];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
		NSMutableString *identifier = [NSMutableString stringWithString:@"before"];

		[request setAllowsConstrainedNetworkAccess:NO];
		[request setAllowsExpensiveNetworkAccess:NO];
		[request setAllowsUltraConstrainedNetworkAccess:NO];
		[request setRequiresDNSSECValidation:YES];
		[request setAssumesHTTP3Capable:YES];
		[request setAllowsPersistentDNS:YES];
		[request setCookiePartitionIdentifier:identifier];
		[identifier appendString:@"-changed"];
		check("urlrequest-network-policy-round-trip",
		      ![request allowsConstrainedNetworkAccess] && ![request allowsExpensiveNetworkAccess] &&
		      ![request allowsUltraConstrainedNetworkAccess] && [request requiresDNSSECValidation] &&
		      [request assumesHTTP3Capable] && [request allowsPersistentDNS] &&
		      [[request cookiePartitionIdentifier] isEqualToString:@"before"],
		      [NSString stringWithFormat:@"allows=%d%d%d policies=%d%d%d cookie=%@ source=%@",
			[request allowsConstrainedNetworkAccess], [request allowsExpensiveNetworkAccess],
			[request allowsUltraConstrainedNetworkAccess], [request requiresDNSSECValidation],
			[request assumesHTTP3Capable], [request allowsPersistentDNS],
			[request cookiePartitionIdentifier], identifier]);
	}
	{
		/* AND A COPY CARRIES THEM, which is what catches a property added to the accessors but not to the
		 * copy-from-another path. */
		NSURL *url = [NSURL URLWithString:@"https://example.com/"];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
		NSURLRequest *copied;

		[request setAllowsConstrainedNetworkAccess:NO];
		[request setAllowsUltraConstrainedNetworkAccess:NO];
		[request setRequiresDNSSECValidation:YES];
		[request setAssumesHTTP3Capable:YES];
		[request setAllowsPersistentDNS:YES];
		[request setCookiePartitionIdentifier:@"kept"];
		copied = [request copy];
		check("urlrequest-network-policy-survives-a-copy",
		      ![copied allowsConstrainedNetworkAccess] && [copied allowsExpensiveNetworkAccess] &&
		      ![copied allowsUltraConstrainedNetworkAccess] && [copied requiresDNSSECValidation] &&
		      [copied assumesHTTP3Capable] && [copied allowsPersistentDNS] &&
		      [[copied cookiePartitionIdentifier] isEqualToString:@"kept"],
		      [NSString stringWithFormat:@"copied=%@ allows=%d policies=%d%d%d cookie=%@",
			[copied class], [copied allowsConstrainedNetworkAccess], [copied requiresDNSSECValidation],
			[copied assumesHTTP3Capable], [copied allowsPersistentDNS],
			[copied cookiePartitionIdentifier]]);
	}

	printf("FOUNDATION-URLREQUEST RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving INPUT for a
	 * while, so an `echo $?` the harness types may never run. */
	printf("FOUNDATION-URLREQUEST-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLREQUEST DONE\n");
	return failc ? 1 : 0;
}
