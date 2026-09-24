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
 *   - the LOADING system: NSURLSession, NSURLConnection,
 *     -startAccessingSecurityScopedResource, +URLByResolvingBookmarkData:... —
 *     a URL here is a VALUE, not a door to I/O. NSURLRequest/NSURLResponse/NSHTTPURLResponse WERE on
 *     this line and are not any more (W7 slice 1, §46): they arrive as VALUES — a description of an
 *     exchange and its answer's metadata — while the class that PERFORMS the exchange stays refused.
 *     NSURLProtocol WERE on this line too and is not any more either (W7 slice 2a): it is the SEAM a
 *     transport ATTACHES to, not a transport, and shipping it is what lets slice 2c's libcurl bridge be
 *     an ordinary subclass of it. What stays refused is the thing that performs an exchange on its own:
 *     NSURLSession, NSURLConnection and -loadRequest:;
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

@class NSArray;
@class NSMutableDictionary;
@class NSDictionary;
@class NSError;
@class NSString;
@class NSNumber;

NS_ASSUME_NONNULL_BEGIN

/* The key type is an opaque string, and the keys' VALUES are their own names, which is this library's
 * standing spelling for a constant whose name Apple publishes and whose string nobody's program reads
 * (§11.6.1 D2, the same rule NSFileManager's key names follow). The typedefs come BEFORE the interface
 * that breathes them, which is what using them in a declaration requires. */
typedef NSString *NSURLResourceKey;

/* Apple's nine file resource types, whose values ARE their own names here (D2, as above). */
typedef NSString *NSURLFileResourceType;

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
	NSMutableDictionary *_cachedResourceValues;	/* the resource-value cache, built lazily (W8 6a) */
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

/* ---- ACCESSING RESOURCE VALUES (W8 slice 6a) ----------------------------------------------------
 *
 * THE FILE'S PROPERTIES AS VALUES, keyed by Cocoa's own key names, so a caller asks ONE question and
 * gets an OBJECT back rather than calling lstat(2) and decoding a bitfield - the same reason
 * NSFileManager's -attributesOfItemAtPath:error: exists, from the other side.
 *
 * THE CACHE IS PART OF THE CONTRACT AND NOT AN OPTIMISATION. Apple documents that a URL OBJECT caches
 * the resource values it has read, that the cache lives until the object goes away, and that
 * -removeCachedResourceValueForKey:/ -removeAllCachedResourceValues take it back out - which is what
 * makes the two reads of a file that CHANGED between them the observable difference this unit is
 * probed on. -setTemporaryResourceValue:forKey: is the other side of the same fact: a value that is
 * NOT on disk and lives only in the object's cache.
 *
 * WHAT IS ANSWERED HERE, AND WHAT IS NOT: a key is answered when this substrate HAS the fact behind it
 * (the stat family, access(2), the path itself), and a key this substrate has nothing behind is
 * registered as open rather than answered with a guess - NSURLFileSecurityKey among them, whose value
 * would have to be the CFFileSecurity facts §11.6.1 D13 records as absent here.
 */
- (BOOL)getResourceValue:(id _Nullable * _Nullable)value
		  forKey:(NSURLResourceKey)key
		   error:(NSError ** _Nullable)error;
/* THE COLLECTIONS ARE THIS LIBRARY'S OWN AND CARRY NO GENERIC PARAMETERS (measured: `NSArray` here has
 * no type argument), so the shape is Cocoa's door with this tree's container spelling. */
- (nullable NSDictionary *)resourceValuesForKeys:(NSArray *)keys
					   error:(NSError ** _Nullable)error;
/* ---- SETTING RESOURCE VALUES, WHERE A REFUSAL IS A NO-OP AND NOT AN ERROR (W8 slice 6b) ---------
 *
 * APPLE'S OWN SENTENCE FOR BOTH SETTERS, AND IT DECIDES THE DESIGN: "Attempts to set a read-only
 * resource property or to set a resource property that is not supported by the resource are IGNORED and
 * are NOT CONSIDERED ERRORS." So the honest refusal here is silence - the getter names what it does not
 * have, and the setter does nothing about it - and this library does not decorate Apple's contract with
 * an error of its own invention.
 *
 * WHAT THIS SUBSTRATE CAN WRITE IS ONE KEY TODAY: NSURLContentModificationDateKey, through
 * NSFileManager's -setAttributes:ofItemAtPath:error: - a DELEGATION rather than new machinery, and the
 * same fact seen from the URL side. Everything else this header declares is read-only here, and the
 * keys Apple documents as settable on its own systems whose substrate this system does not have (the
 * file security object, the quarantine properties, the tags, the hidden-extension bit, the immutables)
 * are simply not answered, which is the same silence by a shorter route.
 *
 * AND THE ONE ERROR SHAPE THAT IS APPLE'S OWN: if a write reaches the file system and FAILS, the
 * dictionary form answers NO with an error whose userInfo carries NSURLKeysOfUnsetValuesKey, whose value
 * is "an array of [URLResourceKey] objects" - the keys that were not set. Apple's page for the method
 * says "the resource values"; its page for the KEY says the keys, and the key's own page is the specific
 * one, so the keys are what this library reports.
 *
 * AND A VALUE THE KEY CANNOT HOLD IS A CALLER ERROR: a value that is not an NSDate for the modification
 * date fails the call rather than being written or silently ignored - the one case where this door
 * answers NO for a reason of its own, named at the check that writes it.
 */
- (BOOL)setResourceValue:(nullable id)value
		  forKey:(NSURLResourceKey)key
		   error:(NSError ** _Nullable)error;
- (BOOL)setResourceValues:(NSDictionary *)keyedValues error:(NSError ** _Nullable)error;

- (BOOL)checkResourceIsReachableAndReturnError:(NSError ** _Nullable)error;
- (void)removeCachedResourceValueForKey:(NSURLResourceKey)key;
- (void)removeAllCachedResourceValues;
- (void)setTemporaryResourceValue:(nullable id)value forKey:(NSURLResourceKey)key;

@end

extern NSURLResourceKey const NSURLNameKey;
extern NSURLResourceKey const NSURLLocalizedNameKey;
extern NSURLResourceKey const NSURLPathKey;
extern NSURLResourceKey const NSURLCanonicalPathKey;
extern NSURLResourceKey const NSURLIsRegularFileKey;
extern NSURLResourceKey const NSURLIsDirectoryKey;
extern NSURLResourceKey const NSURLIsSymbolicLinkKey;
extern NSURLResourceKey const NSURLIsReadableKey;
extern NSURLResourceKey const NSURLIsWritableKey;
extern NSURLResourceKey const NSURLIsExecutableKey;
extern NSURLResourceKey const NSURLIsHiddenKey;
extern NSURLResourceKey const NSURLFileSizeKey;
extern NSURLResourceKey const NSURLFileAllocatedSizeKey;
extern NSURLResourceKey const NSURLTotalFileSizeKey;
extern NSURLResourceKey const NSURLTotalFileAllocatedSizeKey;
extern NSURLResourceKey const NSURLLinkCountKey;
extern NSURLResourceKey const NSURLContentModificationDateKey;
extern NSURLResourceKey const NSURLContentAccessDateKey;
extern NSURLResourceKey const NSURLAttributeModificationDateKey;
extern NSURLResourceKey const NSURLFileIdentifierKey;
extern NSURLResourceKey const NSURLFileResourceIdentifierKey;
extern NSURLResourceKey const NSURLFileResourceTypeKey;
extern NSURLResourceKey const NSURLParentDirectoryURLKey;
/* THE ERROR'S OWN KEY: the one Apple publishes for the setter-set's failure report. */
extern NSURLResourceKey const NSURLKeysOfUnsetValuesKey;

extern NSURLFileResourceType const NSURLFileResourceTypeRegular;
extern NSURLFileResourceType const NSURLFileResourceTypeDirectory;
extern NSURLFileResourceType const NSURLFileResourceTypeSymbolicLink;
extern NSURLFileResourceType const NSURLFileResourceTypeSocket;
extern NSURLFileResourceType const NSURLFileResourceTypeCharacterSpecial;
extern NSURLFileResourceType const NSURLFileResourceTypeBlockSpecial;
extern NSURLFileResourceType const NSURLFileResourceTypeNamedPipe;
extern NSURLFileResourceType const NSURLFileResourceTypeUnknown;

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
