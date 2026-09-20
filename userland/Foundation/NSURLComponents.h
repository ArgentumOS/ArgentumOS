/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLComponents / NSURLQueryItem — a URL as EIGHT FIELDS rather than one string. F13.15,
 * docs/design/foundation-plan.md §10.
 *
 * WHAT THE STRUCTURED FORM IS FOR, and why F8 refused it as a family of its own: NSURL answers the
 * questions a VALUE has to answer (equality, a string, a path). A components object answers the
 * questions an EDITOR has to answer — what is the host, change the host, what are the query items,
 * add one — and it does so without re-parsing a string each time or losing the difference between
 * what was written and what it means. That difference is the reason there are TWO accessors for most
 * fields: `-host` DECODES its percent escapes, and `-percentEncodedHost` is what the URL actually
 * carries.
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
 * WHAT IS NOT HERE, named: `-stringByAddingPercentEncoding…` (the components object RENDERS what it
 * was given rather than re-encoding it, which is the safe direction), `NSURLComponents`'s copy
 * semantics beyond NSCopying, and the deprecated `-queryItems`-less query API.
 */

#ifndef FOUNDATION_NSURLCOMPONENTS_H
#define FOUNDATION_NSURLCOMPONENTS_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSNumber;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* ONE ?name=value PAIR. A name with no "=" has a nil value, which is how a flag is spelled. */
@interface NSURLQueryItem : NSObject <NSCopying>
{
	NSString *_name;
	NSString *_value;
}

+ (instancetype)queryItemWithName:(NSString *)name value:(nullable NSString *)value;
- (instancetype)initWithName:(NSString *)name value:(nullable NSString *)value;

- (NSString *)name;
- (nullable NSString *)value;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

@interface NSURLComponents : NSObject <NSCopying>
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

/* THE QUERY AS PAIRS, which is what a caller usually wants and what a string never gives them. */
- (nullable NSArray *)queryItems;
- (void)setQueryItems:(nullable NSArray *)queryItems;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLCOMPONENTS_H */
