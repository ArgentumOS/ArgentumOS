/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHTTPURLResponse.m — the status, the headers, and the ONE piece of derived arithmetic this slice has.
 * The design and the reasoning are in NSHTTPURLResponse.h.
 *
 * THE DERIVATION IS THE CLASS'S REAL WORK: -initWithURL:statusCode:HTTPVersion:headerFields: takes the
 * response's headers and answers the INHERITED MIME type, text-encoding name and expected length from
 * them, so a response whose headers say `Content-Type: text/html; charset=utf-8` and
 * `Content-Length: 42` reports both rather than "unknown". Nothing here touches a socket: the headers are
 * a value the caller already has.
 */
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

/* The field name, case-insensitively (RFC 9110 §5.1) — the same rule the request's door uses, kept here
 * so this translation unit needs nothing from NSURLRequest.m. */
static NSString *fn_http_header_key(NSDictionary *headers, NSString *field)
{
	NSEnumerator *enumerator;
	NSString *key;

	if (headers == nil) {
		return nil;
	}
	enumerator = [headers keyEnumerator];
	while ((key = [enumerator nextObject]) != nil) {
		if ([key caseInsensitiveCompare:field] == NSOrderedSame) {
			return key;
		}
	}
	return nil;
}

static NSString *fn_http_header_value(NSDictionary *headers, NSString *field)
{
	NSString *key = fn_http_header_key(headers, field);

	return key != nil ? [headers objectForKey:key] : nil;
}

/* THE MEDIA TYPE, with any parameters removed: `text/html; charset=utf-8` -> `text/html`. */
static NSString *fn_media_type(NSString *contentType)
{
	NSRange semicolon;

	if (contentType == nil) {
		return nil;
	}
	semicolon = [contentType rangeOfString:@";"];
	if (semicolon.location != NSNotFound) {
		contentType = [contentType substringToIndex:semicolon.location];
	}
	return [contentType stringByTrimmingCharactersInSet:
			[NSCharacterSet whitespaceCharacterSet]];
}

/* THE `charset` PARAMETER OF A Content-Type, or nil. The parameter name is matched case-insensitively
 * because HTTP's parameter names are (RFC 9110 §5.1) and a header may spell it `Charset`. */
static NSString *fn_charset(NSString *contentType)
{
	NSRange semicolon;
	NSArray *params;
	NSUInteger i;

	if (contentType == nil) {
		return nil;
	}
	semicolon = [contentType rangeOfString:@";"];
	if (semicolon.location == NSNotFound) {
		return nil;
	}
	params = [[contentType substringFromIndex:semicolon.location + 1]
			componentsSeparatedByString:@";"];
	for (i = 0; i < [params count]; i++) {
		NSString *param = [[params objectAtIndex:i] stringByTrimmingCharactersInSet:
					[NSCharacterSet whitespaceCharacterSet]];
		NSRange equals = [param rangeOfString:@"="];

		if (equals.location == NSNotFound) {
			continue;
		}
		if ([[param substringToIndex:equals.location] caseInsensitiveCompare:@"charset"]
				== NSOrderedSame) {
			NSString *value = [[param substringFromIndex:equals.location + 1]
						stringByTrimmingCharactersInSet:
						[NSCharacterSet whitespaceCharacterSet]];
			/* A quoted parameter value is legal and the quotes are not part of it. */
			if ([value length] >= 2 && [value hasPrefix:@"\""] && [value hasSuffix:@"\""]) {
				value = [value substringWithRange:NSMakeRange(1, [value length] - 2)];
			}
			return value;
		}
	}
	return nil;
}

@implementation NSHTTPURLResponse

- (instancetype)initWithURL:(NSURL *)URL
		 statusCode:(NSInteger)statusCode
		HTTPVersion:(NSString *)HTTPVersion
	       headerFields:(NSDictionary *)headerFields
{
	NSString *contentType = fn_http_header_value(headerFields, @"Content-Type");
	NSString *length = fn_http_header_value(headerFields, @"Content-Length");
	NSInteger expected = NSURLResponseUnknownLength;

	/* Content-Length is a decimal byte count; anything that is not one leaves the length UNKNOWN rather
	 * than answered wrongly (RFC 9110 §8.6). */
	if (length != nil) {
		NSInteger parsed = (NSInteger)[length longLongValue];

		if (parsed >= 0) {
			expected = parsed;
		}
	}
	self = [super initWithURL:URL
			MIMEType:fn_media_type(contentType)
	 expectedContentLength:expected
	    textEncodingName:fn_charset(contentType)];
	if (self == nil) {
		return nil;
	}
	(void)HTTPVersion;	/* accepted and not stored: Apple exposes no getter for it (see the header) */
	_statusCode = statusCode;
	_allHeaderFields = headerFields != nil ? [headerFields copy] : [[NSDictionary alloc] init];
	return self;
}

- (NSInteger)statusCode
{
	return _statusCode;
}

- (NSDictionary *)allHeaderFields
{
	return _allHeaderFields;
}

- (NSString *)valueForHTTPHeaderField:(NSString *)field
{
	return fn_http_header_value(_allHeaderFields, field);
}

/* RFC 9110 §15's phrase registry, plus §15.5.17's 418 (RFC 2324). A code the registry does not define
 * answers nil — the header states that plainly rather than inventing a phrase. */
+ (NSString *)localizedStringForStatusCode:(NSInteger)statusCode
{
	switch (statusCode) {
	case 100: return @"Continue";
	case 101: return @"Switching Protocols";
	case 200: return @"OK";
	case 201: return @"Created";
	case 202: return @"Accepted";
	case 203: return @"Non-Authoritative Information";
	case 204: return @"No Content";
	case 205: return @"Reset Content";
	case 206: return @"Partial Content";
	case 300: return @"Multiple Choices";
	case 301: return @"Moved Permanently";
	case 302: return @"Found";
	case 303: return @"See Other";
	case 304: return @"Not Modified";
	case 305: return @"Use Proxy";
	case 307: return @"Temporary Redirect";
	case 308: return @"Permanent Redirect";
	case 400: return @"Bad Request";
	case 401: return @"Unauthorized";
	case 402: return @"Payment Required";
	case 403: return @"Forbidden";
	case 404: return @"Not Found";
	case 405: return @"Method Not Allowed";
	case 406: return @"Not Acceptable";
	case 407: return @"Proxy Authentication Required";
	case 408: return @"Request Timeout";
	case 409: return @"Conflict";
	case 410: return @"Gone";
	case 411: return @"Length Required";
	case 412: return @"Precondition Failed";
	case 413: return @"Content Too Large";
	case 414: return @"URI Too Long";
	case 415: return @"Unsupported Media Type";
	case 416: return @"Range Not Satisfiable";
	case 417: return @"Expectation Failed";
	case 418: return @"I'm a teapot";
	case 421: return @"Misdirected Request";
	case 422: return @"Unprocessable Content";
	case 426: return @"Upgrade Required";
	case 428: return @"Precondition Required";
	case 429: return @"Too Many Requests";
	case 431: return @"Request Header Fields Too Large";
	case 451: return @"Unavailable For Legal Reasons";
	case 500: return @"Internal Server Error";
	case 501: return @"Not Implemented";
	case 502: return @"Bad Gateway";
	case 503: return @"Service Unavailable";
	case 504: return @"Gateway Timeout";
	case 505: return @"HTTP Version Not Supported";
	case 506: return @"Variant Also Negotiates";
	case 507: return @"Insufficient Storage";
	case 508: return @"Loop Detected";
	case 510: return @"Not Extended";
	case 511: return @"Network Authentication Required";
	default: return nil;
	}
}

- (void)dealloc
{
	[_allHeaderFields release];
	[super dealloc];
}

@end
