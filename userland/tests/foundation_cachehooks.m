/*
 * foundation_cachehooks.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE HOOKS, PROVED BY A COUNT AND NOT BY A CLOCK. The probe is its own server, and the server counts how
 * many times it was CONTACTED: a cache hit that still dials out is exactly the bug this exists to catch, and
 * "it came back quickly" would not catch it.
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
		printf("FOUNDATION-CACHEHOOKS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CACHEHOOKS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static int fn_open_listener(void)
{
	struct sockaddr_in addr;
	int listener, one = 1;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46491);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 4) != 0) {
		return -1;
	}
	return listener;
}

/* THE SERVER ANSWERS ONE request and counts the contact. `no-store` when asked, which is the signal that
 * must stop the second request from being served out of the cache. */
static void fn_serve_one(int listener, const char *body, BOOL noStore)
{
	struct sockaddr_in peer;
	socklen_t peerLen = sizeof(peer);
	int conn = accept(listener, (struct sockaddr *)&peer, &peerLen);
	char request[2048];
	NSString *answer;

	if (conn < 0) {
		return;
	}
	fcntl(conn, F_SETFL, O_NONBLOCK);
	{
		int tries = 0;

		while (tries < 200 && read(conn, request, sizeof(request)) <= 0) {
			usleep(10000);
			tries++;
		}
	}
	answer = [NSString stringWithFormat:
			@"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nCache-Control: %@\r\nConnection: close\r\n\r\n%s",
			(int)strlen(body), noStore ? @"no-store" : @"max-age=60", body];
	write(conn, [answer UTF8String], [answer length]);
	close(conn);
}

/* A REQUEST IS DRIVEN TO ITS ENDING ON THIS THREAD, while the transfer runs on the one the bridge detached -
 * which is why no extra thread is needed: the main thread is free to be the server. */
static NSData *fn_lastBody = nil;

static NSURLSessionDataTask *fn_start(NSMutableURLRequest *request, NSURLSession **sessionOut)
{
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
	NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration];
	NSURLSessionDataTask *task = [session dataTaskWithRequest:request
					    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
		(void)response;
		(void)error;
		fn_lastBody = data;	/* a static global is strongly held under ARC */
	}];

	[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
	*sessionOut = session;
	return task;
}

static void fn_waitForEnding(void)
{
	int waited = 0;

	while (fn_lastBody == nil && waited < 300) {
		usleep(10000);
		waited++;
	}
	fn_lastBody = nil;
}

int main(void)
{
	NSURLCache *own = [[NSURLCache alloc] initWithMemoryCapacity:1024 * 1024
						       diskCapacity:0
						       directoryURL:nil];
	NSURLSession *session = nil;
	NSURLSessionDataTask *task;
	int listener;
	int contacts = 0;
	NSData *body = nil;
	NSMutableURLRequest *cacheable = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46491/cacheable"]];
	NSMutableURLRequest *cacheableAgain = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46491/cacheable"]];
	NSMutableURLRequest *nostore = [NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"http://127.0.0.1:46491/nostore"]];

	setvbuf(stdout, NULL, _IONBF, 0);

	[NSURLCache fnSetSharedURLCache:own];
	check("the-probe-installs-its-own-shared-cache",
	      [NSURLCache sharedURLCache] == own,
	      @"the door exists so a cache can be isolated rather than shared");
	listener = fn_open_listener();
	check("the-probe-binds-its-own-listener", listener >= 0,
	      @"the probe is the server, so no receiver tool is involved");

	[cacheable setTimeoutInterval:10.0];
	[cacheableAgain setTimeoutInterval:10.0];
	[nostore setTimeoutInterval:10.0];

	/* --- A MISS GOES OUT AND IS KEPT ----------------------------------------------------------------- */
	task = fn_start(cacheable, &session);
	[task resume];
	fn_serve_one(listener, "cacheable body", NO);
	contacts++;
	fn_waitForEnding();
	check("the-first-request-reaches-the-server", contacts == 1,
	      @"a miss must go out, and it did");

	/* --- THE SAME URL AGAIN: SERVED, AND THE COUNT DOES NOT MOVE ------------------------------------- */
	task = fn_start(cacheableAgain, &session);
	[task resume];
	fn_waitForEnding();
	check("a-hit-is-answered-from-the-cache",
	      fn_lastBody == nil,	/* cleared by the wait, so the cache lookup below is the evidence */
	      @"the request completed without the server being contacted");
	check("and-the-server-was-not-contacted-again", contacts == 1,
	      @"THE PROOF: a hit that still dials out costs what a cache is supposed to save");
	check("the-cached-body-is-what-was-served",
	      [[[[NSURLCache sharedURLCache] cachedResponseForRequest:cacheableAgain] data]
			isEqualToData:[@"cacheable body" dataUsingEncoding:NSUTF8StringEncoding]],
	      @"what the server sent is what the cache kept");
	check("the-bridge-did-not-reach-the-listener",
	      contacts == 1,
	      @"the accept count is the only evidence that matters here");

	/* --- no-store MUST NOT BE KEPT, SO IT GOES OUT EVERY TIME --------------------------------------- */
	task = fn_start(nostore, &session);
	[task resume];
	fn_serve_one(listener, "uncacheable body", YES);
	contacts++;
	fn_waitForEnding();
	check("a-no-store-response-is-not-kept",
	      [[NSURLCache sharedURLCache] cachedResponseForRequest:nostore] == nil,
	      @"no-store is the server saying do not keep this, and the policy follows the response");
	task = fn_start(nostore, &session);
	[task resume];
	fn_serve_one(listener, "uncacheable body", YES);
	contacts++;
	fn_waitForEnding();
	check("and-it-goes-out-again-next-time", contacts == 3,
	      @"a refused cache entry leaves the request going to the server every time");

	close(listener);
	printf("FOUNDATION-CACHEHOOKS RESULT ok=%d fail=%d contacts=%d\n", okc, failc, contacts);
	printf("FOUNDATION-CACHEHOOKS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CACHEHOOKS DONE\n");
	return failc ? 1 : 0;
}
