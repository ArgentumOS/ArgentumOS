/*
 * NSURLSessionTaskMetrics.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * What a transfer cost: the instants it passed through, the bytes it moved, and what it moved them over.
 *
 * THESE ARE RECORDS A SESSION FILLS IN AND EVERYONE ELSE READS, which is Apple's shape: the properties are
 * readonly to a caller and the SESSION is the only writer. The writers are declared below as an internal
 * category with this library's `fn` prefix rather than by making the properties settable, because "a caller
 * cannot fabricate a measurement" is the whole point of the type.
 *
 * AND SOME FIELDS ARE NIL ON THIS SYSTEM RATHER THAN GUESSED (§50.1): the transport is libcurl, which reports
 * DURATIONS and not absolute instants, so a date is the transaction's start plus the elapsed value; and a
 * handful of markers have no counterpart in the transport at all, so they stay nil and are documented here
 * rather than filled with something plausible.
 */

#ifndef _FNX_FOUNDATION_NSURLSESSIONTASKMETRICS_H
#define _FNX_FOUNDATION_NSURLSESSIONTASKMETRICS_H

#import <Foundation/NSObject.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateInterval.h>
#import <Foundation/NSString.h>

@class NSURLRequest;
@class NSURLResponse;
@class NSURLSessionTaskTransactionMetrics;

NS_ASSUME_NONNULL_BEGIN

/*
 * HOW A RESOURCE WAS OBTAINED. `ServerPush` IS NOT HERE: the ledger carries it as STRUCK (deprecated), and
 * section 11.5 strikes deprecated API - so a caller who passes its number gets a value this library does not
 * recognise, which is the honest outcome for a name that is deliberately absent.
 */
typedef NS_ENUM(NSInteger, NSURLSessionTaskMetricsResourceFetchType) {
	NSURLSessionTaskMetricsResourceFetchTypeUnknown = 0,
	NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad = 1,
	NSURLSessionTaskMetricsResourceFetchTypeLocalCache = 2
};

/* HOW A NAME WAS RESOLVED. All five are declared; only `Unknown` is ever answered on this system, because the
 * transport does not say which one it used (§50.1). */
typedef NS_ENUM(NSInteger, NSURLSessionTaskMetricsDomainResolutionProtocol) {
	NSURLSessionTaskMetricsDomainResolutionProtocolUnknown = 0,
	NSURLSessionTaskMetricsDomainResolutionProtocolUDP = 1,
	NSURLSessionTaskMetricsDomainResolutionProtocolTCP = 2,
	NSURLSessionTaskMetricsDomainResolutionProtocolTLS = 3,
	NSURLSessionTaskMetricsDomainResolutionProtocolHTTPS = 4
};

@interface NSURLSessionTaskTransactionMetrics : NSObject
{
	NSURLRequest *_request;
	NSURLResponse *_response;
	NSDate *_fetchStartDate;
	NSDate *_domainLookupStartDate;
	NSDate *_domainLookupEndDate;
	NSDate *_connectStartDate;
	NSDate *_secureConnectionStartDate;
	NSDate *_secureConnectionEndDate;
	NSDate *_connectEndDate;
	NSDate *_requestStartDate;
	NSDate *_requestEndDate;
	NSDate *_responseStartDate;
	NSDate *_responseEndDate;
	NSInteger _countOfRequestBodyBytesBeforeEncoding;
	NSInteger _countOfRequestBodyBytesSent;
	NSInteger _countOfRequestHeaderBytesSent;
	NSInteger _countOfResponseBodyBytesAfterDecoding;
	NSInteger _countOfResponseBodyBytesReceived;
	NSInteger _countOfResponseHeaderBytesReceived;
	NSString *_networkProtocolName;
	NSString *_remoteAddress;
	NSString *_localAddress;
	BOOL _cellular;
	BOOL _expensive;
	BOOL _constrained;
	BOOL _proxyConnection;
	BOOL _reusedConnection;
	BOOL _multipath;
	NSURLSessionTaskMetricsResourceFetchType _resourceFetchType;
	NSURLSessionTaskMetricsDomainResolutionProtocol _domainResolutionProtocol;
}

@property (nullable, readonly, copy) NSURLRequest *request;
@property (nullable, readonly, copy) NSURLResponse *response;

/* THE INSTANTS, in the order a transfer passes through them. Nil means "this did not happen, or the transport
 * does not report it" - never "zero". */
@property (nullable, readonly, copy) NSDate *fetchStartDate;
@property (nullable, readonly, copy) NSDate *domainLookupStartDate;
@property (nullable, readonly, copy) NSDate *domainLookupEndDate;
@property (nullable, readonly, copy) NSDate *connectStartDate;
/* NIL ON THIS SYSTEM, and documented rather than guessed: the transport reports the secure connection's END,
 * not its start. */
@property (nullable, readonly, copy) NSDate *secureConnectionStartDate;
@property (nullable, readonly, copy) NSDate *secureConnectionEndDate;
@property (nullable, readonly, copy) NSDate *connectEndDate;
@property (nullable, readonly, copy) NSDate *requestStartDate;
@property (nullable, readonly, copy) NSDate *requestEndDate;
@property (nullable, readonly, copy) NSDate *responseStartDate;
@property (nullable, readonly, copy) NSDate *responseEndDate;

@property (readonly) NSInteger countOfRequestBodyBytesBeforeEncoding;
@property (readonly) NSInteger countOfRequestBodyBytesSent;
@property (readonly) NSInteger countOfRequestHeaderBytesSent;
@property (readonly) NSInteger countOfResponseBodyBytesAfterDecoding;
@property (readonly) NSInteger countOfResponseBodyBytesReceived;
@property (readonly) NSInteger countOfResponseHeaderBytesReceived;

@property (nullable, readonly, copy) NSString *networkProtocolName;
@property (nullable, readonly, copy) NSString *remoteAddress;
@property (nullable, readonly, copy) NSString *localAddress;
@property (readonly) BOOL cellular;
@property (readonly) BOOL expensive;
@property (readonly) BOOL constrained;
@property (readonly, getter=isProxyConnection) BOOL proxyConnection;
@property (readonly, getter=isReusedConnection) BOOL reusedConnection;
@property (readonly) BOOL multipath;
@property (readonly) NSURLSessionTaskMetricsResourceFetchType resourceFetchType;
@property (readonly) NSURLSessionTaskMetricsDomainResolutionProtocol domainResolutionProtocol;

@end

/* THE SESSION IS THE ONLY WRITER, and this is the door it uses. Kept out of the public property list on
 * purpose: a caller reading a measurement and a caller writing one are different roles, and only one of them
 * belongs in the interface everyone sees. */
@interface NSURLSessionTaskTransactionMetrics (FNXMetricsWriter)
- (void)fnSetRequest:(nullable NSURLRequest *)request response:(nullable NSURLResponse *)response;
- (void)fnSetFetchStartDate:(nullable NSDate *)date;
- (void)fnSetDomainLookupStartDate:(nullable NSDate *)date;
- (void)fnSetDomainLookupEndDate:(nullable NSDate *)date;
- (void)fnSetConnectStartDate:(nullable NSDate *)date;
- (void)fnSetSecureConnectionStartDate:(nullable NSDate *)date;
- (void)fnSetSecureConnectionEndDate:(nullable NSDate *)date;
- (void)fnSetConnectEndDate:(nullable NSDate *)date;
- (void)fnSetRequestStartDate:(nullable NSDate *)date;
- (void)fnSetRequestEndDate:(nullable NSDate *)date;
- (void)fnSetResponseStartDate:(nullable NSDate *)date;
- (void)fnSetResponseEndDate:(nullable NSDate *)date;
- (void)fnSetCountOfRequestBodyBytesBeforeEncoding:(NSInteger)count;
- (void)fnSetCountOfRequestBodyBytesSent:(NSInteger)count;
- (void)fnSetCountOfRequestHeaderBytesSent:(NSInteger)count;
- (void)fnSetCountOfResponseBodyBytesAfterDecoding:(NSInteger)count;
- (void)fnSetCountOfResponseBodyBytesReceived:(NSInteger)count;
- (void)fnSetCountOfResponseHeaderBytesReceived:(NSInteger)count;
- (void)fnSetNetworkProtocolName:(nullable NSString *)name;
- (void)fnSetRemoteAddress:(nullable NSString *)address;
- (void)fnSetLocalAddress:(nullable NSString *)address;
- (void)fnSetCellular:(BOOL)flag;
- (void)fnSetExpensive:(BOOL)flag;
- (void)fnSetConstrained:(BOOL)flag;
- (void)fnSetProxyConnection:(BOOL)flag;
- (void)fnSetReusedConnection:(BOOL)flag;
- (void)fnSetMultipath:(BOOL)flag;
- (void)fnSetResourceFetchType:(NSURLSessionTaskMetricsResourceFetchType)type;
- (void)fnSetDomainResolutionProtocol:(NSURLSessionTaskMetricsDomainResolutionProtocol)protocol;
@end

@interface NSURLSessionTaskMetrics : NSObject
{
	NSArray *_transactionMetrics;
	NSDateInterval *_taskInterval;
	NSInteger _redirectCount;
}

/* ONE ENTRY PER TRANSACTION, in the order they happened: a task that was redirected or re-issued has more
 * than one, which is why this is an array rather than a single record. */
@property (readonly, copy) NSArray *transactionMetrics;
/* THE WHOLE TASK'S SPAN, which is not the sum of its transactions: the gaps between them are part of it. */
@property (readonly, copy) NSDateInterval *taskInterval;
@property (readonly) NSInteger redirectCount;

@end

@interface NSURLSessionTaskMetrics (FNXMetricsWriter)
- (void)fnSetTransactionMetrics:(NSArray *)metrics;
- (void)fnSetTaskInterval:(nullable NSDateInterval *)interval;
- (void)fnSetRedirectCount:(NSInteger)count;
@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLSESSIONTASKMETRICS_H */
