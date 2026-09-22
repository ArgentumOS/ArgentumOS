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
 * REFUSED BY NAME: the two AUTHENTICATION members of the client protocol
 * (-URLProtocol:didReceiveAuthenticationChallenge: and -URLProtocol:didCancelAuthenticationChallenge:),
 * because `NSURLAuthenticationChallenge` is not shipped — it is its own family (protection spaces,
 * credentials, a credential store) and its own slice, and a callback whose argument type does not exist
 * cannot be declared honestly. `urlprotocol-api-inventory` asserts every owed selector EXISTS and those
 * two are ABSENT.
 */

#ifndef FOUNDATION_NSURLPROTOCOL_H
#define FOUNDATION_NSURLPROTOCOL_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCachedURLResponse.h>

@class NSData;
@class NSError;
@class NSMutableURLRequest;
@class NSURLRequest;
@class NSURLResponse;
@class NSURLProtocol;

NS_ASSUME_NONNULL_BEGIN

/* HOW A PROTOCOL REPORTS BACK — the six calls an implementation makes, and the caller's side of the
 * seam. EVERY MEMBER IS REQUIRED in Apple's declaration, so there is no -respondsToSelector: dance here.
 *
 * `-URLProtocolDidFinishLoading:` HAS NO `protocol:` PARAMETER, AND THAT IS APPLE'S OWN INCONSISTENCY
 * rather than a transcription slip: the other five carry the protocol and this one does not. It is kept,
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
