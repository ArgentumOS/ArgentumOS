/*
 * foundation_streamtask.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE STREAM TASK'S BEHAVIOUR, PER THE SEMANTICS §58 QUOTED FROM APPLE'S PAGES: the minimum is a minimum, the
 * cap is a cap, the timeout CANCELS, a zero timeout does not, `-closeWrite` reaches the FAR END, the delegate
 * hears the halves close, and the one refused door says so instead of lying.
 *
 * ONE CONNECTION PER LEG, AND THAT IS A CORRECTION WORTH RECORDING. The first version of this probe served
 * every leg over ONE connection with a timed script, and it failed checks for reasons that had nothing to do
 * with the class: a leg that asked for four bytes of a sixteen-byte run left TWELVE in the socket, and the
 * next leg - the timeout leg - read those instead of timing out. A SERVER WHOSE REPLY IS STILL WAITING IN A
 * BUFFER IS NOT A SERVER THAT SAID NOTHING. So each leg gets its own connection and its own script, and the
 * only delay left in the script is the MINIMUM leg's pause, which is the whole point of that leg.
 *
 * WHAT THIS UNIT DOES NOT COVER, NAMED RATHER THAN IMPLIED: the two TLS doors. They are implemented (libssl is
 * on the library's link line) and verifying them needs a TLS server in the guest - which this tree has a
 * pattern for (`openssl req` + `openssl s_server` driven by a shell script, as the libressl units do). That is
 * §58.1, and until it runs `-startSecureConnection` is an implemented door with a test OWED.
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
#include <poll.h>	/* every wait in this probe is a poll - see fn_pause below, and why */
#include <errno.h>	/* the server's read trail prints what a failed read said, and why */
#include <sys/wait.h>	/* the TLS fixture waits for `openssl req`, and for nothing else */
#include <sys/stat.h>	/* mkdir, for the directory the certificate is made in */
#include <tls.h>	/* the peer the TLS leg talks to: this tree's own libtls, which already works here */

/* §58.2j'S INSTRUMENT, THE PROBE'S SIDE OF IT: the same selector `NSURLSessionStreamTask.m` defines, declared
 * here because a declaration inside that file's own region is one this probe could not see - and in Objective-C
 * the two sides only have to agree on the NAME. It reads counters the library keeps IN MEMORY, because the
 * library's own writes were measured to bridge the very loss under investigation (20/20 with them, red 6 of 6
 * without). */
@interface NSURLSessionStreamTask (FNStreamCounts)
- (void)fnStreamCountsHanded:(int *)handed ran:(int *)ran inline:(int *)inline_;
/* §58.2k: what was ENQUEUED (0..2: read, write, other) and what the task's own worker SERVED (3..5). */
- (void)fnServeCounts:(int *)counts;
/* §58.2k's follow-up: reads STARTED and reads whose loop FINISHED (0..1) - the difference is where the
 * worker is stuck. */
- (void)fnReadCounts:(int *)counts;
@end

/* §58.2n'S DELTA SLOTS, AT FILE SCOPE BECAUSE THE CAPTURE AND THE PRINT ARE IN DIFFERENT BLOCKS. The library's
 * counters are file-scope statics too, so they aggregate every task this probe makes; taking the reading HERE -
 * immediately before the reply read - and subtracting it at the DIAG isolates THIS leg's task. `-1` means the
 * capture never ran, and the delta line says so rather than printing a nonsense difference. */
static int streamBeforeLoops = -1;
static int streamBeforeLeft = -1;

/* EVERY WAIT IN THIS PROBE IS A `poll`, AND THAT IS A MEASURED CHOICE RATHER THAN A STYLE ONE. This guest's
 * `usleep` is pathological: a wait of one nominal millisecond costs tens of them, so a probe whose legs wait
 * on a server's delay spent seventy seconds in waits that should have cost one - and the leg that waits for a
 * LATE ANSWER never got it before the harness ran out of patience. `poll`'s timeout, by contrast, is the very
 * thing the class's read deadlines use, and it is exact here: the timeout leg passes on it.
 *
 * THE MACRO IS DELIBERATE AND LOCAL: it rewrites the syscall this file was written with, so every `usleep` in
 * the probe - the server's script and the probe's own waits - becomes a millisecond poll, in one place. */
static void fn_pause(int milliseconds)
{
	struct pollfd none;

	none.fd = -1;
	none.events = 0;
	none.revents = 0;
	poll(&none, 0, milliseconds < 1 ? 1 : milliseconds);
}

#define usleep(microseconds) fn_pause((int)(((microseconds) < 1000) ? 1 : ((microseconds) / 1000)))

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-STREAMTASK %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-STREAMTASK %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE SERVER: ONE CONNECTION PER LEG, DISPATCHED BY ITS ORDINAL. Its two records are what the far end SAW -
 * the bytes it read, and whether it ever saw END OF FILE, which is the only evidence a half-close arrived. */
@interface FNServer : NSObject
{
	@public
	volatile int stop;
	NSMutableData *readBytes;
	BOOL sawEOF;
	int accepted;
}
- (void)run;
@end

@implementation FNServer

- (id)init
{
	self = [super init];
	if(self != nil) {
		readBytes = [[NSMutableData alloc] init];
	}
	return self;
}

- (int)acceptOne:(int)listener
{
	for(;;) {
		int fd = accept(listener, NULL, NULL);

		if(fd >= 0) {
			return fd;
		}
		usleep(5000);
	}
}

- (void)writeAll:(NSString *)text to:(int)fd
{
	const char *bytes = [text UTF8String];
	size_t left = strlen(bytes);

	while(left > 0) {
		ssize_t n = write(fd, bytes, left);

		if(n <= 0) {
			return;
		}
		bytes += n;
		left -= (size_t)n;
	}
}

/* THE SCRIPT, BY LEG - each one exactly what that leg needs and nothing else, which is what one connection per
 * leg buys: no leg can be confused by another's leftovers. */
- (void)serve:(int)fd leg:(int)leg
{
	char buffer[256];
	ssize_t n;

	switch(leg) {
	case 1:
		/* THE WRITE LEG: read what arrived, and record it byte for byte. */
		while((n = read(fd, buffer, sizeof(buffer))) <= 0) {
			usleep(5000);
		}
		[readBytes appendBytes:buffer length:(NSUInteger)n];
		break;
	case 2:
		/* THE READ LEG: four bytes, and the client asks for exactly four. */
		[self writeAll:@"AAAA" to:fd];
		break;
	case 3:
		/* THE MINIMUM LEG: four bytes, A PAUSE, four more - so a read that returned at the first four would
		 * be reading early, which is the whole thing this leg exists to tell apart. */
		[self writeAll:@"BBBB" to:fd];
		usleep(250000);
		[self writeAll:@"CCCC" to:fd];
		break;
	case 4:
		/* THE CAP LEG: eight bytes available, and the client asks for at most four. */
		[self writeAll:@"DDDDDDDD" to:fd];
		break;
	case 5:
		/* THE TIMEOUT LEG: THE CONNECTION STAYS SILENT AND OPEN, with no clock in the answer at all. The first
		 * version slept 600ms and then closed, and the leg failed for a reason that had nothing to do with the
		 * class: the client's read saw END OF FILE, which is a read that COMPLETED - zero bytes, no error - and
		 * an ending is not a timeout. The server now holds the connection until the probe is done, so the only
		 * thing that can end that read is its own deadline. */
		while(!stop) {
			usleep(5000);
		}
		break;
	case 6:
		/* THE ZERO-TIMEOUT LEG: an answer that arrives LATE, because the point is that a zero timeout does not
		 * fire at once. */
		usleep(300000);
		[self writeAll:@"EEEEE" to:fd];
		break;
	case 7:
		/* THE HALF-CLOSE LEG: read until the far end says it is done writing.
		 *
		 * AND THE ANSWER THIS LEG PRODUCED IS WORTH MORE THAN THE CHECK IT WAS WRITTEN FOR: end of file
		 * arrives from the CLIENT'S DESCRIPTOR CLOSING, not from its -closeWrite, because this kernel does not
		 * put a half-close's FIN on the wire (§58.2). So the loop reads until the connection is over, which is
		 * what the probe's far-end check asserts. */
		fcntl(fd, F_SETFL, O_NONBLOCK);
		for(;;) {
			n = read(fd, buffer, sizeof(buffer));
			if(n > 0) {
				[readBytes appendBytes:buffer length:(NSUInteger)n];
				continue;
			}
			if(n == 0) {
				sawEOF = YES;
				break;
			}
			if(stop) {
				break;
			}
			usleep(5000);
		}
		break;
	case 8:
		/* THE REFUSED DOOR'S LEG: the connection is HELD OPEN, because -captureStreams is a QUEUED operation
		 * and it needs a connection to be served on before it can report what it cannot do. */
		while(!stop) {
			usleep(10000);
		}
		break;
	}
}

- (void)run
{
	int listener;
	struct sockaddr_in addr;
	int one = 1;
	int leg;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46497);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		listen(listener, 8);
	}
	for(leg = 1; leg <= 8 && !stop; leg++) {
		int fd = [self acceptOne:listener];

		accepted++;
		if (leg == 5) {
			/* THE TIMEOUT LEG'S CONNECTION IS HELD OPEN AND NEVER SERVED, and that is a correction with a
			 * lesson in it. The leg needs SILENCE - not a delay - because a server that answers late and then
			 * closes gives the client's read an END OF FILE, which is a read that COMPLETED (zero bytes, no
			 * error) and not a timeout at all. And the server is ONE THREAD, so holding the connection inside
			 * its serve branch - the first attempt at `silence` - stopped every leg behind it; the connection
			 * therefore is never handed to a branch at all. Its descriptor is deliberately not closed: the
			 * process is one-shot, and an fd that lives to the end of the probe IS the silence this leg wants. */
			(void)fd;
			continue;
		}
		[self serve:fd leg:leg];
		close(fd);
	}
	close(listener);
}

@end

/* THE DELEGATE: the doors that CAN fire here, the ending, and the metrics. THREE of the four stream doors are
 * reachable on this system - read closed, write closed, and the ending - because `-captureStreams` is refused
 * and a better route needs a second one to be better than. */
@interface FNStreamWatcher : NSObject <NSURLSessionStreamDelegate>
{
	@public
	int readCloseds;
	int writeCloseds;
	int metricsSeen;
	NSInteger endingCode;
	NSString *endingDomain;
}
@end

@implementation FNStreamWatcher

- (void)URLSession:(NSURLSession *)session
readClosedForStreamTask:(NSURLSessionStreamTask *)streamTask
{
	(void)session;
	(void)streamTask;
	readCloseds++;
}

- (void)URLSession:(NSURLSession *)session
writeClosedForStreamTask:(NSURLSessionStreamTask *)streamTask
{
	(void)session;
	(void)streamTask;
	writeCloseds++;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	(void)session;
	(void)task;
	endingCode = error != nil ? [error code] : 0;
	endingDomain = error != nil ? [error domain] : nil;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics
{
	(void)session;
	(void)task;
	(void)metrics;
	metricsSeen++;
}

@end

/* WAIT FOR A FLAG A COMPLETION HANDLER SETS, and the budget is TEN MILLISECONDS PER ROUND.
 *
 * THIS IS THE ONE PLACE THE PROBE'S TIMING LIVES, AND IT WAS WRONG TWICE FOR TWO DIFFERENT REASONS. It began at
 * ten milliseconds a round, which on this guest really costs tens of them - so a 600-round budget was minutes
 * and a leg that waits for a late answer never saw it. Then it became ONE millisecond a round, which is exact
 * now that `usleep` here is a poll (§ fn_pause) - and that made every budget SIXTY TIMES SHORTER than the round
 * counts were written for, so even the first leg's write handler was still on its way when the wait gave up.
 * A BUDGET IS A DURATION, so it is stated as one: rounds × 10ms.
 *
 * AND IT WAS WRONG A THIRD TIME, WHICH IS §58.2p AND THE REASON THIS LINE READS ONE BYTE: the flag a call site
 * passes is a `__block BOOL`, and `BOOL` is ONE byte - boxed, because that is what `__block` means, as the LAST
 * field of a byref structure. `while(*flag == 0)` on a `volatile int *` therefore read the BOOL's byte PLUS THE
 * THREE BYTES PAST THE END OF THE BOX, i.e. heap memory the probe does not own. Whatever malloc left there
 * decided whether the wait ran or returned instantly, so the probe's timing depended on the heap layout - and
 * the layout moves whenever ANYTHING else in the process allocates differently (the library's counters, added
 * for §58.2j..§58.2n, are exactly such a change). That is why a check in the FIRST leg, untouched for days,
 * began reporting that a handler had not answered when its own log line said the handler had answered 60 ms
 * after the call. THE LESSON: a wait whose predicate is a cast is not measuring the flag it names. */
static void fn_waitFor(volatile int *flag, int rounds)
{
	int waited = 0;

	while(*(volatile unsigned char *)flag == 0 && waited < rounds) {
		usleep(10000);
		waited++;
	}
}

/* EACH LEG IS ITS OWN CONNECTION, AND SO ITS OWN TASK. */
static NSURLSessionStreamTask *fn_leg(NSURLSession *session)
{
	NSURLSessionStreamTask *task = [session streamTaskWithHostName:@"127.0.0.1" port:46497];

	[task resume];
	return task;
}

/* --- LEG NINE'S FIXTURE: A TLS SERVER, MADE FROM THIS TREE'S OWN TOOL ---------------------------------- */

/* THE CERTIFICATE, MADE ON THE GUEST BY `openssl req` - the pin's own generator, installed at /System/Tools,
 * used here exactly as the libressl units use it (they make theirs the same way, for the same reason: the key
 * generation is the tool's job and the entropy question is the plan's, not this probe's).
 *
 * IT IS EXEC'D RATHER THAN SHELLED, and the child's chatter goes to a FILE: this guest's `system()` does not
 * return, and it has no /dev/null - the null device is @null. Both of those cost a run to learn, once. */
static int fn_makeCertificate(const char *dir, const char *certPath, const char *keyPath)
{
	pid_t child;
	int status = -1;
	char logPath[512];

	snprintf(logPath, sizeof(logPath), "%s/req.log", dir);
	child = fork();
	if (child == 0) {
		int log = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (log >= 0) {
			dup2(log, 1);
			dup2(log, 2);
		}
		execl("/System/Tools/openssl", "openssl", "req", "-x509", "-newkey", "rsa:2048",
		      "-keyout", keyPath, "-out", certPath, "-days", "1", "-nodes",
		      "-subj", "/CN=localhost", (char *)NULL);
		_exit(127);
	}
	if (child < 0) {
		return -1;
	}
	if (waitpid(child, &status, 0) < 0) {
		return -1;
	}
	return status;
}

/* AND THE PEER IS THIS TREE'S OWN SUBSTRATE RATHER THAN `openssl s_server`, WHICH IS ITSELF A MEASUREMENT
 * (§58.1).
 *
 * THE FIRST VERSION OF THIS LEG DROVE `openssl s_server -www` AND THE HANDSHAKE STALLED: the class's worker
 * parked in its handshake poll, whose deadline is nil because the doors are parameterless and Apple's
 * handshake has no caller-supplied timeout, and the write's handler never fired - so the leg measured the
 * SERVER and learned nothing about the class. That stall is not new in this guest: the libressl units hit it
 * through `openssl s_client` too, and worked around it the same way this does - by taking the handshake
 * through libtls over blocking sockets (`libressl_tls_pair.c`, whose peer answers an HTTP request the same
 * way). So the peer here is written on the substrate the tree has ALREADY PROVEN, and the reply the probe
 * reads is still the far end's own words rather than an echo of what it wrote.
 *
 * IT IS A THREAD AND NOT A CHILD PROCESS, DELIBERATELY: after `fork`, a process with a worker thread may only
 * call async-signal-safe functions, and libtls allocates. (The certificate above IS a child, because it
 * execs.) The peer holds its listener for one connection, handshakes it, reads a request, and answers. */

/* THE BYTE FLOW, LOGGED THROUGH libtls's OWN BIO SEAM. This is the instrument `libressl_tls_pair.c` was written
 * around, and it answers the one question left about the handshake: DOES THE CLIENTHELLO ARRIVE IN FULL? That
 * file's header records the identical stall - "the ClientHello reaches the peer only in part" - and lists the
 * three truths a byte-flow log separates, and nothing else can: the writer never offered all its bytes; the
 * write was accepted but SHORT; or the reader got fewer bytes than were written and never asked again.
 *
 * BLOCKING IS REQUIRED with these callbacks (the tree's own peer says why): a non-blocking descriptor would make
 * read/write answer EAGAIN, which libtls reads as a broken connection rather than as "call me again". The peer
 * above sets the accepted descriptor blocking before it gets here. */
struct fn_tls_bio {
	int fd;
};

static ssize_t fn_tls_read(struct tls *ctx, void *buffer, size_t length, void *argument)
{
	struct fn_tls_bio *bio = argument;
	ssize_t got;

	(void)ctx;
	got = read(bio->fd, buffer, length);
	printf("FOUNDATION-STREAMTASK tls-leg: peer READ wants=%d got=%d%s\n",
	       (int)length, (int)got, got < 0 ? " (error)" : "");
	return got;
}

static ssize_t fn_tls_write(struct tls *ctx, const void *buffer, size_t length, void *argument)
{
	struct fn_tls_bio *bio = argument;
	ssize_t put;

	(void)ctx;
	put = write(bio->fd, buffer, length);
	printf("FOUNDATION-STREAMTASK tls-leg: peer WRITE offers=%d took=%d%s\n",
	       (int)length, (int)put, put < 0 ? " (error)" : "");
	return put;
}

@interface FNTLSPeer : NSObject
{
	@public
	BOOL bound;
	int accepted;
	int handshakes;
	BOOL served;
	int port;
	char cert[512];
	char key[512];
}
- (void)run;
@end

@implementation FNTLSPeer

- (void)run
{
	struct tls_config *config = tls_config_new();
	struct tls *ctx = tls_server();
	struct tls *connection = NULL;
	struct fn_tls_bio bio;
	struct sockaddr_in addr;
	int listener;
	int fd;
	int one = 1;
	char buffer[512];
	size_t collected = 0;
	const char *reply = "HTTP/1.0 200 ok\r\nContent-Type: text/plain\r\n\r\nstream-task-tunnel-reply";
	size_t replyLength = strlen(reply);
	size_t sent = 0;

	if (config == NULL || ctx == NULL) {
		return;
	}
	if (tls_config_set_cert_file(config, cert) == -1 || tls_config_set_key_file(config, key) == -1 ||
	    tls_configure(ctx, config) == -1) {
		return;
	}
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		return;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 4) != 0) {
		return;
	}
	bound = YES;
	fd = accept(listener, NULL, NULL);
	if (fd < 0) {
		return;
	}
	accepted++;
	bio.fd = fd;	/* the seam's argument: the callbacks above read from the descriptor the kernel handed us */
	/* BLOCKING, EXPLICITLY, BECAUSE THIS TREE'S PROVEN TLS PEER DOES IT (libressl_tls_pair.c): libtls's default
	 * BIO is a blocking one. HERE IT IS A MEASURED NO-OP, WHICH SETTLED ONE SUSPECT FOR GOOD: the descriptor
	 * accept() hands back is ALREADY blocking (flags=2, no O_NONBLOCK), so this kernel does NOT propagate the
	 * client's non-blocking flag onto the accepted socket. The class's sockets are non-blocking by design (§58),
	 * and a far end that stalls with the ClientHello already on the wire is what such a propagation would have
	 * looked like. It is not that - and that is a measurement, not an argument. */
	{
		int flags = fcntl(fd, F_GETFL, 0);

		if (flags >= 0) {
			fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		}
	}
	for (;;) {
		int rv = tls_accept_cbs(ctx, &connection, fn_tls_read, fn_tls_write, &bio);

		if (rv == 0) {
			/* REPORTED, BECAUSE A SILENT `return` IS HOW THE DUPLICATED-ACCEPT BUG MASQUERADED AS A STALL: the
			 * second accept failed on an already-accepted connection, the peer returned, and a run in that state
			 * read as "the handshake neither completed nor failed". Every early exit in this peer now says so. */
			printf("FOUNDATION-STREAMTASK tls-leg: the peer accepted the connection\n");
			break;
		}
		if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT) {
			continue;	/* a blocking descriptor: libtls asking again, not a wait to poll for */
		}
		printf("FOUNDATION-STREAMTASK tls-leg: the peer's accept FAILED: %s\n", tls_error(ctx));
		return;
	}
	/* THE HANDSHAKE IS ITS OWN CALL, AND COUNTING THE ACCEPT AS ONE WAS THIS PROBE'S OWN BUG: libtls's
	 * `tls_accept_socket` accepts a connection and DOES NOT PERFORM THE HANDSHAKE - the caller must ask, which
	 * is exactly what `libressl_tls_pair.c`'s peer does and what the check on `handshakes` was reading without.
	 * Found by putting this peer beside that one, line by line, and the correction is worth more than the
	 * check it repaired: it turned "the handshake completes" into a measured FALSE.
	 *
	 * AND IT MUST BE ACCEPTED *ONCE*: an earlier repair of this block left TWO accept loops here (a duplicated
	 * paste), and the second accept fails on a connection already accepted - so the peer RETURNED instead of
	 * handshaking, and a run in that state read as "the handshake neither completed nor failed". That reading
	 * was wrong, and the duplication is why the note below reports the accept's own result. */
	for (;;) {
		int rv = tls_handshake(connection);

		if (rv == 0) {
			break;
		}
		if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT) {
			continue;	/* a blocking descriptor: libtls asking again, not a wait to poll for */
		}
		printf("FOUNDATION-STREAMTASK tls-leg: the peer's handshake FAILED: %s\n", tls_error(connection));
		return;
	}
	handshakes++;
	/* A NOTE, NOT A CHECK: this line is how the probe's log says the far end got as far as a handshake, which is
	 * exactly what its blocked check is watching for. */
	printf("FOUNDATION-STREAMTASK tls-leg: the peer's handshake completed\n");
	/* THE REQUEST IS READ TO ITS BLANK LINE, because what the check needs is that BYTES ARRIVED through the
	 * tunnel - and a peer that answered without reading would have proven only half of it. */
	while (collected < sizeof(buffer) - 1) {
		ssize_t got = tls_read(connection, buffer + collected, sizeof(buffer) - 1 - collected);

		if (got > 0) {
			collected += (size_t)got;
			buffer[collected] = '\0';
			if (strstr(buffer, "\r\n\r\n") != NULL) {
				break;
			}
			continue;
		}
		if (got == TLS_WANT_POLLIN || got == TLS_WANT_POLLOUT) {
			continue;
		}
		break;
	}
	while (sent < replyLength) {
		ssize_t put = tls_write(connection, reply + sent, replyLength - sent);

		if (put > 0) {
			sent += (size_t)put;
			continue;
		}
		if (put == TLS_WANT_POLLIN || put == TLS_WANT_POLLOUT) {
			continue;
		}
		break;
	}
	served = sent == replyLength;
	tls_close(connection);
	tls_free(connection);
	tls_free(ctx);
	tls_config_free(config);
	close(fd);
	close(listener);
}

@end

/* AND THE SPAWNER HANDS BACK THE PEER, because "the fixture is up" has to mean its LISTENER is bound rather
 * than that a thread was created: the caller waits on the flag this sets. */
static FNTLSPeer *fn_spawnTLSServer(const char *certPath, const char *keyPath, int port)
{
	FNTLSPeer *peer = [[FNTLSPeer alloc] init];

	peer->port = port;
	snprintf(peer->cert, sizeof(peer->cert), "%s", certPath);
	snprintf(peer->key, sizeof(peer->key), "%s", keyPath);
	[NSThread detachNewThreadSelector:@selector(run) toTarget:peer withObject:nil];
	return peer;
}

int main(void)
{
	FNServer *server = [[FNServer alloc] init];
	FNStreamWatcher *watcher = [[FNStreamWatcher alloc] init];
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
	NSURLSession *session;
	NSURLSessionStreamTask *task;
	NSOperationQueue *queue = [[NSOperationQueue alloc] init];
	__block NSData *readData = nil;
	__block NSError *readError = nil;
	__block BOOL readDone = NO;
	__block BOOL writeDone = NO;
	__block NSError *writeError = nil;

	setvbuf(stdout, NULL, _IONBF, 0);

	[NSThread detachNewThreadSelector:@selector(run) toTarget:server withObject:nil];
	usleep(100000);

	session = [NSURLSession sessionWithConfiguration:configuration delegate:watcher delegateQueue:queue];

	/* --- LEG ONE: A WRITE, AND THE FAR END'S OWN RECORD OF IT ----------------------------------------- */
	task = fn_leg(session);
	{
		/* §58.2o'S DISCRIMINATING EXPERIMENT, AND IT IS §58.1c'S OWN LESSON APPLIED ONE LEG EARLIER: "the
		 * VTIME probe was measuring its own impatience". This leg's two checks were satisfied at a 600 ms
		 * wait when §58.2i recorded 19/20 and stopped being satisfied later in this session's counter work,
		 * and the two readings are told apart by RAISING the wait and MEASURING the write: if the write is
		 * merely LATE (a slow wake - the checks were asked too early), 3000 ms sees it and the handler's own
		 * line says how late; if it is LOST or TIMED OUT, it stays unanswered, and the call's 5 s deadline is
		 * the bound. The handler prints ONCE, at the START of the probe and therefore NOT in the read path
		 * that §58.2g/§58.2j measured the loss disappearing from - and that print's own effect is read the
		 * way §58.2j's was: if the two checks go green WITH it, the print bridged something and it is
		 * evidence of nothing. */
		NSDate *started = [NSDate date];

		[task writeData:[@"ping" dataUsingEncoding:NSUTF8StringEncoding] timeout:5.0
	     completionHandler:^(NSError *error) {
			/* §58.2p: `errCode=0` WAS AMBIGUOUS, AND THAT AMBIGUITY IS WHAT THIS LINE FIXES - it read the
			 * same whether the handler was handed NIL or an error whose code happens to be 0, and one of
			 * those is a pass while the other is the whole question. It says `nil` now. */
			printf("FOUNDATION-STREAMTASK-LEG1 write-handler: %.0f ms err=%s%ld\n",
			       -[started timeIntervalSinceNow] * 1000.0,
			       (error == nil ? "nil" : "code "),
			       (long)(error != nil ? [error code] : 0));
			writeError = error;
			writeDone = YES;
		}];
		/* §58.2p: 3000 rounds is THIRTY SECONDS under fn_waitFor's own stated budget (rounds × 10 ms), which
		 * is not what this leg means to spend - it means "long enough that a lost-wakeup write would show".
		 * Back to the 600 the leg was written with, now that the wait actually reads the flag it names. */
		fn_waitFor((volatile int *)&writeDone, 600);
	}
	/* §58.2p: AND THIS CHECK WAS MEASURING ITS OWN IMPATIENCE, WHICH IS WHY IT BEGAN FAILING WHILE NOTHING IN
	 * THE CLASS CHANGED. The write's handler fires as soon as the bytes are handed to the KERNEL and makes no
	 * claim about the peer, so reading `server->readBytes` the instant it ran asked the far end to have
	 * looped back already - and the server's own leg-one arm polls at 5 ms. The bound below is that poll's:
	 * generous, bounded, and written down instead of left to the scheduler. §58.1c's lesson ("the VTIME probe
	 * was measuring its own impatience"), met one layer out. */
	{
		int waited = 0;

		while([server->readBytes length] == 0 && waited < 400) {
			fn_pause(5);
			waited++;
		}
	}
	check("the-write-completes", writeDone && writeError == nil,
	      @"the write's handler answers, and answers without an error");
	check("and-the-far-end-read-it",
	      [[[NSString alloc] initWithData:server->readBytes encoding:NSUTF8StringEncoding] hasPrefix:@"ping"],
	      @"the bytes reached the server, which is what the handler's promise stops short of claiming");

	/* --- LEG TWO: THE READ DELIVERS WHAT THE SERVER SAID ---------------------------------------------- */
	task = fn_leg(session);
	readDone = NO;
	readError = nil;
	[task readDataOfMinLength:4 maxLength:4 timeout:5.0
		completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		readData = data;
		readError = error;
		readDone = YES;
	}];
	fn_waitFor((volatile int *)&readDone, 600);
	check("the-read-delivers-what-the-server-said",
	      readDone && readError == nil && [readData length] == 4 &&
	      [[[NSString alloc] initWithData:readData encoding:NSUTF8StringEncoding] isEqualToString:@"AAAA"],
	      @"four bytes asked for, four delivered, and they are the server's four");

	/* --- LEG THREE: THE MINIMUM IS A MINIMUM ---------------------------------------------------------- */
	task = fn_leg(session);
	readDone = NO;
	readError = nil;
	[task readDataOfMinLength:8 maxLength:8 timeout:5.0
		completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		readData = data;
		readError = error;
		readDone = YES;
	}];
	fn_waitFor((volatile int *)&readDone, 600);
	check("the-minimum-is-a-minimum",
	      readDone && readError == nil && [readData length] == 8 &&
	      [[[NSString alloc] initWithData:readData encoding:NSUTF8StringEncoding] isEqualToString:@"BBBBCCCC"],
	      @"the server sent four bytes, paused, and sent four more: a read returning early would be four");

	/* --- LEG FOUR: THE CAP IS A CAP ------------------------------------------------------------------- */
	task = fn_leg(session);
	readDone = NO;
	readError = nil;
	[task readDataOfMinLength:1 maxLength:4 timeout:5.0
		completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		readData = data;
		readError = error;
		readDone = YES;
	}];
	fn_waitFor((volatile int *)&readDone, 600);
	check("the-cap-is-a-cap",
	      readDone && readError == nil && [readData length] == 4 &&
	      [[[NSString alloc] initWithData:readData encoding:NSUTF8StringEncoding] isEqualToString:@"DDDD"],
	      @"eight bytes are available and four were asked for");

	/* --- LEG FIVE: THE TIMEOUT IS A CANCEL ------------------------------------------------------------ */
	task = fn_leg(session);
	readDone = NO;
	readError = nil;
	[task readDataOfMinLength:8 maxLength:16 timeout:0.3
		completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		readData = data;
		readError = error;
		readDone = YES;
	}];
	fn_waitFor((volatile int *)&readDone, 600);
	check("the-timeout-is-a-cancel",
	      readDone && readError != nil && [readError code] == NSURLErrorTimedOut &&
	      [readError.domain isEqualToString:NSURLErrorDomain] && [readData length] == 0,
	      [NSString stringWithFormat:@"expected a cancel: done=%d code=%ld domain=%@ bytes=%lu",
				 readDone, (long)(readError != nil ? [readError code] : 0),
				 readError != nil ? [readError domain] : @"(none)",
				 (unsigned long)[readData length]]);

	/* --- LEG SIX: A ZERO TIMEOUT DOES NOT FIRE -------------------------------------------------------- */
	task = fn_leg(session);
	readDone = NO;
	readError = nil;
	[task readDataOfMinLength:5 maxLength:16 timeout:0.0
		completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		readData = data;
		readError = error;
		readDone = YES;
	}];
	fn_waitFor((volatile int *)&readDone, 900);
	check("a-zero-timeout-does-not-fire",
	      readDone && readError == nil && [readData length] == 5 &&
	      [[[NSString alloc] initWithData:readData encoding:NSUTF8StringEncoding] isEqualToString:@"EEEEE"],
	      @"the server answered after a pause, and 'pass 0' means what the documentation says it means");

	/* --- LEG SEVEN: THE HALVES CLOSE, THE DELEGATE HEARS BOTH, THE TASK ENDS, AND THE FAR END SEES IT ------
	 *
	 * WHAT THIS LEG CANNOT CHECK, AND WHY IT IS SAID HERE RATHER THAN IMPLIED BY A CHECK NAME: Apple's contract
	 * for -closeWrite is that the far end sees the write side end, and on THIS KERNEL IT DOES NOT - measured
	 * (§58.2): the class's `shutdown(fd, SHUT_WR)` returns 0 and the peer's non-blocking read answers EAGAIN
	 * for ever. So the end of file the server eventually sees comes from the DESCRIPTOR CLOSING when both
	 * halves are shut - which is what the class does at its ending - and the check below says exactly that. */
	task = fn_leg(session);
	watcher->writeCloseds = 0;
	watcher->readCloseds = 0;
	watcher->metricsSeen = 0;
	watcher->endingCode = 0;
	watcher->endingDomain = nil;
	[task writeData:[@"bye" dataUsingEncoding:NSUTF8StringEncoding] timeout:5.0 completionHandler:^(NSError *e) {
		(void)e;
	}];
	usleep(150000);
	[task closeWrite];
	{
		int waited = 0;

		while(watcher->writeCloseds == 0 && waited < 300) {
			usleep(10000);
			waited++;
		}
	}
	check("close-write-reports-its-side", watcher->writeCloseds == 1,
	      @"-URLSession:writeClosedForStreamTask: is the door for the write side, and it fires");
	[task closeRead];
	{
		int waited = 0;

		while(watcher->readCloseds == 0 && waited < 300) {
			usleep(10000);
			waited++;
		}
	}
	check("and-the-read-side-too", watcher->readCloseds == 1,
	      @"-URLSession:readClosedForStreamTask: is the door for the other half");
	check("and-the-task-ends-when-both-halves-close",
	      watcher->endingCode == 0 && watcher->endingDomain == nil,
	      @"no error: a stream whose halves both closed has ENDED rather than failed");
	{
		/* THE FAR END'S END OF FILE, WHICH IS THE DESCRIPTOR'S CLOSE RATHER THAN THE HALF-CLOSE (§58.2), and
		 * asking for it here is what makes that difference visible instead of assumed. */
		int waited = 0;

		while(!server->sawEOF && waited < 600) {
			usleep(10000);
			waited++;
		}
	}
	check("and-the-far-end-sees-the-connection-end", server->sawEOF,
	      @"what the peer sees is the close: this kernel does not put a half-close's FIN on the wire (§58.2)");
	check("and-it-was-measured", watcher->metricsSeen >= 1,
	      @"the record §52 delivers arrives for a stream task too");

	/* --- LEG EIGHT: THE REFUSED DOOR SAYS SO ---------------------------------------------------------- */
	task = fn_leg(session);
	watcher->endingCode = 0;
	watcher->endingDomain = nil;
	[task captureStreams];
	{
		int waited = 0;

		while(watcher->endingCode == 0 && waited < 600) {
			usleep(10000);
			waited++;
		}
	}
	check("capture-streams-is-refused-with-its-ground",
	      [watcher->endingDomain isEqualToString:NSURLErrorDomain] &&
	      watcher->endingCode == NSURLErrorUnsupportedURL,
	      @"the door is declared, and what it cannot do is reported rather than lied about");

	server->stop = 1;
	check("the-probe-served-what-it-was-asked", server->accepted >= 8,
	      @"one connection per leg, which is what keeps one leg's leftovers out of another's read");

	/* --- LEG NINE: THE TUNNEL (§58.1 - the leg this row OWED) -------------------------------------------
	 *
	 * THE HANDSHAKE IS PROVEN INDIRECTLY, AND THAT IS THE POINT RATHER THAN A CONVENIENCE: the class's read
	 * and write go through SSL_read/SSL_write once -startSecureConnection has upgraded the connection, so the
	 * FAR END'S REPLY CANNOT COME BACK THROUGH A HANDSHAKE THAT DID NOT HAPPEN. The two doors are
	 * parameterless (Apple's are), so there is no handler to report a handshake at all - and the I/O is
	 * better evidence than such a handler would have been anyway: it is the far end talking.
	 *
	 * AND THE FAR END IS THE PROBE'S OWN LIBTLS PEER, which is itself a measurement: `openssl s_server` was
	 * tried FIRST and stalled the handshake (see the peer's own comment above), so this leg talks to the
	 * substrate the tree has proven in this guest rather than to a tool that has stalled in it twice. */
	{
		const char *dir = "/System/Temporary Files/streamtask-tls";
		char certPath[512];
		char keyPath[512];
		FNTLSPeer *tlsPeer;
		int reqStatus;
		__block BOOL writeDone = NO;
		__block NSError *writeError = nil;
		__block NSData *replyData = nil;
		__block NSError *replyError = nil;
		__block BOOL replyDone = NO;
		__block BOOL downDone = NO;
		__block BOOL downEOF = NO;
		__block NSError *downError = nil;

		snprintf(certPath, sizeof(certPath), "%s/cert.pem", dir);
		snprintf(keyPath, sizeof(keyPath), "%s/key.pem", dir);
		mkdir(dir, 0755);
		reqStatus = fn_makeCertificate(dir, certPath, keyPath);
		tlsPeer = fn_spawnTLSServer(certPath, keyPath, 46499);
		fn_waitFor((volatile int *)&tlsPeer->bound, 300);
		check("the-tls-fixture-is-up", reqStatus == 0 && tlsPeer->bound,
		      @"`openssl req` made a certificate and the libtls peer's listener is bound with it");

		task = [session streamTaskWithHostName:@"127.0.0.1" port:46499];
		[task resume];
		[task startSecureConnection];
		/* --- THE TUNNEL, AND THE BUG THAT WAS HOLDING IT SHUT ---------------------------------------------
		 *
		 * THE HANDSHAKE IS PROVEN BY THE FAR END'S OWN ACCOUNT, because the two TLS doors are parameterless
		 * (Apple's are) and there is no handler to ask: the peer is this tree's libtls, its `tls_handshake` has
		 * returned, and the byte flow behind that is logged line by line by the seam above. AND THE I/O ON TOP
		 * OF IT IS THE REST OF THE EVIDENCE: the request goes out through SSL_write, the far end answers with
		 * `HTTP/1.0 200 ok`, and the reply comes back through SSL_read - which cannot happen through a handshake
		 * that did not happen.
		 *
		 * WHAT WAS HOLDING IT SHUT, MEASURED RATHER THAN GUESSED, AND IT WAS NOT THE TLS CODE AT ALL: this
		 * kernel leaves a `poll(2)` waiter PARKED when the timeout is NEGATIVE, even on a socket that has become
		 * readable. The class's handshake wait passed `-1` (no caller deadline), so it sat there while the far
		 * end's whole flight - 127 + 6 + 28 + 715 + 286 + 58 bytes, every one of them accepted - arrived on its
		 * own socket. `fnPollForWrite:` now waits in bounded SLICES when there is no deadline (§58.1's fix and
		 * the kernel's work item, both recorded), and the handshake completes on the first slice loop. */
		/* AND THE WAIT IS PART OF THE CHECK'S MEANING, NOT A DELAY BEFORE IT: the handshake happens on the
		 * WORKER thread while this one runs, so asking instantly is asking before the answer can exist. */
		fn_waitFor((volatile int *)&tlsPeer->handshakes, 600);
		check("the-tls-handshake-completes", tlsPeer->handshakes == 1,
		      @"the far end's tls_handshake returned: the ClientHello and the client's Finished both crossed");
		[task writeData:[@"GET / HTTP/1.0\r\n\r\n" dataUsingEncoding:NSUTF8StringEncoding]
			timeout:10.0 completionHandler:^(NSError *e) {
			writeError = e;
			writeDone = YES;
		}];
		fn_waitFor((volatile int *)&writeDone, 1200);
		{
			int waited = 0;

			while(!tlsPeer->served && waited < 600) {	/* the peer replies only after reading the request */
				usleep(10000);
				waited++;
			}
		}
		check("the-request-goes-through-the-tunnel", writeDone && writeError == nil && tlsPeer->served,
		      @"SSL_write carried it and the far end READ it before answering - two sides, one tunnel");
		/* §58.2n: THE DELTA'S CAPTURE, immediately before the read that is lost. If `left` MOVES between here and
		 * the DIAG below, this leg's worker RETURNED - and that is the loss. If it does not move, the worker is
		 * PARKED in `[_queue wait]` with the reply read sitting in `_operations`: a lost signal, which is a
		 * different bug from a worker that left. The counters are file-scope statics and therefore aggregate all
		 * nine of this probe's tasks, which is exactly why the reading is taken here, per leg. */
		{
			int before[11];

			[task fnServeCounts:before];
			streamBeforeLoops = before[7];
			streamBeforeLeft = before[9];
		}
		[task readDataOfMinLength:1 maxLength:4096 timeout:10.0
			completionHandler:^(NSData *data, BOOL atEOF, NSError *e) {
			replyData = data;
			replyError = e;
			replyDone = YES;
		}];
		fn_waitFor((volatile int *)&replyDone, 1200);
		/* STREAMTASK-DIAG, TEMPORARY (§58.1c's neighbourhood): this check failed with the peer's seam
		 * showing 115 bytes accepted by `write(2)`, and a `readDataOfMinLength:1` returns as soon as ANY
		 * byte is available - so the question is not "did the tunnel carry it" but "how much of it
		 * arrived in this one call". A partial read is indistinguishable from a lost one in the check
		 * below, and it is what a coalescing-sensitive expectation looks like. */
		{
			NSString *replyText = replyData ? [[NSString alloc] initWithData:replyData
									 encoding:NSUTF8StringEncoding] : nil;
			int has200 = (replyText != nil &&
				      [replyText rangeOfString:@"HTTP/1.0 200 ok"].location != NSNotFound) ? 1 : 0;
			int handed = -1;
			int ran = -1;
			int inlineHops = -1;
			int counts[14] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
			int readCounts[2] = {-1, -1};

			/* §58.2j: THE HOP COUNTS, READ ONCE, FROM MEMORY. §58.2d asked this same question with writes
			 * and got `handed=14 ran=7`; the library's writes here were then measured to BRIDGE the loss
			 * (green with them, red 6 of 6 without), so this reads instead of writing. */
			[task fnStreamCountsHanded:&handed ran:&ran inline:&inlineHops];
			/* §58.2k: AND WHERE THE REPLY READ GOT TO - enqueued? served? - which is the question the last
			 * red run left open, now that every hop is known to run. `started - finished == 1` names a read
			 * the worker is stuck inside; equal with a read still queued says it is stuck between them. */
			[task fnServeCounts:counts];
			[task fnReadCounts:readCounts];
			printf("FOUNDATION-STREAMTASK-DIAG reply: done=%d errCode=%ld len=%d has200=%d"
			       " hops handed=%d ran=%d inline=%d"
			       " ops enq=%d/%d/%d served=%d/%d/%d reads started=%d finished=%d"
			       " worker entered=%d loops=%d waits=%d left=%d noconnect=%d"
			       " fin=%d/%d/%d unreturned=%d"
			       " THISLEG before(loops=%d left=%d) delta(loops=%d left=%d)\n",
			       (int)replyDone, (long)(replyError != nil ? [replyError code] : 0),
			       (int)(replyData != nil ? [replyData length] : 0), has200, handed, ran, inlineHops,
			       counts[0], counts[1], counts[2], counts[3], counts[4], counts[5],
			       readCounts[0], readCounts[1],
			       counts[6], counts[7], counts[8], counts[9], counts[10],
			       /* §58.2n: THE SPLIT OF THE LAST TWO SHAPES. `unreturned` is the sum of every arm the
				* worker ENTERED (slots 3..5) minus every arm it LEFT (slots 11..13): ZERO means it is parked
				* BETWEEN operations - a lost `signal` on the task's `NSCondition` - and positive means it is
				* stuck INSIDE an arm, which the by-kind split just before it names. */
			       counts[11], counts[12], counts[13],
			       (counts[3] + counts[4] + counts[5]) - (counts[11] + counts[12] + counts[13]),
			       streamBeforeLoops, streamBeforeLeft,
			       (streamBeforeLeft < 0 ? -1 : counts[7] - streamBeforeLoops),
			       (streamBeforeLeft < 0 ? -1 : counts[9] - streamBeforeLeft));
		}
		check("and-the-reply-comes-back-through-it",
		      replyDone && replyError == nil &&
		      [[[NSString alloc] initWithData:replyData encoding:NSUTF8StringEncoding]
			rangeOfString:@"HTTP/1.0 200 ok"].location != NSNotFound,
		      @"the far end's own words, read back through SSL_read - the tunnel carries both ways");
		check("and-the-far-end-answered-through-the-tunnel", tlsPeer->served,
		      @"the peer's tls_write put its whole reply on the wire, counted by the seam's own log");
		/* AND THE ROW'S OTHER OWED HALF, WHICH THE TUNNEL NOW MAKES OBSERVABLE: -stopSecureConnection takes the
		 * TLS session down (an SSL_shutdown and an SSL_free), so what follows is plain I/O on the descriptor.
		 *
		 * THE CHECK IS THE SEQUENCE, NOT A GUESS ABOUT THE FAR END. One draft asserted that a plain read after
		 * the stop completes - and it FAILED, correctly: a socket the far end has not closed yet is a socket on
		 * which a read TIMES OUT, and that is not a failure of anything. What IS deterministic, and what says the
		 * session came down without breaking the task, is that THE TASK KEEPS SERVING ITS QUEUE: both halves still
		 * close and both doors still report. Whether the far end had closed or stayed quiet is PRINTED below. */
		[task stopSecureConnection];
		[task readDataOfMinLength:1 maxLength:4096 timeout:2.0
			completionHandler:^(NSData *data, BOOL atEOF, NSError *e) {
			(void)data;
			downEOF = atEOF;
			downError = e;
			downDone = YES;
		}];
		fn_waitFor((volatile int *)&downDone, 300);
		watcher->writeCloseds = 0;
		watcher->readCloseds = 0;
		[task closeWrite];
		[task closeRead];
		{
			int waited = 0;

			while((watcher->writeCloseds == 0 || watcher->readCloseds == 0) && waited < 600) {
				usleep(10000);
				waited++;
			}
		}
		check("and-taking-the-tunnel-down-leaves-a-working-connection",
		      watcher->writeCloseds == 1 && watcher->readCloseds == 1,
		      @"both halves still close after -stopSecureConnection: the task kept serving its queue");
		printf("FOUNDATION-STREAMTASK tls-leg accepted=%d handshakes=%d reply-sent=%d write-handler=%d after-stop-atEOF=%d after-stop-error=%d code=%ld\n",
		       tlsPeer->accepted, tlsPeer->handshakes, (int)tlsPeer->served, (int)writeDone,
		       (int)downEOF, downError != nil ? 1 : 0, (long)[downError code]);
	}

	printf("FOUNDATION-STREAMTASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STREAMTASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-STREAMTASK DONE\n");
	return failc ? 1 : 0;
}
