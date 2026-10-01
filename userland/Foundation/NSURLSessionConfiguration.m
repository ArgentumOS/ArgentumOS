/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionConfiguration.m — the defaults, and the copy. The design is in the header.
 */
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSURLCache.h>
#import <Foundation/NSURLCredentialStorage.h>

@implementation NSURLSessionConfiguration

/* ONE PLACE THE DEFAULTS LIVE, so the two built-in doors cannot drift apart and the mutable properties
 * above start from Apple's documented values rather than from zeroed memory. */
- (id)init
{
	self = [super init];
	if (self != nil) {
		_requestCachePolicy = NSURLRequestUseProtocolCachePolicy;
		_timeoutIntervalForRequest = 60.0;
		_timeoutIntervalForResource = 604800.0;	/* 7 days */
		_networkServiceType = NSURLNetworkServiceTypeDefault;
		_multipathServiceType = NSURLSessionMultipathServiceTypeNone;
		_allowsCellularAccess = YES;
		_allowsExpensiveNetworkAccess = YES;
		_allowsConstrainedNetworkAccess = YES;
		_waitsForConnectivity = NO;
		_HTTPShouldUsePipelining = NO;
		_HTTPShouldSetCookies = YES;
		_HTTPMaximumConnectionsPerHost = 6;
		_discretionary = NO;
		/* THE FOUR STORAGE/HEADER DEFAULTS ARE APPLE'S DOCUMENTED ONES: an empty header dictionary, the
		 * OnlyFromMainDocumentDomain cookie policy, and the SHARED cache, cookie and credential stores (the
		 * classes whose facilities the transfer already exercises — see the header). */
		_HTTPAdditionalHeaders = [[NSDictionary alloc] init];
		_HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain;
		_HTTPCookieStorage = [[NSHTTPCookieStorage sharedHTTPCookieStorage] retain];
		_URLCache = [[NSURLCache sharedURLCache] retain];
		_URLCredentialStorage = [[NSURLCredentialStorage sharedCredentialStorage] retain];
		_protocolClasses = nil;
	}
	return self;
}

+ (NSURLSessionConfiguration *)defaultSessionConfiguration
{
	return [[[self alloc] init] autorelease];
}

+ (NSURLSessionConfiguration *)ephemeralSessionConfiguration
{
	return [[[self alloc] init] autorelease];
}

+ (NSURLSessionConfiguration *)backgroundSessionConfigurationWithIdentifier:(NSString *)identifier
{
	NSURLSessionConfiguration *configuration = [[[self alloc] init] autorelease];

	configuration->_identifier = [identifier copy];
	return configuration;
}

/* THE DEPRECATED OLD SPELLING IS THE SAME OBJECT: it delegates to the current name, so the two cannot drift. */
+ (NSURLSessionConfiguration *)backgroundSessionConfiguration:(NSString *)identifier
{
	return [self backgroundSessionConfigurationWithIdentifier:identifier];
}

- (NSString *)identifier { return _identifier; }
- (NSURLRequestCachePolicy)requestCachePolicy { return _requestCachePolicy; }
- (void)setRequestCachePolicy:(NSURLRequestCachePolicy)policy { _requestCachePolicy = policy; }
- (NSTimeInterval)timeoutIntervalForRequest { return _timeoutIntervalForRequest; }
- (void)setTimeoutIntervalForRequest:(NSTimeInterval)interval { _timeoutIntervalForRequest = interval; }
- (NSTimeInterval)timeoutIntervalForResource { return _timeoutIntervalForResource; }
- (void)setTimeoutIntervalForResource:(NSTimeInterval)interval { _timeoutIntervalForResource = interval; }
- (NSURLRequestNetworkServiceType)networkServiceType { return _networkServiceType; }
- (void)setNetworkServiceType:(NSURLRequestNetworkServiceType)type { _networkServiceType = type; }
- (NSURLSessionMultipathServiceType)multipathServiceType { return _multipathServiceType; }
- (void)setMultipathServiceType:(NSURLSessionMultipathServiceType)type { _multipathServiceType = type; }
- (BOOL)allowsCellularAccess { return _allowsCellularAccess; }
- (void)setAllowsCellularAccess:(BOOL)flag { _allowsCellularAccess = flag; }
- (BOOL)allowsExpensiveNetworkAccess { return _allowsExpensiveNetworkAccess; }
- (void)setAllowsExpensiveNetworkAccess:(BOOL)flag { _allowsExpensiveNetworkAccess = flag; }
- (BOOL)allowsConstrainedNetworkAccess { return _allowsConstrainedNetworkAccess; }
- (void)setAllowsConstrainedNetworkAccess:(BOOL)flag { _allowsConstrainedNetworkAccess = flag; }
- (BOOL)waitsForConnectivity { return _waitsForConnectivity; }
- (void)setWaitsForConnectivity:(BOOL)flag { _waitsForConnectivity = flag; }
- (BOOL)HTTPShouldUsePipelining { return _HTTPShouldUsePipelining; }
- (void)setHTTPShouldUsePipelining:(BOOL)flag { _HTTPShouldUsePipelining = flag; }
- (BOOL)HTTPShouldSetCookies { return _HTTPShouldSetCookies; }
- (void)setHTTPShouldSetCookies:(BOOL)flag { _HTTPShouldSetCookies = flag; }
- (NSInteger)HTTPMaximumConnectionsPerHost { return _HTTPMaximumConnectionsPerHost; }
- (void)setHTTPMaximumConnectionsPerHost:(NSInteger)count { _HTTPMaximumConnectionsPerHost = count; }
- (BOOL)discretionary { return _discretionary; }
- (void)setDiscretionary:(BOOL)flag { _discretionary = flag; }
- (NSDictionary *)HTTPAdditionalHeaders { return _HTTPAdditionalHeaders; }
- (void)setHTTPAdditionalHeaders:(NSDictionary *)headers
{
	NSDictionary *old = _HTTPAdditionalHeaders;

	_HTTPAdditionalHeaders = [headers copy];	/* a snapshot, like every header bag in this library */
	[old release];
}
- (NSHTTPCookieAcceptPolicy)HTTPCookieAcceptPolicy { return _HTTPCookieAcceptPolicy; }
- (void)setHTTPCookieAcceptPolicy:(NSHTTPCookieAcceptPolicy)policy { _HTTPCookieAcceptPolicy = policy; }
- (NSHTTPCookieStorage *)HTTPCookieStorage { return _HTTPCookieStorage; }
- (void)setHTTPCookieStorage:(NSHTTPCookieStorage *)storage
{
	NSHTTPCookieStorage *old = _HTTPCookieStorage;

	_HTTPCookieStorage = [storage retain];
	[old release];
}
- (NSURLCache *)URLCache { return _URLCache; }
- (void)setURLCache:(NSURLCache *)cache
{
	NSURLCache *old = _URLCache;

	_URLCache = [cache retain];
	[old release];
}
- (NSURLCredentialStorage *)URLCredentialStorage { return _URLCredentialStorage; }
- (void)setURLCredentialStorage:(NSURLCredentialStorage *)storage
{
	NSURLCredentialStorage *old = _URLCredentialStorage;

	_URLCredentialStorage = [storage retain];
	[old release];
}
- (NSArray *)protocolClasses { return _protocolClasses; }
- (void)setProtocolClasses:(NSArray *)classes { NSArray *old = _protocolClasses;
	_protocolClasses = [classes copy]; [old release]; }

/* A REAL COPY, AND IT HAS TO BE ONE: every property here is READWRITE, so "the copy is the receiver"
 * (which the immutable value classes in this library use) would hand back an object whose mutation the
 * caller could not distinguish from mutating the original. The snapshot the probe asserts — editing a
 * copy leaves the original alone — is the reason this method exists rather than a -retain. */
- (id)copy
{
	NSURLSessionConfiguration *copy = [[NSURLSessionConfiguration alloc] init];

	copy->_identifier = [_identifier copy];
	copy->_requestCachePolicy = _requestCachePolicy;
	copy->_timeoutIntervalForRequest = _timeoutIntervalForRequest;
	copy->_timeoutIntervalForResource = _timeoutIntervalForResource;
	copy->_networkServiceType = _networkServiceType;
	copy->_multipathServiceType = _multipathServiceType;
	copy->_allowsCellularAccess = _allowsCellularAccess;
	copy->_allowsExpensiveNetworkAccess = _allowsExpensiveNetworkAccess;
	copy->_allowsConstrainedNetworkAccess = _allowsConstrainedNetworkAccess;
	copy->_waitsForConnectivity = _waitsForConnectivity;
	copy->_HTTPShouldUsePipelining = _HTTPShouldUsePipelining;
	copy->_HTTPShouldSetCookies = _HTTPShouldSetCookies;
	copy->_HTTPMaximumConnectionsPerHost = _HTTPMaximumConnectionsPerHost;
	copy->_discretionary = _discretionary;
	copy->_HTTPAdditionalHeaders = [_HTTPAdditionalHeaders copy];
	copy->_HTTPCookieAcceptPolicy = _HTTPCookieAcceptPolicy;
	copy->_HTTPCookieStorage = [_HTTPCookieStorage retain];
	copy->_URLCache = [_URLCache retain];
	copy->_URLCredentialStorage = [_URLCredentialStorage retain];
	copy->_protocolClasses = [_protocolClasses copy];
	return copy;
}

- (void)dealloc
{
	[_identifier release];
	[_HTTPAdditionalHeaders release];
	[_HTTPCookieStorage release];
	[_URLCache release];
	[_URLCredentialStorage release];
	[_protocolClasses release];
	[super dealloc];
}

@end
