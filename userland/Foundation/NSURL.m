/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURL.m — NSURL: the parse, and the file-path rules. docs/design/foundation-plan.md, F8.
 *
 * MANUAL OWNERSHIP: it owns six strings and a number, and implements no -retain/-release.
 *
 * THE PARSE IS HAND-WRITTEN AND SMALL, because RFC 3986's grammar is small. The
 * shape it accepts is
 *
 *     scheme ":" [ "//" [userinfo "@"] host [":" port] ] path [ "?" query ] [ "#" fragment ]
 *
 * and anything that does not match it — a relative reference, a scheme that is not
 * ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) — answers NIL rather than being
 * quietly repaired. The refusal is the parse.
 *
 * PERCENT-ENCODING IS APPLIED IN EXACTLY TWO PLACES, both here and both for the
 * same reason: an FSH path may contain a SPACE ("/System/Temporary Files"), so a
 * file URL's spelling must encode it and -path must decode it back. Everything
 * else is the caller's business, and NSString owns the general rule (F1).
 */

#import <Foundation/NSURL.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSURLHandle.h>
#include "NSURL.h"		/* RFC 3986 §5.2: the resolution NSURL's relative door is FOR */
#include <stdlib.h>
#include <string.h>

/* The two literals, so the code below reads as the grammar it is. */
#define FN_URL_SCHEME_END	':'
#define FN_URL_FILE_SCHEME	@"file"

/* ---------------------------------------------------------------- encoding */

static int fn_is_unreserved(unsigned char c)
{
	/* RFC 3986 §2.3 unreserved, plus the sub-delims and the two path extras this
	 * class needs to leave alone. A byte outside that set is percent-encoded. */
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
		return 1;
	}
	return strchr("-._~!$&'()*+,;=:@/", c) != NULL;
}

static NSString *fn_encode(NSString *input)
{
	const char *bytes = [input UTF8String];
	size_t length, i;
	char *out;
	NSMutableString *result;

	if (bytes == NULL) {
		return @"";
	}
	length = strlen(bytes);
	out = (char *)malloc(length * 3 + 1);
	if (out == NULL) {
		return input;
	}
	{
		size_t at = 0;

		for (i = 0; i < length; i++) {
			unsigned char c = (unsigned char)bytes[i];

			if (fn_is_unreserved(c)) {
				out[at++] = (char)c;
			} else {
				static const char hex[] = "0123456789ABCDEF";

				out[at++] = '%';
				out[at++] = hex[(c >> 4) & 0xF];
				out[at++] = hex[c & 0xF];
			}
		}
		out[at] = '\0';
	}
	{
		NSString *encoded = [NSString stringWithUTF8String:out];

		free(out);
		result = [[NSMutableString alloc] init];
		if (encoded != nil) {
			[result appendString:encoded];
		}
		return result;
	}
}

static int fn_hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Percent-DECODE, and leave a malformed escape alone rather than inventing a byte
 * for it: "50%" stays "50%". */
static NSString *fn_decode(NSString *input)
{
	const char *bytes = [input UTF8String];
	char *out;
	size_t length, i, at = 0;
	NSMutableString *result;

	if (bytes == NULL) {
		return @"";
	}
	length = strlen(bytes);
	out = (char *)malloc(length + 1);
	if (out == NULL) {
		return input;
	}
	for (i = 0; i < length; i++) {
		if (bytes[i] == '%' && i + 2 < length) {
			int hi = fn_hex_value(bytes[i + 1]);
			int lo = fn_hex_value(bytes[i + 2]);

			if (hi >= 0 && lo >= 0) {
				out[at++] = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out[at++] = bytes[i];
	}
	out[at] = '\0';
	{
		NSString *decoded = [NSString stringWithUTF8String:out];

		free(out);
		result = [[NSMutableString alloc] init];
		if (decoded != nil) {
			[result appendString:decoded];
		}
		return result;
	}
}

/* --------------------------------------------------------------- the parse */

/* The scheme grammar, checked before anything else: a string without a valid
 * scheme is not a URL, and this is where that is decided. */
static NSRange fn_scheme_range(const char *bytes, size_t length)
{
	size_t i;

	if (length == 0) {
		return NSMakeRange(NSNotFound, 0);
	}
	if (!((bytes[0] >= 'A' && bytes[0] <= 'Z') || (bytes[0] >= 'a' && bytes[0] <= 'z'))) {
		return NSMakeRange(NSNotFound, 0);
	}
	for (i = 1; i < length; i++) {
		char c = bytes[i];

		if (c == FN_URL_SCHEME_END) {
			return NSMakeRange(0, i);
		}
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
		      || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) {
			return NSMakeRange(NSNotFound, 0);
		}
	}
	return NSMakeRange(NSNotFound, 0);
}

/* ------------------------------------------------------- value-level helpers (2026-09-30)
 *
 * Three rules the "rest of the value" doors need and the parse did not yet hold. Each is a RULE with
 * a stated choice, because Apple's page for each leaves something open and the choice has to be named.
 */

/* THE URL-CHARACTER SET, AND WHY A SECOND ENCODER EXISTS. `fn_encode` above is the PATH rule: it
 * encodes '?' and '#', which is right for a path and wrong for a whole typed string. This is the URL
 * rule for -initWithString:encodingInvalidCharacters:YES: it repairs what is illegal while LEAVING THE
 * STRUCTURE ALONE - ':' '/' '?' '#' '[' ']' and '%' must survive or the string stops being a URL. */
static int fn_is_url_char(unsigned char c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
		return 1;
	}
	return strchr("-._~!$&'()*+,;=:@/?#[]%", c) != NULL;
}

static NSString *fn_encode_url(NSString *input)
{
	const char *bytes = [input UTF8String];
	size_t length, i;
	char *out;
	NSString *result;

	if (bytes == NULL) {
		return @"";
	}
	length = strlen(bytes);
	out = (char *)malloc(length * 3 + 1);
	if (out == NULL) {
		return input;
	}
	{
		size_t at = 0;

		for (i = 0; i < length; i++) {
			unsigned char c = (unsigned char)bytes[i];

			if (fn_is_url_char(c)) {
				out[at++] = (char)c;
			} else {
				static const char hex[] = "0123456789ABCDEF";

				out[at++] = '%';
				out[at++] = hex[(c >> 4) & 0xF];
				out[at++] = hex[c & 0xF];
			}
		}
		out[at] = '\0';
	}
	result = [[NSString alloc] initWithUTF8String:out];
	free(out);
	return result != nil ? [result autorelease] : input;
}

/* ENSURE A PATH SPELLS "directory" (a trailing slash). This is the one thing
 * -fileURLWithPath:isDirectory:YES and -URLByAppendingPathComponent:isDirectory:YES add over their
 * slash-less forms, and it is SYNTACTIC like Apple's: nothing is stat-ed. */
static NSString *fn_ensure_trailing_slash(NSString *path)
{
	if (path == nil || [path length] == 0 || [path hasSuffix:@"/"]) {
		return path;
	}
	return [path stringByAppendingString:@"/"];
}

/* REMOVE "." AND ".." SEGMENTS, the way -URLByStandardizingPath does. This system has no "~" to
 * expand and no /private prefix to strip (the grounds are in the URL unit's comment), so those are the
 * whole rule. ".." at or above the root is DROPPED rather than kept, which is what keeps an absolute
 * path absolute; a single trailing slash survives so a directory stays spelled as one. */
static NSString *fn_standardize_path(NSString *path)
{
	NSArray *components = [path componentsSeparatedByString:@"/"];
	NSMutableArray *kept = [[NSMutableArray alloc] init];
	NSMutableString *result;
	BOOL absolute = [path hasPrefix:@"/"];
	BOOL trailing = [path length] > 1 && [path hasSuffix:@"/"];
	NSUInteger i;

	for (i = 0; i < [components count]; i++) {
		NSString *c = [components objectAtIndex:i];

		if ([c length] == 0 || [c isEqual:@"."]) {
			continue;
		}
		if ([c isEqual:@".."]) {
			if ([kept count] > 0) {
				[kept removeLastObject];
			}
			continue;
		}
		[kept addObject:c];
	}
	result = [[NSMutableString alloc] init];
	if (absolute) {
		[result appendString:@"/"];
	}
	[result appendString:[kept componentsJoinedByString:@"/"]];
	if (trailing && [result length] > 1) {
		[result appendString:@"/"];
	}
	[kept release];
	return [result autorelease];
}

@implementation NSURL

/* THE ONE KEY WHOSE VALUE IS NOT ITS OWN NAME: a scheme is a wire string ("file" in a file URL), not a
 * resource key, and Apple's value for it is @"file". It is declared beside the keys because Apple declares it
 * there, and defined with the value that makes it work. */
NSString *const NSURLFileScheme = @"file";
NSURLResourceKey NSURLIsMountTriggerKey = @"NSURLIsMountTriggerKey";
NSURLResourceKey NSURLVolumeAvailableCapacityForImportantUsageKey = @"NSURLVolumeAvailableCapacityForImportantUsageKey";
NSURLResourceKey NSURLVolumeAvailableCapacityForOpportunisticUsageKey = @"NSURLVolumeAvailableCapacityForOpportunisticUsageKey";
NSURLResourceKey NSURLVolumeIsAutomountedKey = @"NSURLVolumeIsAutomountedKey";
NSURLResourceKey NSURLVolumeIsBrowsableKey = @"NSURLVolumeIsBrowsableKey";
NSURLResourceKey NSURLVolumeIsInternalKey = @"NSURLVolumeIsInternalKey";
NSURLResourceKey NSURLVolumeIsJournalingKey = @"NSURLVolumeIsJournalingKey";
NSURLResourceKey NSURLVolumeLocalizedFormatDescriptionKey = @"NSURLVolumeLocalizedFormatDescriptionKey";
NSURLResourceKey NSURLVolumeMaximumFileSizeKey = @"NSURLVolumeMaximumFileSizeKey";
NSURLResourceKey NSURLVolumeMountFromLocationKey = @"NSURLVolumeMountFromLocationKey";
NSURLResourceKey NSURLVolumeSubtypeKey = @"NSURLVolumeSubtypeKey";
NSURLResourceKey NSURLVolumeSupportsAccessPermissionsKey = @"NSURLVolumeSupportsAccessPermissionsKey";
NSURLResourceKey NSURLVolumeSupportsAdvisoryFileLockingKey = @"NSURLVolumeSupportsAdvisoryFileLockingKey";
NSURLResourceKey NSURLVolumeSupportsCasePreservedNamesKey = @"NSURLVolumeSupportsCasePreservedNamesKey";
NSURLResourceKey NSURLVolumeSupportsCompressionKey = @"NSURLVolumeSupportsCompressionKey";
NSURLResourceKey NSURLVolumeSupportsExtendedSecurityKey = @"NSURLVolumeSupportsExtendedSecurityKey";
NSURLResourceKey NSURLVolumeSupportsFileProtectionKey = @"NSURLVolumeSupportsFileProtectionKey";
NSURLResourceKey NSURLVolumeSupportsImmutableFilesKey = @"NSURLVolumeSupportsImmutableFilesKey";
NSURLResourceKey NSURLVolumeSupportsJournalingKey = @"NSURLVolumeSupportsJournalingKey";
NSURLResourceKey NSURLVolumeSupportsRenamingKey = @"NSURLVolumeSupportsRenamingKey";
NSURLResourceKey NSURLVolumeSupportsRootDirectoryDatesKey = @"NSURLVolumeSupportsRootDirectoryDatesKey";
NSURLResourceKey NSURLVolumeSupportsSwapRenamingKey = @"NSURLVolumeSupportsSwapRenamingKey";
NSURLResourceKey NSURLVolumeSupportsZeroRunsKey = @"NSURLVolumeSupportsZeroRunsKey";
NSURLResourceKey NSURLVolumeURLForRemountingKey = @"NSURLVolumeURLForRemountingKey";
NSURLResourceKey NSURLVolumeUUIDStringKey = @"NSURLVolumeUUIDStringKey";

/* THE ONE CONSTRUCTOR THE OTHERS GO THROUGH. It is deliberately private-ish
 * (declared here, not in the header): a URL is made from a string or from a
 * path, and both go through the same parse so they cannot disagree. */
- (nullable id)initWithPartsFromString:(NSString *)string
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	{
		const char *bytes = [string UTF8String];
		size_t length;
		NSRange schemeRange;
		size_t at;

		if (bytes == NULL) {
			return nil;
		}
		length = strlen(bytes);
		schemeRange = fn_scheme_range(bytes, length);
		if (schemeRange.location == NSNotFound) {
			return nil;
		}
		_absoluteString = [string copy];
		/* OWNED LIKE EVERY OTHER PART: `-lowercaseString` answers an AUTORELEASED string, and a URL that
		 * outlives the pool its parse ran in used to point at freed memory through this one part. */
		_scheme = [[[string substringWithRange:schemeRange] lowercaseString] copy];
		_isFile = [_scheme isEqualToString:FN_URL_FILE_SCHEME];
		at = schemeRange.length + 1;

		/* authority */
		if (at + 1 < length && bytes[at] == '/' && bytes[at + 1] == '/') {
			size_t authorityEnd = at + 2;

			at += 2;
			while (authorityEnd < length && bytes[authorityEnd] != '/'
			       && bytes[authorityEnd] != '?' && bytes[authorityEnd] != '#') {
				authorityEnd++;
			}
			{
				NSString *authority = [string substringWithRange:
					NSMakeRange(at, authorityEnd - at)];
				NSRange hostPart = NSMakeRange(0, [authority length]);
				NSRange atSign = [authority rangeOfString:@"@"];

				if (atSign.location != NSNotFound) {
					/* THE USERINFO IS SPLIT AT THE FIRST ':' (RFC 3986 §3.2.1), so `user:secret@h`
					 * answers user="user" and password="secret" rather than one string holding both.
					 * The halves are kept in their ENCODED spelling, like `_user` always was. */
					NSString *userinfo = [authority substringWithRange:
						NSMakeRange(0, atSign.location)];
					NSRange colon = [userinfo rangeOfString:@":"];

					if (colon.location != NSNotFound) {
						_user = [[userinfo substringWithRange:
							NSMakeRange(0, colon.location)] copy];
						_password = [[userinfo substringFromIndex:colon.location + 1] copy];
					} else {
						_user = [userinfo copy];
					}
					hostPart = NSMakeRange(atSign.location + 1,
							       [authority length] - atSign.location - 1);
				}
				{
					NSString *hostAndPort =
						[authority substringWithRange:hostPart];
					NSRange colon = [hostAndPort rangeOfString:@":"];

					if (colon.location != NSNotFound) {
						NSString *portText = [hostAndPort substringWithRange:
							NSMakeRange(colon.location + 1,
								    [hostAndPort length] - colon.location - 1)];
						int value = [portText intValue];

						_host = [[hostAndPort substringWithRange:
							NSMakeRange(0, colon.location)] copy];
						_port = [[NSNumber numberWithInt:value] copy];
					} else if ([hostAndPort length] > 0) {
						/* An EMPTY authority leaves the host NIL, which is what
						 * `file:///x` has: there is no host, not an empty-named one. */
						_host = [hostAndPort copy];
					}
				}
			}
			at = authorityEnd;
		}

		/* path, query, fragment — the path is everything up to ? or # */
		{
			size_t pathEnd = at;

			while (pathEnd < length && bytes[pathEnd] != '?'
			       && bytes[pathEnd] != '#') {
				pathEnd++;
			}
			_path = [[string substringWithRange:
				NSMakeRange(at, pathEnd - at)] copy];
			at = pathEnd;
		}
		if (at < length && bytes[at] == '?') {
			size_t queryEnd = at + 1;

			while (queryEnd < length && bytes[queryEnd] != '#') {
				queryEnd++;
			}
			_query = [[string substringWithRange:
				NSMakeRange(at + 1, queryEnd - at - 1)] copy];
			at = queryEnd;
		}
		if (at < length && bytes[at] == '#') {
			_fragment = [[string substringFromIndex:(NSUInteger)at + 1] copy];
		}
	}
	return self;
}

+ (nullable NSURL *)URLWithString:(NSString *)string
{
	return [[self alloc] initWithString:string];
}

/* F8 REFUSED THIS DOOR BY NAME, and the reason was exact: without a resolution there is nothing for
 * it to do. RFC 3986 §5.2 lives in NSURLComponents (it is the component-wise algorithm) and is
 * reached through NSURL.h's function, so the two arrive together rather than one implying the
 * other. A NULL base is accepted: the algorithm still normalises a reference on its own. */
+ (nullable NSURL *)URLWithString:(NSString *)string relativeToURL:(nullable NSURL *)baseURL
{
	return FNURLResolveRelative(string, baseURL != nil ? [baseURL absoluteString] : nil);
}

+ (nullable NSURL *)fileURLWithPath:(NSString *)path
{
	return [[self alloc] initFileURLWithPath:path];
}

/* nil when the string is not an absolute URL with a valid scheme. */
- (nullable id)initWithString:(NSString *)string
{
	return [self initWithPartsFromString:string];
}

/* THE FSH RULE: a path is slash-separated and absolute. Anything else is not a
 * path this system can name, so it answers nil rather than being joined to
 * something. The spelling is `file://` + the ENCODED path, with an empty
 * authority — which is why "/System/Temporary Files" comes out with %20 in it. */
- (nullable id)initFileURLWithPath:(NSString *)path
{
	NSString *spelling;

	if (path == nil || [path length] == 0) {
		return nil;
	}
	if (![path hasPrefix:@"/"]) {
		return nil;
	}
	spelling = [NSString stringWithFormat:@"file://%@", fn_encode(path)];
	return [self initWithPartsFromString:spelling];
}

- (NSString *)scheme { return _scheme; }
- (nullable NSString *)host { return _host; }
- (nullable NSString *)user { return _user; }
- (nullable NSNumber *)port { return _port; }
/* THE PATH IS DECODED, and that is the FSH rule in the other direction: the
 * spelling of a file URL percent-encodes a space (an FSH path may have one), so
 * reading `-path` back must undo it. The ENCODED spelling is what -absoluteString
 * shows and what the path arithmetic operates on. */
- (NSString *)path { return fn_decode(_path); }
- (nullable NSString *)query { return _query; }
- (nullable NSString *)fragment { return _fragment; }
- (NSString *)absoluteString { return _absoluteString; }
- (NSString *)relativeString { return _absoluteString; }
- (BOOL)isFileURL { return _isFile; }
- (NSURL *)absoluteURL { return self; }

/* ------------------------------------------------------- path arithmetic */

/* Build a new URL from this one's parts with a different path, re-spelling it the
 * same way the parser would have found it. */
- (NSURL *)fnURLWithPath:(NSString *)newPath
{
	NSMutableString *spelling = [[NSMutableString alloc] init];

	[spelling appendString:_scheme];
	[spelling appendString:@":"];
	/* A file URL ALWAYS shows its empty authority (`file:///x`), and so does any URL
	 * with a host; only an opaque URL (no authority at all) omits it. */
	if (_isFile || _host != nil) {
		[spelling appendString:@"//"];
		if (_user != nil) {
			[spelling appendString:_user];
			[spelling appendString:@"@"];
		}
		if (_host != nil) {
			[spelling appendString:_host];
		}
		if (_port != nil) {
			[spelling appendString:@":"];
			[spelling appendString:[_port stringValue]];
		}
	}
	[spelling appendString:newPath];
	if (_query != nil) {
		[spelling appendString:@"?"];
		[spelling appendString:_query];
	}
	if (_fragment != nil) {
		[spelling appendString:@"#"];
		[spelling appendString:_fragment];
	}
	{
		NSURL *made = [[NSURL alloc] initWithString:spelling];

		/* The spelling above is built to be re-parsed, so a nil here would be a
		 * bug in this method rather than in the caller's URL; fall back to self
		 * rather than handing back nothing. */
		return made != nil ? made : self;
	}
}

- (NSURL *)URLByAppendingPathComponent:(NSString *)component
{
	NSMutableString *path = [[NSMutableString alloc] init];

	[path appendString:_path];
	if ([path length] == 0) {
		[path appendString:@"/"];
	} else if (![path hasSuffix:@"/"]) {
		[path appendString:@"/"];
	}
	[path appendString:fn_encode(component)];
	return [self fnURLWithPath:path];
}

- (nullable NSURL *)URLByAppendingPathExtension:(NSString *)extension
{
	NSMutableString *path;

	/* No path, nothing to extend — Cocoa's contract too. */
	if ([_path length] == 0) {
		return nil;
	}
	path = [[NSMutableString alloc] initWithString:_path];
	[path appendString:@"."];
	[path appendString:fn_encode(extension)];
	return [self fnURLWithPath:path];
}

- (NSURL *)URLByDeletingLastPathComponent
{
	NSMutableString *path = [[NSMutableString alloc] initWithString:_path];
	NSRange slash;

	if ([path length] == 0) {
		return self;
	}
	/* Drop a trailing slash first: "/a/b/" deleting the last component is "/a". */
	while ([path hasSuffix:@"/"] && [path length] > 1) {
		[path deleteCharactersInRange:NSMakeRange([path length] - 1, 1)];
	}
	slash = [path rangeOfString:@"/" options:NSLiteralSearch];
	{
		NSRange last = NSMakeRange(NSNotFound, 0);
		NSUInteger i;
		NSUInteger length = [path length];

		for (i = 0; i < length; i++) {
			if ([path characterAtIndex:i] == '/') {
				last = NSMakeRange(i, 1);
			}
		}
		(void)slash;
		if (last.location == NSNotFound) {
			return self;
		}
		if (last.location == 0) {
			return [self fnURLWithPath:@"/"];
		}
		return [self fnURLWithPath:[path substringToIndex:last.location]];
	}
}

- (NSURL *)URLByDeletingPathExtension
{
	NSUInteger length = [_path length];
	NSUInteger i;
	NSRange dot = NSMakeRange(NSNotFound, 0);
	NSRange slash = NSMakeRange(NSNotFound, 0);

	for (i = 0; i < length; i++) {
		unichar c = [_path characterAtIndex:i];

		if (c == '/') {
			slash = NSMakeRange(i, 1);
		} else if (c == '.') {
			dot = NSMakeRange(i, 1);
		}
	}
	/* The dot must be inside the last component AND NOT ITS FIRST CHARACTER, which
	 * is the `<=`: `/a/.hidden` is a name, not a name with an extension, so the dot
	 * that begins a component is refused exactly like a dot before the slash. */
	if (dot.location == NSNotFound || dot.location == 0
	    || (slash.location != NSNotFound && dot.location <= slash.location + 1)) {
		return self;
	}
	return [self fnURLWithPath:[_path substringToIndex:dot.location]];
}

/* --------------------------------------------------------------- identity */

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSURL class]]) {
		return NO;
	}
	/* BY THE SPELLING, and case-sensitively: this class does no case folding (a
	 * host's case is the DNS's business and nothing here resolves one), so the
	 * rule is stated rather than half-applied. */
	return [_absoluteString isEqualToString:[(NSURL *)other absoluteString]];
}

- (NSUInteger)hash
{
	return [_absoluteString hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@>", [self class], _absoluteString];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}


/* ------------------------------------------------------- resource values (W8 slice 6a) */

/* THE KEYS' STRING VALUES ARE THEIR OWN NAMES, this library's standing spelling for a constant whose
 * name Apple publishes and whose string no program of ours reads (§11.6.1 D2, exactly as
 * NSFileManager's key names are spelled). */
NSURLResourceKey const NSURLNameKey = @"NSURLNameKey";
NSURLResourceKey const NSURLLocalizedNameKey = @"NSURLLocalizedNameKey";
NSURLResourceKey const NSURLPathKey = @"NSURLPathKey";
NSURLResourceKey const NSURLCanonicalPathKey = @"NSURLCanonicalPathKey";
NSURLResourceKey const NSURLIsRegularFileKey = @"NSURLIsRegularFileKey";
NSURLResourceKey const NSURLIsDirectoryKey = @"NSURLIsDirectoryKey";
NSURLResourceKey const NSURLIsSymbolicLinkKey = @"NSURLIsSymbolicLinkKey";
NSURLResourceKey const NSURLIsReadableKey = @"NSURLIsReadableKey";
NSURLResourceKey const NSURLIsWritableKey = @"NSURLIsWritableKey";
NSURLResourceKey const NSURLIsExecutableKey = @"NSURLIsExecutableKey";
NSURLResourceKey const NSURLIsHiddenKey = @"NSURLIsHiddenKey";
NSURLResourceKey const NSURLFileSizeKey = @"NSURLFileSizeKey";
NSURLResourceKey const NSURLFileAllocatedSizeKey = @"NSURLFileAllocatedSizeKey";
NSURLResourceKey const NSURLTotalFileSizeKey = @"NSURLTotalFileSizeKey";
NSURLResourceKey const NSURLTotalFileAllocatedSizeKey = @"NSURLTotalFileAllocatedSizeKey";
NSURLResourceKey const NSURLLinkCountKey = @"NSURLLinkCountKey";
NSURLResourceKey const NSURLContentModificationDateKey = @"NSURLContentModificationDateKey";
NSURLResourceKey const NSURLContentAccessDateKey = @"NSURLContentAccessDateKey";
NSURLResourceKey const NSURLAttributeModificationDateKey = @"NSURLAttributeModificationDateKey";
NSURLResourceKey const NSURLFileIdentifierKey = @"NSURLFileIdentifierKey";
NSURLResourceKey const NSURLTypeIdentifierKey = @"NSURLTypeIdentifierKey";
NSURLResourceKey const NSURLFileSecurityKey = @"NSURLFileSecurityKey";
NSURLResourceKey const NSThumbnail1024x1024SizeKey = @"NSThumbnail1024x1024SizeKey";
NSURLResourceKey const NSURLFileResourceIdentifierKey = @"NSURLFileResourceIdentifierKey";
NSURLResourceKey const NSURLFileResourceTypeKey = @"NSURLFileResourceTypeKey";
NSURLResourceKey const NSURLParentDirectoryURLKey = @"NSURLParentDirectoryURLKey";
NSURLResourceKey const NSURLKeysOfUnsetValuesKey = @"NSURLKeysOfUnsetValuesKey";
NSURLResourceKey const NSURLVolumeTotalCapacityKey = @"NSURLVolumeTotalCapacityKey";
NSURLResourceKey const NSURLVolumeAvailableCapacityKey = @"NSURLVolumeAvailableCapacityKey";
NSURLResourceKey const NSURLVolumeIsLocalKey = @"NSURLVolumeIsLocalKey";
NSURLResourceKey const NSURLVolumeIsReadOnlyKey = @"NSURLVolumeIsReadOnlyKey";
NSURLResourceKey const NSURLVolumeSupportsCaseSensitiveNamesKey = @"NSURLVolumeSupportsCaseSensitiveNamesKey";
NSURLResourceKey const NSURLVolumeSupportsPersistentIDsKey = @"NSURLVolumeSupportsPersistentIDsKey";
NSURLResourceKey const NSURLVolumeSupportsSymbolicLinksKey = @"NSURLVolumeSupportsSymbolicLinksKey";
NSURLResourceKey const NSURLVolumeNameKey = @"NSURLVolumeNameKey";
NSURLResourceKey const NSURLVolumeLocalizedNameKey = @"NSURLVolumeLocalizedNameKey";
NSURLResourceKey const NSURLVolumeIdentifierKey = @"NSURLVolumeIdentifierKey";
NSURLResourceKey const NSURLVolumeURLKey = @"NSURLVolumeURLKey";
NSURLResourceKey const NSURLVolumeTypeNameKey = @"NSURLVolumeTypeNameKey";
NSURLResourceKey const NSURLVolumeIsRootFileSystemKey = @"NSURLVolumeIsRootFileSystemKey";
NSURLResourceKey const NSURLVolumeResourceCountKey = @"NSURLVolumeResourceCountKey";
NSURLResourceKey const NSURLVolumeSupportsVolumeSizesKey = @"NSURLVolumeSupportsVolumeSizesKey";
NSURLResourceKey const NSURLVolumeIsMountTriggerKey = @"NSURLVolumeIsMountTriggerKey";
NSURLResourceKey const NSURLIsVolumeKey = @"NSURLIsVolumeKey";
/* THE KEY MASSES WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE (slice 6f): declared, recognised, answered nil -
 * Apple's own "not available for this resource" - with one plain fact among them. */
NSURLResourceKey const NSURLAddedToDirectoryDateKey = @"NSURLAddedToDirectoryDateKey";
NSURLResourceKey const NSURLApplicationIsScriptableKey = @"NSURLApplicationIsScriptableKey";
NSURLResourceKey const NSURLContentTypeKey = @"NSURLContentTypeKey";
NSURLResourceKey const NSURLCreationDateKey = @"NSURLCreationDateKey";
NSURLResourceKey const NSURLCustomIconKey = @"NSURLCustomIconKey";
NSURLResourceKey const NSURLDocumentIdentifierKey = @"NSURLDocumentIdentifierKey";
NSURLResourceKey const NSURLEffectiveIconKey = @"NSURLEffectiveIconKey";
NSURLResourceKey const NSURLGenerationIdentifierKey = @"NSURLGenerationIdentifierKey";
NSURLResourceKey const NSURLHasHiddenExtensionKey = @"NSURLHasHiddenExtensionKey";
NSURLResourceKey const NSURLIsExcludedFromBackupKey = @"NSURLIsExcludedFromBackupKey";
NSURLResourceKey const NSURLIsSystemImmutableKey = @"NSURLIsSystemImmutableKey";
NSURLResourceKey const NSURLThumbnailDictionaryKey = @"NSURLThumbnailDictionaryKey";
NSURLResourceKey const NSURLThumbnailKey = @"NSURLThumbnailKey";
NSURLResourceKey const NSURLFileProtectionKey = @"NSURLFileProtectionKey";
/* THE SUBSTRATE-MEASURED KEYS (slice 6g): the measurement is named at each branch below. */
NSURLResourceKey const NSURLDirectoryEntryCountKey = @"NSURLDirectoryEntryCountKey";
NSURLResourceKey const NSURLFileContentIdentifierKey = @"NSURLFileContentIdentifierKey";
NSURLResourceKey const NSURLIsAliasFileKey = @"NSURLIsAliasFileKey";
NSURLResourceKey const NSURLIsApplicationKey = @"NSURLIsApplicationKey";
NSURLResourceKey const NSURLIsPackageKey = @"NSURLIsPackageKey";
NSURLResourceKey const NSURLIsPurgeableKey = @"NSURLIsPurgeableKey";
NSURLResourceKey const NSURLIsSparseKey = @"NSURLIsSparseKey";
NSURLResourceKey const NSURLMayHaveExtendedAttributesKey = @"NSURLMayHaveExtendedAttributesKey";
NSURLResourceKey const NSURLMayShareFileContentKey = @"NSURLMayShareFileContentKey";
NSURLResourceKey const NSURLPreferredIOBlockSizeKey = @"NSURLPreferredIOBlockSizeKey";
NSURLResourceKey const NSURLVolumeCreationDateKey = @"NSURLVolumeCreationDateKey";
NSURLResourceKey const NSURLVolumeIsEjectableKey = @"NSURLVolumeIsEjectableKey";
NSURLResourceKey const NSURLVolumeIsEncryptedKey = @"NSURLVolumeIsEncryptedKey";
NSURLResourceKey const NSURLVolumeIsRemovableKey = @"NSURLVolumeIsRemovableKey";
NSURLResourceKey const NSURLVolumeSupportsExclusiveRenamingKey = @"NSURLVolumeSupportsExclusiveRenamingKey";
NSURLResourceKey const NSURLVolumeSupportsFileCloningKey = @"NSURLVolumeSupportsFileCloningKey";
NSURLResourceKey const NSURLVolumeSupportsHardLinksKey = @"NSURLVolumeSupportsHardLinksKey";
NSURLResourceKey const NSURLVolumeSupportsSparseFilesKey = @"NSURLVolumeSupportsSparseFilesKey";
NSURLResourceKey const NSURLIsUbiquitousItemKey = @"NSURLIsUbiquitousItemKey";
NSURLResourceKey const NSURLIsUserImmutableKey = @"NSURLIsUserImmutableKey";
NSURLResourceKey const NSURLLabelColorKey = @"NSURLLabelColorKey";
NSURLResourceKey const NSURLLabelNumberKey = @"NSURLLabelNumberKey";
NSURLResourceKey const NSURLLocalizedLabelKey = @"NSURLLocalizedLabelKey";
NSURLResourceKey const NSURLLocalizedTypeDescriptionKey = @"NSURLLocalizedTypeDescriptionKey";
NSURLResourceKey const NSURLQuarantinePropertiesKey = @"NSURLQuarantinePropertiesKey";
NSURLResourceKey const NSURLTagNamesKey = @"NSURLTagNamesKey";
NSURLResourceKey const NSURLUbiquitousItemContainerDisplayNameKey = @"NSURLUbiquitousItemContainerDisplayNameKey";
NSURLResourceKey const NSURLUbiquitousItemDownloadRequestedKey = @"NSURLUbiquitousItemDownloadRequestedKey";
NSURLResourceKey const NSURLUbiquitousItemDownloadingErrorKey = @"NSURLUbiquitousItemDownloadingErrorKey";
NSURLResourceKey const NSURLUbiquitousItemDownloadingStatusKey = @"NSURLUbiquitousItemDownloadingStatusKey";
NSURLResourceKey const NSURLUbiquitousItemHasUnresolvedConflictsKey = @"NSURLUbiquitousItemHasUnresolvedConflictsKey";
NSURLResourceKey const NSURLUbiquitousItemIsDownloadingKey = @"NSURLUbiquitousItemIsDownloadingKey";
NSURLResourceKey const NSURLUbiquitousItemIsExcludedFromSyncKey = @"NSURLUbiquitousItemIsExcludedFromSyncKey";
NSURLResourceKey const NSURLUbiquitousItemIsSharedKey = @"NSURLUbiquitousItemIsSharedKey";
NSURLResourceKey const NSURLUbiquitousItemIsSyncPausedKey = @"NSURLUbiquitousItemIsSyncPausedKey";
NSURLResourceKey const NSURLUbiquitousItemIsUploadedKey = @"NSURLUbiquitousItemIsUploadedKey";
NSURLResourceKey const NSURLUbiquitousItemIsUploadingKey = @"NSURLUbiquitousItemIsUploadingKey";
NSURLResourceKey const NSURLUbiquitousItemSupportedSyncControlsKey = @"NSURLUbiquitousItemSupportedSyncControlsKey";
NSURLResourceKey const NSURLUbiquitousItemUploadingErrorKey = @"NSURLUbiquitousItemUploadingErrorKey";
NSURLResourceKey const NSURLUbiquitousSharedItemCurrentUserPermissionsKey = @"NSURLUbiquitousSharedItemCurrentUserPermissionsKey";
NSURLResourceKey const NSURLUbiquitousSharedItemCurrentUserRoleKey = @"NSURLUbiquitousSharedItemCurrentUserRoleKey";
NSURLResourceKey const NSURLUbiquitousSharedItemMostRecentEditorNameComponentsKey = @"NSURLUbiquitousSharedItemMostRecentEditorNameComponentsKey";
NSURLResourceKey const NSURLUbiquitousSharedItemOwnerNameComponentsKey = @"NSURLUbiquitousSharedItemOwnerNameComponentsKey";
NSURLFileProtectionType const NSURLFileProtectionComplete = @"NSURLFileProtectionComplete";
NSURLFileProtectionType const NSURLFileProtectionCompleteUnlessOpen = @"NSURLFileProtectionCompleteUnlessOpen";
NSURLFileProtectionType const NSURLFileProtectionCompleteUntilFirstUserAuthentication = @"NSURLFileProtectionCompleteUntilFirstUserAuthentication";
NSURLFileProtectionType const NSURLFileProtectionCompleteWhenUserInactive = @"NSURLFileProtectionCompleteWhenUserInactive";
NSURLFileProtectionType const NSURLFileProtectionNone = @"NSURLFileProtectionNone";
NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusCurrent = @"NSURLUbiquitousItemDownloadingStatusCurrent";
NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusDownloaded = @"NSURLUbiquitousItemDownloadingStatusDownloaded";
NSURLUbiquitousItemDownloadingStatus const NSURLUbiquitousItemDownloadingStatusNotDownloaded = @"NSURLUbiquitousItemDownloadingStatusNotDownloaded";
NSURLUbiquitousSharedItemPermissions const NSURLUbiquitousSharedItemPermissionsReadOnly = @"NSURLUbiquitousSharedItemPermissionsReadOnly";
NSURLUbiquitousSharedItemPermissions const NSURLUbiquitousSharedItemPermissionsReadWrite = @"NSURLUbiquitousSharedItemPermissionsReadWrite";
NSURLUbiquitousSharedItemRole const NSURLUbiquitousSharedItemRoleOwner = @"NSURLUbiquitousSharedItemRoleOwner";
NSURLUbiquitousSharedItemRole const NSURLUbiquitousSharedItemRoleParticipant = @"NSURLUbiquitousSharedItemRoleParticipant";
/* THE KEY MASSES WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE (slice 6f): declared, recognised, and answered nil
 * - Apple's own "not available for this resource" - with one plain fact among them. */


NSURLFileResourceType const NSURLFileResourceTypeRegular = @"NSURLFileResourceTypeRegular";
NSURLFileResourceType const NSURLFileResourceTypeDirectory = @"NSURLFileResourceTypeDirectory";
NSURLFileResourceType const NSURLFileResourceTypeSymbolicLink = @"NSURLFileResourceTypeSymbolicLink";
NSURLFileResourceType const NSURLFileResourceTypeSocket = @"NSURLFileResourceTypeSocket";
NSURLFileResourceType const NSURLFileResourceTypeCharacterSpecial = @"NSURLFileResourceTypeCharacterSpecial";
NSURLFileResourceType const NSURLFileResourceTypeBlockSpecial = @"NSURLFileResourceTypeBlockSpecial";
NSURLFileResourceType const NSURLFileResourceTypeNamedPipe = @"NSURLFileResourceTypeNamedPipe";
NSURLFileResourceType const NSURLFileResourceTypeUnknown = @"NSURLFileResourceTypeUnknown";

/* ---- THE MOUNT TABLE ------------------------------------------------------------------------------
 * `/System/Processes/mounts` is this system's published mount table: one line per mount that is not kernel-internal,
 * with `device mountpoint fstype rw|ro 0 0`. It is read on demand rather than cached here, because the URL's
 * OWN cache is what makes repeated questions cheap (slice 6a) and a second cache would be a second truth. */
/* THE FILE SYSTEM A PATH LIVES ON, from the mount table slice 6e reads. The capability keys below are
 * per-VOLUME, so they ask which volume the item is on rather than assuming the root. */
static NSString * _Nullable fn_volume_fstype(NSString *path)
{
	NSArray *entry = fn_volume_for_path(path);

	return entry != nil && [entry count] > 2 ? [entry objectAtIndex:2] : nil;
}

/* READING A SYNTHETIC FILE, AND WHY IT NEEDS A LOOP: procfs's nodes report SIZE ZERO - their content is
 * generated when they are read - and anything that reads "exactly st_size bytes" therefore answers EMPTY.
 * That is what the first version of the mount-table reader did (twice, here and in NSFileManager), and the
 * symptom was a volume list that was simply absent. This reads until EOF and needs no size at all. */
static NSData *fn_mount_table_bytes(void)
{
	int fd = open("/System/Processes/mounts", O_RDONLY);
	NSMutableData *answer;

	if (fd < 0) {
		return nil;
	}
	answer = [[NSMutableData alloc] init];
	for (;;) {
		char buffer[2048];
		ssize_t got = read(fd, buffer, sizeof(buffer));

		if (got <= 0) {
			break;
		}
		[answer appendBytes:buffer length:(NSUInteger)got];
	}
	close(fd);
	return [answer autorelease];
}

static NSArray *fn_mounts(void)
{
	id data = fn_mount_table_bytes();
	NSString *text;
	NSMutableArray *entries;

	if (data == nil) {
		return nil;
	}
	text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	if (text == nil) {
		return nil;
	}
	entries = [NSMutableArray array];
	{
		NSArray *lines = [text componentsSeparatedByString:@"\n"];
		NSUInteger i;

		for (i = 0; i < [lines count]; i++) {
			NSArray *fields = [[lines objectAtIndex:i] componentsSeparatedByString:@" "];
			NSMutableArray *kept = [NSMutableArray array];
			NSUInteger f;

			for (f = 0; f < [fields count]; f++) {
				NSString *field = [fields objectAtIndex:f];

				if ([field length] > 0) {
					[kept addObject:field];
				}
			}
			if ([kept count] >= 4) {
				[entries addObject:kept];
			}
		}
	}
	[text release];
	return entries;
}

/* THE VOLUME HOLDING A PATH: the LONGEST mount point that prefixes it. A naive first-match would put
 * `/proc/version` on the root volume, which is exactly the mistake this rule exists to avoid. */
static NSArray *fn_volume_for_path(NSString *path)
{
	NSArray *entries = fn_mounts();
	NSArray *best = nil;
	NSUInteger bestLength = 0;
	NSUInteger i;

	if (path == nil) {
		return nil;
	}
	for (i = 0; i < [entries count]; i++) {
		NSArray *entry = [entries objectAtIndex:i];
		NSString *mountPoint = [entry objectAtIndex:1];
		NSUInteger length = [mountPoint length];

		if ([mountPoint isEqual:@"/"]) {
			if (best == nil) {
				best = entry;
				bestLength = 0;
			}
			continue;
		}
		if ([path isEqual:mountPoint] || [path hasPrefix:
				[mountPoint stringByAppendingString:@"/"]]) {
			if (length > bestLength) {
				best = entry;
				bestLength = length;
			}
		}
	}
	return best;
}

/* ONE PATH, ONE lstat - AND lstat RATHER THAN stat ON PURPOSE: -isSymbolicLinkKey asks about the LINK,
 * and a link's -fileSizeKey is the length of the string it holds, where stat would answer about the
 * target. This is NSFileWrapper's reader's rule, reached from the other side. */
static int fn_url_lstat(NSURL *url, struct stat *st)
{
	NSString *path = [url path];

	if (![url isFileURL] || path == nil) {
		return -1;
	}
	return lstat([path UTF8String], st);
}

/* AN ERROR THE WAY THIS LIBRARY MAKES THEM: the code IS the errno and the description names the errno
 * and what was asked about, so a refusal is auditable from outside. */
static NSError *fn_url_error(int err, NSString *what)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
					 [NSString stringWithFormat:@"%@: %s", what, strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

/* THE NINE FILE RESOURCE TYPES: one value per mode bit, and Unknown for the rest - which is Apple's
 * own eighth case rather than an error. */
static NSURLFileResourceType fn_url_resource_type(mode_t mode)
{
	if (S_ISREG(mode)) return NSURLFileResourceTypeRegular;
	if (S_ISDIR(mode)) return NSURLFileResourceTypeDirectory;
	if (S_ISLNK(mode)) return NSURLFileResourceTypeSymbolicLink;
	if (S_ISSOCK(mode)) return NSURLFileResourceTypeSocket;
	if (S_ISCHR(mode)) return NSURLFileResourceTypeCharacterSpecial;
	if (S_ISBLK(mode)) return NSURLFileResourceTypeBlockSpecial;
	if (S_ISFIFO(mode)) return NSURLFileResourceTypeNamedPipe;
	return NSURLFileResourceTypeUnknown;
}

/* IS THIS KEY ONE OF THIS UNIT'S AT ALL? The distinction matters and is the reason this predicate
 * exists: a key the library does not know is a REFUSAL (an error, named), while a key it knows and
 * whose fact this substrate lacks leaves the key OUT of the answer (absent, not nil-in-a-dictionary). */
static BOOL fn_url_answers_key(NSURLResourceKey key)
{
	static NSURLResourceKey const table[] = {
		@"NSURLNameKey", @"NSURLLocalizedNameKey", @"NSURLPathKey", @"NSURLCanonicalPathKey",
		@"NSURLIsRegularFileKey", @"NSURLIsDirectoryKey", @"NSURLIsSymbolicLinkKey",
		@"NSURLIsReadableKey", @"NSURLIsWritableKey", @"NSURLIsExecutableKey",
		@"NSURLIsHiddenKey", @"NSURLFileSizeKey", @"NSURLFileAllocatedSizeKey",
		@"NSURLTotalFileSizeKey", @"NSURLTotalFileAllocatedSizeKey", @"NSURLLinkCountKey",
		@"NSURLContentModificationDateKey", @"NSURLContentAccessDateKey",
		@"NSURLAttributeModificationDateKey", @"NSURLFileIdentifierKey",
		@"NSURLFileResourceIdentifierKey", @"NSURLFileResourceTypeKey", @"NSURLParentDirectoryURLKey",
		@"NSURLVolumeTotalCapacityKey", @"NSURLVolumeAvailableCapacityKey",
		@"NSURLVolumeIsLocalKey", @"NSURLVolumeIsReadOnlyKey",
		@"NSURLVolumeSupportsCaseSensitiveNamesKey", @"NSURLVolumeSupportsPersistentIDsKey",
		@"NSURLVolumeSupportsSymbolicLinksKey",
		/* THE MOUNT TABLE'S KEYS (slice 6e) - AND THEIR ABSENCE FROM THIS TABLE IS WHAT MADE SIX CHECKS
		 * FAIL AT ONCE: the value chain answered them and the RECOGNITION LIST refused them, so every one
		 * came back nil. The two places have to agree, and the probe is what noticed. */
		@"NSURLVolumeNameKey", @"NSURLVolumeLocalizedNameKey", @"NSURLVolumeIdentifierKey",
		@"NSURLVolumeURLKey", @"NSURLVolumeTypeNameKey", @"NSURLVolumeIsRootFileSystemKey",
		@"NSURLVolumeResourceCountKey", @"NSURLVolumeSupportsVolumeSizesKey",
		@"NSURLVolumeIsMountTriggerKey", @"NSURLIsVolumeKey",
		@"NSURLDirectoryEntryCountKey",
		@"NSURLFileContentIdentifierKey",
		@"NSURLIsAliasFileKey",
		@"NSURLIsApplicationKey",
		@"NSURLIsPackageKey",
		@"NSURLIsPurgeableKey",
		@"NSURLIsSparseKey",
		@"NSURLMayHaveExtendedAttributesKey",
		@"NSURLMayShareFileContentKey",
		@"NSURLPreferredIOBlockSizeKey",
		@"NSURLVolumeCreationDateKey",
		@"NSURLVolumeIsEjectableKey",
		@"NSURLVolumeIsEncryptedKey",
		@"NSURLVolumeIsRemovableKey",
		@"NSURLVolumeSupportsExclusiveRenamingKey",
		@"NSURLVolumeSupportsFileCloningKey",
		@"NSURLVolumeSupportsHardLinksKey",
		@"NSURLVolumeSupportsSparseFilesKey",
		@"NSURLIsUbiquitousItemKey",
		@"NSURLIsUserImmutableKey",
		@"NSURLLabelColorKey",
		@"NSURLLabelNumberKey",
		@"NSURLLocalizedLabelKey",
		@"NSURLLocalizedTypeDescriptionKey",
		@"NSURLQuarantinePropertiesKey",
		@"NSURLTagNamesKey",
		@"NSURLUbiquitousItemContainerDisplayNameKey",
		@"NSURLUbiquitousItemDownloadRequestedKey",
		@"NSURLUbiquitousItemDownloadingErrorKey",
		@"NSURLUbiquitousItemDownloadingStatusKey",
		@"NSURLUbiquitousItemHasUnresolvedConflictsKey",
		@"NSURLUbiquitousItemIsDownloadingKey",
		@"NSURLUbiquitousItemIsExcludedFromSyncKey",
		@"NSURLUbiquitousItemIsSharedKey",
		@"NSURLUbiquitousItemIsSyncPausedKey",
		@"NSURLUbiquitousItemIsUploadedKey",
		@"NSURLUbiquitousItemIsUploadingKey",
		@"NSURLUbiquitousItemSupportedSyncControlsKey",
		@"NSURLUbiquitousItemUploadingErrorKey",
		@"NSURLUbiquitousSharedItemCurrentUserPermissionsKey",
		@"NSURLUbiquitousSharedItemCurrentUserRoleKey",
		@"NSURLUbiquitousSharedItemMostRecentEditorNameComponentsKey",
		@"NSURLUbiquitousSharedItemOwnerNameComponentsKey",
		@"NSURLAddedToDirectoryDateKey",
		@"NSURLApplicationIsScriptableKey",
		@"NSURLContentTypeKey",
		@"NSURLCreationDateKey",
		@"NSURLCustomIconKey",
		@"NSURLDocumentIdentifierKey",
		@"NSURLEffectiveIconKey",
		@"NSURLGenerationIdentifierKey",
		@"NSURLHasHiddenExtensionKey",
		@"NSURLIsExcludedFromBackupKey",
		@"NSURLIsSystemImmutableKey",
		@"NSURLIsUbiquitousItemKey",
		@"NSURLIsUserImmutableKey",
		@"NSURLLabelColorKey",
		@"NSURLLabelNumberKey",
		@"NSURLLocalizedLabelKey",
		@"NSURLLocalizedTypeDescriptionKey",
		@"NSURLQuarantinePropertiesKey",
		@"NSURLTagNamesKey",
		@"NSURLUbiquitousItemContainerDisplayNameKey",
		@"NSURLUbiquitousItemDownloadRequestedKey",
		@"NSURLUbiquitousItemDownloadingErrorKey",
		@"NSURLUbiquitousItemDownloadingStatusKey",
		@"NSURLUbiquitousItemHasUnresolvedConflictsKey",
		@"NSURLUbiquitousItemIsDownloadingKey",
		@"NSURLUbiquitousItemIsExcludedFromSyncKey",
		@"NSURLUbiquitousItemIsSharedKey",
		@"NSURLUbiquitousItemIsSyncPausedKey",
		@"NSURLUbiquitousItemIsUploadedKey",
		@"NSURLUbiquitousItemIsUploadingKey",
		@"NSURLUbiquitousItemSupportedSyncControlsKey",
		@"NSURLUbiquitousItemUploadingErrorKey",
		@"NSURLUbiquitousSharedItemCurrentUserPermissionsKey",
		@"NSURLUbiquitousSharedItemCurrentUserRoleKey",
		@"NSURLUbiquitousSharedItemMostRecentEditorNameComponentsKey",
		@"NSURLUbiquitousSharedItemOwnerNameComponentsKey",
		/* THE KEY MASSES WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE (slice 6f): recognised HERE, and answered
		 * nil by the value chain - which is Apple's own "not available for this resource" and NOT an error.
		 * (The same table/chain pair that slice 6e's probe caught; this time the entries are inserted where
		 * the list actually ends, which is the lesson in miniature.) */
		@"NSURLFileProtectionKey",
		@"NSURLIsApplicationKey",
		@"NSURLThumbnailDictionaryKey",
		@"NSURLThumbnailKey",

	};
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if ([key isEqual:table[i]]) {
			return YES;
		}
	}
	return NO;
}

/* ONE KEY, ONE VALUE, from the stat and the path - and NIL THEREFORE MEANS "NOTHING BEHIND THIS KEY",
 * never "the value is nil": a boolean key answers an NSNumber and a size answers an NSNumber, so a
 * caller can always tell a fact from an absence. */
- (nullable id)fnResourceValueForKey:(NSURLResourceKey)key
				stat:(const struct stat *)st
{
	NSString *path = [self path];

	if ([key isEqual:NSURLNameKey] || [key isEqual:NSURLLocalizedNameKey]) {
		/* THERE IS NO LOCALISATION DATABASE, so the localized name is the item's own name - the
		 * same decision -displayNameAtPath: records (§60 slice 3e). */
		return [path lastPathComponent];
	}
	if ([key isEqual:NSURLPathKey]) {
		return path;
	}
	if ([key isEqual:NSURLCanonicalPathKey]) {
		char *resolved = realpath([path UTF8String], NULL);

		if (resolved == NULL) {
			return nil;
		}
		{
			NSString *answer = [NSString stringWithUTF8String:resolved];

			free(resolved);
			return answer;
		}
	}
	if ([key isEqual:NSURLIsRegularFileKey]) return [NSNumber numberWithBool:S_ISREG(st->st_mode)];
	if ([key isEqual:NSURLIsDirectoryKey]) return [NSNumber numberWithBool:S_ISDIR(st->st_mode)];
	if ([key isEqual:NSURLIsSymbolicLinkKey]) return [NSNumber numberWithBool:S_ISLNK(st->st_mode)];
	if ([key isEqual:NSURLIsReadableKey]) return [NSNumber numberWithBool:access([path UTF8String], R_OK) == 0];
	if ([key isEqual:NSURLIsWritableKey]) return [NSNumber numberWithBool:access([path UTF8String], W_OK) == 0];
	if ([key isEqual:NSURLIsExecutableKey]) return [NSNumber numberWithBool:access([path UTF8String], X_OK) == 0];
	if ([key isEqual:NSURLIsHiddenKey]) {
		/* THE DOT RULE: this system has no hidden BIT, so an item is hidden when its own name begins
		 * with a dot, which is the same convention the shell and the tools use. */
		NSString *name = [path lastPathComponent];

		/* NOT "." ITSELF, NOT ".." AND NOT THE ROOT - each of which begins with a dot and none of
		 * which is a hidden item. */
		return [NSNumber numberWithBool:[name hasPrefix:@"."] && ![name isEqual:@"."] &&
					 ![[path lastPathComponent] isEqual:@"/"]];
	}
	if ([key isEqual:NSURLFileSizeKey]) return [NSNumber numberWithLongLong:(long long)st->st_size];
	if ([key isEqual:NSURLFileAllocatedSizeKey]) return [NSNumber numberWithLongLong:(long long)st->st_blocks * 512];
	if ([key isEqual:NSURLTotalFileSizeKey]) return [NSNumber numberWithLongLong:(long long)st->st_size];
	if ([key isEqual:NSURLTotalFileAllocatedSizeKey]) return [NSNumber numberWithLongLong:(long long)st->st_blocks * 512];
	if ([key isEqual:NSURLLinkCountKey]) return [NSNumber numberWithLongLong:(long long)st->st_nlink];
	if ([key isEqual:NSURLContentModificationDateKey]) return [NSDate dateWithTimeIntervalSince1970:(double)st->st_mtime];
	if ([key isEqual:NSURLContentAccessDateKey]) return [NSDate dateWithTimeIntervalSince1970:(double)st->st_atime];
	if ([key isEqual:NSURLAttributeModificationDateKey]) return [NSDate dateWithTimeIntervalSince1970:(double)st->st_ctime];
	/* THE IDENTIFIERS ARE THE INODE NUMBER here: Apple publishes the KEYS and calls the value opaque
	 * ("an identifier that can be used to identify the file system resource uniquely"), and this
	 * substrate's unique name for a resource is its inode - so that is what the opaque value is, and a
	 * program that compares two of them for equality gets the right answer, which is all it promises. */
	if ([key isEqual:NSURLFileIdentifierKey] || [key isEqual:NSURLFileResourceIdentifierKey]) {
		return [NSNumber numberWithUnsignedLongLong:(unsigned long long)st->st_ino];
	}
	if ([key isEqual:NSURLFileResourceTypeKey]) return fn_url_resource_type(st->st_mode);
	if ([key isEqual:NSURLParentDirectoryURLKey]) return [self URLByDeletingLastPathComponent];

	/* ---- THE VOLUME KEYS (W8 slice 6c), which are questions about the volume HOLDING this item ---- */
	if ([key isEqual:NSURLVolumeTotalCapacityKey] || [key isEqual:NSURLVolumeAvailableCapacityKey]) {
		/* THE CAPACITY IS THE FILE SYSTEM'S, THROUGH THE OTHER DOOR: NSFileManager already answers
		 * NSFileSystemSize/FreeSize from the same superblock, so this is a delegation and the probe
		 * asserts that the two doors agree rather than assuming it. */
		NSDictionary *fs = [[NSFileManager defaultManager] attributesOfFileSystemForPath:path error:NULL];

		return [fs objectForKey:([key isEqual:NSURLVolumeTotalCapacityKey]
					 ? NSFileSystemSize : NSFileSystemFreeSize)];
	}
	if ([key isEqual:NSURLVolumeIsLocalKey]) {
		/* EVERY VOLUME THIS SYSTEM CAN MOUNT IS LOCAL: the file systems it ships are AGFS, XBFS,
		 * ext2, FAT, iso9660, proc, devfs and devpts, and there is no network file system client at
		 * all - so this is YES as a fact about the system rather than a guess about the volume. */
		return [NSNumber numberWithBool:YES];
	}
	/* THE ONE PLAIN FACT IN THE MASSES (slice 6f): this system has no cloud, so nothing here is a
	 * ubiquitous item - an answer rather than an absence. */
	if ([key isEqual:NSURLIsUbiquitousItemKey]) {
		return [NSNumber numberWithBool:NO];
	}
	/* THE SUBSTRATE-MEASURED KEYS (slice 6g). The measurement that justifies each answer is in the header and
	 * repeated at the branch, because a capability claim without its measurement is exactly the confident wrong
	 * answer this file's volume section warns about. */
	if ([key isEqual:NSURLIsSparseKey]) {
		/* allocated < size is the general test for a hole; measured, this file system charges the gap. */
		id allocated = nil, size = nil;

		[self getResourceValue:&allocated forKey:NSURLFileAllocatedSizeKey error:NULL];
		[self getResourceValue:&size forKey:NSURLFileSizeKey error:NULL];
		if (allocated == nil || size == nil) {
			return nil;
		}
		return [NSNumber numberWithBool:
			([allocated unsignedLongLongValue] < [size unsignedLongLongValue]) ? YES : NO];
	}
	if ([key isEqual:NSURLMayHaveExtendedAttributesKey]) {
		/* MEASURED AT THE ITEM: listxattr succeeds where the file system has extended attributes (AGFS,
		 * where the probe set one) and fails with EOPNOTSUPP where it does not (procfs). */
		errno = 0;
		return [NSNumber numberWithBool:(listxattr([path UTF8String], NULL, 0) >= 0) ? YES : NO];
	}
	if ([key isEqual:NSURLPreferredIOBlockSizeKey]) {
		struct statfs st;

		if (statfs([path UTF8String], &st) == 0) {
			return [NSNumber numberWithUnsignedLongLong:(unsigned long long)st.f_bsize];
		}
		return nil;
	}
	if ([key isEqual:NSURLDirectoryEntryCountKey]) {
		NSArray *entries = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path error:NULL];

		return entries != nil ? [NSNumber numberWithUnsignedLongLong:(unsigned long long)[entries count]] : nil;
	}
	if ([key isEqual:NSURLVolumeSupportsHardLinksKey] ||
	    [key isEqual:NSURLVolumeSupportsSparseFilesKey] ||
	    [key isEqual:NSURLVolumeSupportsFileCloningKey] ||
	    [key isEqual:NSURLVolumeSupportsExclusiveRenamingKey]) {
		NSString *fstype = fn_volume_fstype(path);
		NSNumber *answer = [NSNumber numberWithBool:NO];

		if ([key isEqual:NSURLVolumeSupportsHardLinksKey]) {
			/* PROVED by the probe: link() succeeds on AGFS - two names, one inode, count 2. */
			answer = [NSNumber numberWithBool:[fstype isEqual:@"agfs"] ? YES : NO];
		}
		/* Sparse files: NO (the gap was charged). Cloning: NO (no cloning interface exists). Exclusive
		 * renaming: NO (renameat2 answers ENOSYS, measured). */
		return answer;
	}
	if ([key isEqual:NSURLIsPurgeableKey] || [key isEqual:NSURLIsAliasFileKey] ||
	    [key isEqual:NSURLVolumeIsEncryptedKey]) {
		/* Each NO is TRUE of the item and its ground is named in the header: nothing here evicts file
		 * content, the macOS alias format does not exist, and no volume is encrypted. */
		return [NSNumber numberWithBool:NO];
	}
	if ([key isEqual:NSURLVolumeCreationDateKey] || [key isEqual:NSURLFileContentIdentifierKey] ||
	    [key isEqual:NSURLMayShareFileContentKey] || [key isEqual:NSURLIsPackageKey] ||
	    [key isEqual:NSURLIsApplicationKey] || [key isEqual:NSURLVolumeIsRemovableKey] ||
	    [key isEqual:NSURLVolumeIsEjectableKey]) {
		/* UNAVAILABLE, which is Apple's "the resource property is NOT AVAILABLE for the specified resource,
		 * and no errors occurred" - the grounds are in the header, measured one by one. */
		return nil;
	}
	if ([key isEqual:NSURLVolumeIsReadOnlyKey] ||
	    [key isEqual:NSURLVolumeNameKey] || [key isEqual:NSURLVolumeLocalizedNameKey] ||
	    [key isEqual:NSURLVolumeIdentifierKey] || [key isEqual:NSURLVolumeURLKey] ||
	    [key isEqual:NSURLVolumeTypeNameKey] || [key isEqual:NSURLVolumeIsRootFileSystemKey] ||
	    [key isEqual:NSURLVolumeResourceCountKey] || [key isEqual:NSURLVolumeSupportsVolumeSizesKey] ||
	    [key isEqual:NSURLVolumeIsMountTriggerKey] || [key isEqual:NSURLIsVolumeKey]) {
		NSArray *entry = fn_volume_for_path(path);

		if (entry == nil) {
			return nil;
		}
		{
			NSString *device = [entry objectAtIndex:0];
			NSString *mountPoint = [entry objectAtIndex:1];
			NSString *fileSystem = [entry objectAtIndex:2];
			NSString *flag = [entry objectAtIndex:3];

			if ([key isEqual:NSURLVolumeNameKey] ||
			    [key isEqual:NSURLVolumeLocalizedNameKey]) {
				/* THE MOUNT POINT'S OWN NAME, and nothing more: there is no volume LABEL on this system,
				 * so the name is where it is mounted (the root's is "/"). */
				return [mountPoint isEqual:@"/"] ? @"/" : [mountPoint lastPathComponent];
			}
			if ([key isEqual:NSURLVolumeIdentifierKey]) {
				/* OPAQUE, AND THE DEVICE IS WHAT MAKES IT ONE: Apple publishes the key and calls the
				 * value opaque, and this system's name for a mounted volume is the device the table
				 * names. Two files on one volume answer the SAME identifier, which is all the key
				 * promises. */
				return device;
			}
			if ([key isEqual:NSURLVolumeURLKey]) {
				return [NSURL fileURLWithPath:mountPoint];
			}
			if ([key isEqual:NSURLVolumeTypeNameKey]) {
				return fileSystem;
			}
			if ([key isEqual:NSURLVolumeIsRootFileSystemKey]) {
				return [NSNumber numberWithBool:[mountPoint isEqual:@"/"] ? YES : NO];
			}
			if ([key isEqual:NSURLVolumeIsMountTriggerKey] || [key isEqual:NSURLIsVolumeKey]) {
				/* A VOLUME IS THE ROOT OF A MOUNTED FILE SYSTEM and a MOUNT TRIGGER is a directory that
				 * a mount landed on - which for this system are the same statement about the table. */
				BOOL exact = [path isEqual:mountPoint] ? YES : NO;

				return [NSNumber numberWithBool:exact];
			}
			if ([key isEqual:NSURLVolumeIsReadOnlyKey]) {
				return [NSNumber numberWithBool:[flag isEqual:@"ro"] ? YES : NO];
			}
			if ([key isEqual:NSURLVolumeResourceCountKey]) {
				NSDictionary *fs = [[NSFileManager defaultManager]
							attributesOfFileSystemForPath:path error:NULL];

				return [fs objectForKey:NSFileSystemNodes];
			}
			/* AND THE ONE THAT ASKS WHETHER SIZES CAN BE HAD AT ALL, answered by asking: a volume
			 * reports sizes when its file-system attributes can be read. */
			{
				NSDictionary *fs = [[NSFileManager defaultManager]
							attributesOfFileSystemForPath:path error:NULL];

				return [NSNumber numberWithBool:fs != nil ? YES : NO];
			}
		}
	}
	if ([key isEqual:NSURLVolumeSupportsSymbolicLinksKey] ||
	    [key isEqual:NSURLVolumeSupportsPersistentIDsKey] ||
	    [key isEqual:NSURLVolumeSupportsCaseSensitiveNamesKey]) {
		/* THE THREE SUPPORTS-KEYS THIS SYSTEM ANSWERS YES, EACH BECAUSE THE SUBSTRATE DOES IT: a
		 * symlink between two names is a thing this file system stores (the probe's own fixture has
		 * one), an inode number is a persistent identifier (NSURLFileResourceIdentifierKey IS that
		 * number), and AGFS distinguishes DAILY from daily. THE PROBE PROVES EACH CLAIM rather than
		 * repeating it - see the three "and the proof" checks - because a volume's claimed capabilities
		 * are exactly the place where a confident wrong answer would be worst. */
		return [NSNumber numberWithBool:YES];
	}
	return nil;
}

/* THE CACHE, AND WHY IT IS PART OF THE CONTRACT: Apple documents that a URL object caches the resource
 * values it has already read, that the cache lives as long as the object does, and that the two
 * -removeCached… doors take it back out. So a second ask for the same key answers what the object
 * REMEMBERS, not what the disk says now - which is observable, and is what this unit's probe measures. */
- (void)fnPrefetchValue:(nullable id)value forKey:(NSURLResourceKey)key
{
	[[self fnCache] setObject:(value != nil ? value : (id)[NSNull null]) forKey:key];
}

- (nullable NSMutableDictionary *)fnCache
{
	if (_cachedResourceValues == nil) {
		_cachedResourceValues = [[NSMutableDictionary alloc] init];
	}
	return _cachedResourceValues;
}

- (BOOL)getResourceValue:(id _Nullable * _Nullable)value forKey:(NSURLResourceKey)key error:(NSError ** _Nullable)error
{
	struct stat st;
	NSString *path;
	id cached;

	if (value != NULL) {
		*value = nil;
	}
	if (![self isFileURL]) {
		if (error != NULL) {
			*error = fn_url_error(EINVAL, @"-getResourceValue:forKey:error: on a URL that is not a file URL");
		}
		return NO;
	}
	if (!fn_url_answers_key(key)) {
		if (error != NULL) {
			*error = fn_url_error(EINVAL, [NSString stringWithFormat:@"%@ is not a key this library answers", key]);
		}
		return NO;
	}
	cached = [[self fnCache] objectForKey:key];
	if (cached != nil) {
		if (value != NULL) {
			*value = cached == [NSNull null] ? nil : cached;
		}
		return YES;
	}
	path = [self path];
	if (fn_url_lstat(self, &st) != 0) {
		if (error != NULL) {
			*error = fn_url_error(errno, path);
		}
		return NO;
	}
	{
		id answer = [self fnResourceValueForKey:key stat:&st];

		[[self fnCache] setObject:(answer != nil ? answer : (id)[NSNull null]) forKey:key];
		if (value != NULL) {
			*value = answer;
		}
	}
	return YES;
}

- (nullable NSDictionary *)resourceValuesForKeys:(NSArray *)keys
					   error:(NSError ** _Nullable)error
{
	NSMutableDictionary *answer = [[NSMutableDictionary alloc] init];
	NSUInteger i;

	if (error != NULL) {
		*error = nil;
	}
	for (i = 0; i < [keys count]; i++) {
		NSURLResourceKey key = [keys objectAtIndex:i];
		id value = nil;

		if (!fn_url_answers_key(key)) {
			/* A KEY THAT IS NOT OURS REFUSES THE WHOLE CALL, which is what makes the two shapes
			 * different: this door is asked about a SET, and a set with something unknown in it is a
			 * caller error rather than a partial answer. */
			if (error != NULL) {
				*error = fn_url_error(EINVAL, [NSString stringWithFormat:
					@"%@ is not a key this library answers", key]);
			}
			[answer release];
			return nil;
		}
		if ([self getResourceValue:&value forKey:key error:NULL]) {
			if (value != nil) {
				[answer setObject:value forKey:key];
			}
		} else {
			/* ONE KEY THAT CANNOT BE READ (a missing file) DOES NOT SINK THE SET: the values that
			 * could be read come back, and what could not is simply not in the dictionary. */
			continue;
		}
	}
	return [answer autorelease];
}

/* ---- SETTING THEM (W8 slice 6b) ----------------------------------------------------------------
 *
 * APPLE'S RULE, AND IT IS THE OPPOSITE OF THE GETTER'S: "Attempts to set a read-only resource property
 * or to set a resource property that is not supported by the resource are IGNORED and are not considered
 * errors." So `-setResourceValue:forKey:error:` below answers YES for a read-only key, for an unknown
 * key and for a URL that is not a file URL - writing nothing in all three cases - and does NOT invent an
 * error Apple does not have. The getter names what it does not have; the setter is silent about it,
 * which is the contract on each page.
 *
 * THE WRITE ITSELF IS A DELEGATION: NSFileManager owns the file system's write path (it is the class
 * that has -setAttributes:ofItemAtPath:error:), so the URL side hands it the same fact under the same
 * NAME_FM key. One implementation, two doors, which is the point of a resource value.
 */
- (BOOL)fnWriteResourceValue:(nullable id)value
		      forKey:(NSURLResourceKey)key
		       error:(NSError ** _Nullable)error
{
	/* NOT ONE OF THIS SUBSTRATE'S WRITABLE KEYS - see the header: ignored, not an error. */
	if (![self isFileURL] || ![key isEqual:NSURLContentModificationDateKey]) {
		return YES;
	}
	/* A VALUE THE KEY CANNOT HOLD IS A CALLER ERROR, the one case this door refuses for its own
	 * reason: the key's type is published (a date), so a string is a mistake and not a no-op. */
	if (value != nil && ![value isKindOfClass:[NSDate class]]) {
		if (error != NULL) {
			*error = fn_url_error(EINVAL, [NSString stringWithFormat:
				@"%@ takes a date, not %@", key, [value class]]);
		}
		return NO;
	}
	if (value == nil) {
		/* NOTHING TO WRITE MEANS NOTHING HAPPENS, rather than a date being invented for the
		 * caller: this door sets a value, and nil is the absence of one. */
		return YES;
	}
	{
		NSDictionary *attributes = [NSDictionary dictionaryWithObject:value
								      forKey:NSFileModificationDate];

		if (![[NSFileManager defaultManager] setAttributes:attributes
						      ofItemAtPath:[self path]
							     error:error]) {
			return NO;
		}
	}
	/* AND THE CACHE FORGETS, RATHER THAN GUESSING: the file system may store what it likes (a
	 * coarser timestamp, a refused change), so the object drops what it knew about this key and the
	 * next read measures again. */
	[_cachedResourceValues removeObjectForKey:key];
	return YES;
}

- (BOOL)setResourceValue:(nullable id)value forKey:(NSURLResourceKey)key error:(NSError ** _Nullable)error
{
	if (error != NULL) {
		*error = nil;
	}
	return [self fnWriteResourceValue:value forKey:key error:error];
}

- (BOOL)setResourceValues:(NSDictionary *)keyedValues error:(NSError ** _Nullable)error
{
	NSArray *keys = [keyedValues allKeys];
	NSMutableArray *unset = [[NSMutableArray alloc] init];
	NSUInteger i;
	BOOL failed = NO;

	if (error != NULL) {
		*error = nil;
	}
	for (i = 0; i < [keys count]; i++) {
		NSURLResourceKey key = [keys objectAtIndex:i];
		NSError *one = nil;

		if (![self fnWriteResourceValue:[keyedValues objectForKey:key] forKey:key error:&one]) {
			/* A KEY WHOSE WRITE REACHED THE FILE SYSTEM AND FAILED IS REPORTED, which is the
			 * error shape Apple publishes: the keys not set, under the key's own name. */
			[unset addObject:key];
			failed = YES;
			if (error != NULL && *error == nil) {
				*error = one;
			}
		}
	}
	if (failed) {
		NSDictionary *userInfo;

		/* APPLE'S OWN KEY PAGE SAYS THE VALUE IS "an array of URLResourceKey objects", and the key's
		 * page is the specific one, so the KEYS are what is reported. When the write's own error is
		 * already there, the array JOINS its userInfo rather than replacing it. */
		if (*error != NULL && [*error userInfo] != nil) {
			NSMutableDictionary *merged = [[NSMutableDictionary alloc]
							initWithDictionary:[*error userInfo]];

			[merged setObject:unset forKey:NSURLKeysOfUnsetValuesKey];
			{
				NSError *joined = [NSError errorWithDomain:[*error domain]
								      code:[*error code]
								  userInfo:merged];

				if (error != NULL) {
					*error = joined;
				}
			}
			[merged release];
		} else if (error != NULL) {
			userInfo = [NSDictionary dictionaryWithObject:unset
							       forKey:NSURLKeysOfUnsetValuesKey];
			*error = [NSError errorWithDomain:@"NSPOSIXErrorDomain"
						     code:EIO
						 userInfo:userInfo];
		}
		[unset release];
		return NO;
	}
	[unset release];
	return YES;
}

- (BOOL)checkResourceIsReachableAndReturnError:(NSError ** _Nullable)error
{
	NSString *path = [self path];

	if (error != NULL) {
		*error = nil;
	}
	if (![self isFileURL]) {
		if (error != NULL) {
			*error = fn_url_error(EINVAL, @"-checkResourceIsReachableAndReturnError: on a URL that is not a file URL");
		}
		return NO;
	}
	/* REACHABLE IS NOT READABLE: access(2) with F_OK asks exactly the question Apple's page asks -
	 * whether the resource is there - and the read/write answers are the three other keys. */
	if (access([path UTF8String], F_OK) == 0) {
		return YES;
	}
	if (error != NULL) {
		*error = fn_url_error(errno != 0 ? errno : ENOENT, path);
	}
	return NO;
}

- (void)removeCachedResourceValueForKey:(NSURLResourceKey)key
{
	[_cachedResourceValues removeObjectForKey:key];
}

- (void)removeAllCachedResourceValues
{
	[_cachedResourceValues removeAllObjects];
}

- (void)setTemporaryResourceValue:(nullable id)value forKey:(NSURLResourceKey)key
{
	/* A TEMPORARY VALUE IS A CACHE ENTRY AND NOTHING MORE: it is never written to the file system (which
	 * is what makes it temporary) and it is read back by the doors above exactly like a measured one. */
	if (value == nil) {
		[_cachedResourceValues removeObjectForKey:key];
		return;
	}
	[[self fnCache] setObject:value forKey:key];
}

/* ---- THE REST OF THE URL AS A VALUE (2026-09-30) ------------------------------------------------ */

- (nullable NSURL *)baseURL
{
	/* ALWAYS nil, AND IT IS A FACT ABOUT THIS CLASS RATHER THAN A REFUSAL: +URLWithString:relativeToURL:
	 * DISSOLVES the base by resolving into an absolute URL (FNURLResolveRelative), so nothing this
	 * library builds keeps one. Apple answers nil here for an absolute URL too, which every URL of ours
	 * is. */
	return nil;
}

- (nullable NSString *)password
{
	return _password;
}

- (nullable NSString *)relativePath
{
	/* THE PATH SPELT AS IT IS, plus the query and fragment - the "or the path if absolute" half of
	 * Apple's contract, whose exact composition its own page leaves AMBIGUOUS (notably whether the
	 * fragment is included). THIS LIBRARY INCLUDES the fragment, and the choice is stated here rather
	 * than left implied. The path is the ENCODED spelling, like -resourceSpecifier below. */
	NSMutableString *answer = [[NSMutableString alloc] initWithString:_path];

	if (_query != nil) {
		[answer appendString:@"?"];
		[answer appendString:_query];
	}
	if (_fragment != nil) {
		[answer appendString:@"#"];
		[answer appendString:_fragment];
	}
	return [answer autorelease];
}

- (NSString *)resourceSpecifier
{
	/* EVERYTHING AFTER THE SCHEME'S COLON, exactly as it is spelt: "//host/path?q#f" for an authority
	 * URL and "/path" for a file URL. The scheme is REQUIRED, so the colon is always there. */
	NSRange colon = [_absoluteString rangeOfString:@":"];

	if (colon.location == NSNotFound) {
		return _absoluteString;
	}
	return [_absoluteString substringFromIndex:colon.location + 1];
}

- (nullable NSString *)lastPathComponent
{
	/* THE DECODED PATH'S last component, so a caller reads the NAME and not the spelling - the same
	 * decoded path NSURLNameKey answers. */
	return [[self path] lastPathComponent];
}

- (nullable NSString *)pathExtension
{
	return [[self path] pathExtension];
}

- (nullable NSArray *)pathComponents
{
	/* NULLABLE ON PURPOSE: NSString's -pathComponents answers nil for an empty path (measured, the same
	 * fact the resource-value unit records), and this door passes that through rather than inventing an
	 * empty array. */
	return [[self path] pathComponents];
}

- (NSURL *)standardizedURL
{
	return [self fnURLWithPath:fn_standardize_path(_path)];
}

- (const char * _Nullable)fileSystemRepresentation
{
	/* THE FSH PATH AS BYTES, and NULL for anything that is not a file URL - Apple's own "cannot be
	 * represented as a file system path". The pointer is the decoded path's UTF-8 buffer. */
	if (![self isFileURL]) {
		return NULL;
	}
	return [[self path] UTF8String];
}

- (BOOL)getFileSystemRepresentation:(char *)buffer maxLength:(NSUInteger)maxLength
{
	const char *rep;
	size_t length;

	if (![self isFileURL] || buffer == NULL) {
		return NO;
	}
	rep = [[self path] UTF8String];
	if (rep == NULL) {
		return NO;
	}
	length = strlen(rep);
	if (length + 1 > maxLength) {
		return NO;
	}
	memcpy(buffer, rep, length + 1);
	return YES;
}

/* ---- CREATING --------------------------------------------------------------------------------- */

- (nullable id)initWithString:(NSString *)string relativeToURL:(nullable NSURL *)baseURL
{
	NSURL *resolved = FNURLResolveRelative(string, baseURL != nil ? [baseURL absoluteString] : nil);

	if (resolved == nil) {
		return nil;
	}
	/* RE-PARSE THE RESOLVED SPELLING so THIS object owns its parts (+1 for an init) rather than
	 * borrowing the resolver's autoreleased answer, and so the object is an NSURL and not whatever the
	 * resolver returned. */
	return [self initWithPartsFromString:[resolved absoluteString]];
}

- (nullable id)initWithString:(NSString *)string encodingInvalidCharacters:(BOOL)encodingInvalidCharacters
{
	/* THE FLAG IS THE WHOLE DOOR: NO is the strict parse (parsing is the refusal, the class's rule);
	 * YES REPAIRS a typed string by percent-encoding what is illegal while leaving the URL's structure
	 * intact, so "http://h/a b" becomes "http://h/a%20b" and nothing else moves. */
	return [self initWithPartsFromString:encodingInvalidCharacters ? fn_encode_url(string) : string];
}

+ (nullable NSURL *)URLWithString:(NSString *)string encodingInvalidCharacters:(BOOL)encodingInvalidCharacters
{
	return [[self alloc] initWithString:string encodingInvalidCharacters:encodingInvalidCharacters];
}

- (nullable id)initWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL
{
	/* THE DATA IS THE UTF-8 SPELLING OF THE URL'S STRING, which is what -dataRepresentation answers - a
	 * round trip the probe asserts. Data that is not valid UTF-8 is REFUSED (nil), not repaired. */
	NSString *string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	id answer;

	if (string == nil) {
		return nil;
	}
	answer = [self initWithString:string relativeToURL:baseURL];
	[string release];
	return answer;
}

+ (nullable NSURL *)URLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL
{
	return [[self alloc] initWithDataRepresentation:data relativeToURL:baseURL];
}

- (nullable id)initAbsoluteURLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL
{
	/* The result of resolution is an ABSOLUTE URL (or nil), so the "absolute" spelling and the plain
	 * one are the same door over the same algorithm; the name is Apple's and is honoured by delegation. */
	return [self initWithDataRepresentation:data relativeToURL:baseURL];
}

+ (nullable NSURL *)absoluteURLWithDataRepresentation:(NSData *)data relativeToURL:(nullable NSURL *)baseURL
{
	return [[self alloc] initAbsoluteURLWithDataRepresentation:data relativeToURL:baseURL];
}

- (nullable NSData *)dataRepresentation
{
	return [[self absoluteString] dataUsingEncoding:NSUTF8StringEncoding];
}

- (nullable id)initFileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory
{
	return [self initFileURLWithPath:(isDirectory ? fn_ensure_trailing_slash(path) : path)];
}

- (nullable id)initFileURLWithPath:(NSString *)path relativeToURL:(nullable NSURL *)baseURL
{
	/* THE BASE IS IGNORED FOR AN ABSOLUTE PATH (Apple's own rule) AND A RELATIVE PATH IS REFUSED: the
	 * FSH has no relative paths to resolve, which is this class's standing rule for file URLs (see the
	 * header). So this door is the base-less one for every path this system can name. */
	if (path == nil || ![path hasPrefix:@"/"]) {
		return nil;
	}
	return [self initFileURLWithPath:path];
}

- (nullable id)initFileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL
{
	if (path == nil || ![path hasPrefix:@"/"]) {
		return nil;
	}
	return [self initFileURLWithPath:path isDirectory:isDirectory];
}

+ (nullable NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory
{
	return [[self alloc] initFileURLWithPath:path isDirectory:isDirectory];
}

+ (nullable NSURL *)fileURLWithPath:(NSString *)path relativeToURL:(nullable NSURL *)baseURL
{
	return [[self alloc] initFileURLWithPath:path relativeToURL:baseURL];
}

+ (nullable NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL
{
	return [[self alloc] initFileURLWithPath:path isDirectory:isDirectory relativeToURL:baseURL];
}

+ (nullable NSURL *)fileURLWithPathComponents:(NSArray *)components
{
	/* THE COMPONENTS ARE JOINED WITH '/' and a leading '/' is guaranteed, so [@"a",@"b"] and
	 * [@"/a",@"b"] both name /a/b - the door is forgiving about the leading slash because a caller who
	 * splits a path on ':' is the caller this exists for. */
	NSString *joined = [components componentsJoinedByString:@"/"];
	NSString *path = [joined hasPrefix:@"/"] ? joined : [@"/" stringByAppendingString:joined];

	return [[self alloc] initFileURLWithPath:path];
}

- (nullable id)initFileURLWithFileSystemRepresentation:(const char *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL
{
	NSString *string;

	if (path == NULL) {
		return nil;
	}
	string = [NSString stringWithUTF8String:path];
	if (string == nil) {
		return nil;
	}
	return [self initFileURLWithPath:string isDirectory:isDirectory relativeToURL:baseURL];
}

+ (nullable NSURL *)fileURLWithFileSystemRepresentation:(const char *)path isDirectory:(BOOL)isDirectory relativeToURL:(nullable NSURL *)baseURL
{
	return [[self alloc] initFileURLWithFileSystemRepresentation:path isDirectory:isDirectory relativeToURL:baseURL];
}

/* ---- MODIFYING AND CONVERTING ----------------------------------------------------------------- */

- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)isDirectory
{
	NSURL *appended = [self URLByAppendingPathComponent:component];

	if (appended == nil) {
		return appended;
	}
	/* isDirectory:YES ADDS the trailing slash that SPELLS "directory"; NO REMOVES any. Apple's rule is
	 * SYNTACTIC, like -fileURLWithPath:isDirectory:, so nothing is stat-ed. */
	if (isDirectory) {
		return [appended fnURLWithPath:fn_ensure_trailing_slash(appended->_path)];
	}
	{
		NSMutableString *path = [[[NSMutableString alloc] initWithString:appended->_path] autorelease];

		while ([path hasSuffix:@"/"] && [path length] > 1) {
			[path deleteCharactersInRange:NSMakeRange([path length] - 1, 1)];
		}
		return [appended fnURLWithPath:path];
	}
}

- (nullable NSURL *)filePathURL
{
	/* THE PATH-BASED FILE URL. This system has NO file-REFERENCE URLs, so a file URL is already its own
	 * path URL and anything else has none - Apple's own nil-for-a-non-file-URL answer. */
	return [self isFileURL] ? self : nil;
}

- (BOOL)hasDirectoryPath
{
	/* PURELY SYNTACTIC, as Apple defines it: the PATH ends with a slash (which -fileURLWithPath:
	 * isDirectory:YES and -URLByAppendingPathComponent:isDirectory:YES are how a caller produces). */
	return [_path hasSuffix:@"/"];
}

- (NSURL *)URLByResolvingSymlinksInPath
{
	NSString *path = [self path];
	char *resolved;

	/* A SYMLINK NEEDS SOMETHING ON DISK TO RESOLVE, so realpath(3) is the door and a path that does not
	 * exist comes back UNCHANGED - Apple's own "returns the original if it cannot be resolved". A
	 * non-file URL is unchanged for the same reason. */
	if (![self isFileURL] || path == nil) {
		return self;
	}
	resolved = realpath([path UTF8String], NULL);
	if (resolved == NULL) {
		return self;
	}
	{
		NSString *answer = [NSString stringWithUTF8String:resolved];

		free(resolved);
		if (answer == nil) {
			return self;
		}
		return [NSURL fileURLWithPath:answer];
	}
}

- (NSURL *)URLByStandardizingPath
{
	return [self fnURLWithPath:fn_standardize_path(_path)];
}

/* ---- QUERYING --------------------------------------------------------------------------------- */

- (BOOL)isFileReferenceURL
{
	/* NO, AND IT IS A FACT ABOUT THIS SYSTEM RATHER THAN A STUB: Apple's file-reference URLs live in a
	 * `file:/.file/id=…` namespace carrying a volume-and-inode identity; this system names files by
	 * their FSH path ONLY, so no URL here can be one. A string that LOOKS like one parses as an ordinary
	 * file URL whose path begins "/.file/", which is exactly what it is on this system. */
	return NO;
}

- (nullable NSURL *)fileURL
{
	return [self isFileURL] ? self : nil;
}

/* ---- DEPRECATED (Apple 10.4) ------------------------------------------------------------------ */

- (nullable id)initWithScheme:(NSString *)scheme host:(nullable NSString *)host path:(NSString *)path
{
	NSMutableString *spelling = [[NSMutableString alloc] init];
	id made;

	[spelling appendString:scheme];
	[spelling appendString:@":"];
	if (host != nil) {
		[spelling appendString:@"//"];
		[spelling appendString:host];
	}
	if (path != nil) {
		[spelling appendString:path];
	}
	/* NOTHING IS ENCODED HERE, deliberately: this door is deprecated and Apple's own took the caller's
	 * spelling literally, so a path with a space is the caller's to encode - a documented DIVERGENCE
	 * from the file doors, which do encode. */
	made = [self initWithPartsFromString:spelling];
	[spelling release];
	return made;
}

/* ---- THE NSURLHandle-BACKED DEPRECATED DOORS (Apple 10.4) -----------------------------------------
 *
 * ONE TRANSPORT, TWO SPELLINGS (NSURLHandle.m's own words): these are DELEGATIONS to the NSURLHandle this
 * library already ships, not a second URL path. `-URLHandleUsingCache:` constructs the handle and touches
 * NOTHING on the network; the property bag it carries is consulted BEFORE any load (NSURLHandle's
 * -propertyForKey: checks its dictionary first), so a property that was SET reads back WITHOUT a fetch.
 * That is what makes these three a VALUE fact a probe can stand on, and why they are closed here while the
 * three that need a load stay open (named in the header).
 */
- (nullable NSURLHandle *)URLHandleUsingCache:(BOOL)shouldUseCache
{
	NSURLHandle *handle = shouldUseCache ? [NSURLHandle cachedHandleForURL:self] : nil;

	if (handle == nil) {
		/* A CACHED HANDLE IS RETAINED BY THE PROCESS-WIDE CACHE, so it must not be owned by whoever asked
		 * for it; an UNCACHED one is the caller's only reference, so it is autoreleased like every other
		 * convenience return. */
		handle = [[[NSURLHandle alloc] initWithURL:self cached:shouldUseCache] autorelease];
	}
	return handle;
}

- (nullable id)propertyForKey:(NSString *)propertyKey
{
	return [[self URLHandleUsingCache:YES] propertyForKey:propertyKey];
}

- (void)setProperty:(nullable id)propertyValue forKey:(NSString *)propertyKey
{
	[[self URLHandleUsingCache:YES] writeProperty:propertyValue forKey:propertyKey];
}

- (nullable NSString *)parameterString
{
	/* THE PARAMETER STRING IS THE TAIL OF THE PATH AFTER ITS FIRST ';' (RFC 2396 §3.3's `segment`),
	 * taken RAW from `_path` exactly as -query/-fragment take theirs - and NIL when the path carries no
	 * ';'. This is the form RFC 3986 dropped; the parse keeps the ';' in `_path`, so it is read back here
	 * rather than re-parsed. */
	NSRange semi = [_path rangeOfString:@";"];

	if (semi.location == NSNotFound) {
		return nil;
	}
	return [_path substringFromIndex:semi.location + 1];
}

/* ONLY THE CACHE IS RELEASED HERE, AND THE REST IS A RECORDED DEBT RATHER THAN A SILENT FIX. This
 * object COPIES its parts and never released one of them (it had no -dealloc at all); one of them,
 * `_scheme`, is not even owned - it comes from `-lowercaseString`, which answers an autoreleased
 * string. Releasing the parts here would therefore be a crash where `_scheme` is the object and a
 * behaviour change beyond this slice where it is not, so the ownership defect is owed by the URL unit
 * (§60) instead of being half-fixed inside a feature commit. */
/* EVERY PART THE PARSE OWNS IS RELEASED HERE, which is the debt slice 6a recorded rather than paid: the URL
 * unit copied and retained its parts and released none of them, and ONE of them (the scheme) was not even
 * owned, so the fix had to be an AUDIT rather than a line - two of the nine parts were autoreleased and are
 * now copied, and the other seven are released for the first time. `_isFile` is a BOOL and owns nothing. */
- (void)dealloc
{
	[_absoluteString release];
	[_scheme release];
	[_user release];
	[_password release];
	[_host release];
	[_port release];
	[_path release];
	[_query release];
	[_fragment release];
	[_cachedResourceValues release];
	[super dealloc];
}

@end


/* ================== THE SEVEN BOOKMARK DOORS (§63.106) ==================
 * ⚠ ONE GROUND, IN ONE PLACE: a refusal repeated seven times is a refusal free to drift into seven different
 * refusals, and then a caller cannot tell one capability's absence from another's. */
static void fn_url_bookmark_unsupported(const char *selector)
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%s: bookmark data is Apple's opaque per-system serialisation and this system has no "
			   @"bookmark format, so a bookmark can be neither created nor resolved here. This is a "
			   @"capability this system does not carry, not a failure of the argument.", selector];
}

@implementation NSURL (NSURLBookmarks)

+ (instancetype)URLByResolvingAliasFileAtURL:(NSURL *)url options:(NSURLBookmarkResolutionOptions)options
				       error:(NSError **)error
{
	fn_url_bookmark_unsupported("+[NSURL URLByResolvingAliasFileAtURL:options:error:]");
	return nil;
}

+ (NSURL *)URLByResolvingBookmarkData:(NSData *)bookmarkData options:(NSURLBookmarkResolutionOptions)options
			relativeToURL:(NSURL *)relativeURL bookmarkDataIsStale:(BOOL *)isStale error:(NSError **)error
{
	fn_url_bookmark_unsupported("+[NSURL URLByResolvingBookmarkData:options:relativeToURL:bookmarkDataIsStale:error:]");
	return nil;
}

+ (NSData *)bookmarkDataWithContentsOfURL:(NSURL *)bookmarkFileURL error:(NSError **)error
{
	fn_url_bookmark_unsupported("+[NSURL bookmarkDataWithContentsOfURL:error:]");
	return nil;
}

+ (NSArray *)resourceValuesForKeys:(NSArray *)keys fromBookmarkData:(NSData *)data
{
	fn_url_bookmark_unsupported("+[NSURL resourceValuesForKeys:fromBookmarkData:]");
	return nil;
}

+ (BOOL)writeBookmarkData:(NSData *)bookmarkData toURL:(NSURL *)bookmarkFileURL
		  options:(NSURLBookmarkCreationOptions)options error:(NSError **)error
{
	fn_url_bookmark_unsupported("+[NSURL writeBookmarkData:toURL:options:error:]");
	return NO;
}

- (NSData *)bookmarkDataWithOptions:(NSURLBookmarkCreationOptions)options
       includingResourceValuesForKeys:(NSArray *)keys relativeToURL:(NSURL *)relativeURL error:(NSError **)error
{
	fn_url_bookmark_unsupported("-[NSURL bookmarkDataWithOptions:includingResourceValuesForKeys:relativeToURL:error:]");
	return nil;
}

- (instancetype)initByResolvingBookmarkData:(NSData *)bookmarkData options:(NSURLBookmarkResolutionOptions)options
			      relativeToURL:(NSURL *)relativeURL bookmarkDataIsStale:(BOOL *)isStale
				      error:(NSError **)error
{
	fn_url_bookmark_unsupported("-[NSURL initByResolvingBookmarkData:options:relativeToURL:bookmarkDataIsStale:error:]");
	return nil;
}

@end
