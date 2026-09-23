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
 * to test, so this hands over a credential whenever asked and the server refuses to ask twice anyway. */
@interface FNAnswerer : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int asked;
}
@end

@implementation FNAnswerer
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
	asked++;
	handler(NSURLSessionAuthChallengeUseCredential,
		[NSURLCredential credentialWithUser:@"kyle"
					   password:@"secret"
					persistence:NSURLCredentialPersistenceNone]);
}
@end

static NSString *fn_read_request(int fd)
{
	NSMutableData *data = [NSMutableData data];
	char buf[1024];
	int tries = 0;
	ssize_t n;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	while(tries < 200) {
		n = read(fd, buf, sizeof(buf));
		if(n > 0) {
			[data appendBytes:buf length:(NSUInteger)n];
			if([data length] > 0) {
				break;	/* one read is the head of the request; that is all this looks at */
			}
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
		NSString *body = @"{\"ok\":true}";

		[request setTimeoutInterval:10.0];
		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
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
	check("the-attempt-guard-held", answerer->asked == 1,
	      @"asked exactly once, so the second 401 would have ended it rather than looping");
	close(listener);

	printf("FOUNDATION-AUTHLOOP RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-AUTHLOOP-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-AUTHLOOP DONE\n");
	return failc ? 1 : 0;
}
