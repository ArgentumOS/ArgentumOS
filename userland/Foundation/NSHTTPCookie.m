/*
 * NSHTTPCookie.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSHTTPCookie.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSDateFormatter.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSCharacterSet.h>

/* THE VALUES ARE THIS LIBRARY'S, CHOSEN AS THE RFC 6265 ATTRIBUTE NAMES, and the reasoning lives in the
 * header next to the declarations. Keeping the definitions together makes the choice visible in one screen
 * and keeps a printed property dictionary legible to anyone who knows HTTP. */
NSHTTPCookiePropertyKey const NSHTTPCookieName = @"Name";
NSHTTPCookiePropertyKey const NSHTTPCookieValue = @"Value";
NSHTTPCookiePropertyKey const NSHTTPCookieDomain = @"Domain";
NSHTTPCookiePropertyKey const NSHTTPCookiePath = @"Path";
NSHTTPCookiePropertyKey const NSHTTPCookiePort = @"Port";
NSHTTPCookiePropertyKey const NSHTTPCookieVersion = @"Version";
NSHTTPCookiePropertyKey const NSHTTPCookieExpires = @"Expires";
NSHTTPCookiePropertyKey const NSHTTPCookieDiscard = @"Discard";
NSHTTPCookiePropertyKey const NSHTTPCookieSecure = @"Secure";
NSHTTPCookiePropertyKey const NSHTTPCookieComment = @"Comment";
NSHTTPCookiePropertyKey const NSHTTPCookieCommentURL = @"CommentURL";
NSHTTPCookiePropertyKey const NSHTTPCookieMaximumAge = @"MaximumAge";
NSHTTPCookiePropertyKey const NSHTTPCookieOriginURL = @"OriginURL";
NSHTTPCookiePropertyKey const NSHTTPCookieSameSitePolicy = @"SameSitePolicy";
NSHTTPCookiePropertyKey const NSHTTPCookieSetByJavaScript = @"SetByJavaScript";

NSHTTPCookieStringPolicy const NSHTTPCookieSameSiteStrict = @"Strict";
NSHTTPCookieStringPolicy const NSHTTPCookieSameSiteLax = @"Lax";

/* THE DATE FORM, read off RFC 1123 rather than invented: `Set-Cookie` writes an absolute date, and the
 * documented `Expires` property accepts EITHER an NSDate or such a STRING - so a string is converted here
 * rather than silently dropped. The locale is pinned to en_US_POSIX because the wire format is a fixed
 * English one, not the user's. */
static NSDate *fn_parse_expiry(NSString *text)
{
	static NSDateFormatter *formatter = nil;
	NSDate *date;

	if(text == nil) {
		return nil;
	}
	if(!formatter) {
		formatter = [[NSDateFormatter alloc] init];
		[formatter setLocale:[[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease]];
		[formatter setDateFormat:@"EEE, dd MMM yyyy HH:mm:ss zzz"];
	}
	date = [formatter dateFromString:text];
	return date;
}

@implementation NSHTTPCookie

+ (instancetype)cookieWithProperties:(NSDictionary *)properties
{
	return [[[self alloc] initWithProperties:properties] autorelease];
}

- (instancetype)initWithProperties:(NSDictionary *)properties
{
	id value;

	if(!(self = [super init])) {
		return nil;
	}
	/* THE REQUIRED PAIR, AND IT IS THE DOCUMENTED RULE: the name's own page says "(required)", and a cookie
	 * without a name or a value is not something a store could do anything with. Answering nil is the
	 * contract - it is why the class factory is nullable. */
	value = [properties objectForKey:NSHTTPCookieName];
	if(value == nil) {
		[self release];
		return nil;
	}
	_name = [[value description] copy];
	value = [properties objectForKey:NSHTTPCookieValue];
	if(value == nil) {
		[self release];
		return nil;
	}
	_value = [[value description] copy];

	_properties = [properties copy];
	_domain = [[properties objectForKey:NSHTTPCookieDomain] copy];
	_comment = [[properties objectForKey:NSHTTPCookieComment] copy];
	_version = [[properties objectForKey:NSHTTPCookieVersion] copy];
	_sameSitePolicy = [[properties objectForKey:NSHTTPCookieSameSitePolicy] copy];
	_commentURL = [[properties objectForKey:NSHTTPCookieCommentURL] copy];
	/* THE PORT LIST IS COMMA-SEPARATED, and an ARRAY is accepted because that is how a caller holding several
	 * ports naturally has them; both spellings are one string in the end. */
	value = [properties objectForKey:NSHTTPCookiePort];
	if([value isKindOfClass:[NSArray class]]) {
		_portList = [[value componentsJoinedByString:@","] copy];
	} else {
		_portList = [[value description] copy];
	}
	/* THE RFC DEFAULT PATH IS "/", supplied rather than left absent: a cookie without one would be sent to
	 * nothing, and every implementation in the wild resolves the omission the same way. A default that is a
	 * rule is not a default that is a guess. */
	value = [properties objectForKey:NSHTTPCookiePath];
	_path = [(value != nil ? [value description] : @"/") copy];

	value = [properties objectForKey:NSHTTPCookieExpires];
	if([value isKindOfClass:[NSDate class]]) {
		_expiresDate = [value copy];
	} else if([value isKindOfClass:[NSString class]]) {
		_expiresDate = [fn_parse_expiry(value) retain];
	}
	value = [properties objectForKey:NSHTTPCookieSecure];
	_secure = [value boolValue];
	value = [properties objectForKey:@"HTTPOnly"];
	_httpOnly = [value boolValue];
	value = [properties objectForKey:NSHTTPCookieDiscard];
	_sessionOnly = [value boolValue];
	value = [properties objectForKey:NSHTTPCookieSetByJavaScript];
	_setByJavaScript = [value boolValue];

	return self;
}

- (void)dealloc
{
	[_name release];
	[_value release];
	[_domain release];
	[_path release];
	[_portList release];
	[_comment release];
	[_commentURL release];
	[_version release];
	[_sameSitePolicy release];
	[_expiresDate release];
	[_properties release];
	[super dealloc];
}

- (NSString *)domain { return _domain; }
- (NSString *)path { return _path; }
- (NSString *)portList { return _portList; }
- (NSString *)name { return _name; }
- (NSString *)value { return _value; }
- (NSString *)version { return _version; }
- (NSDate *)expiresDate { return _expiresDate; }
- (BOOL)isSecure { return _secure; }
- (BOOL)isHTTPOnly { return _httpOnly; }
- (NSHTTPCookieStringPolicy)sameSitePolicy { return _sameSitePolicy; }
- (NSString *)comment { return _comment; }
/* APPLE'S TYPE IS NSURL AND THE STORAGE IS THE ATTRIBUTE'S TEXT, so the getter CONVERTS: a Set-Cookie header
 * carries a URL as characters, and a getter that promised an NSURL while handing back a string was a promise the
 * return did not keep. */
- (nullable NSURL *)commentURL
{
	return _commentURL != nil ? [NSURL URLWithString:_commentURL] : nil;
}
- (NSDictionary *)properties { return _properties; }

/* A COOKIE WITH NO EXPIRY DIES WITH THE SESSION, and so does one carrying the old `Discard` attribute -
 * `sessionOnly` is a function of those two facts rather than a flag of its own, which is why it cannot
 * disagree with `expiresDate`. */
- (BOOL)isSessionOnly { return (_expiresDate == nil || _sessionOnly) ? YES : NO; }

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ %@=%@ path=%@>", [self class], _name, _value, _path];
}

+ (NSDictionary *)requestHeaderFieldsWithCookies:(NSArray *)cookies
{
	NSMutableArray *pairs = [NSMutableArray array];
	NSMutableDictionary *fields = [NSMutableDictionary dictionary];
	NSEnumerator *e = [cookies objectEnumerator];
	NSHTTPCookie *cookie;

	while((cookie = [e nextObject]) != nil) {
		[pairs addObject:[NSString stringWithFormat:@"%@=%@", [cookie name], [cookie value]]];
	}
	if([pairs count] > 0) {
		[fields setObject:[pairs componentsJoinedByString:@"; "] forKey:@"Cookie"];
	}
	return fields;
}

/* A `Set-Cookie` VALUE IS `name=value; Attribute; Attribute=value`, and the attribute names are matched
 * CASE-INSENSITIVELY because RFC 6265 says they are - a parser that cared about the case would reject
 * `path=/` from a server that meant the same thing. */
+ (NSHTTPCookie *)cookieFromSetCookie:(NSString *)header forURL:(NSURL *)URL
{
	NSMutableDictionary *properties = [NSMutableDictionary dictionary];
	NSArray *parts = [header componentsSeparatedByString:@";"];
	NSEnumerator *e;
	NSString *part;

	if([parts count] == 0) {
		return nil;
	}
	part = [parts objectAtIndex:0];
	{
		NSRange eq = [part rangeOfString:@"="];

		if(eq.location == NSNotFound) {
			return nil;
		}
		[properties setObject:[[part substringToIndex:eq.location]
					stringByTrimmingCharactersInSet:
					[NSCharacterSet whitespaceCharacterSet]]
			       forKey:NSHTTPCookieName];
		[properties setObject:[[part substringFromIndex:eq.location + 1]
					stringByTrimmingCharactersInSet:
					[NSCharacterSet whitespaceCharacterSet]]
			       forKey:NSHTTPCookieValue];
	}
	e = [[parts subarrayWithRange:NSMakeRange(1, [parts count] - 1)] objectEnumerator];
	while((part = [e nextObject]) != nil) {
		NSRange eq = [part rangeOfString:@"="];
		NSString *name, *value;

		if(eq.location == NSNotFound) {
			name = part;
			value = @"";
		} else {
			name = [part substringToIndex:eq.location];
			value = [part substringFromIndex:eq.location + 1];
		}
		name = [[name stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]]
				lowercaseString];
		value = [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];

		if([name isEqualToString:@"domain"]) {
			[properties setObject:value forKey:NSHTTPCookieDomain];
		} else if([name isEqualToString:@"path"]) {
			[properties setObject:value forKey:NSHTTPCookiePath];
		} else if([name isEqualToString:@"expires"]) {
			[properties setObject:value forKey:NSHTTPCookieExpires];
		} else if([name isEqualToString:@"max-age"]) {
			[properties setObject:value forKey:NSHTTPCookieMaximumAge];
		} else if([name isEqualToString:@"secure"]) {
			[properties setObject:[NSNumber numberWithBool:YES] forKey:NSHTTPCookieSecure];
		} else if([name isEqualToString:@"httponly"]) {
			[properties setObject:[NSNumber numberWithBool:YES] forKey:@"HTTPOnly"];
		} else if([name isEqualToString:@"discard"]) {
			[properties setObject:[NSNumber numberWithBool:YES] forKey:NSHTTPCookieDiscard];
		} else if([name isEqualToString:@"version"]) {
			[properties setObject:value forKey:NSHTTPCookieVersion];
		} else if([name isEqualToString:@"comment"]) {
			[properties setObject:value forKey:NSHTTPCookieComment];
		} else if([name isEqualToString:@"commenturl"]) {
			[properties setObject:value forKey:NSHTTPCookieCommentURL];
		} else if([name isEqualToString:@"port"]) {
			[properties setObject:value forKey:NSHTTPCookiePort];
		} else if([name isEqualToString:@"samesite"]) {
			[properties setObject:([value caseInsensitiveCompare:@"strict"] == NSOrderedSame)
						? NSHTTPCookieSameSiteStrict : NSHTTPCookieSameSiteLax
				       forKey:NSHTTPCookieSameSitePolicy];
		}
	}
	/* A `Set-Cookie` FROM A SERVER FREQUENTLY OMITS Domain AND Path, and the response's URL supplies them -
	 * which is why the factory takes a URL at all: a cookie without a domain would be sent nowhere. */
	if([properties objectForKey:NSHTTPCookieDomain] == nil) {
		[properties setObject:[URL host] forKey:NSHTTPCookieDomain];
	}
	if([properties objectForKey:NSHTTPCookiePath] == nil) {
		[properties setObject:@"/" forKey:NSHTTPCookiePath];
	}
	return [[[self alloc] initWithProperties:properties] autorelease];
}

+ (NSArray *)cookiesWithResponseHeaderFields:(NSDictionary *)headerFields
						     forURL:(NSURL *)URL
{
	NSMutableArray *cookies = [NSMutableArray array];
	NSEnumerator *e = [headerFields keyEnumerator];
	NSString *field;

	while((field = [e nextObject]) != nil) {
		if([field caseInsensitiveCompare:@"Set-Cookie"] == NSOrderedSame) {
			NSHTTPCookie *cookie = [self cookieFromSetCookie:[headerFields objectForKey:field]
								  forURL:URL];

			if(cookie != nil) {
				[cookies addObject:cookie];
			}
		}
	}
	return cookies;
}

@end
