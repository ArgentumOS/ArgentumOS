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
 * THE THREE CONVENIENCE DOORS ARE THE SHAPE APPLE GIVES IT, and the two built-in ones differ in exactly
 * one observable way HERE: `+ephemeralSessionConfiguration` is documented as keeping no persistent
 * caches, cookies or credentials — and THIS LIBRARY SHIPS NONE OF THOSE YET, so the honest statement is
 * that the two are indistinguishable in this tree except by identity and identifier. The header says so,
 * and the probe pins it rather than letting a reader assume the ephemeral door does something it cannot.
 *
 * REFUSED BY NAME, each because the CLASS behind the property is its own ledger row and not shipped yet:
 * `URLCache` (the store), `HTTPCookieStorage` and `URLCredentialStorage`. Shipping the properties anyway
 * would mean declaring an `NSURLCache *` that nothing can create — a type in a signature with no
 * implementation behind it. For the same reason `HTTPCookieAcceptPolicy` is refused: its enum belongs to
 * the cookie family, not to this one. Also refused, as everywhere in this library: the coder doors.
 *
 * `protocolClasses` IS THE ONE PROPERTY HERE THAT REACHES ANOTHER SLICE, and it is why this class is
 * worth reading beside slice 2a: it is how a session says WHICH NSURLProtocol subclasses to consult, so
 * the seam's registry and the session's configuration meet here. It is carried and not yet consulted —
 * consulting it is the session's execution half — and the header says that rather than implying more.
 */

#ifndef FOUNDATION_NSURLSESSIONCONFIGURATION_H
#define FOUNDATION_NSURLSESSIONCONFIGURATION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLRequest.h>	/* the cache policy and the network service type */

@class NSArray;
@class NSString;

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
	NSArray *_protocolClasses;
}

+ (NSURLSessionConfiguration *)defaultSessionConfiguration;
/* Keeps no persistent caches, cookies or credentials — and this library ships none of those yet, so in
 * THIS tree it is indistinguishable from the default except by identity and identifier. */
+ (NSURLSessionConfiguration *)ephemeralSessionConfiguration;
+ (NSURLSessionConfiguration *)backgroundSessionConfigurationWithIdentifier:(NSString *)identifier;

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

/* THE SUBCLASSES A SESSION MAY CONSULT — carried here, consulted by the session's execution half, and
 * the one property that ties this class to slice 2a's registry. */
@property (nullable, copy) NSArray *protocolClasses;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSIONCONFIGURATION_H */
