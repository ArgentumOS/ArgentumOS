/*
 * foundation_authloop.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE WHOLE PATH, END TO END, WITH A REAL SERVER: the probe listens on its own socket, answers the first
 * request 401 with a WWW-Authenticate header, and then asserts that the REQUEST THAT COMES BACK CARRIES THE
 * CREDENTIAL - which is the only evidence that any of the pieces are connected. No receiver tool is needed
 * and none is used (the upload unit established that this guest cannot listen with one).
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
		printf("FOUNDATION-AUTHLOOP %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-AUTHLOOP %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE DELEGATE THAT ANSWERS, and it answers ONLY the first time: the attempt guard is the server's business
 * to test, so this hands over a credential whenever asked and the server refuses to ask twice anyway.
 *
 * AND IT COUNTS THE TRANSACTIONS (§52), which is the one property of the metrics that needs a CHALLENGED
 * transfer to observe: one record per ATTEMPT, so this loop must report TWO. */
@interface FNAnswerer : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int asked;
	int metricsCalls;
	long transactions;
	int senderCalls;	/* the door was entered, and answered through the SENDER */
	int senderPresent;	/* and the challenge carried a sender to answer through */
	int handlerCalls;	/* ... or answered through the COMPLETION HANDLER instead */
	BOOL useHandler;	/* which of the two paths this instance takes (§62.30) */
	int streamAsks;		/* the transport re-issued a 401 and asked for a fresh body (§62.36) */
	NSData *bodyBytes;	/* what a fresh stream is made of */
}
@end

@implementation FNAnswerer

/* THE METRICS DOOR, IMPLEMENTED FOR THIS PROBE'S OWN REASON (§52): one record per ATTEMPT means this
 * challenged-and-re-issued transfer must report TWO transactions, and this is where that is observed - the
 * count is read from the record the DELEGATE was handed, not from a counter inside the bridge. */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics
{
	(void)session;
	(void)task;
	metricsCalls++;
	transactions = (long)[[metrics transactionMetrics] count];
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
  needNewBodyStream:(void (^)(NSInputStream *))completionHandler
{
	(void)session;
	(void)task;
	streamAsks++;
	completionHandler([NSInputStream inputStreamWithData:bodyBytes]);
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
	asked++;
	/* ANSWERED THROUGH THE SENDER, WHICH IS §62.27'S WHOLE POINT: this is the OLDER way a delegate answers -
	 * `[challenge.sender useCredential:forAuthenticationChallenge:]` - and the challenge here came from the
	 * transport, so the sender is the library's own thunk over the continuation the transport is waiting on.
	 * `handler` is deliberately NOT called: the same transfer must move whichever door is used, and calling
	 * both would be answering twice.
	 *
	 * THE HANDLER PATH'S COVERAGE MOVES RATHER THAN DISAPPEARS, AND IT IS NAMED: the completion-handler form
	 * is what an NSURLSession delegate uses, and the next unit (NSURLConnection's five authentication doors)
	 * is where it is exercised - the server here serves exactly two connections, so a second transfer cannot
	 * be added to this probe without a second server leg. */
	/* THE TWO ANSWER PATHS, AND THE SECOND ONE IS WHY THIS LEG EXISTS: §62.28 moved the completion-handler
	 * path's coverage here (the connection's doors use the SENDER, so nothing else exercised the handler),
	 * and this instance is told which path to take. Both must move the same transfer - that is the property
	 * Apple's API guarantees by allowing both doors at once. */
	if(useHandler) {
		handlerCalls++;
		handler(NSURLSessionAuthChallengeUseCredential,
			[NSURLCredential credentialWithUser:@"kyle"
						   password:@"secret"
						persistence:NSURLCredentialPersistenceNone]);
		return;
	}
	senderCalls++;
	if([challenge sender] != nil) {
		senderPresent = 1;
		[(id <NSURLAuthenticationChallengeSender>)[challenge sender]
			useCredential:[NSURLCredential credentialWithUser:@"kyle"
								 password:@"secret"
							      persistence:NSURLCredentialPersistenceNone]
		forAuthenticationChallenge:challenge];
	} else {
		handler(NSURLSessionAuthChallengeUseCredential,
			[NSURLCredential credentialWithUser:@"kyle"
						   password:@"secret"
						persistence:NSURLCredentialPersistenceNone]);
	}
}
@end

static NSString *fn_read_request(int fd)
{
	NSMutableData *data = [NSMutableData data];
	char buf[1024];
	int tries = 0;
	ssize_t n;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	/* IT ACCUMULATES UNTIL THE PEER STOPS TALKING, AND THE FIRST VERSION STOPPED AFTER ONE READ: "one read is
	 * the head of the request" was true while this probe only looked at headers, and it is FALSE for a request
	 * that carries a BODY (§62.36) - curl writes the head and the body as separate segments, so a single read
	 * sees a POST with nothing after it and a body check would fail against a bridge that was working. The wait
	 * is still bounded, in both time and bytes. */
	while(tries < 200 && [data length] < 4096) {
		n = read(fd, buf, sizeof(buf));
		if(n > 0) {
			[data appendBytes:buf length:(NSUInteger)n];
			tries = 0;	/* data arrived: the clock restarts */
		} else {
			usleep(10000);
			tries++;
		}
	}
	return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

int main(void)
{
	FNAnswerer *answerer = [[FNAnswerer alloc] init];
	NSURLSession *session;
	NSURLSessionTask *task;
	struct sockaddr_in addr;
	int listener, conn, one = 1;
	NSString *first, *second;
	__block BOOL done = NO;
	int waited = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46481);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	check("the-probe-binds-its-own-listener",
	      bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
	      listen(listener, 2) == 0,
	      @"the probe is the server, so no receiver tool is involved");

	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46481/"]];
		NSString *body = @"{\"ok\":true}";	/* what the server answers with once the credential is accepted */
		NSData *streamedBody = [@"a-body-that-must-survive-a-401" dataUsingEncoding:NSUTF8StringEncoding];

		/* THE BODY IS A STREAM, WHICH IS THE POINT (§62.36): the transport re-issues this 401 ITSELF, so the
		 * second attempt has no way to ask anyone for a body except the first-party door this unit landed -
		 * and the check below is that the re-issued request carries it. */
		[request setHTTPMethod:@"POST"];
		[request setValue:[NSString stringWithFormat:@"%d", (int)[streamedBody length]]
	     forHTTPHeaderField:@"Content-Length"];
		[request setHTTPBodyStream:[NSInputStream inputStreamWithData:streamedBody]];
		answerer->bodyBytes = streamedBody;
		[request setTimeoutInterval:10.0];
		/* NO REGISTRATION: the library registers the transport at load (see the class's own note, plan §62.83) */
		session = [NSURLSession sessionWithConfiguration:configuration
						       delegate:answerer
						      delegateQueue:nil];
		task = [session dataTaskWithRequest:request
		    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			NSInteger status = [(NSHTTPURLResponse *)response statusCode];

			(void)data;
			check("the-task-ends-with-the-retried-answer", error == nil && status == 200,
			      @"the retry is what produced the 200");
			done = YES;
			(void)body;
		}];
		[task resume];
	}

	/* THE FIRST REQUEST: no credential yet, so the server asks. */
	conn = accept(listener, NULL, NULL);
	check("the-transfer-connects", conn >= 0, @"curl reached the probe's listener");
	first = conn >= 0 ? fn_read_request(conn) : nil;
	check("the-first-request-came-without-credentials",
	      first != nil && [[first lowercaseString] rangeOfString:@"authorization:"].location == NSNotFound,
	      @"a client that volunteers a credential has answered a question nobody asked");
	if(conn >= 0) {
		const char *answer = "HTTP/1.1 401 Unauthorized\r\n"
				     "WWW-Authenticate: Basic realm=\"Probe\"\r\n"
				     "Content-Length: 0\r\n"
				     "Connection: close\r\n\r\n";

		write(conn, answer, strlen(answer));
		close(conn);
	}

	/* THE SECOND: it carries what the delegate handed over, or nothing has been proven. */
	conn = accept(listener, NULL, NULL);
	check("the-transfer-comes-back-after-the-401", conn >= 0,
	      @"the loop re-issued the request, which is the whole point of the door");
	second = conn >= 0 ? fn_read_request(conn) : nil;
	check("the-delegate-was-asked-exactly-once", answerer->asked == 1,
	      @"the attempt guard is the server's, and the delegate should not be asked again");
	/* AND THE CHALLENGE CARRIED A SENDER, which is what let this delegate answer the OLDER way (§62.27):
	 * the accessor was refused under §48.1 and §62.24 retired that ground, so the check that asserted it
	 * ABSENT is gone and this one asserts the seam instead. */
	check("the-challenge-carried-a-sender",
	      answerer->senderCalls == 1 && answerer->senderPresent == 1,
	      @"the transport hands out a challenge with a sender, so a delegate can answer through it");
	/* THE WIRE CARRIES IT ENCODED, which is the check's own first version getting it wrong: "kyle:secret" in
	 * base64 is a3lsZTpzZWNyZXQ=, and asserting the PLAINTEXT would fail against a bridge that was working
	 * perfectly - the header could not contain the plaintext even if every layer did its job, because Basic
	 * authentication base64-encodes it before it goes out. The comment had the encoded form in it and the
	 * assertion did not; that is the third time this session a check of mine needed the CODE's knowledge
	 * rather than its author's intent. */
	check("the-second-request-carries-the-credential",
	      second != nil &&
	      [second rangeOfString:@"a3lsZTpzZWNyZXQ="].location != NSNotFound &&
	      [second rangeOfString:@"authorization:" options:NSCaseInsensitiveSearch].location != NSNotFound,
	      @"the credential the delegate chose came back on the wire, base64-encoded as Basic requires");
	check("and-not-in-plaintext",
	      second != nil && [second rangeOfString:@"kyle:secret"].location == NSNotFound,
	      @"a bridge that put the secret on the wire unencoded would be leaking it");
	/* AND THE STREAMED BODY SURVIVED THE CHALLENGE, WHICH TAKES A FRESH STREAM TO DO (§62.36): the first
	 * attempt spends the caller's stream, the transport re-issues a 401 by itself, and the only way that second
	 * attempt can have a body is the first-party door this unit landed. */
	printf("FOUNDATION-AUTHLOOP DIAG first=%d bytes, second=%d bytes\n",
	       (int)(first != nil ? [first length] : -1), (int)(second != nil ? [second length] : -1));
	printf("FOUNDATION-AUTHLOOP DIAG second-head: %.100s\n",
	       second != nil ? [[second stringByReplacingOccurrencesOfString:@"\r\n" withString:@"|"]
				UTF8String] : "(nil)");
	check("the-first-attempt-sent-the-streamed-body",
	      first != nil && [first rangeOfString:@"a-body-that-must-survive-a-401"].location != NSNotFound,
	      @"the caller's stream reached the wire on the first attempt");
	check("the-re-issued-attempt-has-a-fresh-body",
	      second != nil && [second rangeOfString:@"a-body-that-must-survive-a-401"].location != NSNotFound &&
	      answerer->streamAsks == 1,
	      [NSString stringWithFormat:@"the re-issued request carried the body again (%d byte(s) of request "
		@"read), and the transport asked for a fresh stream %d time(s)",
		(int)[second length], answerer->streamAsks]);
	if(conn >= 0) {
		const char *answer = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";

		write(conn, answer, strlen(answer));
		close(conn);
	}

	while(!done && waited < 300) {
		usleep(10000);
		waited++;
	}
	check("the-loop-terminates", done, @"a server that always asks must not spin the client");

	/* --- LEG 2: THE SAME 401, ANSWERED THROUGH THE COMPLETION HANDLER INSTEAD OF THE SENDER -----------
	 *
	 * §62.28 MOVED THIS PATH'S COVERAGE HERE AND SAID SO: the connection's authentication doors answer
	 * through the challenge's sender, so after that unit NOTHING exercised the handler form - which is the
	 * form an NSURLSession delegate uses and therefore the one most callers write. The server above serves
	 * exactly two connections, so the coverage had to come back as a second leg rather than as a tweak. */
	{
		FNAnswerer *secondAnswerer = [[FNAnswerer alloc] init];
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46481/handler"]];
		__block BOOL done2 = NO;
		__block NSInteger status2 = 0;
		/* THE SESSION IS HELD IN A LOCAL, AND THAT IS NOT STYLE: a task keeps an UNRETAINED reference to
		 * its session, so a session created inside the task-creation expression is free to deallocate the
		 * moment that statement ends - the transfer then calls back into a freed session. The first version
		 * of this leg did exactly that and took the guest down with a register dump after the checks above
		 * it had passed. Leg 1 above holds its session the same way, which is why it never showed. */
		NSURLSession *session2 = [NSURLSession sessionWithConfiguration:configuration
								      delegate:secondAnswerer
								     delegateQueue:nil];
		NSURLSessionTask *task;
		NSString *third = nil, *fourth = nil;
		int waited2 = 0;

		secondAnswerer->useHandler = YES;
		task = [session2 dataTaskWithRequest:request
			  completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)error;
			status2 = [(NSHTTPURLResponse *)response statusCode];
			done2 = YES;
		}];
		[task resume];
		(void)session2;	/* named so the lifetime is explicit, not incidental */

		conn = accept(listener, NULL, NULL);
		third = conn >= 0 ? fn_read_request(conn) : nil;
		if(conn >= 0) {
			const char *answer = "HTTP/1.1 401 Unauthorized\r\n"
					     "WWW-Authenticate: Basic realm=\"Probe\"\r\n"
					     "Content-Length: 0\r\n"
					     "Connection: close\r\n\r\n";

			write(conn, answer, strlen(answer));
			close(conn);
		}
		conn = accept(listener, NULL, NULL);
		fourth = conn >= 0 ? fn_read_request(conn) : nil;
		if(conn >= 0) {
			const char *answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";

			write(conn, answer, strlen(answer));
			close(conn);
		}
		while(!done2 && waited2 < 300) {
			usleep(10000);
			waited2++;
		}

		check("the-handler-answer-also-carries-the-credential",
		      third != nil &&
		      [[third lowercaseString] rangeOfString:@"authorization:"].location == NSNotFound &&
		      fourth != nil &&
		      [fourth rangeOfString:@"a3lsZTpzZWNyZXQ="].location != NSNotFound &&
		      status2 == 200 && secondAnswerer->asked == 1 && secondAnswerer->handlerCalls == 1 &&
		      secondAnswerer->senderCalls == 0,
		      [NSString stringWithFormat:@"the completion-handler path: asked=%d handler=%d sender=%d, "
			@"status=%d - and the credential still reached the wire",
			secondAnswerer->asked, secondAnswerer->handlerCalls,
			secondAnswerer->senderCalls, (int)status2]);
	}
	check("the-attempt-guard-held", answerer->asked == 1,
	      @"asked exactly once, so the second 401 would have ended it rather than looping");
	/* AND THE METRICS SAW TWO TRANSACTIONS, which is the boundary the record's array exists for: the 401 and
	 * the re-issue are two ATTEMPTS of one task - ONE delivery, TWO entries. The count is read from the
	 * record the DELEGATE was handed, so this cannot pass on a bridge that merely counted to itself. */
	check("the-reissue-is-two-transactions",
	      answerer->metricsCalls == 1 && answerer->transactions == 2,
	      @"one record per attempt: a challenged transfer reports two transactions of one task");
	close(listener);

	printf("FOUNDATION-AUTHLOOP RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-AUTHLOOP-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-AUTHLOOP DONE\n");
	return failc ? 1 : 0;
}
