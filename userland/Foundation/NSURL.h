/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURL — a URL as a VALUE. docs/design/foundation-plan.md, F8.
 *
 * THE BOUNDARY IS A RULE, NOT A TABLE, one level up from NSLocale's: a URL is a
 * SYNTAX (RFC 3986 — scheme, authority, path, query, fragment) plus two rules
 * this system already has, the FSH's slash-separated absolute paths and UTF-8.
 * All of that ships. What does not ship is anything needing a STACK, and the
 * header names each refusal below rather than half-answering it.
 *
 * PARSING IS THE REFUSAL. `+URLWithString:` answers nil for a string that is not
 * a URL rather than silently re-encoding it, and a scheme is REQUIRED: a relative
 * URL needs a base to be resolved against (RFC 3986 §5), and that merge is
 * refused by name. Percent-ENCODING is not reimplemented here — NSString already
 * has the rule (F1: -stringByAddingPercentEncodingWithAllowedCharacters:) — but
 * the two places this class must apply it are the file-path rules below, because
 * an FSH path may contain a space.
 *
 * FILE URLs ARE THE FSH'S PATHS. `+fileURLWithPath:` takes a slash-separated
 * ABSOLUTE path (anything else answers nil) and produces `file:///System/...`
 * with an empty authority; `-path` reverses it, percent-decoding on the way. The
 * round trip is a RULE and the probe asserts it, spaces included.
 *
 * REFUSED BY NAME, each needing something this library does not ship:
 *   - the LOADING system: NSURLSession, NSURLConnection, NSURLRequest,
 *     -startAccessingSecurityScopedResource, +URLByResolvingBookmarkData:... —
 *     a URL here is a VALUE, not a door to I/O;
 *   - NSURLComponents / NSURLQueryItem — the STRUCTURED form is its own family
 *     and a later slice;
 *   - NSFileManager and every filesystem QUERY
 *     (-checkResourceIsReachableAndReturnError:, -resourceValuesForKeys:error:,
 *     -getFileSystemRepresentation:maxLength:) — a URL is a NAME, and the file
 *     APIs that DO exist take paths;
 *   - general RELATIVE RESOLUTION (+URLWithString:relativeToURL:): RFC 3986 §5
 *     is a merge algorithm with its own test vectors.
 */

#ifndef FOUNDATION_NSURL_H
#define FOUNDATION_NSURL_H

#import <Foundation/NSObject.h>

@class NSString;
@class NSNumber;

NS_ASSUME_NONNULL_BEGIN

@interface NSURL : NSObject <NSCopying>
{
	NSString *_absoluteString;	/* what it was made from, always absolute */
	NSString *_scheme;		/* never nil: a scheme is required */
	NSString *_host;		/* nil when there is no authority (a file URL) */
	NSString *_user;
	NSNumber *_port;		/* nil when absent */
	NSString *_path;		/* never nil; empty for an opaque URL */
	NSString *_query;		/* nil when absent */
	NSString *_fragment;		/* nil when absent */
	BOOL _isFile;
}

/* nil when the string is not an absolute URL with a valid scheme. */
+ (nullable NSURL *)URLWithString:(NSString *)string;
/* THE RELATIVE DOOR, refused by name until RFC 3986 §5.2's resolution existed. `baseURL` is nullable
 * because a reference resolved against nothing is still as far as the algorithm gets. */
+ (nullable NSURL *)URLWithString:(NSString *)string relativeToURL:(nullable NSURL *)baseURL;
/* nil when the path is not slash-separated and absolute, which is what the FSH
 * has: there are no relative paths to resolve. */
+ (nullable NSURL *)fileURLWithPath:(NSString *)path;

- (nullable id)initWithString:(NSString *)string;
- (nullable id)initFileURLWithPath:(NSString *)path;

/* The RFC 3986 parts. `scheme` and `path` are never nil (a scheme is required,
 * and a path is empty rather than absent); the rest are nil when the URL has no
 * such part — which for a FILE URL means no host, port, query or fragment. */
- (NSString *)scheme;
- (nullable NSString *)host;
- (nullable NSString *)user;
- (nullable NSNumber *)port;
- (NSString *)path;
- (nullable NSString *)query;
- (nullable NSString *)fragment;

/* The spellings. `-absoluteString` is what -description renders; an absolute URL's
 * `-relativeString` is the same string, because no relative URLs exist here. */
- (NSString *)absoluteString;
- (NSString *)relativeString;
- (BOOL)isFileURL;

/* Always self: there is nothing to resolve against, so every URL here is already
 * absolute (see +URLWithString:relativeToURL: in the refusal list). */
- (NSURL *)absoluteURL;

/* THE PATH ARITHMETIC, and each one is a rule over the path this URL already
 * has. -URLByAppendingPathExtension: answers nil when there is no path to extend,
 * which is Cocoa's contract as well. */
- (NSURL *)URLByAppendingPathComponent:(NSString *)component;
- (nullable NSURL *)URLByAppendingPathExtension:(NSString *)extension;
- (NSURL *)URLByDeletingLastPathComponent;
- (NSURL *)URLByDeletingPathExtension;

- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

/* Immutable, so copying returns self. */
- (id)copy;

@end

/*
 * THE PRIVATE HALF, folded in from fnurl.h: the declarations this library shares internally.
 * They are HERE because the public headers are now the only headers - what used to be a
 * private file two units imported is a section of the class's own header. The region is its
 * own one only when this point in the header is outside the header's own (a nested region
 * does not compile, and neither does an unclosed one).
 */
@class NSURL;
@class NSString;


/* RESOLVE `reference` AGAINST `base`, both as URL text, per RFC 3986 §5.2. A reference that is
 * already absolute comes back as itself (normalised); one with no base to resolve against is
 * answered as far as it can be, which is the same algorithm with the base's fields absent. */
NSURL * _Nullable FNURLResolveRelative(NSString *reference, NSString * _Nullable base);


/* THE BOOKMARK TYPES (2026-09-20). The METHODS that take them are in this header's
 * refusal list above — +URLByResolvingBookmarkData: and its creation counterpart need
 * the security-scope machinery this system does not have — but the TYPES are Apple's
 * API and cost nothing to declare, so a program that names them compiles. Names from
 * Apple's documentation index; values are ours (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSURLBookmarkCreationMinimalBookmark = 1 << 0,
	NSURLBookmarkCreationSuitableForBookmarkFile = 1 << 1,
	NSURLBookmarkCreationWithSecurityScope = 1 << 2,
	NSURLBookmarkCreationSecurityScopeAllowOnlyReadAccess = 1 << 3,
	NSURLBookmarkCreationWithoutImplicitSecurityScope = 1 << 4,
} NSURLBookmarkCreationOptions;

typedef enum {
	NSURLBookmarkResolutionWithoutUI = 1 << 0,
	NSURLBookmarkResolutionWithoutMounting = 1 << 1,
	NSURLBookmarkResolutionWithSecurityScope = 1 << 2,
	NSURLBookmarkResolutionWithoutImplicitStartAccessing = 1 << 3
} NSURLBookmarkResolutionOptions;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURL_H */
