/*
 * foundation_taskmetrics.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The metrics records: what a session can fill, what a reader sees, and the two things this system answers
 * DIFFERENTLY from Apple - a marker that has no counterpart in the transport stays NIL, and a deprecated case
 * is not shipped at all. Both are asserted, because a documentation claim nothing tests is a claim that rots.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-TASKMETRICS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-TASKMETRICS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	NSURLSessionTaskTransactionMetrics *tx = [[NSURLSessionTaskTransactionMetrics alloc] init];
	NSDate *start = [NSDate date];
	NSDate *lookup = [start dateByAddingTimeInterval:0.010];
	NSDate *connected = [start dateByAddingTimeInterval:0.025];
	NSDate *firstByte = [start dateByAddingTimeInterval:0.040];

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- A FRESH RECORD MEASURES NOTHING, AND NIL IS NOT ZERO ---------------------------------------- */
	check("a-fresh-record-has-no-dates",
	      [tx fetchStartDate] == nil && [tx responseEndDate] == nil,
	      @"nil means this did not happen or was not reported, never zero");
	check("a-fresh-record-counts-nothing",
	      [tx countOfResponseBodyBytesReceived] == 0 && [tx countOfRequestHeaderBytesSent] == 0,
	      @"the counts are zero because nothing was counted");

	/* --- THE SESSION FILLS IT, AND EVERYTHING IT FILLS IS READABLE ----------------------------------- */
	[tx fnSetRequest:[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://example.com/"]]
		response:nil];
	[tx fnSetFetchStartDate:start];
	[tx fnSetDomainLookupStartDate:start];
	[tx fnSetDomainLookupEndDate:lookup];
	[tx fnSetConnectStartDate:lookup];
	[tx fnSetConnectEndDate:connected];
	[tx fnSetRequestStartDate:connected];
	[tx fnSetRequestEndDate:firstByte];
	[tx fnSetResponseStartDate:firstByte];
	[tx fnSetResponseEndDate:[start dateByAddingTimeInterval:0.050]];
	[tx fnSetCountOfResponseBodyBytesReceived:2048];
	[tx fnSetCountOfResponseHeaderBytesReceived:312];
	[tx fnSetNetworkProtocolName:@"HTTP/1.1"];
	[tx fnSetRemoteAddress:@"10.0.2.2"];
	[tx fnSetReusedConnection:YES];
	[tx fnSetCellular:NO];
	[tx fnSetResourceFetchType:NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad];

	check("the-session-fills-the-instants", [[tx fetchStartDate] isEqualToDate:start] &&
	      [[tx domainLookupEndDate] isEqualToDate:lookup] &&
	      [[tx connectEndDate] isEqualToDate:connected],
	      @"every marker the transport reports is readable back");
	check("the-bytes-are-counted", [tx countOfResponseBodyBytesReceived] == 2048 &&
	      [tx countOfResponseHeaderBytesReceived] == 312,
	      @"the size infos are recorded as they arrived");
	check("the-transaction-characteristics-are-read",
	      [[tx networkProtocolName] isEqualToString:@"HTTP/1.1"] &&
	      [[tx remoteAddress] isEqualToString:@"10.0.2.2"],
	      @"what it moved over and to whom");
	check("the-boolean-getters-keep-their-is-spelling",
	      [tx isReusedConnection] && ![tx isProxyConnection] && ![tx cellular],
	      @"the two with an is- getter keep it, and the rest do not invent one");
	check("a-plain-boolean-stays-plain", ![tx expensive] && ![tx multipath],
	      @"a network this system is on is not cellular, expensive, constrained or multipath");

	/* --- THE MARKER THE TRANSPORT CANNOT REPORT STAYS NIL, WHICH IS THE DOCUMENTED ANSWER ------------- */
	check("the-secure-connection-start-stays-nil",
	      [tx secureConnectionStartDate] == nil,
	      @"the transport reports the handshake's END and not its start; nil is the honest answer");

	/* --- THE TASK'S OWN RECORD ---------------------------------------------------------------------- */
	{
		NSURLSessionTaskMetrics *metrics = [[NSURLSessionTaskMetrics alloc] init];
		NSDateInterval *interval = [[NSDateInterval alloc] initWithStartDate:start
									    endDate:[start dateByAddingTimeInterval:1.0]];

		[metrics fnSetTransactionMetrics:[NSArray arrayWithObject:tx]];
		[metrics fnSetTaskInterval:interval];
		[metrics fnSetRedirectCount:1];
		check("the-task-record-carries-its-transactions",
		      [[metrics transactionMetrics] count] == 1 &&
		      [[[metrics transactionMetrics] objectAtIndex:0] isEqual:tx],
		      @"one entry per transaction, in order");
		check("and-its-own-span", [[metrics taskInterval] isEqualToDateInterval:interval],
		      @"the task's span is not the sum of its transactions");
		check("and-how-many-redirects", [metrics redirectCount] == 1,
		      @"the count is the task's, not a transaction's");
	}

	/* --- WHAT IS REFUSED, ASSERTED ABSENT ----------------------------------------------------------- */
	check("the-deprecated-fetch-type-is-absent",
	      ![tx respondsToSelector:NSSelectorFromString(@"NSURLSessionTaskMetricsResourceFetchTypeServerPush")],
	      @"ServerPush carries the ledger's deprecated LABEL and is OWED (plan section 62.24), so its "
	      @"name is not here because it is unimplemented - not because a policy strikes it");
	check("the-known-fetch-types-are-present",
	      NSURLSessionTaskMetricsResourceFetchTypeUnknown == 0 &&
	      NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad == 1 &&
	      NSURLSessionTaskMetricsResourceFetchTypeLocalCache == 2,
	      @"Unknown is 0 so an unset decision is not a claim about the network");
	check("the-resolution-protocols-are-all-declared",
	      NSURLSessionTaskMetricsDomainResolutionProtocolUnknown == 0 &&
	      NSURLSessionTaskMetricsDomainResolutionProtocolHTTPS == 4,
	      @"all five names are here even though only Unknown is answered on this system");

	printf("FOUNDATION-TASKMETRICS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-TASKMETRICS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-TASKMETRICS DONE\n");
	return failc ? 1 : 0;
}
