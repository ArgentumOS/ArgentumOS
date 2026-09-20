/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsurlcomponents.m — a URL as eight fields (F13.15). MANUAL OWNERSHIP.
 *
 * THE PARSER WALKS BACKWARDS FROM THE END, which is the order RFC 3986's grammar allows: the
 * fragment is everything after the FIRST "#", the query everything after the first "?" in what is
 * left, the scheme is a colon that comes before any slash, and the authority — if the remainder
 * starts with "//" — ends at the next slash. Doing it in that order is what makes a "?" inside a
 * fragment a fragment's problem and a ":" inside a path a path's.
 *
 * THE RESOLVER IS §5.2 IN FULL, and the probe checks it against §5.4's own table of examples rather
 * than against strings this file could have been written to match.
 *
 * PERCENT-DECODING IS HERE because the library has no other home for it (F8's note that NSString
 * "already has it" is not the case in this tree). It DECODES only: the renderer writes back what it
 * was given, so nothing this object holds is ever re-encoded behind the caller's back.
 */

#import <foundation/NSURLComponents.h>
#import <foundation/NSURL.h>
#import <foundation/NSArray.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#include <stdlib.h>		/* malloc/free: the decoder's buffer */
#include <string.h>		/* strlen/memcpy, for a character that is already UTF-8 */

static int fn_hex_digit(unichar c)
{
	if (c >= '0' && c <= '9') {
		return (int)(c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return (int)(c - 'a') + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return (int)(c - 'A') + 10;
	}
	return -1;
}

static BOOL fn_is_hex(unichar c)
{
	return fn_hex_digit(c) >= 0;
}

static NSString *fn_percent_decode(NSString *text)
{
	NSUInteger length;
	NSUInteger i;
	char *bytes;
	NSUInteger used = 0;
	NSString *decoded;

	if (text == nil) {
		return nil;
	}
	length = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	bytes = malloc(length * 3 + 1);	/* worst case: every character is multi-byte */
	if (bytes == NULL) {
		return text;
	}
	for (i = 0; i < length; ) {
		unichar c = [text characterAtIndex:i];

		if (c == '%' && i + 2 < length &&
		    fn_is_hex([text characterAtIndex:i + 1]) &&
		    fn_is_hex([text characterAtIndex:i + 2])) {
			bytes[used++] = (char)(fn_hex_digit([text characterAtIndex:i + 1]) * 16 +
					       fn_hex_digit([text characterAtIndex:i + 2]));
			i += 3;
			continue;
		}
		if (c < 0x80) {
			bytes[used++] = (char)c;
		} else {
			/* A NON-ASCII CHARACTER IS TAKEN AS UTF-8 ALREADY, which is what a URL's bytes
			 * are: re-encoding it here would change what the caller wrote. */
			NSString *one = [text substringWithRange:NSMakeRange(i, 1)];
			const char *utf8 = [one UTF8String];
			size_t n = utf8 != NULL ? strlen(utf8) : 0;

			if (used + n < length * 3) {
				memcpy(bytes + used, utf8, n);
				used += n;
			}
		}
		i++;
	}
	bytes[used] = '\0';
	decoded = [NSString stringWithUTF8String:bytes];
	free(bytes);
	return decoded != nil ? decoded : text;
}

/* A SCHEME IS A LETTER FOLLOWED BY LETTERS, DIGITS, "+", "-" OR "." — RFC 3986 §3.1. */
static BOOL fn_is_valid_scheme(NSString *candidate)
{
	NSUInteger i;

	if ([candidate lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		return NO;
	}
	for (i = 0; i < [candidate lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
		unichar c = [candidate characterAtIndex:i];
		BOOL letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

		if (i == 0 && !letter) {
			return NO;
		}
		if (!letter && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') {
			return NO;
		}
	}
	return YES;
}

/* THE LAST "@" BEFORE THE HOST, because a user name may contain one: RFC 3986 §3.2. */
static NSInteger fn_last_at(NSString *text)
{
	NSInteger i;

	for (i = (NSInteger)[text lengthOfBytesUsingEncoding:NSUTF8StringEncoding] - 1; i >= 0; i--) {
		if ([text characterAtIndex:(NSUInteger)i] == '@') {
			return i;
		}
	}
	return -1;
}

/* --------------------------------------------------------------- RFC 3986 §5.2 */

/* §5.2.4, and it is a LOOP rather than a set of cases because "a/b/../c" has to come out as "a/c"
 * wherever the segments sit. */
static NSString *fn_remove_dot_segments(NSString *path)
{
	NSMutableArray *output = [NSMutableArray array];
	BOOL absolute = [path hasPrefix:@"/"];
	BOOL trailingSlash = [path hasSuffix:@"/"] || [path hasSuffix:@"/."] || [path hasSuffix:@"/.."];
	NSArray *segments = [path componentsSeparatedByString:@"/"];
	NSUInteger i;
	NSMutableString *result;

	for (i = 0; i < [segments count]; i++) {
		NSString *segment = [segments objectAtIndex:i];

		if ([segment isEqualToString:@"."]) {
			continue;
		}
		if ([segment isEqualToString:@".."]) {
			if ([output count] > 0) {
				[output removeLastObject];
			}
			continue;
		}
		if ([segment lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
			continue;
		}
		[output addObject:segment];
	}
	result = [NSMutableString string];
	if (absolute) {
		[result appendString:@"/"];
	}
	for (i = 0; i < [output count]; i++) {
		if (i > 0) {
			[result appendString:@"/"];
		}
		[result appendString:[output objectAtIndex:i]];
	}
	if (trailingSlash && [output count] > 0) {
		[result appendString:@"/"];
	}
	if ([result lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0 && absolute) {
		return @"/";
	}
	return result;
}

/* §5.3: the reference's path appended to the base's, MINUS the base's last segment. */
static NSString *fn_merge_paths(NSString *basePath, NSString *referencePath)
{
	NSRange lastSlash;

	if ([basePath lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		return [NSString stringWithFormat:@"/%@", referencePath];
	}
	lastSlash = [basePath rangeOfString:@"/" options:0];
	(void)lastSlash;
	{
		/* THE LAST SLASH, found by scanning from the end — the strings here are short and the
		 * search has no options door to ask for a backwards one. */
		NSInteger i;

		for (i = (NSInteger)[basePath lengthOfBytesUsingEncoding:NSUTF8StringEncoding] - 1; i >= 0; i--) {
			if ([basePath characterAtIndex:(NSUInteger)i] == '/') {
				return [NSString stringWithFormat:@"%@%@",
					[basePath substringToIndex:(NSUInteger)i + 1], referencePath];
			}
		}
	}
	return [NSString stringWithFormat:@"/%@", referencePath];
}

/* THE RESOLUTION'S ONE PUBLIC DOOR IS NSURL's, so the algorithm is reached through this function:
 * NSURLComponents' own -initWithURL:resolvingAgainstBaseURL: has no base to resolve against (this
 * library's NSURLs carry no base — F8 said so), which is exactly why F8 refused the two of them
 * together. Declared here rather than in the header because it is a detail of THIS file's class. */
@interface NSURLComponents (FNPrivate)
- (NSURLComponents *)fnResolveAgainst:(NSURLComponents *)base;
@end

NSURL * _Nullable FNURLResolveRelative(NSString *reference, NSString * _Nullable base)
{
	NSURLComponents *ref = [NSURLComponents componentsWithString:reference];
	NSURLComponents *bas = base != nil ? [NSURLComponents componentsWithString:base] : nil;
	NSURLComponents *target;

	if (ref == nil) {
		return nil;
	}
	target = [ref fnResolveAgainst:bas];
	return [target URL];
}

@implementation NSURLQueryItem

+ (instancetype)queryItemWithName:(NSString *)name value:(nullable NSString *)value
{
	return [[self alloc] initWithName:name value:value];
}

- (instancetype)initWithName:(NSString *)name value:(nullable NSString *)value
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_name = name;
	_value = value;
	return self;
}

- (NSString *)name
{
	return _name;
}

- (nullable NSString *)value
{
	return _value;
}

- (BOOL)isEqual:(id)other
{
	NSURLQueryItem *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSURLQueryItem class]]) {
		return NO;
	}
	them = (NSURLQueryItem *)other;
	return [_name isEqualToString:them->_name] &&
	       (_value == them->_value || [_value isEqualToString:them->_value]);
}

- (NSUInteger)hash
{
	return [_name hash] ^ [_value hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"%@=%@", _name, _value];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

@end

@implementation NSURLComponents

+ (nullable instancetype)componentsWithString:(NSString *)URLString
{
	return [[self alloc] initWithString:URLString];
}

+ (nullable instancetype)componentsWithURL:(NSURL *)url
		    resolvingAgainstBaseURL:(BOOL)resolve
{
	return [[self alloc] initWithURL:url resolvingAgainstBaseURL:resolve];
}

- (nullable instancetype)initWithString:(NSString *)URLString
{
	self = [super init];
	if (self == nil || URLString == nil) {
		return nil;
	}
	[self fnParse:URLString];
	return self;
}

- (nullable instancetype)initWithURL:(NSURL *)url resolvingAgainstBaseURL:(BOOL)resolve
{
	self = [super init];
	if (self == nil || url == nil) {
		return nil;
	}
	[self fnParse:[url absoluteString]];
	if (resolve && [url isFileURL] == NO && [self scheme] == nil) {
		/* WITHOUT A BASE THERE IS NOTHING TO RESOLVE AGAINST, and a components object with no
		 * scheme is exactly what a reference looks like — which is the honest answer. */
	}
	return self;
}

- (void)fnParse:(NSString *)URLString
{
	NSString *rest = URLString;
	NSRange cut;

	cut = [rest rangeOfString:@"#"];
	if (cut.location != NSNotFound) {
		_fragment = [rest substringFromIndex:NSMaxRange(cut)];
		rest = [rest substringToIndex:cut.location];
	}
	cut = [rest rangeOfString:@"?"];
	if (cut.location != NSNotFound) {
		_query = [rest substringFromIndex:NSMaxRange(cut)];
		rest = [rest substringToIndex:cut.location];
	}
	{
		NSRange colon = [rest rangeOfString:@":"];
		NSRange slash = [rest rangeOfString:@"/"];

		if (colon.location != NSNotFound &&
		    (slash.location == NSNotFound || colon.location < slash.location)) {
			NSString *candidate = [rest substringToIndex:colon.location];

			if (fn_is_valid_scheme(candidate)) {
				_scheme = candidate;
				rest = [rest substringFromIndex:NSMaxRange(colon)];
			}
		}
	}
	if ([rest hasPrefix:@"//"]) {
		NSString *authority;
		NSString *hostPort;
		NSInteger at;
		NSRange slash;

		rest = [rest substringFromIndex:2];
		slash = [rest rangeOfString:@"/"];
		if (slash.location == NSNotFound) {
			authority = rest;
			rest = @"";
		} else {
			authority = [rest substringToIndex:slash.location];
			rest = [rest substringFromIndex:slash.location];
		}
		hostPort = authority;
		at = fn_last_at(authority);
		if (at >= 0) {
			NSString *userInfo = [authority substringToIndex:(NSUInteger)at];
			NSRange colon = [userInfo rangeOfString:@":"];

			if (colon.location != NSNotFound) {
				_user = [userInfo substringToIndex:colon.location];
				_password = [userInfo substringFromIndex:NSMaxRange(colon)];
			} else {
				_user = userInfo;
			}
			hostPort = [authority substringFromIndex:(NSUInteger)at + 1];
		}
		if ([hostPort hasPrefix:@"["]) {
			/* AN IPv6 LITERAL: THE PORT IS AFTER THE CLOSING BRACKET, and a colon inside the
			 * brackets is not a port separator. */
			NSInteger i;

			for (i = 1; i < (NSInteger)[hostPort lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
				unichar c = [hostPort characterAtIndex:(NSUInteger)i];

				if (c == ']') {
					_host = [hostPort substringToIndex:(NSUInteger)i + 1];
					if (i + 1 < (NSInteger)[hostPort lengthOfBytesUsingEncoding:NSUTF8StringEncoding] &&
					    [hostPort characterAtIndex:(NSUInteger)i + 1] == ':') {
						_port = [hostPort substringFromIndex:(NSUInteger)i + 2];
					}
					break;
				}
			}
		} else {
			NSRange colon = [hostPort rangeOfString:@":"];

			if (colon.location != NSNotFound) {
				_host = [hostPort substringToIndex:colon.location];
				_port = [hostPort substringFromIndex:NSMaxRange(colon)];
			} else {
				_host = hostPort;
			}
		}
	}
	_path = rest;
}

- (nullable NSString *)string
{
	NSMutableString *out = [NSMutableString string];

	if (_scheme != nil) {
		[out appendString:_scheme];
		[out appendString:@":"];
	}
	if (_host != nil || _user != nil) {
		[out appendString:@"//"];
		if (_user != nil) {
			[out appendString:_user];
			if (_password != nil) {
				[out appendString:@":"];
				[out appendString:_password];
			}
			[out appendString:@"@"];
		}
		if (_host != nil) {
			[out appendString:_host];
		}
		if (_port != nil) {
			[out appendString:@":"];
			[out appendString:_port];
		}
	}
	if (_path != nil) {
		[out appendString:_path];
	}
	if (_query != nil) {
		[out appendString:@"?"];
		[out appendString:_query];
	}
	if (_fragment != nil) {
		[out appendString:@"#"];
		[out appendString:_fragment];
	}
	return [out lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > 0 ? out : nil;
}

- (nullable NSURL *)URL
{
	NSString *text = [self string];

	return text != nil ? [NSURL URLWithString:text] : nil;
}

- (nullable NSString *)scheme { return _scheme; }
- (void)setScheme:(nullable NSString *)scheme { _scheme = scheme; }
- (void)setUser:(nullable NSString *)user { _user = user; }
- (void)setPassword:(nullable NSString *)password { _password = password; }
- (void)setHost:(nullable NSString *)host { _host = host; }
- (void)setPath:(nullable NSString *)path { _path = path; }
- (void)setQuery:(nullable NSString *)query { _query = query; }
- (void)setFragment:(nullable NSString *)fragment { _fragment = fragment; }

- (nullable NSString *)percentEncodedUser { return _user; }
- (nullable NSString *)percentEncodedPassword { return _password; }
- (nullable NSString *)percentEncodedHost { return _host; }
- (nullable NSString *)percentEncodedPath { return _path; }
- (nullable NSString *)percentEncodedQuery { return _query; }
- (nullable NSString *)percentEncodedFragment { return _fragment; }

- (nullable NSString *)user { return fn_percent_decode(_user); }
- (nullable NSString *)password { return fn_percent_decode(_password); }
- (nullable NSString *)host { return fn_percent_decode(_host); }
- (nullable NSString *)path { return fn_percent_decode(_path); }
- (nullable NSString *)query { return fn_percent_decode(_query); }
- (nullable NSString *)fragment { return fn_percent_decode(_fragment); }

- (nullable NSNumber *)port
{
	if (_port == nil) {
		return nil;
	}
	return [NSNumber numberWithInt:[_port intValue]];
}

- (void)setPort:(nullable NSNumber *)port
{
	_port = port != nil ? [port stringValue] : nil;
}

- (nullable NSArray *)queryItems
{
	NSMutableArray *items;
	NSArray *pairs;
	NSUInteger i;

	if (_query == nil) {
		return nil;
	}
	items = [NSMutableArray array];
	pairs = [_query componentsSeparatedByString:@"&"];
	for (i = 0; i < [pairs count]; i++) {
		NSString *pair = [pairs objectAtIndex:i];
		NSRange equals = [pair rangeOfString:@"="];

		if ([pair lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
			continue;
		}
		if (equals.location == NSNotFound) {
			[items addObject:[NSURLQueryItem queryItemWithName:fn_percent_decode(pair)
								    value:nil]];
		} else {
			[items addObject:[NSURLQueryItem
				queryItemWithName:fn_percent_decode([pair substringToIndex:equals.location])
					    value:fn_percent_decode([pair substringFromIndex:
									NSMaxRange(equals)])]];
		}
	}
	return items;
}

- (void)setQueryItems:(nullable NSArray *)queryItems
{
	NSMutableString *out;
	NSUInteger i;

	if (queryItems == nil) {
		_query = nil;
		return;
	}
	out = [NSMutableString string];
	for (i = 0; i < [queryItems count]; i++) {
		NSURLQueryItem *item = [queryItems objectAtIndex:i];

		if (i > 0) {
			[out appendString:@"&"];
		}
		[out appendString:[item name]];
		if ([item value] != nil) {
			[out appendString:@"="];
			[out appendString:[item value]];
		}
	}
	_query = out;
}

/* THE RESOLUTION, and it is the algorithm rather than a set of special cases: the reference's own
 * scheme wins; then its authority; then an EMPTY PATH keeps the base's path and query; then a path
 * starting with "/" replaces the base's; and anything else merges with the base's last segment. The
 * fragment always comes from the reference. */
- (NSURLComponents *)fnResolveAgainst:(NSURLComponents *)base
{
	NSURLComponents *target = [[NSURLComponents alloc] init];

	if (_scheme != nil) {
		target->_scheme = _scheme;
		target->_user = _user;
		target->_password = _password;
		target->_host = _host;
		target->_port = _port;
		target->_path = fn_remove_dot_segments(_path != nil ? _path : @"");
		target->_query = _query;
	} else if (_host != nil || _user != nil) {
		target->_scheme = base->_scheme;
		target->_user = _user;
		target->_password = _password;
		target->_host = _host;
		target->_port = _port;
		target->_path = fn_remove_dot_segments(_path != nil ? _path : @"");
		target->_query = _query;
	} else if (_path == nil || [_path lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		target->_scheme = base->_scheme;
		target->_user = base->_user;
		target->_password = base->_password;
		target->_host = base->_host;
		target->_port = base->_port;
		target->_path = base->_path;
		target->_query = _query != nil ? _query : base->_query;
	} else if ([_path hasPrefix:@"/"]) {
		target->_scheme = base->_scheme;
		target->_user = base->_user;
		target->_password = base->_password;
		target->_host = base->_host;
		target->_port = base->_port;
		target->_path = fn_remove_dot_segments(_path);
		target->_query = _query;
	} else {
		target->_scheme = base->_scheme;
		target->_user = base->_user;
		target->_password = base->_password;
		target->_host = base->_host;
		target->_port = base->_port;
		target->_path = fn_remove_dot_segments(fn_merge_paths(base->_path != nil ? base->_path : @"",
								     _path));
		target->_query = _query;
	}
	target->_fragment = _fragment;
	return target;
}

- (BOOL)isEqual:(id)other
{
	NSURLComponents *them;
	NSString *mine;
	NSString *theirs;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSURLComponents class]]) {
		return NO;
	}
	them = (NSURLComponents *)other;
	mine = [self string];
	theirs = [them string];
	return mine == theirs || [mine isEqualToString:theirs];
}

- (NSUInteger)hash
{
	return [[self string] hash];
}

- (NSString *)description
{
	return [self string];
}

- (id)copy
{
	NSURLComponents *copy;

	copy = [[NSURLComponents alloc] init];
	copy->_scheme = _scheme;
	copy->_user = _user;
	copy->_password = _password;
	copy->_host = _host;
	copy->_port = _port;
	copy->_path = _path;
	copy->_query = _query;
	copy->_fragment = _fragment;
	return copy;
}

@end
