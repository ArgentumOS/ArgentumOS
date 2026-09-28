/*
 * foundation_redirect.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FOLLOWING A REDIRECT, END TO END (§54): the probe IS its own HTTP server, exactly as the authentication loop
 * and the metrics units are, and it drives FIVE transfers through the bridge - a 302 that is followed, a moved
 * POST, a 307 that must keep its method, a redirect the delegate DECLINES, and a chain that never ends.
 *
 * WHY ALL FIVE: each one is a different rule of the row, and the rules can only be checked at the FAR END
 * (what the second request actually looks like on the wire) or at the END of the task (what the record and the
 * task's own requests say afterwards). A probe that asserted only "the body arrived" would pass on an
 * implementation that dropped the method, the headers or the loop guard.
 *
 * AND THE RECORD IS READ FROM THE DELEGATE, which is the only place §52's task metrics are delivered: the
 * followed leg must show TWO transactions in order with a redirect count of one, the declined leg ONE with a
 * count of zero, and the loop leg twenty-one with a count of TWENTY. That is §52's array and §54's count
 * proved together, and it is the check a straight-fetch unit could not make.
 *
 * ARC, like every probe.
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
		printf("FOUNDATION-REDIRECT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-REDIRECT %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static const char *const final_body = "the redirect unit's own body\n";
static const char *const post_body = "post target\n";
static const char *const keep_body = "keep target\n";

/* THE SERVER RUNS ON ITS OWN THREAD, because the transfer that is being followed is blocking the main one. It
 * records EVERY request line it was asked, which is how "the second request was a GET" and "no second request
 * was made" become facts rather than hopes. */
@interface FNServer : NSObject
{
	@public
	volatile int stop;
	NSMutableArray *requests;	/* every request line, in order */
}
- (void)run;
@end

@implementation FNServer

- (id)init
{
	self = [super init];
	if(self != nil) {
		requests = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)answer:(int)fd
{
	char buf[2048];
	ssize_t n = read(fd, buf, sizeof(buf));
	NSString *head;
	NSString *path;
	NSString *answer = nil;
	NSString *location = nil;
	int show = 200;

	if(n <= 0) {
		close(fd);
		return;
	}
	/* THE HEAD IS READ AS BYTES AND CONVERTED, because that is the door this tree's NSString ships (its
	 * `-initWithBytes:length:encoding:` is not one of them). */
	head = [[NSString alloc] initWithData:[NSData dataWithBytes:buf length:(NSUInteger)n]
				     encoding:NSUTF8StringEncoding];
	if(head == nil) {
		close(fd);
		return;
	}
	[requests addObject:[[head componentsSeparatedByString:@"\r\n"] objectAtIndex:0]];

	/* THE PATH IS THE SECOND WORD OF THE REQUEST LINE, which is all this server needs to know. */
	{
		NSArray *words = [head componentsSeparatedByString:@" "];

		path = [words count] > 1 ? [words objectAtIndex:1] : @"";
	}
	if([path isEqualToString:@"/hop"]) {
		show = 302;
		location = @"http://127.0.0.1:46495/final";
	} else if([path isEqualToString:@"/final"]) {
		answer = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
			  (unsigned)strlen(final_body), final_body];
	} else if([path isEqualToString:@"/post-moved"]) {
		show = 302;
		location = @"http://127.0.0.1:46495/post-final";
	} else if([path isEqualToString:@"/post-final"]) {
		answer = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
			  (unsigned)strlen(post_body), post_body];
	} else if([path isEqualToString:@"/keep"]) {
		show = 307;
		location = @"http://127.0.0.1:46495/keep-final";
	} else if([path isEqualToString:@"/keep-final"]) {
		answer = [NSString stringWithFormat:@"HTTP/1.1 200 OK\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
			  (unsigned)strlen(keep_body), keep_body];
	} else if([path isEqualToString:@"/stay"]) {
		show = 302;
		location = @"http://127.0.0.1:46495/stay-elsewhere";
	} else if([path isEqualToString:@"/stay-elsewhere"]) {
		answer = @"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	} else if([path isEqualToString:@"/loop"]) {
		show = 302;
		location = @"http://127.0.0.1:46495/loop";
	} else {
		show = 404;
	}
	if(answer == nil) {
		answer = location != nil
			? [NSString stringWithFormat:@"HTTP/1.1 %d Moved\r\nLocation: %@\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", show, location]
			: @"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	}
	write(fd, [answer UTF8String], strlen([answer UTF8String]));
	close(fd);
}

- (void)run
{
	int listener;
	struct sockaddr_in addr;
	int one = 1;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46495);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		listen(listener, 8);
	}
	fcntl(listener, F_SETFL, O_NONBLOCK);
	/* A BOUNDED POLL LOOP, so -stop is observed rather than waited on: the probe sets it once every task has
	 * ended, and a server thread blocked in accept() would never see it. */
	while(!stop) {
		int fd = accept(listener, NULL, NULL);

		if(fd >= 0) {
			[self answer:fd];
		} else {
			usleep(5000);
		}
	}
	close(listener);
}

@end

/* THE DELEGATE THAT DECLINES ONE REDIRECT AND FOLLOWS THE REST: the door is the row's own decision point, so
 * the probe drives it rather than leaving the default to be tested by accident. It also KEEPS the last record
 * it was handed, which is how the chain and the hop count are asserted (the delivered record is the only place
 * either is visible). */
@interface FNHopper : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int refusals;
	int endings;
	NSURLSessionTaskMetrics *lastMetrics;
}
@end

@implementation FNHopper

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
willPerformHTTPRedirection:(NSHTTPURLResponse *)response
	newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest *))completionHandler
{
	(void)session;
	(void)task;
	(void)response;
	/* DECLINE ONE, AND ONLY ONE: the URL names it, so the check cannot pass by accident. */
	if([[[request URL] absoluteString] rangeOfString:@"stay-elsewhere"].location != NSNotFound) {
		refusals++;
		completionHandler(nil);
		return;
	}
	completionHandler(request);
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	(void)session;
	(void)task;
	(void)error;
	endings++;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics
{
	(void)session;
	(void)task;
	lastMetrics = metrics;
}

@end

/* RUN ONE TASK TO ITS END, bounded: the ending is delivered on the transfer's own thread, so this waits for
 * the state rather than assuming it. */
static void fn_wait(NSURLSessionTask *task)
{
	int waited = 0;

	while([task state] != NSURLSessionTaskStateCompleted && waited < 500) {
		usleep(10000);
		waited++;
	}
}

int main(void)
{
	FNServer *server = [[FNServer alloc] init];
	FNHopper *hopper = [[FNHopper alloc] init];
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
	NSURLSession *session;
	NSMutableURLRequest *request;
	__block NSInteger lastStatus = 0;
	__block NSUInteger lastLength = 0;
	int i;

	setvbuf(stdout, NULL, _IONBF, 0);

	[NSThread detachNewThreadSelector:@selector(run) toTarget:server withObject:nil];
	usleep(100000);	/* let the listener bind before the first transfer dials it */

	/* NO REGISTRATION: the library registers the transport at load (see the class's own note, plan §62.83) */
	session = [NSURLSession sessionWithConfiguration:configuration delegate:hopper delegateQueue:nil];

	/* --- ONE: A 302 THAT IS FOLLOWED ---------------------------------------------------------------- */
	{
		NSURLSessionDataTask *moved;
		NSArray *transactions;

		request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://127.0.0.1:46495/hop"]];
		[request setTimeoutInterval:10.0];
		hopper->lastMetrics = nil;
		moved = [session dataTaskWithRequest:request
		    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			lastStatus = [(NSHTTPURLResponse *)response statusCode];
			lastLength = [data length];
			(void)error;
		}];
		[moved resume];
		fn_wait(moved);
		check("the-followed-task-answers-with-the-final-body",
		      lastStatus == 200 && lastLength == strlen(final_body),
		      @"the task ended on the SECOND exchange, with the target's body");
		check("and-the-second-request-is-the-one-that-arrived",
		      [server->requests containsObject:@"GET /final HTTP/1.1"],
		      @"the redirect target was dialled, which is what following MEANS");
		check("the-task-remembers-both-requests",
		      [[[[moved currentRequest] URL] path] isEqualToString:@"/final"] &&
		      [[[[moved originalRequest] URL] path] isEqualToString:@"/hop"],
		      @"currentRequest moved to the target; originalRequest is what the caller made");

		/* ONE RECORD, TWO TRANSACTIONS, IN ORDER, AND ONE HOP - the whole chain in one assertion. */
		transactions = hopper->lastMetrics != nil ? [hopper->lastMetrics transactionMetrics] : nil;
		check("the-record-holds-the-chain",
		      [transactions count] == 2 &&
		      [hopper->lastMetrics redirectCount] == 1 &&
		      [[[[[transactions objectAtIndex:0] request] URL] path] isEqualToString:@"/hop"] &&
		      [[[[[transactions objectAtIndex:1] request] URL] path] isEqualToString:@"/final"],
		      @"two transactions in order, and a redirect count of one");
	}

	/* --- TWO: A MOVED POST BECOMES A GET, WITH NO BODY ---------------------------------------------- */
	{
		NSMutableURLRequest *post = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46495/post-moved"]];
		NSURLSessionTask *moved;

		[post setHTTPMethod:@"POST"];
		[post setHTTPBody:[NSData dataWithBytes:"a body" length:6]];
		[post setTimeoutInterval:10.0];
		moved = [session dataTaskWithRequest:post completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
			(void)d; (void)r; (void)e;
		}];
		[moved resume];
		fn_wait(moved);
		check("a-moved-post-arrives-as-a-get",
		      [server->requests containsObject:@"GET /post-final HTTP/1.1"] &&
		      ![server->requests containsObject:@"POST /post-final HTTP/1.1"],
		      @"301/302/303 propose GET: a body cannot be replayed to a different resource");
	}

	/* --- THREE: A 307 KEEPS ITS METHOD AND ITS BODY -------------------------------------------------- */
	{
		NSMutableURLRequest *put = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46495/keep"]];
		NSURLSessionTask *moved;

		[put setHTTPMethod:@"PUT"];
		[put setHTTPBody:[NSData dataWithBytes:"kept" length:4]];
		[put setTimeoutInterval:10.0];
		moved = [session dataTaskWithRequest:put completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
			(void)d; (void)r; (void)e;
		}];
		[moved resume];
		fn_wait(moved);
		check("a-307-keeps-its-method",
		      [server->requests containsObject:@"PUT /keep-final HTTP/1.1"],
		      @"307 exists precisely so the method survives the hop");
	}

	/* --- FOUR: THE DELEGATE DECLINES, AND THAT IS NOT A FAILURE ------------------------------------- */
	{
		NSMutableURLRequest *stay = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46495/stay"]];
		NSURLSessionTask *refused;
		int before = (int)[server->requests count];
		int refusalsBefore = hopper->refusals;

		[stay setTimeoutInterval:10.0];
		lastStatus = 0;
		lastLength = 0;
		hopper->lastMetrics = nil;
		refused = [session dataTaskWithRequest:stay completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
			(void)e;
			lastStatus = [(NSHTTPURLResponse *)r statusCode];
			lastLength = [d length];
		}];
		[refused resume];
		fn_wait(refused);
		check("a-declined-redirect-is-not-a-failure",
		      refused != nil && [refused error] == nil && lastStatus == 302 && lastLength == 0,
		      @"the task finishes WITH the 3xx it was handed, and no body came with it");
		check("and-nothing-was-run-in-its-place",
		      (int)[server->requests count] - before == 1 &&
		      ![server->requests containsObject:@"GET /stay-elsewhere HTTP/1.1"],
		      @"a declined hop is not performed, which is what makes it declined");
		check("and-the-delegate-was-asked",
		      hopper->refusals == refusalsBefore + 1,
		      @"the door is the decision point, and the probe drove it");
		check("and-the-declined-hop-was-not-counted",
		      [[hopper->lastMetrics transactionMetrics] count] == 1 &&
		      [hopper->lastMetrics redirectCount] == 0,
		      @"redirectCount counts what was PERFORMED, so a refused hop is not in it");
	}

	/* --- FIVE: A CHAIN THAT NEVER ENDS STILL ENDS --------------------------------------------------- */
	{
		NSMutableURLRequest *loop = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46495/loop"]];
		NSURLSessionTask *spinning = nil;
		int loops = 0;

		[loop setTimeoutInterval:20.0];
		hopper->lastMetrics = nil;
		spinning = [session dataTaskWithRequest:loop completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
			(void)d; (void)r; (void)e;
		}];
		[spinning resume];
		fn_wait(spinning);
		for(i = 0; i < (int)[server->requests count]; i++) {
			if([[server->requests objectAtIndex:i] hasPrefix:@"GET /loop "]) {
				loops++;
			}
		}
		check("a-redirect-loop-ends",
		      [spinning error] != nil,
		      @"a chain that never terminates must still terminate, and the hop bound is what does it");
	/* AND THE FAILURE NAMES ITSELF, which §54 could not do when it shipped this bound (§56): the codes are
	 * declared now, so the hop limit's error is Apple's own domain and code rather than a private one. */
	check("and-the-failure-names-itself",
	      [spinning error] != nil &&
	      [[[spinning error] domain] isEqualToString:NSURLErrorDomain] &&
	      [[spinning error] code] == NSURLErrorHTTPTooManyRedirects,
	      @"a caller reads 'too many redirects', not a code in a domain of this library's own");
		check("at-the-bound-and-not-before",
		      loops == 21 &&
		      [[hopper->lastMetrics transactionMetrics] count] == 21 &&
		      [hopper->lastMetrics redirectCount] == 20,
		      @"twenty hops were PERFORMED - twenty-one requests reached the server, twenty-one transactions "
		      @"are in the record - and then it stopped");
	}

	server->stop = 1;
	check("the-probe-served-what-it-was-asked",
	      [server->requests count] >= 26,
	      @"every leg dialled the listener this probe owns");
	check("and-every-task-ended",
	      hopper->endings >= 4,
	      @"the delegate saw the ending of each delegate-driven task");

	printf("FOUNDATION-REDIRECT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-REDIRECT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-REDIRECT DONE\n");
	return failc ? 1 : 0;
}
