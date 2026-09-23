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
 * A BUDGET IS A DURATION, so it is stated as one: rounds × 10ms. */
static void fn_waitFor(volatile int *flag, int rounds)
{
	int waited = 0;

	while(*flag == 0 && waited < rounds) {
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
	[task writeData:[@"ping" dataUsingEncoding:NSUTF8StringEncoding] timeout:5.0
     completionHandler:^(NSError *error) {
		writeError = error;
		writeDone = YES;
	}];
	fn_waitFor((volatile int *)&writeDone, 600);
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

	printf("FOUNDATION-STREAMTASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STREAMTASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-STREAMTASK DONE\n");
	return failc ? 1 : 0;
}
