/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLRequest.m — the value's machinery. The design and the reasoning are in NSURLRequest.h.
 *
 * THE DEFAULTS ARE STATED IN ONE PLACE SO THE PROBE CAN PIN THEM: GET, cookies handled, no pipelining,
 * cellular allowed, attribution Developer, network service type Default, and a 60-second timeout from the
 * two-argument door. The three booleans and the two enums are Apple-documented BY NAME only, so their
 * values are this tree's (§11.6.1 D2).
 *
 * THE HEADER SNAPSHOT IS ALWAYS IMMUTABLE. `_allHTTPHeaderFields` is an immutable NSDictionary (or nil),
 * never a live mutable one, so a dictionary a caller obtained from -allHTTPHeaderFields cannot change
 * under it and the class's own consistency is a property of the storage rather than of a comment. The
 * mutable class's header mutations therefore work on a temporary mutable copy and store the result back
 * as an immutable copy — one line more, and no shared mutable state.
 */
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSStream.h>		/* NSInputStream */
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

/* THE DEFAULT TIMEOUT: Apple documents "the default" and not the number, so 60 s is this tree's under
 * D2, chosen because it is HTTP's conventional client timeout. */
#define FN_URLREQUEST_DEFAULT_TIMEOUT	60.0

/* THE FIELD NAME, CASE-INSENSITIVELY (RFC 9110 §5.1). Answers the KEY AS STORED so that a replacement
 * keeps the caller's original spelling rather than the spelling it was looked up with. */
static NSString *fn_header_key(NSDictionary *headers, NSString *field)
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

/* NIL-SAFE EQUALITY, which is the rule the field comparison needs: two absent fields are equal, and an
 * absent one never equals a present one. */
static BOOL fn_object_equal(NSObject *a, NSObject *b)
{
	if (a == b) {
		return YES;
	}
	if (a == nil || b == nil) {
		return NO;
	}
	return [a isEqual:b];
}

/* THE ONE SEAM -copy AND -mutableCopy SHARE, so the field list is written once and cannot drift between
 * the two. It lives in the .m rather than the public header because it is not API: Apple has no such
 * initializer, and a caller has no reason to reach it. */
@interface NSURLRequest (FNPrivate)
- (instancetype)fnInitWithRequest:(NSURLRequest *)other;
@end

@implementation NSURLRequest

+ (instancetype)requestWithURL:(NSURL *)URL
{
	return [[[self alloc] initWithURL:URL] autorelease];
}

+ (instancetype)requestWithURL:(NSURL *)URL
		   cachePolicy:(NSURLRequestCachePolicy)cachePolicy
	       timeoutInterval:(NSTimeInterval)timeoutInterval
{
	return [[[self alloc] initWithURL:URL cachePolicy:cachePolicy
			  timeoutInterval:timeoutInterval] autorelease];
}

- (instancetype)initWithURL:(NSURL *)URL
{
	return [self initWithURL:URL
		     cachePolicy:NSURLRequestUseProtocolCachePolicy
		 timeoutInterval:FN_URLREQUEST_DEFAULT_TIMEOUT];
}

- (instancetype)initWithURL:(NSURL *)URL
		cachePolicy:(NSURLRequestCachePolicy)cachePolicy
	    timeoutInterval:(NSTimeInterval)timeoutInterval
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_url = [URL copy];
	_cachePolicy = cachePolicy;
	_timeoutInterval = timeoutInterval;
	_mainDocumentURL = nil;
	_networkServiceType = NSURLNetworkServiceTypeDefault;
	_attribution = NSURLRequestAttributionDeveloper;
	_HTTPMethod = [@"GET" copy];
	_allHTTPHeaderFields = nil;
	_HTTPBody = nil;
	_HTTPBodyStream = nil;
	_HTTPShouldHandleCookies = YES;
	_HTTPShouldUsePipelining = NO;
	_allowsCellularAccess = YES;
	_allowsConstrainedNetworkAccess = YES;
	_allowsExpensiveNetworkAccess = YES;
	_allowsUltraConstrainedNetworkAccess = YES;
	_requiresDNSSECValidation = NO;
	_assumesHTTP3Capable = NO;
	_allowsPersistentDNS = NO;
	_cookiePartitionIdentifier = nil;
	return self;
}

/* BUILD A REQUEST OF THE RECEIVER'S CLASS from `other`'s fields. `self` is an NSURLRequest for an
 * immutable snapshot and an NSMutableURLRequest for -mutableCopy, so the fields are adopted through the
 * IVARS: this must also work for the plain class, which has no setters. */
- (instancetype)fnInitWithRequest:(NSURLRequest *)other
{
	self = [self initWithURL:[other URL]
		     cachePolicy:[other cachePolicy]
		 timeoutInterval:[other timeoutInterval]];
	if (self == nil) {
		return nil;
	}
	/* The designated initializer above already filled every field with its DEFAULT, so the ones it set to
	 * an object are given back before being replaced (the `@"GET"` copy would otherwise leak). */
	[_mainDocumentURL release];
	[_HTTPMethod release];
	[_allHTTPHeaderFields release];
	[_HTTPBody release];
	[_HTTPBodyStream release];
	[_cookiePartitionIdentifier release];
	_mainDocumentURL = [[other mainDocumentURL] copy];
	_networkServiceType = [other networkServiceType];
	_attribution = [other attribution];
	_HTTPMethod = [[other HTTPMethod] copy];
	_allHTTPHeaderFields = [[other allHTTPHeaderFields] copy];
	_HTTPBody = [[other HTTPBody] copy];
	_HTTPBodyStream = [[other HTTPBodyStream] retain];
	_HTTPShouldHandleCookies = [other HTTPShouldHandleCookies];
	_HTTPShouldUsePipelining = [other HTTPShouldUsePipelining];
	_allowsCellularAccess = [other allowsCellularAccess];
	_allowsConstrainedNetworkAccess = [other allowsConstrainedNetworkAccess];
	_allowsExpensiveNetworkAccess = [other allowsExpensiveNetworkAccess];
	_allowsUltraConstrainedNetworkAccess = [other allowsUltraConstrainedNetworkAccess];
	_requiresDNSSECValidation = [other requiresDNSSECValidation];
	_assumesHTTP3Capable = [other assumesHTTP3Capable];
	_allowsPersistentDNS = [other allowsPersistentDNS];
	_cookiePartitionIdentifier = [[other cookiePartitionIdentifier] copy];
	return self;
}

#pragma mark - accessors

- (NSURL *)URL
{
	return _url;
}

- (NSURLRequestCachePolicy)cachePolicy
{
	return _cachePolicy;
}

- (NSTimeInterval)timeoutInterval
{
	return _timeoutInterval;
}

- (NSURL *)mainDocumentURL
{
	return _mainDocumentURL;
}

- (NSURLRequestNetworkServiceType)networkServiceType
{
	return _networkServiceType;
}

- (NSURLRequestAttribution)attribution
{
	return _attribution;
}

- (NSString *)HTTPMethod
{
	return _HTTPMethod;
}

- (NSDictionary *)allHTTPHeaderFields
{
	return _allHTTPHeaderFields;
}

- (NSData *)HTTPBody
{
	return _HTTPBody;
}

- (NSInputStream *)HTTPBodyStream
{
	return _HTTPBodyStream;
}

- (BOOL)HTTPShouldHandleCookies
{
	return _HTTPShouldHandleCookies;
}

- (BOOL)HTTPShouldUsePipelining
{
	return _HTTPShouldUsePipelining;
}

- (BOOL)allowsCellularAccess
{
	return _allowsCellularAccess;
}

- (BOOL)allowsConstrainedNetworkAccess
{
	return _allowsConstrainedNetworkAccess;
}

- (BOOL)allowsExpensiveNetworkAccess
{
	return _allowsExpensiveNetworkAccess;
}

- (BOOL)allowsUltraConstrainedNetworkAccess
{
	return _allowsUltraConstrainedNetworkAccess;
}

- (BOOL)requiresDNSSECValidation
{
	return _requiresDNSSECValidation;
}

- (BOOL)assumesHTTP3Capable
{
	return _assumesHTTP3Capable;
}

- (BOOL)allowsPersistentDNS
{
	return _allowsPersistentDNS;
}

- (NSString *)cookiePartitionIdentifier
{
	return _cookiePartitionIdentifier;
}

- (NSString *)valueForHTTPHeaderField:(NSString *)field
{
	NSString *key = fn_header_key(_allHTTPHeaderFields, field);

	return key != nil ? [_allHTTPHeaderFields objectForKey:key] : nil;
}

#pragma mark - value contract

- (BOOL)isEqual:(id)other
{
	NSURLRequest *request;

	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSURLRequest class]]) {
		return NO;
	}
	request = (NSURLRequest *)other;
	return fn_object_equal(_url, [request URL]) &&
	       _cachePolicy == [request cachePolicy] &&
	       _timeoutInterval == [request timeoutInterval] &&
	       fn_object_equal(_mainDocumentURL, [request mainDocumentURL]) &&
	       _networkServiceType == [request networkServiceType] &&
	       _attribution == [request attribution] &&
	       fn_object_equal(_HTTPMethod, [request HTTPMethod]) &&
	       fn_object_equal(_allHTTPHeaderFields, [request allHTTPHeaderFields]) &&
	       fn_object_equal(_HTTPBody, [request HTTPBody]) &&
	       _HTTPBodyStream == [request HTTPBodyStream] &&
	       _HTTPShouldHandleCookies == [request HTTPShouldHandleCookies] &&
	       _HTTPShouldUsePipelining == [request HTTPShouldUsePipelining] &&
	       _allowsCellularAccess == [request allowsCellularAccess];
}

- (NSUInteger)hash
{
	/* THE HASH IS OURS (D2): equal requests hash equally because the components folded here are the ones
	 * the equality test compares — the URL's hash and the cache policy's value. */
	return [_url hash] ^ (NSUInteger)_cachePolicy;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@ policy=%d timeout=%g method=%@>",
		[self class], _url, (int)_cachePolicy, _timeoutInterval, _HTTPMethod];
}

- (void)dealloc
{
	[_url release];
	[_mainDocumentURL release];
	[_HTTPMethod release];
	[_allHTTPHeaderFields release];
	[_HTTPBody release];
	[_HTTPBodyStream release];
	[super dealloc];
}

- (id)copy
{
	/* IMMUTABLE, so the copy IS the receiver (plan §15.2: `copy` is an OWNED family). */
	return [self retain];
}

- (id)mutableCopy
{
	return [[NSMutableURLRequest alloc] fnInitWithRequest:self];
}


+ (BOOL)supportsSecureCoding
{
	return YES;
}
@end

@implementation NSMutableURLRequest

- (void)setURL:(NSURL *)URL
{
	NSURL *old = _url;

	_url = [URL copy];
	[old release];
}

- (void)setCachePolicy:(NSURLRequestCachePolicy)cachePolicy
{
	_cachePolicy = cachePolicy;
}

- (void)setTimeoutInterval:(NSTimeInterval)timeoutInterval
{
	_timeoutInterval = timeoutInterval;
}

- (void)setMainDocumentURL:(NSURL *)mainDocumentURL
{
	NSURL *old = _mainDocumentURL;

	_mainDocumentURL = [mainDocumentURL copy];
	[old release];
}

- (void)setNetworkServiceType:(NSURLRequestNetworkServiceType)networkServiceType
{
	_networkServiceType = networkServiceType;
}

- (void)setAttribution:(NSURLRequestAttribution)attribution
{
	_attribution = attribution;
}

- (void)setHTTPMethod:(NSString *)HTTPMethod
{
	NSString *old = _HTTPMethod;

	_HTTPMethod = [HTTPMethod copy];
	[old release];
}

- (void)setAllHTTPHeaderFields:(NSDictionary *)allHTTPHeaderFields
{
	NSDictionary *old = _allHTTPHeaderFields;

	_allHTTPHeaderFields = [allHTTPHeaderFields copy];
	[old release];
}

- (void)setHTTPBody:(NSData *)HTTPBody
{
	NSData *old = _HTTPBody;

	_HTTPBody = [HTTPBody copy];
	[old release];
}

- (void)setHTTPBodyStream:(NSInputStream *)HTTPBodyStream
{
	NSInputStream *old = _HTTPBodyStream;

	_HTTPBodyStream = [HTTPBodyStream retain];
	[old release];
}

- (void)setHTTPShouldHandleCookies:(BOOL)HTTPShouldHandleCookies
{
	_HTTPShouldHandleCookies = HTTPShouldHandleCookies;
}

- (void)setHTTPShouldUsePipelining:(BOOL)HTTPShouldUsePipelining
{
	_HTTPShouldUsePipelining = HTTPShouldUsePipelining;
}

- (void)setAllowsCellularAccess:(BOOL)allowsCellularAccess
{
	_allowsCellularAccess = allowsCellularAccess;
}

- (void)setAllowsConstrainedNetworkAccess:(BOOL)allowsConstrainedNetworkAccess
{
	_allowsConstrainedNetworkAccess = allowsConstrainedNetworkAccess;
}

- (void)setAllowsExpensiveNetworkAccess:(BOOL)allowsExpensiveNetworkAccess
{
	_allowsExpensiveNetworkAccess = allowsExpensiveNetworkAccess;
}

- (void)setAllowsUltraConstrainedNetworkAccess:(BOOL)allowsUltraConstrainedNetworkAccess
{
	_allowsUltraConstrainedNetworkAccess = allowsUltraConstrainedNetworkAccess;
}

- (void)setRequiresDNSSECValidation:(BOOL)requiresDNSSECValidation
{
	_requiresDNSSECValidation = requiresDNSSECValidation;
}

- (void)setAssumesHTTP3Capable:(BOOL)assumesHTTP3Capable
{
	_assumesHTTP3Capable = assumesHTTP3Capable;
}

- (void)setAllowsPersistentDNS:(BOOL)allowsPersistentDNS
{
	_allowsPersistentDNS = allowsPersistentDNS;
}

- (void)setCookiePartitionIdentifier:(NSString *)cookiePartitionIdentifier
{
	/* COPIED, because the property is `copy`: a caller's mutable string must not change under us. */
	NSString *held = [cookiePartitionIdentifier copy];

	[_cookiePartitionIdentifier release];
	_cookiePartitionIdentifier = held;
}

- (void)setValue:(NSString *)value forHTTPHeaderField:(NSString *)field
{
	NSMutableDictionary *headers = _allHTTPHeaderFields != nil
		? [[_allHTTPHeaderFields mutableCopy] autorelease]
		: [[[NSMutableDictionary alloc] init] autorelease];
	NSString *stored = fn_header_key(headers, field);

	if (value == nil) {
		[headers removeObjectForKey:(stored != nil ? stored : field)];
	} else {
		[headers setObject:value forKey:(stored != nil ? stored : field)];
	}
	/* STORED BACK AS AN IMMUTABLE COPY, so a dictionary handed out earlier cannot change under it. */
	[_allHTTPHeaderFields release];
	_allHTTPHeaderFields = [headers copy];
}

- (void)addValue:(NSString *)value forHTTPHeaderField:(NSString *)field
{
	NSString *existing = [self valueForHTTPHeaderField:field];

	/* RFC 9110 §5.2: a repeated field is a comma-separated list, so appending is the list rule and not
	 * a second entry under the same name. */
	if (existing == nil) {
		[self setValue:value forHTTPHeaderField:field];
	} else {
		[self setValue:[existing stringByAppendingFormat:@", %@", value]
			forHTTPHeaderField:field];
	}
}

- (id)copy
{
	/* AN IMMUTABLE SNAPSHOT, which is Cocoa's rule: the thing a transport is handed cannot be mutated by
	 * the caller that handed it over. */
	return [[NSURLRequest alloc] fnInitWithRequest:self];
}

@end
