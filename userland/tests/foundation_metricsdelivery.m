/*
 * foundation_metricsdelivery.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT A TRANSFER COST, ARRIVING WHERE APPLE SAYS IT ARRIVES (§52): the probe IS its own HTTP server (the
 * authentication loop established that this guest cannot listen with a receiver tool), runs ONE real
 * transfer through the curl bridge, and then asserts on the record the DELEGATE was handed - the fields that
 * have a source, the ORDER of the two ending calls, and the absences that are documented rather than filled.
 *
 * WHY AN END-TO-END PROBE RATHER THAN A UNIT OF THE MAPPING: the mapping's inputs are CURLINFO_* values on a
 * live handle, so a probe that fed them in by hand would assert the setters it had just called. The transfer
 * either measured something or it did not, and the only way to tell is to run one and read what came out.
 *
 * ARC, LIKE EVERY PROBE (the LIBRARY is this tree's MRC half): so there is no retain, release or dealloc
 * here, and an assignment to an ivar IS the retain/release pair.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-METRICSDELIVERY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-METRICSDELIVERY %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE DELEGATE THAT RECORDS THE ORDER. Both ending doors append to ONE log, which is what makes "the metrics
 * arrive before the ending" a fact about the run rather than a claim about the code - and the record is kept
 * so the checks below read what was actually delivered. */
@interface FNMetricsWatcher : NSObject <NSURLSessionTaskDelegate>
{
	@public
	NSMutableArray *log;
	NSURLSessionTaskMetrics *metrics;
	NSInteger completions;
	NSError *ending;
}
@end

@implementation FNMetricsWatcher

- (id)init
{
	self = [super init];
	if(self != nil) {
		log = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)delivered
{
	(void)session;
	(void)task;
	[log addObject:@"metrics"];
	metrics = delivered;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	(void)session;
	(void)task;
	[log addObject:@"complete"];
	completions++;
	ending = error;
}

@end

static NSString *fn_read_request(int fd)
{
	NSMutableData *data = [[NSMutableData alloc] init];
	char buf[1024];
	int tries = 0;
	ssize_t n;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	while(tries < 200) {
		n = read(fd, buf, sizeof(buf));
		if(n > 0) {
			[data appendBytes:buf length:(NSUInteger)n];
			if([data length] > 0) {
				break;	/* the head of the request is all this looks at */
			}
		} else {
			usleep(10000);
			tries++;
		}
	}
	return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

/* THE FIXTURE BODY, and its length is a CHECK rather than a number in a comment: the byte count the record
 * reports has to be exactly this many bytes. */
static const char *const fixture_body = "the metrics unit's own body\n";

int main(void)
{
	FNMetricsWatcher *watcher = [[FNMetricsWatcher alloc] init];
	NSURLSession *session;
	NSURLSessionTask *task;
	struct sockaddr_in addr;
	int listener, conn, one = 1;
	NSString *first = nil;
	__block BOOL done = NO;
	__block NSInteger status = 0;
	int waited = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46493);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	check("the-probe-binds-its-own-listener",
	      bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
	      listen(listener, 2) == 0,
	      @"the probe is the server, so the transfer is a real one over a real socket");

	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46493/measure"]];

		[request setTimeoutInterval:10.0];
		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		session = [NSURLSession sessionWithConfiguration:configuration
						       delegate:watcher
						      delegateQueue:nil];
		task = [session dataTaskWithRequest:request
		    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)error;
			status = [(NSHTTPURLResponse *)response statusCode];
			done = YES;
		}];
		[task resume];
	}

	conn = accept(listener, NULL, NULL);
	check("the-transfer-reaches-the-probe", conn >= 0, @"curl dialled the listener this probe owns");
	first = conn >= 0 ? fn_read_request(conn) : nil;
	check("and-asks-for-the-path-it-was-given",
	      first != nil && [first rangeOfString:@"GET /measure"].location != NSNotFound,
	      @"the request that arrived is the one the task was made from");
	if(conn >= 0) {
		char answer[512];
		int len = snprintf(answer, sizeof(answer),
				   "HTTP/1.1 200 OK\r\n"
				   "Content-Length: %u\r\n"
				   "Content-Type: text/plain\r\n"
				   "Connection: close\r\n\r\n%s",
				   (unsigned)strlen(fixture_body), fixture_body);

		write(conn, answer, (size_t)len);
		close(conn);
	}
	close(listener);

	while(!done && waited < 300) {
		usleep(10000);
		waited++;
	}
	check("the-task-ends-with-the-answer", done && status == 200 && watcher->ending == nil,
	      @"a transfer that ended cleanly, which is what makes the record worth reading");

	/* --- THE DOOR WAS USED, ONCE, AND IN APPLE'S ORDER ----------------------------------------------- */
	check("the-metrics-door-is-delivered", watcher->metrics != nil,
	      @"a real transfer reports a record: nil here would mean the door never carried one");
	check("the-ending-was-told-once-and-the-metrics-once",
	      [watcher->log count] == 2 && watcher->completions == 1,
	      @"one call each, which is the contract both doors keep");
	check("and-the-metrics-came-first",
	      [watcher->log count] == 2 &&
	      [[watcher->log objectAtIndex:0] isEqualToString:@"metrics"] &&
	      [[watcher->log objectAtIndex:1] isEqualToString:@"complete"],
	      @"Apple delivers didFinishCollectingMetrics before didCompleteWithError");

	/* --- THE RECORD ITSELF --------------------------------------------------------------------------- */
	if(watcher->metrics != nil) {
		NSArray *transactions = [watcher->metrics transactionMetrics];
		NSURLSessionTaskTransactionMetrics *tx =
			[transactions count] == 1 ? [transactions objectAtIndex:0] : nil;
		NSDate *fetchStart = [tx fetchStartDate];
		NSDate *responseEnd = [tx responseEndDate];
		NSDateInterval *span = [watcher->metrics taskInterval];

		check("the-task-carries-one-transaction",
		      [transactions count] == 1,
		      @"one transfer ran, so there is exactly one transaction to report");
		check("and-it-names-the-exchange",
		      tx != nil &&
		      [[[[tx request] URL] path] isEqualToString:@"/measure"] &&
		      [(NSHTTPURLResponse *)[tx response] statusCode] == 200,
		      @"the request and the answer the transaction is about");
		check("it-is-a-network-load-and-names-the-address",
		      [tx resourceFetchType] == NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad &&
		      [[tx remoteAddress] isEqualToString:@"127.0.0.1"] &&
		      [[tx networkProtocolName] isEqualToString:@"http/1.1"],
		      @"the fetch type, the peer and the protocol are the ones this transfer actually used");
		check("the-instants-are-ordered",
		      fetchStart != nil && responseEnd != nil &&
		      [tx responseStartDate] != nil &&
		      [fetchStart compare:responseEnd] == NSOrderedAscending &&
		      [[tx responseStartDate] compare:responseEnd] != NSOrderedDescending,
		      @"the instants go forwards, which is what makes them instants rather than durations");
		check("the-bytes-are-counted",
		      [tx countOfResponseBodyBytesReceived] == (NSInteger)strlen(fixture_body) &&
		      [tx countOfResponseHeaderBytesReceived] > 0 &&
		      [tx countOfRequestHeaderBytesSent] == 0,
		      @"the body and the headers received are counted; the request header count has NO SOURCE and "
		      @"stays 0, which the record's own header documents");
		check("the-task-span-covers-its-transaction",
		      span != nil &&
		      [[span startDate] compare:fetchStart] != NSOrderedDescending &&
		      [[span endDate] compare:responseEnd] != NSOrderedAscending &&
		      [watcher->metrics redirectCount] == 0,
		      @"the span is the whole task's; no redirect happened, so its count is zero");
		check("the-documented-absences-hold",
		      [tx secureConnectionStartDate] == nil && [tx localAddress] == nil &&
		      [tx domainResolutionProtocol] == NSURLSessionTaskMetricsDomainResolutionProtocolUnknown &&
		      ![tx cellular] && ![tx expensive] && ![tx constrained] && ![tx multipath],
		      @"the handshake's start is not reported by the transport, localAddress and the resolution "
		      @"protocol are not either, and the four network booleans describe a network this is not");
	} else {
		check("the-task-carries-one-transaction", NO, @"no record was delivered to read");
		check("and-it-names-the-exchange", NO, @"no record was delivered to read");
		check("it-is-a-network-load-and-names-the-address", NO, @"no record was delivered to read");
		check("the-instants-are-ordered", NO, @"no record was delivered to read");
		check("the-bytes-are-counted", NO, @"no record was delivered to read");
		check("the-task-span-covers-its-transaction", NO, @"no record was delivered to read");
		check("the-documented-absences-hold", NO, @"no record was delivered to read");
	}

	printf("FOUNDATION-METRICSDELIVERY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-METRICSDELIVERY-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-METRICSDELIVERY DONE\n");
	return failc ? 1 : 0;
}
