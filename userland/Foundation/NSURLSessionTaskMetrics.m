/*
 * NSURLSessionTaskMetrics.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * GENERATED ACCESSORS, and generated on purpose: thirty accessors and thirty-two setters is exactly the kind
 * of block where a hand-typed line becomes a property that reads one ivar and writes another - the shape that
 * has already cost this session three doors that did nothing.
 */

#import <Foundation/NSURLSessionTaskMetrics.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>

@implementation NSURLSessionTaskTransactionMetrics
- (NSURLRequest *)request { return _request; }
- (NSURLResponse *)response { return _response; }
- (NSDate *)fetchStartDate { return _fetchStartDate; }
- (NSDate *)domainLookupStartDate { return _domainLookupStartDate; }
- (NSDate *)domainLookupEndDate { return _domainLookupEndDate; }
- (NSDate *)connectStartDate { return _connectStartDate; }
- (NSDate *)secureConnectionStartDate { return _secureConnectionStartDate; }
- (NSDate *)secureConnectionEndDate { return _secureConnectionEndDate; }
- (NSDate *)connectEndDate { return _connectEndDate; }
- (NSDate *)requestStartDate { return _requestStartDate; }
- (NSDate *)requestEndDate { return _requestEndDate; }
- (NSDate *)responseStartDate { return _responseStartDate; }
- (NSDate *)responseEndDate { return _responseEndDate; }
- (NSString *)networkProtocolName { return _networkProtocolName; }
- (NSString *)remoteAddress { return _remoteAddress; }
- (NSString *)localAddress { return _localAddress; }
- (NSInteger)countOfRequestBodyBytesBeforeEncoding { return _countOfRequestBodyBytesBeforeEncoding; }
- (NSInteger)countOfRequestBodyBytesSent { return _countOfRequestBodyBytesSent; }
- (NSInteger)countOfRequestHeaderBytesSent { return _countOfRequestHeaderBytesSent; }
- (NSInteger)countOfResponseBodyBytesAfterDecoding { return _countOfResponseBodyBytesAfterDecoding; }
- (NSInteger)countOfResponseBodyBytesReceived { return _countOfResponseBodyBytesReceived; }
- (NSInteger)countOfResponseHeaderBytesReceived { return _countOfResponseHeaderBytesReceived; }
- (BOOL)cellular { return _cellular; }
- (BOOL)expensive { return _expensive; }
- (BOOL)constrained { return _constrained; }
- (BOOL)isProxyConnection { return _proxyConnection; }
- (BOOL)isReusedConnection { return _reusedConnection; }
- (BOOL)multipath { return _multipath; }
- (NSURLSessionTaskMetricsResourceFetchType)resourceFetchType { return _resourceFetchType; }
- (NSURLSessionTaskMetricsDomainResolutionProtocol)domainResolutionProtocol { return _domainResolutionProtocol; }

- (void)dealloc
{
	[_request release];
	[_response release];
	[_fetchStartDate release];
	[_domainLookupStartDate release];
	[_domainLookupEndDate release];
	[_connectStartDate release];
	[_secureConnectionStartDate release];
	[_secureConnectionEndDate release];
	[_connectEndDate release];
	[_requestStartDate release];
	[_requestEndDate release];
	[_responseStartDate release];
	[_responseEndDate release];
	[_networkProtocolName release];
	[_remoteAddress release];
	[_localAddress release];
	[super dealloc];
}

- (void)fnSetRequest:(NSURLRequest *)request response:(NSURLResponse *)response
{
	[request retain];
	[_request release];
	_request = request;
	[response retain];
	[_response release];
	_response = response;
}

- (void)fnSetFetchStartDate:(NSDate *)value
{
	[value retain];
	[_fetchStartDate release];
	_fetchStartDate = value;
}

- (void)fnSetDomainLookupStartDate:(NSDate *)value
{
	[value retain];
	[_domainLookupStartDate release];
	_domainLookupStartDate = value;
}

- (void)fnSetDomainLookupEndDate:(NSDate *)value
{
	[value retain];
	[_domainLookupEndDate release];
	_domainLookupEndDate = value;
}

- (void)fnSetConnectStartDate:(NSDate *)value
{
	[value retain];
	[_connectStartDate release];
	_connectStartDate = value;
}

- (void)fnSetSecureConnectionStartDate:(NSDate *)value
{
	[value retain];
	[_secureConnectionStartDate release];
	_secureConnectionStartDate = value;
}

- (void)fnSetSecureConnectionEndDate:(NSDate *)value
{
	[value retain];
	[_secureConnectionEndDate release];
	_secureConnectionEndDate = value;
}

- (void)fnSetConnectEndDate:(NSDate *)value
{
	[value retain];
	[_connectEndDate release];
	_connectEndDate = value;
}

- (void)fnSetRequestStartDate:(NSDate *)value
{
	[value retain];
	[_requestStartDate release];
	_requestStartDate = value;
}

- (void)fnSetRequestEndDate:(NSDate *)value
{
	[value retain];
	[_requestEndDate release];
	_requestEndDate = value;
}

- (void)fnSetResponseStartDate:(NSDate *)value
{
	[value retain];
	[_responseStartDate release];
	_responseStartDate = value;
}

- (void)fnSetResponseEndDate:(NSDate *)value
{
	[value retain];
	[_responseEndDate release];
	_responseEndDate = value;
}

- (void)fnSetNetworkProtocolName:(NSString *)value
{
	[value retain];
	[_networkProtocolName release];
	_networkProtocolName = value;
}

- (void)fnSetRemoteAddress:(NSString *)value
{
	[value retain];
	[_remoteAddress release];
	_remoteAddress = value;
}

- (void)fnSetLocalAddress:(NSString *)value
{
	[value retain];
	[_localAddress release];
	_localAddress = value;
}

- (void)fnSetCountOfRequestBodyBytesBeforeEncoding:(NSInteger)value { _countOfRequestBodyBytesBeforeEncoding = value; }
- (void)fnSetCountOfRequestBodyBytesSent:(NSInteger)value { _countOfRequestBodyBytesSent = value; }
- (void)fnSetCountOfRequestHeaderBytesSent:(NSInteger)value { _countOfRequestHeaderBytesSent = value; }
- (void)fnSetCountOfResponseBodyBytesAfterDecoding:(NSInteger)value { _countOfResponseBodyBytesAfterDecoding = value; }
- (void)fnSetCountOfResponseBodyBytesReceived:(NSInteger)value { _countOfResponseBodyBytesReceived = value; }
- (void)fnSetCountOfResponseHeaderBytesReceived:(NSInteger)value { _countOfResponseHeaderBytesReceived = value; }
- (void)fnSetCellular:(BOOL)flag { _cellular = flag; }
- (void)fnSetExpensive:(BOOL)flag { _expensive = flag; }
- (void)fnSetConstrained:(BOOL)flag { _constrained = flag; }
- (void)fnSetProxyConnection:(BOOL)flag { _proxyConnection = flag; }
- (void)fnSetReusedConnection:(BOOL)flag { _reusedConnection = flag; }
- (void)fnSetMultipath:(BOOL)flag { _multipath = flag; }
- (void)fnSetResourceFetchType:(NSURLSessionTaskMetricsResourceFetchType)value { _resourceFetchType = value; }
- (void)fnSetDomainResolutionProtocol:(NSURLSessionTaskMetricsDomainResolutionProtocol)value { _domainResolutionProtocol = value; }

@end

@implementation NSURLSessionTaskMetrics

- (NSArray *)transactionMetrics { return _transactionMetrics; }
- (NSDateInterval *)taskInterval { return _taskInterval; }
- (NSInteger)redirectCount { return _redirectCount; }

- (void)dealloc
{
	[_transactionMetrics release];
	[_taskInterval release];
	[super dealloc];
}

- (void)fnSetTransactionMetrics:(NSArray *)metrics
{
	NSArray *copy = [metrics copy];

	[_transactionMetrics release];
	_transactionMetrics = copy;
}

- (void)fnSetTaskInterval:(NSDateInterval *)interval
{
	[interval retain];
	[_taskInterval release];
	_taskInterval = interval;
}

- (void)fnSetRedirectCount:(NSInteger)count { _redirectCount = count; }

@end
