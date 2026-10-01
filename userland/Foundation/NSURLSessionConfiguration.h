/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionConfiguration — THE SESSION'S OWN SETTINGS, as a value.
 * docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c.
 *
 * A SESSION CANNOT BE CREATED WITHOUT ONE, which is why this class comes first in the session half of
 * 2c — and why it is a class at all rather than a pile of properties on the session: a configuration is
 * meant to be prepared once and reused (Apple's own advice), and a session TAKES A SNAPSHOT of it.
 *
 * THE DOORS ARE THE SHAPE APPLE GIVES IT: the two built-in factories, the background factory and its
 * DEPRECATED OLD SPELLING `+backgroundSessionConfiguration:` (the same object, its own name, because a
 * caller porting an old program reaches for that name), and the readwrite properties below. The two
 * built-in doors differ in exactly one observable way HERE: `+ephemeralSessionConfiguration` is documented
 * as keeping no persistent caches, cookies or credentials — and THIS LIBRARY SHIPS NONE OF THOSE PERSISTENT
 * YET — so the honest statement is that the two are indistinguishable in this tree except by identity and
 * identifier. The header says so, and the probe pins it rather than letting a reader assume the ephemeral
 * door does something it cannot.
 *
 * WHAT THE LAYER READS TODAY, STATED SO NO DOOR OVERPROMISES: the tree's session half consults exactly ONE
 * property of a configuration — `protocolClasses` (NSURLSession.m:1137, where a transfer picks the class to
 * run). Everything else is the configuration's VALUE CONTRACT and is carried: a session SNAPSHOTS the object
 * (NSURLSession.m:743) so a caller can read every field back, and a later execution row is what begins to act
 * on them. Said here rather than left for a reader to infer from a getter.
 *
 * THE STORAGE DOORS LAND NOW, AND THE REASON THEY WERE REFUSED IS MEASURED FALSE TODAY. The earlier note
 * read "the class behind the property is its own ledger row and not shipped yet" — TRUE WHEN WRITTEN, FALSE
 * NOW: NSURLCache, NSHTTPCookieStorage, NSURLCredentialStorage and the NSHTTPCookieAcceptPolicy enum are all
 * shipped (NSURLCache.h:47, NSHTTPCookieStorage.h:59, NSURLCredentialStorage.h:49, NSHTTPCookie.h:70), and
 * the tree's transfer ALREADY exercises the facilities they store an override for ([[NSURLCache
 * sharedURLCache] cachedResponseForRequest:] at FNCURLURLProtocol.m:548 and [[NSURLCache sharedURLCache]
 * storeCachedResponse:...] at NSURLSession.m:461). A configuration's `URLCache`, `HTTPCookieStorage` and
 * `URLCredentialStorage` ARE the override for those facilities, so they land as typed, round-tripping doors
 * whose documented defaults are the SHARED instances; `HTTPAdditionalHeaders` lands beside them because the
 * transport builds its HTTP header list from the request (FNCURLURLProtocol.m:~609), which is the facility a
 * session's extra headers feed. Each default is Apple's documented one (the primary spec); the field values
 * are not published as constants, the rule D2 this library already follows for the timeouts.
 *
 * STILL OPEN, EACH NAMED WITH WHAT WOULD HAVE TO EXIST FIRST — not "declared and hollow", because no code
 * path can read them and their substrate is absent from this tree:
 *   - `TLSMinimumSupportedProtocol` / `TLSMaximumSupportedProtocol` (deprecated) and their `...Version` twins:
 *     their types are the Security framework's `SSLProtocol` and `tls_protocol_version_t`, enums THIS
 *     Foundation does not declare. The doors land only once that type exists here.
 *   - `connectionProxyDictionary` and `proxyConfigurations`: the transfer hands curl no proxy at all, and
 *     `proxyConfigurations`' element type (Apple's `NSURLSessionProxyConfiguration` protocol) is not in this
 *     tree. A proxy door lands only once the transport honours a proxy.
 *   - `sessionSendsLaunchEvents`, `shouldUseExtendedBackgroundIdleMode`, `sharedContainerIdentifier`:
 *     background relaunch, extended idle and app-group containers are iOS machinery this system does not have
 *     (the same reason NSHTTPCookieStorage refuses `+sharedCookieStorageForGroupContainerIdentifier:`).
 *   - `enablesEarlyData` and `requiresDNSSECValidation`: TLS 1.3 early data and DNSSEC validation need a TLS
 *     stack and a validating resolver the transport does not expose.
 *   - `allowsUltraConstrainedNetworkAccess` and `usesClassicLoadingMode`: carried flags whose near-identical
 *     twin (`allowsConstrainedNetworkAccess`) is already carried; they are left OPEN rather than adding more
 *     stored flags nothing reads.
 *   - the coder doors, as everywhere in this library.
 */

#ifndef FOUNDATION_NSURLSESSIONCONFIGURATION_H
#define FOUNDATION_NSURLSESSIONCONFIGURATION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLRequest.h>	/* the cache policy and the network service type */
#import <Foundation/NSHTTPCookieStorage.h>	/* the accept-policy enum and the storage class */

@class NSArray;
@class NSString;
@class NSDictionary;
@class NSURLCache;
@class NSURLCredentialStorage;

NS_ASSUME_NONNULL_BEGIN

/* MULTIPATH TCP'S SERVICE TYPE (§62.101), Apple's four values. NOTHING HERE OPENS A SECOND SUBFLOW — this
 * system has no MPTCP — so the property is CARRIED rather than acted on, the standing the file-protection bits
 * and the other carried flags already have: a caller sets it, gets it back, and this note is the record of what
 * does not happen. */
typedef NS_ENUM(NSInteger, NSURLSessionMultipathServiceType) {
	NSURLSessionMultipathServiceTypeNone = 0,
	NSURLSessionMultipathServiceTypeHandover = 1,
	NSURLSessionMultipathServiceTypeInteractive = 2,
	NSURLSessionMultipathServiceTypeAggregate = 3
};

@interface NSURLSessionConfiguration : NSObject <NSCopying>
{
	NSString *_identifier;
	NSURLRequestCachePolicy _requestCachePolicy;
	NSTimeInterval _timeoutIntervalForRequest;
	NSTimeInterval _timeoutIntervalForResource;
	NSURLRequestNetworkServiceType _networkServiceType;
	NSURLSessionMultipathServiceType _multipathServiceType;
	BOOL _allowsCellularAccess;
	BOOL _allowsExpensiveNetworkAccess;
	BOOL _allowsConstrainedNetworkAccess;
	BOOL _waitsForConnectivity;
	BOOL _HTTPShouldUsePipelining;
	BOOL _HTTPShouldSetCookies;
	NSInteger _HTTPMaximumConnectionsPerHost;
	BOOL _discretionary;
	NSDictionary *_HTTPAdditionalHeaders;
	NSHTTPCookieAcceptPolicy _HTTPCookieAcceptPolicy;
	NSHTTPCookieStorage *_HTTPCookieStorage;
	NSURLCache *_URLCache;
	NSURLCredentialStorage *_URLCredentialStorage;
	NSArray *_protocolClasses;
}

+ (NSURLSessionConfiguration *)defaultSessionConfiguration;
/* Keeps no persistent caches, cookies or credentials — and this library ships none of those PERSISTENT yet,
 * so in THIS tree it is indistinguishable from the default except by identity and identifier. */
+ (NSURLSessionConfiguration *)ephemeralSessionConfiguration;
+ (NSURLSessionConfiguration *)backgroundSessionConfigurationWithIdentifier:(NSString *)identifier;
/* APPLE'S DEPRECATED OLD SPELLING, named because a caller porting an old program meets it: the same object
 * `+backgroundSessionConfigurationWithIdentifier:` makes. (Deprecation is recorded in prose — this library
 * carries no availability attributes — the standing NSURLRequest.h records for its own deprecated names.) */
+ (NSURLSessionConfiguration *)backgroundSessionConfiguration:(NSString *)identifier;

/* nil for the two built-in doors; the background door's own string otherwise. */
@property (nullable, readonly, copy) NSString *identifier;

@property NSURLRequestCachePolicy requestCachePolicy;
/* 60 seconds and 7 days, Apple's documented defaults (the numbers are D2: Apple documents the durations
 * in prose and not as constants). */
@property NSTimeInterval timeoutIntervalForRequest;
@property NSTimeInterval timeoutIntervalForResource;
@property NSURLRequestNetworkServiceType networkServiceType;

/* CARRIED, NOT ACTED ON — see the enum's note above. */
@property NSURLSessionMultipathServiceType multipathServiceType;
@property BOOL allowsCellularAccess;
@property BOOL allowsExpensiveNetworkAccess;
@property BOOL allowsConstrainedNetworkAccess;
@property BOOL waitsForConnectivity;
@property BOOL HTTPShouldUsePipelining;
@property BOOL HTTPShouldSetCookies;
/* 6, Apple's documented default (D2, as above). */
@property NSInteger HTTPMaximumConnectionsPerHost;
@property BOOL discretionary;

/* THE FOUR STORAGE/HEADER DOORS — see the file header for why they land now. Each default is Apple's
 * documented one and each is the override for a facility the tree's transfer already uses. */
@property (nullable, copy) NSDictionary *HTTPAdditionalHeaders;	/* default: an empty dictionary */
@property NSHTTPCookieAcceptPolicy HTTPCookieAcceptPolicy;		/* default: OnlyFromMainDocumentDomain */
@property (nullable, strong) NSHTTPCookieStorage *HTTPCookieStorage;	/* default: the shared store */
@property (nullable, strong) NSURLCache *URLCache;			/* default: the shared cache */
@property (nullable, strong) NSURLCredentialStorage *URLCredentialStorage;	/* default: the shared storage */

/* THE SUBCLASSES A SESSION MAY CONSULT — carried here, consulted by the session's execution half, and
 * the one property that ties this class to slice 2a's registry. */
@property (nullable, copy) NSArray *protocolClasses;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSIONCONFIGURATION_H */
