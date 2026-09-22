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
		_allowsCellularAccess = YES;
		_allowsExpensiveNetworkAccess = YES;
		_allowsConstrainedNetworkAccess = YES;
		_waitsForConnectivity = NO;
		_HTTPShouldUsePipelining = NO;
		_HTTPShouldSetCookies = YES;
		_HTTPMaximumConnectionsPerHost = 6;
		_discretionary = NO;
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

- (NSString *)identifier { return _identifier; }
- (NSURLRequestCachePolicy)requestCachePolicy { return _requestCachePolicy; }
- (void)setRequestCachePolicy:(NSURLRequestCachePolicy)policy { _requestCachePolicy = policy; }
- (NSTimeInterval)timeoutIntervalForRequest { return _timeoutIntervalForRequest; }
- (void)setTimeoutIntervalForRequest:(NSTimeInterval)interval { _timeoutIntervalForRequest = interval; }
- (NSTimeInterval)timeoutIntervalForResource { return _timeoutIntervalForResource; }
- (void)setTimeoutIntervalForResource:(NSTimeInterval)interval { _timeoutIntervalForResource = interval; }
- (NSURLRequestNetworkServiceType)networkServiceType { return _networkServiceType; }
- (void)setNetworkServiceType:(NSURLRequestNetworkServiceType)type { _networkServiceType = type; }
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
	copy->_allowsCellularAccess = _allowsCellularAccess;
	copy->_allowsExpensiveNetworkAccess = _allowsExpensiveNetworkAccess;
	copy->_allowsConstrainedNetworkAccess = _allowsConstrainedNetworkAccess;
	copy->_waitsForConnectivity = _waitsForConnectivity;
	copy->_HTTPShouldUsePipelining = _HTTPShouldUsePipelining;
	copy->_HTTPShouldSetCookies = _HTTPShouldSetCookies;
	copy->_HTTPMaximumConnectionsPerHost = _HTTPMaximumConnectionsPerHost;
	copy->_discretionary = _discretionary;
	copy->_protocolClasses = [_protocolClasses copy];
	return copy;
}

- (void)dealloc
{
	[_identifier release];
	[_protocolClasses release];
	[super dealloc];
}

@end
