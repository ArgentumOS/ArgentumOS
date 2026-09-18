/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nurl.m — NSURL: the parse, and the file-path rules. docs/design/foundation-plan.md, F8.
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

#import <foundation/NSURL.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#include "fnurl.h"		/* RFC 3986 §5.2: the resolution NSURL's relative door is FOR */
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

@implementation NSURL

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
		_scheme = [[string substringWithRange:schemeRange] lowercaseString];
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
					_user = [[authority substringWithRange:
						NSMakeRange(0, atSign.location)] copy];
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
						_port = [NSNumber numberWithInt:value];
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
 * reached through fnurl.h's function, so the two arrive together rather than one implying the
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

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;
}

@end
