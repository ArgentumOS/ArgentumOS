/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLProtocol — THE SEAM A TRANSPORT PLUGS INTO, and nothing else. docs/design/foundation-plan.md W7;
 * the transport plan's slice 2a.
 *
 * THIS SLICE SHIPS THE SEAM, NOT A LOADER. There is no session here, no socket, and no request that
 * actually travels: what is here is the point where a transport ATTACHES, plus the client callback
 * protocol it reports through. The plan makes that ordering explicit — a `NSURLProtocol` subclass is how
 * *any* protocol plugs in, so it comes before the transport that will be the first one (slice 2c's
 * `FNCURLURLProtocol`, a libcurl bridge that is itself just a subclass).
 *
 * THE BASE CLASS IS ABSTRACT, AND ITS CLASS METHODS ARE THE OVERRIDE POINTS. `+canInitWithRequest:`
 * answers NO, so the base claims nothing; `+canonicalRequestForRequest:` answers the request unchanged;
 * `+requestIsCacheEquivalent:toRequest:` answers value equality, which is the honest default for two
 * requests (NSURLRequest implements -isEqual:, so "equivalent" has something to mean). `-startLoading`
 * and `-stopLoading` do nothing, because a subclass that does not implement them has nothing to start.
 * Every one of those defaults is asserted, because an abstract base whose defaults are undocumented is
 * indistinguishable from an unfinished class.
 *
 * REQUEST PROPERTIES ARE OURS, AND SO IS THE MECHANISM, WHICH IS THE PART WORTH STATING. Apple's shape is
 * a property that TRAVELS WITH A REQUEST: `+setProperty:forKey:inRequest:` attaches one, and the same
 * request answers it later, possibly in another protocol's code. A request is an IMMUTABLE VALUE here, so
 * there is no field to write into: the table below is keyed by the request's IDENTITY, and it RETAINS the
 * key — without that retention a freed request's address could be handed to a new one and the new request
 * would answer the old one's properties. THE RULE A CALLER CAN DEPEND ON is therefore "per instance, and
 * a copy starts empty": a request handed to `-setProperty:` and later COPIED does not carry the property
 * to the copy. `request-properties-are-per-instance` pins all of it.
 *
 * THE REGISTRY HAS A DEFINITE ORDER, STATED RATHER THAN IMPLIED: classes are consulted
 * MOST-RECENTLY-REGISTERED FIRST, so the last word belongs to the most recent caller. That direction is
 * this tree's rule (the check `registration-order-decides-precedence` pins it) rather than a claim about
 * what Apple does, and only a subclass may be registered — `+registerClass:` answers NO for the base
 * itself, which is what makes a registry of override points meaningful.
 *
 * THE AUTHENTICATION DOOR IS DECLARED HERE, AND UNTIL §50.3 IT WAS DECLARED NOWHERE. The bridge has been
 * messaging -URLProtocol:didReceiveAuthenticationChallenge:completionHandler: since slice 4, and the
 * selector existed in no header at all: `_client` is typed `id <NSURLProtocolClient>` and Objective-C
 * permits an undeclared selector on an id-typed receiver, so the call compiled, the bridge's own
 * implementation answered, and the authentication loop passed ten checks — WITH NO CONTRACT A CALLER COULD
 * READ AND NO COMPILER THAT COULD CHECK IT. That is the worst of the three possible states (the behaviour
 * right, the declaration missing, and nothing failing to draw attention to either), and a declaration is
 * the whole fix. The completion-handler form is §48.6's REGISTERED DEVIATION: Apple's client door answers
 * by messaging the challenge's NSURLAuthenticationChallengeSender, this library refuses `-sender` as
 * Legacy, and a completion handler is the shape used everywhere else this seam must wait.
 *
 * AND THE ENUM LIVES BESIDE THE FIRST DOOR THAT TAKES IT, which is the other half of that fix rather than
 * tidiness: `NSURLSessionAuthChallengeDisposition` was declared in NSURLSession.h, and NSURLSession.h
 * IMPORTS THIS HEADER — so declaring the door with the type where the type already was would have been a
 * cycle, and importing the session header into this one would have put the enum's declaration on the wrong
 * side of a guard. Moving the enum here is the move with no cycle in it, and NSURLSession.h keeps using it
 * through the import it already had.
 *
 * THE IMPORT LIST IS NOW COMPLETE, which is the third fact that fix exposed: this header used to import
 * only NSObject.h and NSCachedURLResponse.h and RELY on being included after the headers that define
 * NSURLRequest, NSURLResponse and NSData. That holds inside Foundation.h, whose order supplies them, and
 * fails for a consumer that includes this header alone — the Sterling-compiler staging copy is one. A
 * header that compiles in one include position only is a header with an undeclared dependency.
 *
 * REFUSED BY NAME, each with its ground stated:
 *   * -URLProtocol:didCancelAuthenticationChallenge: — Apple declares it and NOTHING IN THIS TREE RAISES
 *     IT, because the round trip it completes is the one §48.6 refuses: a client that cancels answers
 *     through the authentication door's `CancelAuthenticationChallenge` disposition instead, and no
 *     notification comes back. Recorded as a WORK ITEM rather than a boundary (§11.3), and its exact
 *     signature is to be taken from Apple's published documentation rather than recalled — inventing an
 *     API is worse than refusing one;
 *   * the coder doors, which need a keyed archiving format whose keys Apple does not publish.
 * `urlprotocol-api-inventory` asserts every owed selector EXISTS and every listed refusal is ABSENT, and
 * `urlprotocol-client-declares-the-authentication-door` is the check that fails on the state §50.3 was.
 */

#ifndef FOUNDATION_NSURLPROTOCOL_H
#define FOUNDATION_NSURLPROTOCOL_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCachedURLResponse.h>
/* THE THREE THIS HEADER USES IN ITS OWN DECLARATIONS, imported rather than assumed (§50.3): a consumer
 * that includes this header first must not depend on the order Foundation.h happens to supply them in. */
#import <Foundation/NSData.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>

@class NSError;
@class NSMutableURLRequest;
@class NSURLProtocol;
/* The challenge and the credential the authentication door's handler carries are FORWARD declared: this
 * header is included BY NSURLSession.h, and NSURLCredential.h comes after it in Foundation.h, so a
 * declaration that needed the full type here would be exactly the cycle §50.3 set out to remove. */
@class NSURLAuthenticationChallenge;
@class NSURLCredential;

NS_ASSUME_NONNULL_BEGIN

/* WHAT A CLIENT DECIDES WHEN A SERVER ASKS FOR CREDENTIALS — declared beside the first door that takes it
 * (§50.3: it lived in NSURLSession.h until that header's import of this one made the door's type and the
 * door's declaration a cycle).
 *
 * THE VALUES ARE OURS UNDER §11.6.1 D2, as every enum's in this library are — Apple publishes the case
 * names and the case names only. UseCredential is 0 because it is the case a handler reaches for first,
 * and a zeroed decision must not mean the opposite of what its author intended. */
typedef NS_ENUM(NSInteger, NSURLSessionAuthChallengeDisposition) {
	NSURLSessionAuthChallengeUseCredential = 0,
	NSURLSessionAuthChallengePerformDefaultHandling = 1,
	NSURLSessionAuthChallengeCancelAuthenticationChallenge = 2,
	NSURLSessionAuthChallengeRejectProtectionSpace = 3
};

/* HOW A PROTOCOL REPORTS BACK — the seven calls an implementation makes, and the caller's side of the
 * seam. EVERY MEMBER IS REQUIRED in Apple's declaration, so there is no -respondsToSelector: dance here.
 *
 * `-URLProtocolDidFinishLoading:` HAS NO `protocol:` PARAMETER, AND THAT IS APPLE'S OWN INCONSISTENCY
 * rather than a transcription slip: the other six carry the protocol and this one does not. It is kept,
 * asserted, and commented — "fixing" it would produce a selector no existing implementation implements.
 */
@protocol NSURLProtocolClient <NSObject>

/* THE REQUEST CHANGED MID-FLIGHT (a redirect): the caller is told what it became and what answered it. */
- (void)URLProtocol:(NSURLProtocol *)protocol
	wasRedirectedToRequest:(NSURLRequest *)request
	     redirectResponse:(NSURLResponse *)redirectResponse;

/* THE CACHED ANSWER THE PROTOCOL WAS HANDED IS STILL GOOD. */
- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse;

/* THE ANSWER'S HEAD. `policy` is what the protocol ADVISES for storekeeping, which is why this is the one
 * callback that takes a NSURLCacheStoragePolicy: the metadata has arrived and the bytes have not. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy;

/* THE BODY, in as many calls as the protocol likes; the order is the protocol's to decide. */
- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data;

/* THE SERVER ASKED FOR CREDENTIALS, AND THE CLIENT ANSWERS THROUGH THE HANDLER — SYNCHRONOUSLY by contract,
 * the way this seam answers at the head of a response: the transport is holding the transfer while it
 * waits, and the handler is what releases it. `disposition` is what to do with the challenge and
 * `credential` is what to send with the retry. THE HANDLER IS CALLED EXACTLY ONCE.
 *
 * THIS IS §48.6's REGISTERED DEVIATION FROM APPLE (see the header above): Apple's door is asynchronous and
 * its client answers by messaging the challenge's NSURLAuthenticationChallengeSender, which this library
 * refuses as Legacy. §50.3 is why the deviation is now VISIBLE HERE rather than only in the bridge — the
 * selector existed in no header at all, and an undeclared selector on an id-typed receiver compiles. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
		  completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
					      NSURLCredential *credential))completionHandler;

/* AND THE END — one of these two, and this one carries no protocol, as Apple declares it. */
- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol;
- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error;

@end

@interface NSURLProtocol : NSObject
{
	NSURLRequest *_request;
	NSCachedURLResponse *_cachedResponse;
	id <NSURLProtocolClient> _client;
}

/* --- THE OVERRIDE POINTS: a subclass answers YES for the requests it handles, and may canonicalise one
 * (a protocol that strips a fragment, say) before it starts. */
+ (BOOL)canInitWithRequest:(NSURLRequest *)request;
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request;
+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b;

/* --- THE REQUEST'S PROPERTY TABLE: attached by identity, answered by the same instance, and not carried
 * by a copy. `-setProperty:`/`-removePropertyForKey:` take the MUTABLE request, as Apple declares them —
 * which is the same asymmetry NSURLRequest itself has between its read and write doors. */
+ (nullable id)propertyForKey:(NSString *)key inRequest:(NSURLRequest *)request;
+ (void)setProperty:(nullable id)value forKey:(NSString *)key inRequest:(NSMutableURLRequest *)request;
+ (void)removePropertyForKey:(NSString *)key inRequest:(NSMutableURLRequest *)request;

/* --- THE REGISTRY: only a SUBCLASS registers, and the most recently registered is consulted first. */
+ (BOOL)registerClass:(Class)protocolClass;
+ (void)unregisterClass:(Class)protocolClass;

/* FNX: THE CONSULTATION DOOR, first-party on purpose. Apple dispatches a request to a registered class
 * inside its loader and does not publish that step, but slice 2c's loader must do exactly this — and a
 * registry whose ORDER cannot be observed cannot be pinned by a check, which would leave the one
 * behavioural rule this slice has as a comment. It answers the first class, walking
 * most-recently-registered first, whose +canInitWithRequest: says YES, and Nil when none does. */
+ (nullable Class)fnProtocolClassForRequest:(NSURLRequest *)request;

/* --- THE INSTANCE. `cachedResponse` and `client` are nullable exactly as Apple declares them: a protocol
 * may be started with no cached answer and with no listener, and must cope with both. */
- (instancetype)initWithRequest:(NSURLRequest *)request
		 cachedResponse:(nullable NSCachedURLResponse *)cachedResponse
			 client:(nullable id <NSURLProtocolClient>)client;

- (void)startLoading;
- (void)stopLoading;

@property (readonly, copy) NSURLRequest *request;
@property (nullable, readonly, copy) NSCachedURLResponse *cachedResponse;
@property (nullable, readonly, strong) id <NSURLProtocolClient> client;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLPROTOCOL_H */
