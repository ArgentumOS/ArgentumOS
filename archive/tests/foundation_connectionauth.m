/*
 * foundation_connectionauth.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE COMPOSITION, ALONE IN ONE PROBE (§62.41): a CONNECTION, a 401, a stream body, and its own listener.
 *
 * WHY IT IS ITS OWN PROBE RATHER THAN A LEG OF foundation_authloop, WHICH ALREADY HAS THE 401 SERVER: that probe
 * has FOUR legs sharing ONE listener with per-leg `accept()` calls, so a fifth leg's connections interleave with
 * the earlier ones' — and the diagnostic that would have separated them cannot, because two of those legs use the
 * SAME DELEGATE CLASS and a class name cannot tell two instances apart. That attempt was reverted rather than kept
 * with a result nobody could attribute; this file is the remedy — ONE path, ONE listener, nothing to interleave.
 *
 * WHAT IT JOINS, AND WHY THE JOIN WAS WORTH A PROBE OF ITS OWN: §62.40 proves the transport's own 401 re-issue can
 * ask for a fresh body (`401 -> the session's delegate -> a new stream`), and §62.35 proves the CONNECTION's
 * `-connection:needNewBodyStream:` translates that question faithfully. What neither proves is that the two meet:
 * a connection whose transfer is challenged has to hear the challenge, answer it through the CHALLENGE'S SENDER,
 * be asked for a NEW body when the transport re-issues, and end with its delegate told the transfer finished.
 *
 * THE SERVER IS THE PROBE, AND IT ANSWERS TWICE: the first request gets a 401 with a Basic challenge, and the
 * SECOND — which must carry the credential AND the body again — gets a 200. The body arrives as an
 * `NSInputStream`, which is what makes the re-send interesting: a stream is CONSUMED by being sent, so the
 * re-issued attempt needs the fresh one this delegate hands over.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-CONNECTIONAUTH %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CONNECTIONAUTH %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE REQUEST IS READ UNTIL THE PEER STOPS TALKING, because a POST arrives in more than one segment: the head and
 * the body are separate writes, and a reader that stops after the first would report a request with no body - the
 * mistake foundation_authloop's reader made until it was fixed. The wait is bounded in both time and bytes. */
static NSString *fn_read_request(int fd)
{
	NSMutableData *data = [[NSMutableData alloc] init];
	char buf[1024];
	int tries = 0;
	ssize_t n;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	while(tries < 200 && [data length] < 4096) {
		n = read(fd, buf, sizeof(buf));
		if(n > 0) {
			[data appendBytes:buf length:(NSUInteger)n];
			tries = 0;
		} else {
			usleep(10000);
			tries++;
		}
	}
	return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

static void fn_serve(int fd, const char *answer)
{
	if(fd >= 0) {
		write(fd, answer, strlen(answer));
		close(fd);
	}
}

/* THE CONNECTION'S DELEGATE, AND IT ANSWERS THE WAY A CONNECTION DELEGATE MUST: there is no completion handler in
 * this protocol, so the challenge goes back through the CHALLENGE'S SENDER (§62.27's seam) - and the body comes
 * back as a FRESH STREAM when the transport re-issues (§62.40's ask, §62.35's translation). EVERY door records
 * what it was asked, so every check below reads rather than assumes. */
@interface FNAuthConnectionDelegate : NSObject <NSURLConnectionDataDelegate>
{
	@public
	int challenges;
	int streamAsks;
	int finishes;
	int failures;
	NSData *body;
	NSString *endedWith;
}

@end

@implementation FNAuthConnectionDelegate

- (void)connection:(NSURLConnection *)connection
willSendRequestForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)connection;
	challenges++;
	[(id <NSURLAuthenticationChallengeSender>)[challenge sender]
		useCredential:[NSURLCredential credentialWithUser:@"kyle"
							 password:@"secret"
						      persistence:NSURLCredentialPersistenceNone]
	forAuthenticationChallenge:challenge];
}

- (NSInputStream *)connection:(NSURLConnection *)connection needNewBodyStream:(NSURLRequest *)request
{
	(void)connection;
	(void)request;
	streamAsks++;
	return [NSInputStream inputStreamWithData:body];
}

- (void)connectionDidFinishLoading:(NSURLConnection *)connection
{
	(void)connection;
	finishes++;
	endedWith = @"finished";
}

- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error
{
	(void)connection;
	failures++;
	endedWith = [error description];
}

@end

int main(void)
{
	FNAuthConnectionDelegate *delegate = [[FNAuthConnectionDelegate alloc] init];
	NSData *body = [@"a-body-across-a-401-on-a-connection" dataUsingEncoding:NSUTF8StringEncoding];
	id url = [NSURL URLWithString:@"http://127.0.0.1:46474/"];	/* the nullable-factory idiom this tier wants */
	NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
	NSURLConnection *connection;
	struct sockaddr_in addr;
	int listener, conn = -1, one = 1, waited = 0;
	NSString *first = nil, *second = nil;

	setvbuf(stdout, NULL, _IONBF, 0);
	/* A SERVER PROBE WRITES TO SOCKETS THE CLIENT MAY HAVE CLOSED, and SIGPIPE's default action would kill it. */
	signal(SIGPIPE, SIG_IGN);

	/* NO REGISTRATION: the library registers the transport at load (see the class's own note, plan §62.83) */

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46474);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	check("the-probe-binds-its-own-listener",
	      bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(listener, 2) == 0,
	      @"the probe is the server, and this listener is the only one in the file");

	delegate->body = body;
	[request setHTTPMethod:@"POST"];
	[request setValue:[NSString stringWithFormat:@"%d", (int)[body length]]
     forHTTPHeaderField:@"Content-Length"];
	[request setHTTPBodyStream:[NSInputStream inputStreamWithData:body]];
	[request setTimeoutInterval:10.0];

	connection = [NSURLConnection connectionWithRequest:request delegate:(id)delegate];
	check("the-connection-was-made", connection != nil,
	      @"a connection whose delegate is the one above");

	/* THE FIRST REQUEST: no credential, so the server asks. */
	conn = accept(listener, NULL, NULL);
	first = conn >= 0 ? fn_read_request(conn) : nil;
	check("the-first-attempt-sent-the-streamed-body",
	      first != nil && [first rangeOfString:@"a-body-across-a-401-on-a-connection"].location != NSNotFound,
	      @"the stream body reached the wire on the attempt that had not been challenged yet");
	fn_serve(conn, "HTTP/1.1 401 Unauthorized\r\n"
		       "WWW-Authenticate: Basic realm=\"Probe\"\r\n"
		       "Content-Length: 0\r\nConnection: close\r\n\r\n");

	/* THE SECOND: it must carry the credential AND the body again - the body because the delegate handed over a
	 * FRESH stream when the transport re-issued, which is the whole of what this probe exists to show. */
	conn = accept(listener, NULL, NULL);
	second = conn >= 0 ? fn_read_request(conn) : nil;
	fn_serve(conn, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");

	while(delegate->endedWith == nil && waited < 600) {
		usleep(10000);
		waited++;
	}
	close(listener);

	check("the-connection-delegate-heard-the-challenge",
	      delegate->challenges == 1,
	      [NSString stringWithFormat:@"the challenge reached the CONNECTION's delegate %d time(s): 401 -> the "
		@"session's delegate (the connection) -> the connection's delegate", delegate->challenges]);
	check("the-connection-delegate-was-asked-for-a-new-body-stream",
	      delegate->streamAsks == 1,
	      [NSString stringWithFormat:@"the transport re-issued inside itself and asked for a fresh body %d "
		@"time(s), which is the link §62.40 named as untested", delegate->streamAsks]);
	check("the-re-issued-request-carried-the-credential-and-the-body",
	      second != nil &&
	      [second rangeOfString:@"a3lsZTpzZWNyZXQ="].location != NSNotFound &&
	      [second rangeOfString:@"authorization:" options:NSCaseInsensitiveSearch].location != NSNotFound &&
	      [second rangeOfString:@"a-body-across-a-401-on-a-connection"].location != NSNotFound,
	      @"the re-issued attempt carried the credential AND the body the delegate handed back");
	check("the-connection-finished-without-failing",
	      delegate->finishes == 1 && delegate->failures == 0,
	      [NSString stringWithFormat:@"the connection ended as %@ (finishes=%d failures=%d)",
		delegate->endedWith != nil ? delegate->endedWith : @"(nothing)",
		delegate->finishes, delegate->failures]);

	printf("FOUNDATION-CONNECTIONAUTH RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CONNECTIONAUTH-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CONNECTIONAUTH DONE\n");
	return failc ? 1 : 0;
}
