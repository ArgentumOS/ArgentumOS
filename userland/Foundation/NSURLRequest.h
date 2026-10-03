/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLRequest / NSMutableURLRequest — A REQUEST IS A VALUE. docs/design/foundation-plan.md W7, §46.
 *
 * THE BOUNDARY IS THE ONE NSURL DREW, one layer in: a request DESCRIBES an exchange — a URL, a cache
 * policy, a timeout, and the HTTP baggage a message carries — and it does NOT PERFORM one. There is no
 * -start, no connection and no session here; those are W7's transport slices and they are REFUSED BY
 * NAME below rather than half-answered. What this pair owes is the value contract: the accessors,
 * equality, -copy and -description.
 *
 * THE ENUM VALUES ARE OURS, AND THAT IS THE RULING RATHER THAN A SHORTCUT. Apple publishes the CASE
 * NAMES of NSURLRequestCachePolicy, NSURLRequestNetworkServiceType and NSURLRequestAttribution — and
 * the case names only; the numbers are in no document this project may read (§2's clean-room wall, and
 * the plan's §11.6.1 D2 ruling). The values below are this tree's choice, stated here and pinned by the
 * probe. NSURLRequestReloadIgnoringCacheData is the ALIAS Apple documents it as, so it shares a value
 * with NSURLRequestReloadIgnoringLocalCacheData instead of being a seventh number.
 *
 * THE ONE VACANT VALUE IS FILLED NOW (§62.101): NSURLNetworkServiceTypeVoIP sat at 1 and was struck for
 * being deprecated, which the retired ground no longer justifies. The enum is Apple's own list with no hole in
 * it — a hole a caller can fall into is worse than a name that compiles. DEPRECATION DOES NOT MAKE A NAME
 * ABSENT: a caller porting an old program is exactly who needs it.
 *
 * REFUSED BY NAME, each because it needs something this library does not ship or is a different unit:
 *   - THE TRANSPORT: `-loadRequest:` and the `-resume`/`-start` family on the URL itself. **⚠⚠ AND THE
 *     THREE CLASSES THAT USED TO BE NAMED HERE ARE NOT REFUSED ANY MORE, EACH FOR ITS OWN REASON:** the
 *     seam shipped (§52), `NSURLConnection` shipped (§62.25) and drives that seam itself (§63.153), and
 *     `NSURLSession` left the surface altogether with the 10.2 cut (§63.159). A request here is still a
 *     DESCRIPTION — that has not changed — and what performs it is `NSURLConnection`.
 *   - CODING (NSSecureCoding): a request's archived form is Apple's own keyed structure and its key
 *     names are unpublished, so implementing it would invent a new format wearing Apple's name. The
 *     coder door is therefore refused rather than invented — the reasoning D2 records for values.
 *   - THE DEPRECATED CERTIFICATE DOORS `+allowsAnyHTTPSCertificateForHost:` and
 *     `+setAllowsAnyHTTPSCertificate:forHost:`: Apple removed both, and §11.5 strikes what it deprecated.
 */

#ifndef FOUNDATION_NSURLREQUEST_H
#define FOUNDATION_NSURLREQUEST_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>	/* NS_ENUM */
#import <Foundation/NSDate.h>		/* NSTimeInterval */

@class NSString;
@class NSURL;
@class NSData;
@class NSDictionary;
@class NSInputStream;

NS_ASSUME_NONNULL_BEGIN

/* WHICH CACHE A TRANSPORT MAY USE. The transport is a later slice; this is the policy a request CARRIES,
 * and the two `ReloadIgnoring…` names differ in whether intermediaries are ignored along with the local
 * cache. */
typedef NS_ENUM(NSUInteger, NSURLRequestCachePolicy) {
	NSURLRequestUseProtocolCachePolicy = 0,
	NSURLRequestReloadIgnoringLocalCacheData = 1,
	NSURLRequestReturnCacheDataElseLoad = 2,
	NSURLRequestReturnCacheDataDontLoad = 3,
	NSURLRequestReloadIgnoringLocalAndRemoteCacheData = 4,
	NSURLRequestReloadRevalidatingCacheData = 5,
	/* THE ALIAS Apple documents: the same value as …LocalCacheData, not a seventh case. */
	NSURLRequestReloadIgnoringCacheData = NSURLRequestReloadIgnoringLocalCacheData,
};

/* A HINT TO THE NETWORK LAYER about the traffic's shape. Declared and CARRIED, with nothing acting on
 * them yet — there is no transport in this slice to read one (the honest property bag NSStream's network
 * keys are). Value 1 is vacant: see the note above on the struck …VoIP. */
typedef NS_ENUM(NSUInteger, NSURLRequestNetworkServiceType) {
	NSURLNetworkServiceTypeDefault = 0,
	/* §62.101: THE GAP IS FILLED. This slot was left vacant on purpose when the name was STRUCK for being
	 * deprecated — and that ground was RETIRED on 2026-09-26 (plan row D7, §62.24), so the value is owed and
	 * 1 is the VoIP case Apple's own header puts there. */
	NSURLNetworkServiceTypeVoIP = 1,
	NSURLNetworkServiceTypeVideo = 2,
	NSURLNetworkServiceTypeBackground = 3,
	NSURLNetworkServiceTypeVoice = 4,
	NSURLNetworkServiceTypeAVStreaming = 5,
	NSURLNetworkServiceTypeResponsiveAV = 6,
	NSURLNetworkServiceTypeResponsiveData = 7,
	NSURLNetworkServiceTypeCallSignaling = 8,
};

/* WHO THE REQUEST IS ATTRIBUTED TO, which is a privacy signal a transport may report. Carried. */
typedef NS_ENUM(NSUInteger, NSURLRequestAttribution) {
	NSURLRequestAttributionDeveloper = 0,
	NSURLRequestAttributionUser = 1,
};

@interface NSURLRequest : NSObject <NSCopying>
{
	NSURL *_url;			/* nullable: a request may be built without one */
	NSURLRequestCachePolicy _cachePolicy;
	NSTimeInterval _timeoutInterval;
	NSURL *_mainDocumentURL;
	NSURLRequestNetworkServiceType _networkServiceType;
	NSURLRequestAttribution _attribution;
	NSString *_HTTPMethod;		/* never nil: GET is the default */
	NSDictionary *_allHTTPHeaderFields;	/* ALWAYS an immutable snapshot, nil until a header is set */
	NSData *_HTTPBody;
	NSInputStream *_HTTPBodyStream;
	BOOL _HTTPShouldHandleCookies;
	BOOL _HTTPShouldUsePipelining;
	BOOL _allowsCellularAccess;

	/* §63.192: the network-policy and DNS surface. */
	BOOL _allowsConstrainedNetworkAccess;
	BOOL _allowsExpensiveNetworkAccess;
	BOOL _allowsUltraConstrainedNetworkAccess;
	BOOL _requiresDNSSECValidation;
	BOOL _assumesHTTP3Capable;
	BOOL _allowsPersistentDNS;
	id _cookiePartitionIdentifier;
}

/* THE CONVENIENCE DOORS, both answering a request with the DEFAULT cache policy and a 60-second timeout
 * (a value Apple documents as "the default" without publishing the number, so it is this tree's under
 * D2 and is pinned by the probe). */
+ (instancetype)requestWithURL:(NSURL *)URL;
+ (instancetype)requestWithURL:(NSURL *)URL
		   cachePolicy:(NSURLRequestCachePolicy)cachePolicy
	       timeoutInterval:(NSTimeInterval)timeoutInterval;

- (instancetype)initWithURL:(NSURL *)URL;
- (instancetype)initWithURL:(NSURL *)URL
		cachePolicy:(NSURLRequestCachePolicy)cachePolicy
	    timeoutInterval:(NSTimeInterval)timeoutInterval;

/* `URL` is nullable because a request may be built without one (Apple declares it nullable too), and the
 * initializers above take a nonnull URL — a request with no URL is reachable only by mutating one back. */
@property (nullable, readonly, copy) NSURL *URL;
@property (readonly) NSURLRequestCachePolicy cachePolicy;
@property (readonly) NSTimeInterval timeoutInterval;
@property (nullable, readonly, copy) NSURL *mainDocumentURL;
@property (readonly) NSURLRequestNetworkServiceType networkServiceType;
@property (readonly) NSURLRequestAttribution attribution;

/* THE HTTP BAGGAGE. `HTTPMethod` is never nil — GET is the default Apple documents — and the rest are nil
 * until set. `-valueForHTTPHeaderField:` is CASE-INSENSITIVE, which is HTTP's own rule (RFC 9110 §5.1)
 * rather than a convenience. */
@property (readonly, copy) NSString *HTTPMethod;
@property (nullable, readonly, copy) NSDictionary *allHTTPHeaderFields;
@property (nullable, readonly, copy) NSData *HTTPBody;
@property (nullable, readonly, retain) NSInputStream *HTTPBodyStream;
@property (readonly) BOOL HTTPShouldHandleCookies;
@property (readonly) BOOL HTTPShouldUsePipelining;
@property (readonly) BOOL allowsCellularAccess;

/* THE NETWORK-POLICY AND DNS SURFACE (§63.192). THE DEFAULTS ARE PART OF THE CONTRACT and are pinned by the
 * probe: the three `allows…Access` doors default YES — the same default `allowsCellularAccess` has — and the
 * three policy doors default NO, because a door nobody asked for is a door nobody uses. */
@property (readonly) BOOL allowsConstrainedNetworkAccess;
@property (readonly) BOOL allowsExpensiveNetworkAccess;
@property (readonly) BOOL allowsUltraConstrainedNetworkAccess;
@property (readonly) BOOL requiresDNSSECValidation;
@property (readonly) BOOL assumesHTTP3Capable;
@property (readonly) BOOL allowsPersistentDNS;
@property (nullable, readonly, copy) NSString *cookiePartitionIdentifier;

- (nullable NSString *)valueForHTTPHeaderField:(NSString *)field;

@end

/* THE MUTABLE FORM. Its -copy answers an IMMUTABLE NSURLRequest — Cocoa's rule, and the reason a request
 * handed to a transport cannot be changed under it.
 *
 * THE SETTERS ARE DECLARED AS METHODS RATHER THAN `readwrite` PROPERTIES, deliberately: this library is
 * MRC and compiles the class hierarchy across two @implementations, and a `readwrite` property whose
 * getter lives in the superclass makes the compiler synthesize an ivar in the SUBCLASS that would shadow
 * the base's — so the getter would answer the shadow and every field would read back empty. Method
 * declarations sidestep a hazard with no upside; dot syntax works on the pair exactly as it does on a
 * property. */
@interface NSMutableURLRequest : NSURLRequest <NSMutableCopying>

- (void)setURL:(nullable NSURL *)URL;
- (void)setCachePolicy:(NSURLRequestCachePolicy)cachePolicy;
- (void)setTimeoutInterval:(NSTimeInterval)timeoutInterval;
- (void)setMainDocumentURL:(nullable NSURL *)mainDocumentURL;
- (void)setNetworkServiceType:(NSURLRequestNetworkServiceType)networkServiceType;
- (void)setAttribution:(NSURLRequestAttribution)attribution;
- (void)setHTTPMethod:(NSString *)HTTPMethod;
- (void)setAllHTTPHeaderFields:(nullable NSDictionary *)allHTTPHeaderFields;
- (void)setHTTPBody:(nullable NSData *)HTTPBody;
- (void)setHTTPBodyStream:(nullable NSInputStream *)HTTPBodyStream;
- (void)setHTTPShouldHandleCookies:(BOOL)HTTPShouldHandleCookies;
- (void)setHTTPShouldUsePipelining:(BOOL)HTTPShouldUsePipelining;
- (void)setAllowsCellularAccess:(BOOL)allowsCellularAccess;
/* THE SAME SEVEN, SETTABLE — Apple's shape: the immutable class reads them, the mutable one writes. */
@property (readwrite) BOOL allowsConstrainedNetworkAccess;
@property (readwrite) BOOL allowsExpensiveNetworkAccess;
@property (readwrite) BOOL allowsUltraConstrainedNetworkAccess;
@property (readwrite) BOOL requiresDNSSECValidation;
@property (readwrite) BOOL assumesHTTP3Capable;
@property (readwrite) BOOL allowsPersistentDNS;
@property (nullable, readwrite, copy) NSString *cookiePartitionIdentifier;

/* THE TWO HEADER DOORS, AND THEY DIFFER: -setValue:forHTTPHeaderField: REPLACES the field, while
 * -addValue:forHTTPHeaderField: APPENDS to it with ", " (RFC 9110 §5.2's list rule). Both address the
 * field case-insensitively. A nil value to -setValue: REMOVES the field. */
- (void)setValue:(nullable NSString *)value forHTTPHeaderField:(NSString *)field;
- (void)addValue:(NSString *)value forHTTPHeaderField:(NSString *)field;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLREQUEST_H */
