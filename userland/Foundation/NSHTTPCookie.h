/*
 * NSHTTPCookie.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The cookie as a value: a name, a value, and the host scope it is sent to.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT. It is the value type and the two conversions that make a cookie
 * useful on its own - a response's `Set-Cookie` fields into objects, and objects back into a request's
 * `Cookie` field. It is NOT the store: `NSHTTPCookieStorage` owns policy and lifetime, and neither class
 * performs any I/O (this library's transport is libcurl's business, and a cookie is data).
 *
 * WHERE THE MEMBERS COME FROM. Every name here was READ from Apple's published documentation (the occ
 * variant of the class page), never from a header - see docs/design/foundation-plan.md §47.1.
 */

#ifndef _FNX_FOUNDATION_NSHTTPCOOKIE_H
#define _FNX_FOUNDATION_NSHTTPCOOKIE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSURL.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * THE PROPERTY KEYS ARE STRINGS, AND THE STRINGS ARE OURS.
 *
 * Apple publishes the constant NAMES and their meanings but NOT their values - measured, not assumed: the
 * member page for NSHTTPCookieName carries one section kind (declarations) and its declaration is an
 * `extern` with no initializer, with no discussion section anywhere to hold a literal (§47.2). So the
 * values are this library's under the plan's §11.6.1 D2 rule - and they are chosen as the RFC 6265
 * attribute names, because a property key is a key in the `properties` dictionary AND the vocabulary a
 * `Set-Cookie` header is written and parsed in. Choosing the wire's own words keeps a printed dictionary
 * legible and keeps the decision in one place if it is ever revisited.
 */
typedef NSString *NSHTTPCookiePropertyKey;

extern NSHTTPCookiePropertyKey const NSHTTPCookieName;
extern NSHTTPCookiePropertyKey const NSHTTPCookieValue;
extern NSHTTPCookiePropertyKey const NSHTTPCookieDomain;
extern NSHTTPCookiePropertyKey const NSHTTPCookiePath;
extern NSHTTPCookiePropertyKey const NSHTTPCookiePort;
extern NSHTTPCookiePropertyKey const NSHTTPCookieVersion;
extern NSHTTPCookiePropertyKey const NSHTTPCookieExpires;
extern NSHTTPCookiePropertyKey const NSHTTPCookieDiscard;
extern NSHTTPCookiePropertyKey const NSHTTPCookieSecure;
extern NSHTTPCookiePropertyKey const NSHTTPCookieComment;
extern NSHTTPCookiePropertyKey const NSHTTPCookieCommentURL;
extern NSHTTPCookiePropertyKey const NSHTTPCookieMaximumAge;
extern NSHTTPCookiePropertyKey const NSHTTPCookieOriginURL;
extern NSHTTPCookiePropertyKey const NSHTTPCookieSameSitePolicy;
extern NSHTTPCookiePropertyKey const NSHTTPCookieSetByJavaScript;

/* The same-site policies are values too, for the same reason and by the same rule: `Strict` and `Lax`. */
typedef NSString *NSHTTPCookieStringPolicy;

extern NSHTTPCookieStringPolicy const NSHTTPCookieSameSiteStrict;
extern NSHTTPCookieStringPolicy const NSHTTPCookieSameSiteLax;

/*
 * The accept policy's four cases, with Apple's order and meanings. These ARE case names only - the one
 * category where D2 has always applied - so the numbers are ours: `always` must be 0, because a store's
 * default has to be the permissive one it documents.
 */
typedef NS_ENUM(NSUInteger, NSHTTPCookieAcceptPolicy) {
	NSHTTPCookieAcceptPolicyAlways = 0,
	NSHTTPCookieAcceptPolicyNever = 1,
	NSHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain = 2
};

@interface NSHTTPCookie : NSObject
{
	NSString *_name;
	NSString *_value;
	NSString *_domain;
	NSString *_path;
	NSString *_portList;
	NSString *_comment;
	NSString *_commentURL;
	NSString *_version;
	NSString *_sameSitePolicy;
	NSDate *_expiresDate;
	NSDictionary *_properties;
	unsigned int _secure:1;
	unsigned int _httpOnly:1;
	unsigned int _sessionOnly:1;
	unsigned int _setByJavaScript:1;
}

/*
 * THE THREE CONVERSIONS. `-initWithProperties:` is the real door and it ANSWERS nil when the required
 * properties are missing - the documented rule, and the reason the two class factories below can return a
 * dictionary of cookies without any of them being usable.
 *
 * `properties` speaks the property keys above; `headerFields`/`requestHeaderFields` speak the WIRE, so the
 * keys there are header field names ("Set-Cookie" in, "Cookie" out) and the values are header field values.
 */
+ (nullable instancetype)cookieWithProperties:(NSDictionary *)properties;
+ (NSArray *)cookiesWithResponseHeaderFields:(NSDictionary *)headerFields
						     forURL:(NSURL *)URL;
+ (NSDictionary *)requestHeaderFieldsWithCookies:(NSArray *)cookies;

- (nullable instancetype)initWithProperties:(NSDictionary *)properties;

/* Getting cookie host properties */
/* NULLABLE, and truthfully so: a cookie built from properties without a Domain is a HOST-ONLY
 * cookie, which is a real and common thing (RFC 6265 section 5.3), not a malformed one. */
@property (readonly, copy, nullable) NSString *domain;
@property (readonly, copy) NSString *path;
@property (readonly, copy, nullable) NSString *portList;

/* Getting cookie metadata */
@property (readonly, copy) NSString *name;
@property (readonly, copy) NSString *value;
@property (readonly, copy, nullable) NSString *version;

/* Determining cookie lifespan: a cookie with NO expiry is a session cookie, which is what `sessionOnly`
 * answers - it is a function of the absence of `expiresDate`, not a separate stored flag. */
@property (readonly, copy, nullable) NSDate *expiresDate;
@property (readonly, getter=isSessionOnly) BOOL sessionOnly;

/* Securing cookies */
@property (readonly, getter=isHTTPOnly) BOOL HTTPOnly;
@property (readonly, getter=isSecure) BOOL secure;
@property (readonly, copy, nullable) NSHTTPCookieStringPolicy sameSitePolicy;

/* Getting user-readable cookie metadata */
@property (readonly, copy, nullable) NSString *comment;
@property (readonly, copy, nullable) NSURL *commentURL;

/* Accessing cookie properties as key-value pairs: the dictionary the cookie was made from, with
 * `NSHTTPCookieSetByJavaScript` and `NSHTTPCookieMaximumAge` surviving here without appearing on the wire. */
@property (readonly, copy) NSDictionary *properties;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSHTTPCOOKIE_H */
