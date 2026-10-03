/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNURLComponents — a URL as EIGHT FIELDS, private to the library. It is what is LEFT of the 10.9
 * NSURLComponents/NSURLQueryItem family after §63.174 cut it, and it stays because something real
 * needs it: RFC 3986 §5.2 is the component-wise algorithm, and NSURL's
 * `+URLWithString:relativeToURL:` is that algorithm's only door. A relative spelling cannot be
 * resolved by splitting a string at the last slash — the reference's own scheme, its authority, an
 * empty path, and dot segments all matter — so the resolution needs a parsed component form to work
 * on. That form is this class.
 *
 * WHY THE NAME IS GONE AND THE MACHINERY IS NOT. Apple's family is a PUBLIC editor's API: it answers
 * what is the host, change the host, what are the query items, add one — and this library's ledger
 * rule is that a class Apple has and this library does not is a refusal (§11). The refusal is of the
 * DOOR, not of the algorithm BEHIND this library's own door: `FNURLResolveRelative` is reached from
 * NSURL, and its input is a parsed URL. That is what §63.164's "vocabulary moved rather than went"
 * means. `NSURLQueryItem` and the `-queryItems` pair go with the public family: this class has no
 * user for them and the resolver never wanted them — the query stays one string, as a URL carries it.
 *
 * THE PARSER IS RFC 3986'S GRAMMAR, spelled out: scheme ":" [ "//" [user[:password]@]host[:port] ]
 * path [ "?" query ] [ "#" fragment ]. It does not translate anything — a URL is not a FILE PATH and
 * nothing here joins or normalises beyond what the RFC says.
 *
 * RELATIVE RESOLUTION IS HERE, and it is RFC 3986 §5.2's algorithm in full — dot-segment removal
 * included — because that is the operation a base URL exists FOR, and because F8 refused it by name.
 * The reference's own scheme wins if it has one; an authority wins next; an empty path keeps the
 * base's path AND query; a path starting with "/" replaces the base's; and anything else MERGES with
 * the base's path's last segment before the dot segments are removed.
 *
 * A PRIVATE HEADER: it is not staged to the guest and is not part of the public surface — the
 * library's own translation units import it, and nothing else may.
 */

#ifndef FOUNDATION_FNURLCOMPONENTS_H
#define FOUNDATION_FNURLCOMPONENTS_H

#import <Foundation/NSObject.h>

@class NSNumber;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@interface FNURLComponents : NSObject <NSCopying>
{
	NSString *_scheme;
	NSString *_user;
	NSString *_password;
	NSString *_host;
	NSString *_port;
	NSString *_path;
	NSString *_query;
	NSString *_fragment;
}

+ (nullable instancetype)componentsWithString:(NSString *)URLString;
+ (nullable instancetype)componentsWithURL:(NSURL *)url
		    resolvingAgainstBaseURL:(BOOL)resolve;
- (nullable instancetype)initWithString:(NSString *)URLString;
- (nullable instancetype)initWithURL:(NSURL *)url resolvingAgainstBaseURL:(BOOL)resolve;

/* THE WHOLE THING, as it was parsed or as it has been edited. */
- (nullable NSString *)string;
- (nullable NSURL *)URL;

/* THE FIELDS, each in its two spellings where the two can differ. */
- (nullable NSString *)scheme;
- (void)setScheme:(nullable NSString *)scheme;
- (nullable NSString *)user;
- (void)setUser:(nullable NSString *)user;
- (nullable NSString *)password;
- (void)setPassword:(nullable NSString *)password;
- (nullable NSString *)host;
- (void)setHost:(nullable NSString *)host;
- (nullable NSNumber *)port;
- (void)setPort:(nullable NSNumber *)port;
- (nullable NSString *)path;
- (void)setPath:(nullable NSString *)path;
- (nullable NSString *)query;
- (void)setQuery:(nullable NSString *)query;
- (nullable NSString *)fragment;
- (void)setFragment:(nullable NSString *)fragment;

- (nullable NSString *)percentEncodedUser;
- (nullable NSString *)percentEncodedPassword;
- (nullable NSString *)percentEncodedHost;
- (nullable NSString *)percentEncodedPath;
- (nullable NSString *)percentEncodedQuery;
- (nullable NSString *)percentEncodedFragment;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNURLCOMPONENTS_H */
