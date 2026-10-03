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
 *   - the LOADING system, in part: the THIN one-liner `-loadRequest:` and
 *     -startAccessingSecurityScopedResource, +URLByResolvingBookmarkData:... —
 *     a URL here is a VALUE, not a door to I/O. NSURLRequest/NSURLResponse/NSHTTPURLResponse WERE on
 *     this line and are not any more (W7 slice 1, §46): they arrive as VALUES — a description of an
 *     exchange and its answer's metadata. NSURLProtocol was on this line too and is not any more either
 *     (W7 slice 2a): it is the SEAM a transport ATTACHES to, not a transport, and shipping it is what
 *     lets slice 2c's libcurl bridge be an ordinary subclass of it. **AND `NSURLConnection` LEFT THIS
 *     LINE IN §62.25 — it is a PORTING TARGET — and `NSURLSession` NEVER STAYED REFUSED AT ALL: it
 *     shipped and then left the SURFACE entirely with the 10.2 cut (§63.159).** So the honest sentence is
 *     narrow: what a URL will not do here is FETCH ITSELF;
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
@class NSData;
@class NSMutableDictionary;
@class NSDictionary;
@class NSError;
@class NSString;
@class NSNumber;
@class NSURLHandle;

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
	NSString *_password;	/* nil when absent: the half of the userinfo after the first ':' (2026-09-30) */
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

/* ---- THE REST OF THE URL AS A VALUE (2026-09-30) ------------------------------------------------
 *
 * These are the "Accessing the Parts", "Creating", "Converting", "Querying" and the one pure
 * "Deprecated" doors WHOSE SUBSTRATE THIS TREE ALREADY HAS - a scheme, an authority, a path and an
 * FSH file path all ship here - so they are answered FROM THE PARSE rather than refused. Where
 * Apple's contract is ambiguous, or a value had to be chosen, the method's comment in NSURL.m says
 * so by name. The rows this slice DID NOT close (bookmarks, aliases, promised items, pasteboards,
 * the conformingToType/extensionForType pairs, file-REFERENCE URLs and the NSURLHandle-backed
 * deprecated I/O doors) are named in the header's refusal list and in the unit's report.
 *
 * `-fileSystemRepresentation` is the one place a URL crosses back into the C world: the FSH path as
 * bytes, NULL for anything that is not a file URL, exactly as Apple's page says.
 */

/* ACCESSING THE PARTS. `-baseURL` is ALWAYS nil here, and that is a FACT rather than a refusal:
 * +URLWithString:relativeToURL: RESOLVES into an absolute URL (FNURLResolveRelative), so no URL this
 * library builds remembers a base. `-user`/`-password` are the two halves of the userinfo, split at
 * the FIRST ':' (the split RFC 3986 §3.2.1 defines). */
- (nullable NSURL *)baseURL;
- (nullable NSString *)password;
- (nullable NSString *)relativePath;
- (NSString *)resourceSpecifier;
- (nullable NSString *)lastPathComponent;
- (nullable NSString *)pathExtension;
- (nullable NSArray *)pathComponents;
- (NSURL *)standardizedURL;
- (const char * _Nullable)fileSystemRepresentation;

/* CREATING. The data doors carry the UTF-8 SPELLING of the string (what -dataRepresentation
 * answers); the file doors add the FSH's directory slash and a C-string spelling. NONE of the
 * relativeToURL: file doors RESOLVES a relative path, because the FSH has none - the base is ignored
 * for an absolute path (Apple's own rule) and a relative one is refused (see NSURL.m). */
- (nullable instancetype)initWithString:(NSString *)string relativeToURL:(nullable NSURL *)baseURL;
- (nullable instancetype)initWithString:(NSString *)string encodingInvalidCharacters:(BOOL)encodingInvalidCharacters;
+ (nullable instancetype)URLWithString:(NSString *)string encodingInvalidCharacters:(BOOL)encodingInvalidCharacters;
- (nullable instancetype)initWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)URLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL;
- (nullable instancetype)initAbsoluteURLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)absoluteURLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL;
- (nullable NSData *)dataRepresentation;
- (nullable instancetype)initFileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory;
- (nullable instancetype)initFileURLWithPath:(NSString *)path relativeToURL:(nullable NSURL *)baseURL;
- (nullable instancetype)initFileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory;
+ (nullable instancetype)fileURLWithPath:(NSString *)path relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)fileURLWithPathComponents:(NSArray *)components;
- (nullable instancetype)initFileURLWithFileSystemRepresentation:(const char *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL;
+ (nullable instancetype)fileURLWithFileSystemRepresentation:(const char *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL;
- (BOOL)getFileSystemRepresentation:(char *)buffer maxLength:(NSUInteger)maxLength;

/* MODIFYING AND CONVERTING. -URLByAppendingPathComponent:isDirectory: is the slash-aware form of the
 * appending door above; the "file" doors below are the spellings a PATH URL has. */
- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)isDirectory;
- (nullable NSURL *)filePathURL;
- (BOOL)hasDirectoryPath;
- (NSURL *)URLByResolvingSymlinksInPath;
- (NSURL *)URLByStandardizingPath;

/* QUERYING. This system has NO file-REFERENCE namespace (Apple's `file:/.file/id=…`), so a URL here is
 * never one and -isFileReferenceURL answers NO rather than pretending (the ground is at NSURL.m). */
- (BOOL)isFileReferenceURL;
- (nullable NSURL *)fileURL;

/* DEPRECATED (Apple 10.4). `-initWithScheme:host:path:` is PURE - it builds a spelling and parses it,
 * touching nothing. The NSURLHandle-backed family is SPLIT rather than left wholly open: the three doors
 * below are closed as the DELEGATIONS they are (one transport, two spellings, NSURLHandle.m's own words),
 * because each is a VALUE fact that asks for NO fetch - a handle is CONSTRUCTED without one, and a property
 * that was SET reads back from the bag BEFORE NSURLHandle would load. `-resourceDataUsingCache:`,
 * `-loadResourceDataNotifyingClient:usingCache:` and `-setResourceData:` stay OPEN: observing the first two
 * needs a load (a live fetch), and the third writes a body no door of this class reads back. */
- (nullable instancetype)initWithScheme:(NSString *)scheme host:(nullable NSString *)host path:(NSString *)path;
- (nullable NSURLHandle *)URLHandleUsingCache:(BOOL)shouldUseCache;
- (nullable id)propertyForKey:(NSString *)propertyKey;
- (void)setProperty:(nullable id)propertyValue forKey:(NSString *)propertyKey;
/* DEPRECATED (Apple 10.4), AND PURE: the path's `;`-separated parameter string (RFC 2396 §3.3's `segment`
 * tail), taken RAW from the parse exactly as -query/-fragment take theirs. nil when the path has no ';'. */
- (nullable NSString *)parameterString;

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

/* ⚠⚠ THE SECURITY-SCOPED PAIR, DECLARED ON THE CLASS BECAUSE THAT IS WHERE THE CORPUS HAS THEM, AND WITH THEIR
 * CONTRACT READ RATHER THAN GUESSED (§63.96's instrument, first applied in §63.103):
 *
 *   "Given a NSURL CREATED BY RESOLVING A BOOKMARK DATA CREATED WITH SECURITY SCOPE, make the resource referenced by
 *    the url accessible to the process. Each call to startAccessingSecurityScopedResource that returns YES must be
 *    balanced with a call to stopAccessingSecurityScopedResource… Calls to start and stop accessing the resource are
 *    REFERENCE COUNTED and may be NESTED."
 *
 * ⚠ SO THE DOOR ANSWERS A QUESTION — WHETHER ACCESS WAS GRANTED — AND THE HONEST ANSWER HERE IS NO: this system has no
 * sandbox and no security-scoped bookmark to resolve, so there is nothing to grant. **THAT IS A CAPABILITY ANSWER AND
 * NOT A REFUSAL** (§63.99's shape: `+canInflectLanguage:` answers NO rather than raising). And
 * `-stopAccessingSecurityScopedResource` "removes one 'accessing' reference… When all references are removed, it
 * revokes the access" — with none ever granted there is nothing to remove and nothing to revoke. */
- (BOOL)startAccessingSecurityScopedResource;
- (void)stopAccessingSecurityScopedResource;
/* ⚠⚠ TWO OF THE DEPRECATED RESOURCE-DATA TRIO, AND THEY ARE **OWED** RATHER THAN SKIPPED: the user's policy of
 * 2026-09-26 un-deprecated everything Apple had deprecated, so an `API_DEPRECATED` on the other side changes what a row
 * means here and not whether it is ours.
 *
 * ⚠ AND BOTH CONTRACTS ARE APPLE'S OWN COMMENTS, READ FROM THE CORPUS (§63.96's instrument):
 *   `-resourceDataUsingCache:` — "BLOCKS to load the data if necessary. If shouldUseCache is YES, then if an
 *    equivalent URL has already been loaded and cached, its resource data will be returned immediately. If
 *    shouldUseCache is NO, a new load will be started";
 *   `-setResourceData:` — "These attempt to WRITE the given arguments for the resource specified by the URL; they
 *    RETURN SUCCESS OR FAILURE".
 *
 * ⚠ AND THE THIRD — `-loadResourceDataNotifyingClient:usingCache:` — IS DELIBERATELY NOT HERE: its contract is an
 * ASYNCHRONOUS load that registers a CLIENT for an informal protocol's notifications, and "only one such background
 * load can proceed at a time". **That needs the client-notification path measured before it is written, rather than
 * assumed — which is the mistake this campaign has paid for six times.** */
- (nullable NSData *)resourceDataUsingCache:(BOOL)shouldUseCache;
- (BOOL)setResourceData:(NSData *)data;
/* ⚠⚠ THE LAST OF THE DEPRECATED RESOURCE-DATA TRIO, AND IT IS IMPLEMENTED RATHER THAN REFUSED, ON A MEASUREMENT
 * THAT CHANGED THE ANSWER (§63.110's instrument, taken before a line was written):
 *
 *   APPLE'S CONTRACT: "STARTS AN ASYNCHRONOUS LOAD of the data, REGISTERING DELEGATE to receive notification. Only one
 *   such background load can proceed at a time." The delegate is an INFORMAL protocol — the corpus declares it as
 *   `@interface NSObject (NSURLClient)` with `-URLResourceDidFinishLoading:`, `-URLResourceDidCancelLoading:` and
 *   `-URL:resourceDidFailLoadingWithReason:`.
 *
 * ⚠⚠ AND THE MEASUREMENT: THIS LIBRARY'S URL LOADING IS SYNCHRONOUS AND ITS CLIENT MESSAGES ARE NOWHERE — the three
 * names appear in no header except ONE (`NSURLHandle.h` carries `resourceDidFailLoadingWithReason`). **SO THE DOOR'S
 * PROMISE IS NOT IMPOSSIBLE THE WAY A BOOKMARK IS: THE DATA IS PERFECTLY OBTAINABLE AND ONLY THE TIMING DIFFERS.**
 *
 * ⚠ AND THAT IS WHY IT IS IMPLEMENTED WITH A STATED DEVIATION RATHER THAN REFUSED: **THE LOAD IS PERFORMED
 * SYNCHRONOUSLY AND THE CLIENT IS NOTIFIED BEFORE THE DOOR RETURNS.** Every observable of the contract holds except
 * when the notification arrives — including “only one such background load at a time”, which is trivially true for
 * a load that does not outlive the call (§11.6.1 D2: our reading, written down). **A REFUSAL WOULD HAVE BEEN THE
 * WRONG SHAPE HERE: it is what a MISSING CAPABILITY gets, and this is a missing THREAD.** */
- (void)loadResourceDataNotifyingClient:(id)client usingCache:(BOOL)shouldUseCache;
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
/* §62.103: three more resource keys, each valued as its own name. The type identifier and the file security
 * are facts about a LOCAL file (which is all this system has); the 1024x1024 thumbnail is a SIZE a snapshot can
 * be asked for, declared because Apple declares it and a caller may spell it. */
extern NSURLResourceKey const NSURLTypeIdentifierKey;
extern NSURLResourceKey const NSURLFileSecurityKey;
extern NSURLResourceKey const NSThumbnail1024x1024SizeKey;
extern NSURLResourceKey const NSURLFileResourceIdentifierKey;
extern NSURLResourceKey const NSURLFileResourceTypeKey;
extern NSURLResourceKey const NSURLParentDirectoryURLKey;
/* THE ERROR'S OWN KEY: the one Apple publishes for the setter-set's failure report. */
extern NSURLResourceKey const NSURLKeysOfUnsetValuesKey;

/* ---- THE VOLUME KEYS THIS SUBSTRATE CAN ANSWER (W8 slice 6c) ---------------------------------------
 *
 * A VOLUME KEY ASKED OF A FILE URL IS A QUESTION ABOUT THE VOLUME HOLDING IT, and the family is 49 rows
 * in the ledger whose answers are NOT uniform: several of Apple's `NSURLVolumeSupports…` questions are
 * TRUE of this system's file systems (AGFS is case-sensitive, it has symlinks and persistent inode
 * identifiers) and several have no substrate at all (no journal, no compression, no cloning, no file
 * vault, no automount, no ejection). **A blanket answer would therefore be a LIE for the ones that are
 * true, so this slice answers only the keys whose fact this system HAS, and leaves the rest open with
 * their grounds recorded in §60 rather than guessing on either side.**
 *
 * WHERE A KEY'S VALUE IS A CLAIM ABOUT THE VOLUME, THE PROBE PROVES THE CLAIM: the three supports-keys
 * below are asserted together with something the fixture DOES that could only work if the claim held (two
 * names differing only in case, an identifier read back from a second URL, a link that resolves).
 */
extern NSURLResourceKey const NSURLVolumeTotalCapacityKey;
extern NSURLResourceKey const NSURLVolumeAvailableCapacityKey;
extern NSURLResourceKey const NSURLVolumeIsLocalKey;
extern NSURLResourceKey const NSURLVolumeIsReadOnlyKey;
extern NSURLResourceKey const NSURLVolumeSupportsCaseSensitiveNamesKey;
extern NSURLResourceKey const NSURLVolumeSupportsPersistentIDsKey;
extern NSURLResourceKey const NSURLVolumeSupportsSymbolicLinksKey;

/* ---- THE MOUNT TABLE'S KEYS (W8p, slice 6e) --------------------------------------------------------
 *
 * THIS SYSTEM PUBLISHES ITS MOUNTS: `/proc/mounts` prints `device mountpoint fstype rw|ro 0 0` for every
 * mount that is not a kernel-internal one (`fs/procfs/data.c`, measured), which is what these keys read.
 * AND ONE OF THEM IS AN IMPROVEMENT ON SLICE 6c: `NSURLVolumeIsReadOnlyKey` was answered there by probing a
 * WRITE and looking for EROFS (the kernel refuses a write on a read-only file system with that errno); the
 * table says it outright, so it is read from the FLAG FIELD now and the probe asserts the two agree where
 * both can be asked.
 *
 * A URL'S VOLUME IS THE LONGEST MOUNT POINT THAT PREFIXES ITS PATH, which is what makes `/proc/version` sit
 * on the procfs volume rather than on the root that contains the mount point - the probe asserts exactly
 * that, because a longest-prefix rule is the one thing a naive "first matching mount" gets wrong.
 */
extern NSURLResourceKey const NSURLVolumeNameKey;
extern NSURLResourceKey const NSURLVolumeLocalizedNameKey;
extern NSURLResourceKey const NSURLVolumeIdentifierKey;
extern NSURLResourceKey const NSURLVolumeURLKey;
extern NSURLResourceKey const NSURLVolumeTypeNameKey;
extern NSURLResourceKey const NSURLVolumeIsRootFileSystemKey;
extern NSURLResourceKey const NSURLVolumeResourceCountKey;
extern NSURLResourceKey const NSURLVolumeSupportsVolumeSizesKey;
extern NSURLResourceKey const NSURLVolumeIsMountTriggerKey;
extern NSURLResourceKey const NSURLIsVolumeKey;

/* AND THE OPTION SET IS NSFileManager's, NOT THIS CLASS'S: `NSVolumeEnumerationOptions` is declared in
 * NSFileManager.h (with the two members Apple publishes) and this slice would have INVENTED a duplicate
 * type here - which the ledger stopped, because a measured check of the tree showed the type already
 * shipped. `NSVolumeEnumerationSkipHiddenVolumes` is CARRIED and acts on nothing: this system has no hidden
 * volumes, since the table lists what is mounted. */

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

/* THE PRIVATE HALF THAT IS METHODS: a CATEGORY, because a bare method declaration has no @interface to
 * live in, and this point of the header is already inside its own NS_ASSUME_NONNULL region (a nested one
 * does not compile) - the same shape NSFileWrapper uses for its private doors. */
@interface NSURL (FNPrivate)
/* PUT A VALUE THE CALLER HAS ALREADY READ INTO THIS URL'S CACHE - which is what "prefetched" means on
 * Apple's `includingPropertiesForKeys:` doors: the URL answers from what it was GIVEN rather than asking
 * the disk again, so a change made after the enumeration is not seen until the cache is cleared. It is the
 * same cache the measured values go into, entered from the other side. */
- (void)fnPrefetchValue:(nullable id)value forKey:(NSURLResourceKey)key;
@end


/* RESOLVE `reference` AGAINST `base`, both as URL text, per RFC 3986 §5.2. A reference that is
 * already absolute comes back as itself (normalised); one with no base to resolve against is
 * answered as far as it can be, which is the same algorithm with the base's fields absent. */
NSURL * _Nullable FNURLResolveRelative(NSString *reference, NSString * _Nullable base);


/* THE BOOKMARK TYPES (2026-09-20). The METHODS that take them are in this header's
 * refusal list above — +URLByResolvingBookmarkData: and its creation counterpart need
 * the security-scope machinery this system does not have — but the TYPES are Apple's
 * API and cost nothing to declare, so a program that names them compiles. Names from
 * Apple's documentation index; values are ours (§11.6.1 D2, see NSFileManager.h). */

/* ---- THE KEY MASSES WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE (W8p, slice 6f) ------------------------

 *
 * THESE ARE DECLARED AND THEY ARE RECOGNISED, AND THE ANSWER THEY GET IS APPLE'S OWN: the getter's page says
 * "If this method returns [YES] and the value is populated with [nil], it means that the resource property is
 * NOT AVAILABLE for the specified resource, and that no errors occurred when determining that the resource
 * property was unavailable." A ubiquitous item, a thumbnail, a quarantine flag, an icon, a label, a UTI, a
 * tag list and a file-protection level are things this system does not have, so their keys are ANSWERED NIL -
 * which is not the same as an error and not the same as a fabricated value. A program written for Apple
 * compiles, asks, and is told the truth.
 *
 * EXACTLY ONE OF THEM HAS A PLAIN FACT FOR AN ANSWER, and it is the one the others hang from:
 * `NSURLIsUbiquitousItemKey` answers NO - the item is not a ubiquitous item, because there is no cloud to be
 * ubiquitous WITH - so a caller can ask that question and get an answer rather than an absence.
 *
 * AND THE VALUES ARE OURS IN NUMBER (§11.6.1 D2): the names are Apple's, and the string values are the names
 * themselves, this library's standing spelling (the same rule NSFileManager's and NSXMLParser's constants
 * follow). */
/* THE TYPES THE VALUES BELOW ARE SPELLED WITH. Apple publishes some of them in the families this slice also
 * closes, so they are declared here with the values that use them rather than left to an earlier group. */
typedef NSString *NSURLFileProtectionType;	/* the protection LEVELS’ type */




/* ---- THE KEY MASSES WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE (W8p, slice 6f) ------------------------
 *
 * THESE ARE DECLARED AND RECOGNISED, AND THE ANSWER THEY GET IS APPLE'S OWN: the getter's page says "If this
 * method returns [YES] and the value is populated with [nil], it means that the resource property is NOT
 * AVAILABLE for the specified resource, and that no errors occurred when determining that the resource
 * property was unavailable." A ubiquitous item, a thumbnail, a quarantine flag, an icon, a label, a UTI, a
 * tag list and a file-protection level are things this system does not have, so their keys are ANSWERED NIL -
 * which is neither an error nor a fabricated value. A program written for Apple compiles, asks, and is told.
 *
 * EXACTLY ONE HAS A PLAIN FACT FOR AN ANSWER, and it is the one the others hang from:
 * `NSURLIsUbiquitousItemKey` answers NO - nothing here is a ubiquitous item, because there is no cloud to be
 * ubiquitous WITH.
 *
 * AND THE VALUES ARE OURS IN NUMBER (§11.6.1 D2): the names are Apple's and the strings are the names, this
 * library's standing spelling for a constant whose name is published and whose value is not.
 */
typedef NSString *NSURLThumbnailDictionaryItem;
typedef NSString *NSURLUbiquitousItemDownloadingStatus;
typedef NSString *NSURLUbiquitousSharedItemPermissions;
typedef NSString *NSURLUbiquitousSharedItemRole;
extern NSURLResourceKey const NSURLAddedToDirectoryDateKey;
extern NSURLResourceKey const NSURLApplicationIsScriptableKey;
extern NSURLResourceKey const NSURLContentTypeKey;
extern NSURLResourceKey const NSURLCreationDateKey;
extern NSURLResourceKey const NSURLCustomIconKey;
extern NSURLResourceKey const NSURLDocumentIdentifierKey;
extern NSURLResourceKey const NSURLEffectiveIconKey;
extern NSURLResourceKey const NSURLGenerationIdentifierKey;
extern NSURLResourceKey const NSURLHasHiddenExtensionKey;
extern NSURLResourceKey const NSURLIsExcludedFromBackupKey;
extern NSURLResourceKey const NSURLIsSystemImmutableKey;
extern NSURLResourceKey const NSURLThumbnailDictionaryKey;
extern NSURLResourceKey const NSURLThumbnailKey;
extern NSURLResourceKey const NSURLFileProtectionKey;

/* ---- THE SUBSTRATE-MEASURED KEYS (W8p, slice 6g) ---------------------------------------------------
 *
 * EACH OF THESE WAS ANSWERED FROM A MEASUREMENT TAKEN IN THE GUEST, NOT FROM A GUESS, and the measurement is
 * named per key:
 *
 *   - `NSURLIsSparseKey` - a file with a hole is CHARGED IN FULL by this file system (measured: one byte at 0,
 *     one at 1 MiB, `size=1048577 allocated=1049088`), so the general test (`allocated < size`) answers NO;
 *   - `NSURLMayHaveExtendedAttributesKey` - MEASURED AT THE ITEM: `setxattr`/`listxattr`/`getxattr` all succeed
 *     on AGFS and `listxattr` fails with EOPNOTSUPP on procfs, so this one key answers YES and NO on the same
 *     machine;
 *   - `NSURLPreferredIOBlockSizeKey` - the volume's own block size from `statfs` (1024 on the root, 4096 on
 *     procfs);
 *   - `NSURLVolumeSupportsHardLinksKey` - PROVED by the probe: two names, one inode, a link count of 2;
 *   - `NSURLVolumeSupportsExclusiveRenamingKey` - `renameat2(RENAME_NOREPLACE)` answers ENOSYS;
 *   - `NSURLDirectoryEntryCountKey` - the count of the entries a listing returns (measured two-sidedly: 0 for
 *     an empty directory, n for n);
 *   - `NSURLIsPurgeableKey`, `NSURLIsAliasFileKey`, `NSURLVolumeIsEncryptedKey` - NO, each a TRUE statement
 *     about the item with its ground named: nothing here evicts file content, the macOS alias format does not
 *     exist, and no volume is encrypted;
 *   - AND SEVEN THAT ARE UNAVAILABLE: nil, which is Apple's own "the resource property is NOT AVAILABLE for
 *     the specified resource, and no errors occurred". `NSURLVolumeCreationDateKey` (the file system records
 *     no volume creation time), `NSURLFileContentIdentifierKey` (content identifiers identify clone-shared
 *     content and there is no cloning), `NSURLMayShareFileContentKey` (no sharing substrate),
 *     `NSURLIsPackageKey`/`NSURLIsApplicationKey` (package identity is a Finder/LaunchServices notion; the
 *     build stages `/Applications` EMPTY and the kernel has no bundle marker - measured), and
 *     `NSURLVolumeIsRemovableKey`/`...IsEjectableKey` (the device tree publishes removable media as device
 *     CLASSES - `Disk/USB`, `Disk/Floppy` - but never associates a MOUNT with its device node, so a USB
 *     stick and a fixed disk are indistinguishable through it: a NAMED limitation of the substrate, not a
 *     NO we could stand behind).
 */
extern NSURLResourceKey const NSURLDirectoryEntryCountKey;
extern NSURLResourceKey const NSURLFileContentIdentifierKey;
extern NSURLResourceKey const NSURLIsAliasFileKey;
extern NSURLResourceKey const NSURLIsApplicationKey;
extern NSURLResourceKey const NSURLIsPackageKey;
extern NSURLResourceKey const NSURLIsPurgeableKey;
extern NSURLResourceKey const NSURLIsSparseKey;
extern NSURLResourceKey const NSURLMayHaveExtendedAttributesKey;
extern NSURLResourceKey const NSURLMayShareFileContentKey;
extern NSURLResourceKey const NSURLPreferredIOBlockSizeKey;
extern NSURLResourceKey const NSURLVolumeCreationDateKey;
extern NSURLResourceKey const NSURLVolumeIsEjectableKey;
extern NSURLResourceKey const NSURLVolumeIsEncryptedKey;
extern NSURLResourceKey const NSURLVolumeIsRemovableKey;
extern NSURLResourceKey const NSURLVolumeSupportsExclusiveRenamingKey;
extern NSURLResourceKey const NSURLVolumeSupportsFileCloningKey;
extern NSURLResourceKey const NSURLVolumeSupportsHardLinksKey;
extern NSURLResourceKey const NSURLVolumeSupportsSparseFilesKey;
extern NSURLResourceKey const NSURLIsUbiquitousItemKey;
extern NSURLResourceKey const NSURLIsUserImmutableKey;
extern NSURLResourceKey const NSURLLabelColorKey;
extern NSURLResourceKey const NSURLLabelNumberKey;
extern NSURLResourceKey const NSURLLocalizedLabelKey;
extern NSURLResourceKey const NSURLLocalizedTypeDescriptionKey;
extern NSURLResourceKey const NSURLQuarantinePropertiesKey;
extern NSURLResourceKey const NSURLTagNamesKey;
extern NSURLResourceKey const NSURLUbiquitousItemContainerDisplayNameKey;
extern NSURLResourceKey const NSURLUbiquitousItemDownloadRequestedKey;
extern NSURLResourceKey const NSURLUbiquitousItemDownloadingErrorKey;
extern NSURLResourceKey const NSURLUbiquitousItemDownloadingStatusKey;
extern NSURLResourceKey const NSURLUbiquitousItemHasUnresolvedConflictsKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsDownloadingKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsExcludedFromSyncKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsSharedKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsSyncPausedKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsUploadedKey;
extern NSURLResourceKey const NSURLUbiquitousItemIsUploadingKey;
extern NSURLResourceKey const NSURLUbiquitousItemSupportedSyncControlsKey;
extern NSURLResourceKey const NSURLUbiquitousItemUploadingErrorKey;
extern NSURLResourceKey const NSURLUbiquitousSharedItemCurrentUserPermissionsKey;
extern NSURLResourceKey const NSURLUbiquitousSharedItemCurrentUserRoleKey;
extern NSURLResourceKey const NSURLUbiquitousSharedItemMostRecentEditorNameComponentsKey;
extern NSURLResourceKey const NSURLUbiquitousSharedItemOwnerNameComponentsKey;
extern NSURLFileProtectionType const NSURLFileProtectionComplete;
extern NSURLFileProtectionType const NSURLFileProtectionCompleteUnlessOpen;
extern NSURLFileProtectionType const NSURLFileProtectionCompleteUntilFirstUserAuthentication;
extern NSURLFileProtectionType const NSURLFileProtectionCompleteWhenUserInactive;
extern NSURLFileProtectionType const NSURLFileProtectionNone;
extern NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusCurrent;
extern NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusDownloaded;
extern NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusNotDownloaded;
extern NSURLUbiquitousSharedItemPermissions const NSURLUbiquitousSharedItemPermissionsReadOnly;
extern NSURLUbiquitousSharedItemPermissions const NSURLUbiquitousSharedItemPermissionsReadWrite;
extern NSURLUbiquitousSharedItemRole const NSURLUbiquitousSharedItemRoleOwner;
extern NSURLUbiquitousSharedItemRole const NSURLUbiquitousSharedItemRoleParticipant;

typedef NSUInteger NSURLBookmarkFileCreationOptions;

typedef enum {
	NSURLBookmarkCreationMinimalBookmark = 1 << 0,
	NSURLBookmarkCreationSuitableForBookmarkFile = 1 << 1,
	NSURLBookmarkCreationWithSecurityScope = 1 << 2,
	NSURLBookmarkCreationSecurityScopeAllowOnlyReadAccess = 1 << 3,
	NSURLBookmarkCreationWithoutImplicitSecurityScope = 1 << 4,
	/* §62.103. APPLE'S NUMBER FOR THIS ONE IS 256 AND OURS IS SMALL: this enum's values are this tree's
	 * (the scheme above is 1<<0 … 1<<4), so the new name takes the next free bit rather than the number
	 * Apple happens to use — and a caller who compiles against this header gets a value the door here
	 * understands, which is the property that matters. */
	NSURLBookmarkCreationPreferFileIDResolution = 1 << 5,
} NSURLBookmarkCreationOptions;

typedef enum {
	NSURLBookmarkResolutionWithoutUI = 1 << 0,
	NSURLBookmarkResolutionWithoutMounting = 1 << 1,
	NSURLBookmarkResolutionWithSecurityScope = 1 << 2,
	NSURLBookmarkResolutionWithoutImplicitStartAccessing = 1 << 3
} NSURLBookmarkResolutionOptions;

/* ---- THE FILE-SYSTEM AND VOLUME RESOURCE KEYS (the coverage slice) --------------------------------
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE THE SAME STRINGS, because a resource key is a WIRE NAME: a
 * caller asks -getResourceValue:forKey: with it, and a key answering a different string would be a
 * different question. No key here carries any value other than its own name, and no door in this system
 * reads them yet - they ship as the vocabulary of a door that does not exist, and this comment says so
 * rather than implying one. */
extern NSString *const NSURLFileScheme;   /* @"file": a scheme, not a key - Apple declares it here */
extern NSURLResourceKey NSURLIsMountTriggerKey;
extern NSURLResourceKey NSURLVolumeAvailableCapacityForImportantUsageKey;
extern NSURLResourceKey NSURLVolumeAvailableCapacityForOpportunisticUsageKey;
extern NSURLResourceKey NSURLVolumeIsAutomountedKey;
extern NSURLResourceKey NSURLVolumeIsBrowsableKey;
extern NSURLResourceKey NSURLVolumeIsInternalKey;
extern NSURLResourceKey NSURLVolumeIsJournalingKey;
extern NSURLResourceKey NSURLVolumeLocalizedFormatDescriptionKey;
extern NSURLResourceKey NSURLVolumeMaximumFileSizeKey;
extern NSURLResourceKey NSURLVolumeMountFromLocationKey;
extern NSURLResourceKey NSURLVolumeSubtypeKey;
extern NSURLResourceKey NSURLVolumeSupportsAccessPermissionsKey;
extern NSURLResourceKey NSURLVolumeSupportsAdvisoryFileLockingKey;
extern NSURLResourceKey NSURLVolumeSupportsCasePreservedNamesKey;
extern NSURLResourceKey NSURLVolumeSupportsCompressionKey;
extern NSURLResourceKey NSURLVolumeSupportsExtendedSecurityKey;
extern NSURLResourceKey NSURLVolumeSupportsFileProtectionKey;
extern NSURLResourceKey NSURLVolumeSupportsImmutableFilesKey;
extern NSURLResourceKey NSURLVolumeSupportsJournalingKey;
extern NSURLResourceKey NSURLVolumeSupportsRenamingKey;
extern NSURLResourceKey NSURLVolumeSupportsRootDirectoryDatesKey;
extern NSURLResourceKey NSURLVolumeSupportsSwapRenamingKey;
extern NSURLResourceKey NSURLVolumeSupportsZeroRunsKey;
extern NSURLResourceKey NSURLVolumeURLForRemountingKey;
extern NSURLResourceKey NSURLVolumeUUIDStringKey;

/* ⚠⚠ THE SEVEN BOOKMARK DOORS REFUSE BY NAME, ON THE USER'S DECISION OF 2026-10-01 (dec-412cc6306e238994),
 * CONSISTENTLY WITH `-attributedStringByInflectingString` (§63.99): Apple's bookmark data is AN OPAQUE PER-SYSTEM
 * SERIALISATION, so a system without that format can neither create nor resolve one — and **a door returning a
 * plausible-looking `NSData` would be WORSE THAN A REFUSAL, because the data would round-trip here and mean nothing
 * anywhere else.**
 *
 * ⚠ AND THE REFUSAL IS WHAT KEEPS THE OTHER CHOICE OPEN: a caller who meets the exception knows exactly what is
 * missing, and a bookmark format of ours can land behind these same seven signatures later **without anybody having
 * been misled in the meantime.**
 *
 * ⚠⚠ AND THE CATEGORY IS HERE, AT THE END OF THE HEADER, FOR A MEASURED REASON: the two option TYPES the doors take
 * are declared at line 569, AFTER the class block — so declarations placed in the class named types that did not yet
 * exist, which is precisely the `expected a type` × 4 that §63.104 read as missing typedefs. **THEY WERE NOT
 * MISSING; THEY WERE LATER.** */
@interface NSURL (NSURLBookmarks)
+ (nullable instancetype)URLByResolvingAliasFileAtURL:(NSURL *)url
					      options:(NSURLBookmarkResolutionOptions)options
						error:(NSError **)error;
+ (nullable NSURL *)URLByResolvingBookmarkData:(NSData *)bookmarkData
				       options:(NSURLBookmarkResolutionOptions)options
				 relativeToURL:(nullable NSURL *)relativeURL
			   bookmarkDataIsStale:(nullable BOOL *)isStale
					 error:(NSError **)error;
+ (nullable NSData *)bookmarkDataWithContentsOfURL:(NSURL *)bookmarkFileURL error:(NSError **)error;
+ (nullable NSArray *)resourceValuesForKeys:(NSArray *)keys fromBookmarkData:(NSData *)data;
+ (BOOL)writeBookmarkData:(NSData *)bookmarkData
		    toURL:(NSURL *)bookmarkFileURL
		  options:(NSURLBookmarkCreationOptions)options
		    error:(NSError **)error;
- (nullable NSData *)bookmarkDataWithOptions:(NSURLBookmarkCreationOptions)options
	       includingResourceValuesForKeys:(nullable NSArray *)keys
				relativeToURL:(nullable NSURL *)relativeURL
					error:(NSError **)error;
- (nullable instancetype)initByResolvingBookmarkData:(NSData *)bookmarkData
					    options:(NSURLBookmarkResolutionOptions)options
				      relativeToURL:(nullable NSURL *)relativeURL
				bookmarkDataIsStale:(nullable BOOL *)isStale
					      error:(NSError **)error;
@end

/* ⚠⚠ THE PROMISED-ITEM TRIO, AND APPLE'S OWN COMMENTS FULLY SPECIFY THEM (§63.96's instrument, read from the
 * corpus before a line was written): "Most of the NSURL resource value keys will work with these APIs. However, there
 * are some that are tied to the item's contents that will not work, such as NSURLContentAccessDateKey or
 * NSURLGenerationIdentifierKey. **IF ONE OF THESE KEYS IS USED, THE METHOD WILL RETURN YES, BUT THE VALUE FOR THE KEY
 * WILL BE NIL.**"
 *
 * ⚠ SO THIS IS **NOT** A SPECIAL CASE FOR iCLOUD: on a system with no ubiquity, a "promised item" is an ordinary file
 * and the trio READS ITS RESOURCE VALUES — which is why they DELEGATE to the resource-value doors this library
 * already ships, rather than growing a second reader that could disagree with the first.
 *
 * ⚠ AND THE NIL-KEY RULE IS THE ONE PLACE THEY DO NOT DELEGATE: the ordinary door FAILS for a key it cannot answer,
 * and Apple's contract here is to answer YES WITH A NIL VALUE for the content-tied keys. **The two keys Apple NAMES
 * are the whole of our set — generalising "such as" to "any key that fails" would hide every real error behind a
 * YES, which is the opposite of what the sentence is for (§11.6.1 D2, our reading, written down).** */
@interface NSURL (NSURLPromisedItems)
- (BOOL)getPromisedItemResourceValue:(id _Nullable * _Nonnull)value
			      forKey:(NSURLResourceKey)key
			       error:(NSError **)error;
- (nullable NSDictionary *)promisedItemResourceValuesForKeys:(NSArray *)keys
									    error:(NSError **)error;
- (BOOL)checkPromisedItemIsReachableAndReturnError:(NSError **)error;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURL_H */
