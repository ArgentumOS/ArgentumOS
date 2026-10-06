/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_port — W6b's acceptance: NSPort and NSSocketPort. docs/design/foundation-plan.md §43.
 *
 * ONE unit, importing <Foundation/Foundation.h> plus the socket headers, because a port IS a socket and the
 * checks have to talk to the kernel on the other side of one.
 *
 *   port-valid-until-invalidated   a fresh port is valid, `-invalidate` flips it, and a SECOND one is a
 *                                  no-op — which is what makes it safe on a teardown path that runs twice
 *   port-invalidation-notification the notification's NAME and its OBJECT, which is the port itself
 *   socketport-local-init          `-init` binds a real AF_INET/SOCK_STREAM socket on an EPHEMERAL port
 *   socketport-tcp-port-binds      `-initWithTCPPort:` then `getsockname(2)` on `-socket` — THE KERNEL is
 *                                  asked what was bound, so this cannot agree with a wrong accessor
 *   socketport-remote-connects     a full loopback round trip: accept(2) on the server, write(2) on the
 *                                  client, read(2) the byte on the accepted connection — plus the peer
 *                                  address `-address` reports for a REMOTE port
 *   socketport-wraps-a-descriptor  a descriptor the CALLER made is adopted and NOT owned: it is still open
 *                                  after `-invalidate` (the NSFileHandle rule, on a socket)
 *   socketport-closes-what-it-owns a descriptor the port CREATED is closed: fcntl(2) answers EBADF after
 *   port-schedule-fires-on-readiness  THE SEAM, through a port: silent with no client, fires the moment one
 *                                  connects (a pair, both halves required)
 *   port-remove-from-runloop       after `-removeFromRunLoop:forMode:` the same readiness says nothing
 *   port-source-respects-the-mode  a port scheduled in one mode is NOT fired by a pass in another
 *   port-archiving-refused         Apple documents "coding only by an NSPortCoder" and a port is not
 *                                  archivable — the refusal is checked rather than asserted in prose
 */

#import <Foundation/Foundation.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* A PORT THE PROBE CAN LISTEN TO. The hook is OURS (the delegate that would have carried the readiness is
 * Apple-deprecated - which since 2026-09-26 makes it a WORK ITEM rather than an exclusion, plan section
 * 62.24 - so this is a placeholder for API this library still OWES), so a consumer subclasses to hear it. */
@interface FnReadablePort : NSSocketPort
{
	NSUInteger _ready;
}
- (NSUInteger)ready;
- (void)portDidBecomeReadable;
@end

@implementation FnReadablePort

- (NSUInteger)ready
{
	return _ready;
}

- (void)portDidBecomeReadable
{
	_ready++;
}

@end

@interface FnPortObserver : NSObject
{
	NSUInteger _invalids;
	id _lastObject;
}
- (void)note:(NSNotification *)notification;
- (NSUInteger)invalids;
- (id)lastObject;
@end

@implementation FnPortObserver

- (void)note:(NSNotification *)notification
{
	if ([[notification name] isEqualToString:NSPortDidBecomeInvalidNotification]) {
		_invalids++;
		_lastObject = [notification object];
	}
}

- (NSUInteger)invalids
{
	return _invalids;
}

- (id)lastObject
{
	return _lastObject;
}

@end

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-PORT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PORT %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSSocketPort", "socket") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* THE PORT A BOUND SOCKET REPORTS, read out of the raw sockaddr `-address` hands back. */
static unsigned short port_of(NSData *address)
{
	struct sockaddr_in sin;

	if ([address length] < sizeof(sin)) {
		return 0;
	}
	memcpy(&sin, [address bytes], sizeof(sin));
	if (sin.sin_family != AF_INET) {
		return 0;
	}
	return ntohs(sin.sin_port);
}

#define PROBE_PORT 45678

int main(void)
{
	/* ---- validity, once and only once ---------------------------------- */
	{
		FnPortObserver *observer = [[FnPortObserver alloc] init];
		NSPort *port = [NSPort port];

		[[NSNotificationCenter defaultCenter] addObserver:observer
							selector:@selector(note:)
							    name:NSPortDidBecomeInvalidNotification
							  object:nil];
		check("port-valid-until-invalidated",
		      port != nil && [port isValid] && [port reservedSpaceLength] == 0,
		      [NSString stringWithFormat:@"a fresh port: isValid=%d reserved=%lu",
			(int)[port isValid], (unsigned long)[port reservedSpaceLength]]);

		[port invalidate];
		[port invalidate];		/* the SECOND one must change nothing and announce nothing */
		check("port-invalidation-notification",
		      ![port isValid] && [observer invalids] == 1 && [observer lastObject] == port,
		      [NSString stringWithFormat:@"isValid=%d notifications=%lu object-is-port=%d",
			(int)[port isValid], (unsigned long)[observer invalids],
			(int)([observer lastObject] == port)]);

		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	/* ---- the socket half ------------------------------------------------ */
	{
		NSSocketPort *port = [[NSSocketPort alloc] init];

		check("socketport-local-init",
		      port != nil && [port socket] >= 0 && [port protocolFamily] == AF_INET &&
		      [port socketType] == SOCK_STREAM && [[port address] length] == sizeof(struct sockaddr_in) &&
		      port_of([port address]) != 0,
		      [NSString stringWithFormat:@"socket=%d family=%d type=%d addressLength=%lu port=%u",
			(int)[port socket], [port protocolFamily], [port socketType],
			(unsigned long)[[port address] length], port_of([port address])]);
		[port invalidate];
	}

	{
		NSSocketPort *port = [[NSSocketPort alloc] initWithTCPPort:PROBE_PORT];
		struct sockaddr_in bound;
		socklen_t length = sizeof(bound);
		unsigned short asked = 0;

		memset(&bound, 0, sizeof(bound));
		if (port != nil && getsockname([port socket], (struct sockaddr *)&bound, &length) == 0 &&
		    bound.sin_family == AF_INET) {
			asked = ntohs(bound.sin_port);
		}
		check("socketport-tcp-port-binds",
		      port != nil && asked == PROBE_PORT && port_of([port address]) == PROBE_PORT,
		      [NSString stringWithFormat:@"asked %d, the kernel bound %u, -address says %u",
			PROBE_PORT, (unsigned)asked, (unsigned)port_of([port address])]);
	covers("NSSocketPort", "initWithTCPPort:");
	covers("NSSocketPort", "socket");
		[port invalidate];
	}

	{
		NSSocketPort *server = [[NSSocketPort alloc] initWithTCPPort:PROBE_PORT];
		NSSocketPort *client = [[NSSocketPort alloc] initWithRemoteWithTCPPort:PROBE_PORT
									  host:@"127.0.0.1"];
		int conn = -1;
		char byte = 'z';
		char got = 0;
		ssize_t wrote = -1, readBack = -1;

		if (server != nil && client != nil) {
			conn = accept([server socket], NULL, NULL);
			wrote = write([client socket], &byte, 1);
			readBack = (conn >= 0) ? read(conn, &got, 1) : -1;
			if (conn >= 0) {
				close(conn);
			}
		}
		check("socketport-remote-connects",
		      server != nil && client != nil && conn >= 0 && wrote == 1 && readBack == 1 &&
		      got == 'z' && port_of([client address]) == PROBE_PORT,
		      [NSString stringWithFormat:@"server=%d client=%d accept=%d wrote=%ld read=%ld byte=%d "
			@"peerPort=%u", (int)(server != nil), (int)(client != nil), conn, (long)wrote,
			(long)readBack, (int)got, (unsigned)port_of([client address])]);
		[server invalidate];
		[client invalidate];
	}

	{
		int pair[2];
		int adoptedIsOpen = 0;
		NSSocketPort *port = nil;

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0) {
			port = [[NSSocketPort alloc] initWithProtocolFamily:AF_UNIX
								 socketType:SOCK_STREAM
								   protocol:0
								     socket:pair[0]];
			[port invalidate];
			adoptedIsOpen = fcntl(pair[0], F_GETFD) != -1;	/* THE CALLER'S DESCRIPTOR SURVIVES */
			if (adoptedIsOpen) {
				close(pair[0]);
			}
			close(pair[1]);
		}
		check("socketport-wraps-a-descriptor",
		      pair[0] >= 0 && port != nil && [port socket] == pair[0] && adoptedIsOpen,
		      [NSString stringWithFormat:@"port=%d socket-was=%d still-open-after-invalidate=%d",
			(int)(port != nil), pair[0], adoptedIsOpen]);
	}

	{
		NSSocketPort *port = [[NSSocketPort alloc] init];
		int fd = [port socket];

		[port invalidate];
		check("socketport-closes-what-it-owns",
		      fd >= 0 && fcntl(fd, F_GETFD) == -1,
		      [NSString stringWithFormat:@"descriptor %d was still open after -invalidate", fd]);
	}

	/* ---- being a run-loop source ---------------------------------------- */
	{
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		int pair[2];
		int pairMade = socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
		FnReadablePort *port = (pairMade == 0)
			? [[FnReadablePort alloc] initWithProtocolFamily:AF_UNIX
							      socketType:SOCK_STREAM
								protocol:0
								  socket:pair[0]]
			: nil;
		char byte = 'q';
		BOOL quietWhileIdle;

		/* A CONNECTED SOCKET RATHER THAN A LISTENING ONE, AND THAT IS A MEASURED CHOICE (§43): `select(2)`
		 * on this kernel does NOT report a LISTENING descriptor as readable while a peer waits on it —
		 * `accept(2)` returns the connection and `select(2)` says nothing (the run before this one
		 * measured exactly that: ephemeral=49161, client connected, select-says-readable=0) — so a source
		 * test built on a listening socket would be a test of the KERNEL rather than of the port. A
		 * socketpair CARRIES data, and a descriptor with data is readable; W6a's pipe checks are the
		 * precedent. */
		[port scheduleInRunLoop:loop forMode:NSDefaultRunLoopMode];
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		quietWhileIdle = [port ready] == 0;

		(void)write(pair[1], &byte, 1);
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		/* A PAIR: silent with nothing written, and told the moment something is. */
		check("port-schedule-fires-on-readiness",
		      pairMade == 0 && quietWhileIdle && [port ready] == 1,
		      [NSString stringWithFormat:@"socketpair=%d quiet-while-idle=%d ready=%lu",
			pairMade, (int)quietWhileIdle, (unsigned long)[port ready]]);

		/* ---- and removal makes it quiet again ---------------------------- */
		[port removeFromRunLoop:loop forMode:NSDefaultRunLoopMode];
		(void)write(pair[1], &byte, 1);
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		check("port-remove-from-runloop",
		      [port ready] == 1,
		      [NSString stringWithFormat:@"ready=%lu after removal (was 1)",
			(unsigned long)[port ready]]);

		[port invalidate];
		if (pairMade == 0) {
			close(pair[1]);
		}
	}

	{
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		int pair[2];
		int pairMade = socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
		FnReadablePort *port = (pairMade == 0)
			? [[FnReadablePort alloc] initWithProtocolFamily:AF_UNIX
							      socketType:SOCK_STREAM
								protocol:0
								  socket:pair[0]]
			: nil;
		char byte = 'm';
		BOOL quietInAnotherMode;

		/* THE MODE IS PART OF THE REGISTRATION, so a pass in another mode must not deliver: the source
		 * was added FOR a mode, and only a pass running it (or the common modes) may fire it. */
		[port scheduleInRunLoop:loop forMode:@"FNPortProbeMode"];
		(void)write(pair[1], &byte, 1);
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		quietInAnotherMode = [port ready] == 0;
		[loop runMode:@"FNPortProbeMode"
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		check("port-source-respects-the-mode",
		      pairMade == 0 && quietInAnotherMode && [port ready] == 1,
		      [NSString stringWithFormat:@"quiet-in-default-mode=%d ready=%lu",
			(int)quietInAnotherMode, (unsigned long)[port ready]]);

		[port invalidate];
		if (pairMade == 0) {
			close(pair[1]);
		}
	}

	/* ---- and the thing a port cannot do --------------------------------- */
	{
		NSPort *port = [NSPort port];
		int raised = 0;

		@try {
			(void)[NSKeyedArchiver archivedDataWithRootObject:port];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSInvalidArgumentException];
		}
		check("port-archiving-refused", raised,
		      @"a port was ARCHIVED: Apple documents that only an NSPortCoder may code one");
		[port invalidate];
	}

	printf("FOUNDATION-PORT RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving input for a
	 * while, so `echo $?` may never run. This is the same value: failc ? 1 : 0 is the return below. */
	printf("FOUNDATION-PORT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PORT DONE\n");
	return failc ? 1 : 0;
}
