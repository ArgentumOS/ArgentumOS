/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlprotocol, unit of 1 — W7 slice 2a's acceptance for NSURLProtocol, NSURLProtocolClient,
 * NSCachedURLResponse and NSURLCacheStoragePolicy. docs/design/foundation-plan.md §46 and
 * docs/design/foundation-transport-plan.md §4.
 *
 * ONE unit, importing <Foundation/Foundation.h> and <objc/runtime.h>, and NO TRANSPORT ANYWHERE. What this
 * slice ships is the SEAM (the point a transport attaches) and the cached answer's value contract, so the
 * probe asserts a plug-in point and a value rather than a conversation: no socket, no session, no loader.
 * THE RUNTIME HEADER IS HERE FOR ONE REASON (§50.3): a DECLARATION is asserted where declarations live, and
 * a protocol's own method list is only readable from the runtime.
 *
 * IT DEFINES ITS OWN SUBCLASSES, WHICH IS NOT A CONVENIENCE BUT THE ONLY WAY TO TEST THE THING: an
 * override point is invisible until something overrides it, so `FnClaimA` and `FnClaimB` both claim one
 * scheme — which is also what makes the REGISTRATION ORDER observable — and `FnClient` implements the seven
 * client callbacks so that "the protocol conforms" is a fact about a class rather than about a header.
 *
 * THREE CHECKS EARN THEIR PLACE THE WAY SLICE 1'S DID:
 *   request-properties-are-per-instance  the property table is keyed by IDENTITY and RETAINS its key, so
 *                                        this pins the identity rule, the per-instance rule, the "a copy
 *                                        starts empty" rule, and that a nil value removes;
 *   urlprotocol-api-inventory            the audited inventory: every selector this unit owes EXISTS on
 *                                        the class it belongs to, and every refused one is ABSENT — the
 *                                        cancel notification and the coder doors, each with its ground
 *                                        stated rather than assumed;
 *   urlprotocol-client-declares-the-authentication-door  the check §50.3 was FOR: the bridge messaged a
 *                                        selector no header declared, which compiles on an id-typed
 *                                        receiver, so the behaviour passed while the CONTRACT was absent.
 *                                        This one reads the protocol's own method list, and the sibling
 *                                        check pins the one member that is still deliberately absent.
 */

#import <Foundation/Foundation.h>

/* SO A DECLARATION CAN BE ASSERTED WHERE DECLARATIONS LIVE (§50.3): a protocol's own method list is only
 * readable from the runtime, and the door that was missing was a DECLARATION, not a behaviour. */
#import <objc/runtime.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLPROTOCOL %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLPROTOCOL %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSURL *fn_url(NSString *string)
{
	return [NSURL URLWithString:string];
}

/* --- TWO PROTOCOLS THAT CLAIM THE SAME SCHEME, AND ONE CLIENT ------------------------------------ */

@interface FnClaimA : NSURLProtocol
@end
@implementation FnClaimA
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	return [[[request URL] scheme] isEqualToString:@"fn-a"];
}
@end

@interface FnClaimB : NSURLProtocol
@end
@implementation FnClaimB
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	return [[[request URL] scheme] isEqualToString:@"fn-a"];
}
@end

@interface FnClient : NSObject <NSURLProtocolClient>
{
	int _calls;
}
- (int)calls;
@end
@implementation FnClient
- (int)calls { return _calls; }
- (void)URLProtocol:(NSURLProtocol *)protocol
	wasRedirectedToRequest:(NSURLRequest *)request
	     redirectResponse:(NSURLResponse *)redirectResponse { _calls++; }
- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse { _calls++; }
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy { _calls++; }
- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data { _calls++; }
/* THE AUTHENTICATION DOOR IS ANSWERED IN THE PROBE, AND ANSWERING IT IS THE POINT (§50.3): this class is
 * the client half of the seam, and until the door was DECLARED the bridge's call to it compiled only
 * because `_client` is id-typed. An answer here is a legal answer, never a state the probe depends on —
 * nothing in this unit raises a challenge — and it is counted like the other callbacks. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
		  completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
					      NSURLCredential *credential))completionHandler
{
	NSURLCredential *credential =
		[[NSURLCredential alloc] initWithUser:@"probe"
					     password:@"probe"
					  persistence:NSURLCredentialPersistenceNone];

	_calls++;
	completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
}
- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol { _calls++; }
- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error { _calls++; }
@end

static NSURLResponse *fn_response(void)
{
	return [[NSURLResponse alloc] initWithURL:fn_url(@"https://example.com/a")
					 MIMEType:@"text/html"
			    expectedContentLength:12
				 textEncodingName:@"utf-8"];
}

int main(void)
{
	/* --- THE ENUM'S VALUES ARE OURS (D2), AND THEY ARE PINNED ---------------------------------- */
	{
		check("cache-storage-policy-values",
		      NSURLCacheStorageAllowed == 0 &&
		      NSURLCacheStorageAllowedInMemoryOnly == 1 &&
		      NSURLCacheStorageNotAllowed == 2,
		      @"Apple publishes the case names and no numbers; these three are this tree's (D2)");
	}

	/* --- THE CACHED ANSWER IS A VALUE --------------------------------------------------------- */
	{
		NSData *payload = [NSData dataWithBytes:"hello world!" length:12];
		NSCachedURLResponse *convenience =
			[[NSCachedURLResponse alloc] initWithResponse:fn_response() data:payload];

		/* THE CONVENIENCE DOOR ANSWERS THE DOCUMENTED DEFAULTS rather than whatever the assignments
		 * happen to produce: storage allowed, and no user info at all. */
		check("cached-response-convenience-door-answers-the-defaults",
		      [convenience response] != nil &&
		      [[convenience data] isEqualToData:payload] &&
		      [convenience userInfo] == nil &&
		      [convenience storagePolicy] == NSURLCacheStorageAllowed,
		      @"-initWithResponse:data: must answer NSURLCacheStorageAllowed and a nil userInfo");

		/* AND THE FULL DOOR STORES WHAT IT IS HANDED. */
		NSMutableDictionary *info = [NSMutableDictionary dictionary];
		[info setObject:@"a note" forKey:@"note"];
		NSCachedURLResponse *full =
			[[NSCachedURLResponse alloc] initWithResponse:fn_response()
							 data:payload
						     userInfo:info
						storagePolicy:NSURLCacheStorageNotAllowed];

		check("cached-response-full-init-stores-what-it-is-handed",
		      [[full userInfo] objectForKey:@"note"] != nil &&
		      [[[full userInfo] objectForKey:@"note"] isEqual:@"a note"] &&
		      [full storagePolicy] == NSURLCacheStorageNotAllowed,
		      @"the four-argument door stores the user info and the policy it was given");

		/* A SNAPSHOT, NOT A WINDOW: the dictionary the caller passed is MUTATED afterwards and the
		 * stored copy must not move with it. */
		[info setObject:@"changed" forKey:@"note"];
		check("cached-response-snapshots-its-inputs",
		      [[[full userInfo] objectForKey:@"note"] isEqual:@"a note"],
		      @"the initializer copies: editing the caller's dictionary cannot reach the stored one");
	}

	/* --- THE ABSTRACT BASE'S DEFAULTS ----------------------------------------------------------- */
	{
		NSURLRequest *one = [NSURLRequest requestWithURL:fn_url(@"https://example.com/same")];
		NSURLRequest *same = [NSURLRequest requestWithURL:fn_url(@"https://example.com/same")];
		NSURLRequest *other = [NSURLRequest requestWithURL:fn_url(@"https://example.com/different")];

		check("urlprotocol-base-claims-nothing",
		      [NSURLProtocol canInitWithRequest:one] == NO &&
		      [NSURLProtocol canonicalRequestForRequest:one] == one &&
		      [NSURLProtocol requestIsCacheEquivalent:one toRequest:same] == YES &&
		      [NSURLProtocol requestIsCacheEquivalent:one toRequest:other] == NO,
		      @"the base claims no request, canonicalises nothing, and defines equivalence as equality");
	}

	/* --- THE REQUEST-PROPERTY TABLE, AND ITS IDENTITY RULE -------------------------------------- */
	{
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:fn_url(@"https://example.com/p")];
		NSMutableURLRequest *another = [NSMutableURLRequest requestWithURL:fn_url(@"https://example.com/p")];
		NSMutableURLRequest *copy;

		[NSURLProtocol setProperty:@"a value" forKey:@"key" inRequest:request];
		[NSURLProtocol setProperty:@"second" forKey:@"key2" inRequest:request];
		copy = [request mutableCopy];

		check("request-properties-are-per-instance",
		      [[NSURLProtocol propertyForKey:@"key" inRequest:request] isEqual:@"a value"] &&
		      [[NSURLProtocol propertyForKey:@"key2" inRequest:request] isEqual:@"second"] &&
		      /* AN EQUAL BUT DIFFERENT REQUEST IS A DIFFERENT REQUEST. */
		      [NSURLProtocol propertyForKey:@"key" inRequest:another] == nil &&
		      /* AND A COPY STARTS EMPTY: the table is keyed by the instance, not by the value. */
		      [NSURLProtocol propertyForKey:@"key" inRequest:copy] == nil,
		      @"properties belong to the instance they were set on, and a copy carries none");

		/* A NIL VALUE REMOVES, which is the documented reading of the setter — so the removal rule is
		 * asserted through -setProperty: rather than only through -removePropertyForKey:. */
		[NSURLProtocol setProperty:nil forKey:@"key" inRequest:request];
		check("request-property-nil-value-removes",
		      [NSURLProtocol propertyForKey:@"key" inRequest:request] == nil &&
		      [[NSURLProtocol propertyForKey:@"key2" inRequest:request] isEqual:@"second"],
		      @"setting a property to nil removes that key and leaves its neighbours alone");

		[NSURLProtocol removePropertyForKey:@"key2" inRequest:request];
		check("request-property-remove",
		      [NSURLProtocol propertyForKey:@"key2" inRequest:request] == nil,
		      @"-removePropertyForKey: takes the key out");
	}

	/* --- THE REGISTRY, AND THE ORDER IT CONSULTS IN --------------------------------------------- */
	{
		NSURLRequest *claimed = [NSURLRequest requestWithURL:fn_url(@"fn-a://host/path")];
		/* THE UNCLAIMED REQUEST NEEDS A SCHEME NOTHING CLAIMS, AND `https` IS NOT ONE ANY MORE (§62.83): the
		 * library registers FNCURLURLProtocol at load and it claims file/http/https, so an https request reaches
		 * a class. This probe asserts what happens when NOTHING claims a request, which needs a scheme outside
		 * every registered protocol. */
		NSURLRequest *unclaimed = [NSURLRequest requestWithURL:fn_url(@"fn-nothing://host/path")];

		/* THE BASE REFUSES ITSELF BY NAME: if it could be registered, a class whose every override
		 * point is the default would shadow every real protocol behind it. And a non-subclass is not a
		 * protocol at all. */
		check("registration-refuses-the-base-and-non-subclasses",
		      [NSURLProtocol registerClass:[NSURLProtocol class]] == NO &&
		      [NSURLProtocol registerClass:[NSObject class]] == NO,
		      @"only a subclass of NSURLProtocol registers");

		check("registration-accepts-a-subclass",
		      [NSURLProtocol registerClass:[FnClaimA class]] == YES &&
		      [NSURLProtocol fnProtocolClassForRequest:claimed] == [FnClaimA class] &&
		      [NSURLProtocol fnProtocolClassForRequest:unclaimed] == Nil,
		      @"a claiming subclass is registered, and an unclaimed request reaches no class");

		/* MOST-RECENTLY-REGISTERED FIRST: B claims the same scheme as A and is registered later, so B
		 * answers — and unregistering B gives the request back to A. */
		[NSURLProtocol registerClass:[FnClaimB class]];
		check("registration-order-decides-precedence",
		      [NSURLProtocol fnProtocolClassForRequest:claimed] == [FnClaimB class],
		      @"the most recently registered class that claims the request is consulted first");

		[NSURLProtocol unregisterClass:[FnClaimB class]];
		check("unregister-restores-the-previous-class",
		      [NSURLProtocol fnProtocolClassForRequest:claimed] == [FnClaimA class],
		      @"unregistering compacts the registry, so the class behind it answers again");

		[NSURLProtocol unregisterClass:[FnClaimA class]];
		check("unregister-removes-the-class",
		      [NSURLProtocol fnProtocolClassForRequest:claimed] == Nil,
		      @"an unregistered class is no longer consulted");
	}

	/* --- THE INSTANCE: WHAT IT CARRIES, AND THAT THE BASE DOES NOTHING --------------------------- */
	{
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:fn_url(@"https://example.com/i")];
		NSCachedURLResponse *cached =
			[[NSCachedURLResponse alloc] initWithResponse:fn_response()
							 data:[NSData dataWithBytes:"x" length:1]];
		FnClient *client = [[FnClient alloc] init];
		NSURLProtocol *protocol =
			[[NSURLProtocol alloc] initWithRequest:request cachedResponse:cached client:client];
		NSURLProtocol *bare =
			[[NSURLProtocol alloc] initWithRequest:request cachedResponse:nil client:nil];

		check("urlprotocol-instance-carries-its-request",
		      [[protocol request] isEqual:request] && [[protocol request] isKindOfClass:[NSURLRequest class]],
		      @"the request is kept as a value (a mutable one is copied in)");

		/* `cachedResponse` AND `client` ARE NULLABLE AS APPLE DECLARES THEM, so a protocol with neither
		 * must be constructible — a loader starts one with no cache hit all the time. */
		check("urlprotocol-instance-carries-its-client",
		      [protocol cachedResponse] == cached && [protocol client] == client &&
		      [bare cachedResponse] == nil && [bare client] == nil,
		      @"the cached response and the client are kept, and both may be nil");

		/* THE BASE'S -startLoading/-stopLoading DO NOTHING — and "nothing" is asserted as "the client was
		 * told nothing", which is stronger than "it did not crash". */
		[protocol startLoading];
		[protocol stopLoading];
		check("urlprotocol-base-loading-is-a-no-op",
		      [client calls] == 0,
		      @"the abstract base's -startLoading/-stopLoading reach the client with no callback");
	}

	/* --- THE CLIENT PROTOCOL'S SHAPE ------------------------------------------------------------ */
	{
		FnClient *client = [[FnClient alloc] init];
		int shape = [client respondsToSelector:@selector(URLProtocol:wasRedirectedToRequest:redirectResponse:)] &&
			    [client respondsToSelector:@selector(URLProtocol:cachedResponseIsValid:)] &&
			    [client respondsToSelector:@selector(URLProtocol:didReceiveResponse:cacheStoragePolicy:)] &&
			    [client respondsToSelector:@selector(URLProtocol:didLoadData:)] &&
			    [client respondsToSelector:@selector(URLProtocol:didReceiveAuthenticationChallenge:completionHandler:)] &&
			    [client respondsToSelector:@selector(URLProtocolDidFinishLoading:)] &&
			    [client respondsToSelector:@selector(URLProtocol:didFailWithError:)];

		check("urlprotocol-client-protocol-shape",
		      shape && [client conformsToProtocol:@protocol(NSURLProtocolClient)],
		      @"all seven callbacks, and the class conforms to the protocol");

		/* THE DECLARATION IS ASSERTED WHERE DECLARATIONS LIVE, which is the whole of §50.3: the bridge
		 * messaged -URLProtocol:didReceiveAuthenticationChallenge:completionHandler: through an id-typed
		 * receiver, and THAT COMPILES WHETHER OR NOT ANY HEADER DECLARES IT — so the authentication loop
		 * passed ten checks while a caller could not read the contract and no compiler could check a call
		 * to it. A `-respondsToSelector:` on the client cannot see the difference, because the class
		 * implements the method either way. THE PROTOCOL'S OWN METHOD LIST CAN, so that is what is read
		 * here; the same call pins the one member that is still deliberately absent. */
		struct objc_method_description declared =
			protocol_getMethodDescription(@protocol(NSURLProtocolClient),
				@selector(URLProtocol:didReceiveAuthenticationChallenge:completionHandler:),
				YES /* required */, YES /* instance */);
		struct objc_method_description absent =
			protocol_getMethodDescription(@protocol(NSURLProtocolClient),
				@selector(URLProtocol:didCancelAuthenticationChallenge:),
				YES, YES);

		check("urlprotocol-client-declares-the-authentication-door",
		      declared.name != NULL &&
		      [client respondsToSelector:@selector(URLProtocol:didReceiveAuthenticationChallenge:completionHandler:)],
		      @"the protocol declares the authentication door the bridge messages, and a client answers it");

		/* AND THE ABSENCE IS PINNED, so a deliberate refusal cannot quietly become either a declaration
		 * nobody noticed or a gap nobody recorded. Its ground is in the header: it completes the
		 * NSURLAuthenticationChallengeSender round trip §48.6 refuses, and a client that cancels answers
		 * through the authentication door's CancelAuthenticationChallenge disposition instead. */
		check("the-cancel-notification-is-absent-and-recorded",
		      absent.name == NULL,
		      @"-URLProtocol:didCancelAuthenticationChallenge: stays undeclared, and the header says why");

		/* THE ODD ONE IS ODD ON PURPOSE: six callbacks carry the protocol and this one does not, which
		 * is Apple's own declaration — so the SELECTOR is asserted, not "fixed". */
		check("finish-loading-carries-no-protocol-argument",
		      [client respondsToSelector:@selector(URLProtocolDidFinishLoading:)] &&
		      ![client respondsToSelector:@selector(URLProtocol:didFinishLoading:)],
		      @"the selector is URLProtocolDidFinishLoading: — the one callback with no protocol: prefix");
	}

	/* --- THE AUDITED INVENTORY ------------------------------------------------------------------ */
	{
		static const char *protocolClassSelectors[] = {
			"canInitWithRequest:", "canonicalRequestForRequest:",
			"requestIsCacheEquivalent:toRequest:", "propertyForKey:inRequest:",
			"setProperty:forKey:inRequest:", "removePropertyForKey:inRequest:",
			"registerClass:", "unregisterClass:", NULL
		};
		static const char *protocolSelectors[] = {
			"initWithRequest:cachedResponse:client:", "startLoading", "stopLoading",
			"request", "cachedResponse", "client", NULL
		};
		static const char *cachedResponseClassSelectors[] = { NULL };
		static const char *cachedResponseSelectors[] = {
			"response", "data", "userInfo", "storagePolicy", NULL
		};
		/* REFUSED, EACH WITH ITS GROUND STATED — and the RECEIVE door is not here any more (§50.3): it was
		 * refused for a reason that had EXPIRED (its argument type, NSURLAuthenticationChallenge, has been
		 * shipped since slice 4), and the bridge had been messaging it all along through an id-typed
		 * receiver, so the refusal was a comment no compiler or caller enforced. What stays is the CANCEL
		 * NOTIFICATION — Apple declares it and nothing here raises it, because a client that cancels
		 * answers through the door's CancelAuthenticationChallenge disposition instead — and the coder
		 * doors, which need a keyed archiving format whose keys Apple does not publish. */
		static const char *excluded[] = {
			"URLProtocol:didCancelAuthenticationChallenge:",
			"initWithCoder:", "encodeWithCoder:", NULL
		};
		NSURLProtocol *protocol = [[NSURLProtocol alloc] initWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/x")]
									 cachedResponse:nil
									     client:nil];
		NSCachedURLResponse *cached =
			[[NSCachedURLResponse alloc] initWithResponse:fn_response()
							 data:[NSData dataWithBytes:"x" length:1]];
		FnClient *client = [[FnClient alloc] init];
		int complete = 1;
		int i;

		for (i = 0; protocolClassSelectors[i] != NULL; i++) {
			if (![NSURLProtocol respondsToSelector:sel_registerName(protocolClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLPROTOCOL missing +%s\n", protocolClassSelectors[i]);
			}
		}
		for (i = 0; protocolSelectors[i] != NULL; i++) {
			if (![protocol respondsToSelector:sel_registerName(protocolSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLPROTOCOL missing -%s\n", protocolSelectors[i]);
			}
		}
		for (i = 0; cachedResponseClassSelectors[i] != NULL; i++) {
			if (![NSCachedURLResponse respondsToSelector:sel_registerName(cachedResponseClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLPROTOCOL missing +%s (cached response)\n",
				       cachedResponseClassSelectors[i]);
			}
		}
		for (i = 0; cachedResponseSelectors[i] != NULL; i++) {
			if (![cached respondsToSelector:sel_registerName(cachedResponseSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLPROTOCOL missing -%s (cached response)\n",
				       cachedResponseSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([protocol respondsToSelector:sel_registerName(excluded[i])] ||
			    [client respondsToSelector:sel_registerName(excluded[i])] ||
			    [cached respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-URLPROTOCOL present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("urlprotocol-api-inventory", complete,
		      @"the audited inventory: every owed selector exists, and nothing listed as excluded does");
	}

	printf("FOUNDATION-URLPROTOCOL RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving INPUT for a
	 * while, so an `echo $?` the harness types may never run. */
	printf("FOUNDATION-URLPROTOCOL-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLPROTOCOL DONE\n");
	return failc ? 1 : 0;
}
