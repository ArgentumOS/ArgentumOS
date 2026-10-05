/*
 * NSMachPort.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSMachPort.h for the substitution, the two refusals and the ownership rule `-peerPort` follows. What is
 * here is the smallest thing that can be a port on a system with no Mach: ONE SOCKET PAIR, one end held by the
 * port and one waiting to be handed out.
 */

#import <Foundation/NSMachPort.h>
#import <Foundation/NSSocketPort.h>
#include <sys/socket.h>
#include <unistd.h>		/* close(2): the end nobody asked for, and the pair after a failed init */

/* THE OWNERSHIP HANDOFF, DECLARED HERE AND DEFINED IN NSSocketPort.m. A descriptor the CALLER made is the
 * caller's to close — which is right until the caller HANDS IT OVER, and that hand is the only reason this door
 * exists. It is private to the library in the sense that only this family calls it. */
@interface NSSocketPort (FNPrivate)
- (void)fnTakeOwnership;
@end

@implementation NSMachPort

- (instancetype)init
{
	int pair[2];

	/* A PORT PAIR, MADE HERE. `socketpair(2)` rather than a listening TCP socket — which is what NSSocketPort's
	 * own `-init` makes — because a port is an endpoint you send THROUGH, and a pair is the smallest thing that
	 * has a far end to send to. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
		return nil;
	}
	self = [super initWithProtocolFamily:AF_UNIX socketType:SOCK_STREAM protocol:0 socket:pair[0]];
	if (self == nil) {
		close(pair[0]);
		close(pair[1]);
		return nil;
	}
	/* THIS INITIALIZER MADE IT, SO THIS PORT CLOSES IT — the opposite of the rule the initializer it called
	 * applies to a descriptor IT was handed, which is the distinction the ownership rule is made of. */
	[self fnTakeOwnership];
	_peerSocket = pair[1];
	return self;
}

/* ---- the doors that cannot be honest here, and refuse instead ---------------- */

- (instancetype)initWithMachPort:(uint32_t)machPort
{
	/* A NUMBER IS NOT A PORT ON THIS SYSTEM. Mach port rights are what make a number name something, and there
	 * are none here — so a port cannot be rebuilt from one, and answering nil says so where a caller asks. */
	(void)machPort;
	[self release];
	return nil;
}

- (instancetype)initWithMachPort:(uint32_t)machPort options:(NSMachPortOptions)options
{
	(void)machPort;
	(void)options;
	[self release];
	return nil;
}

+ (NSPort *)portWithMachPort:(uint32_t)machPort
{
	(void)machPort;
	return nil;
}

+ (NSPort *)portWithMachPort:(uint32_t)machPort options:(NSMachPortOptions)options
{
	(void)machPort;
	(void)options;
	return nil;
}

/* ---- what a port here is, and how it is reached ----------------------------- */

- (uint32_t)machPort
{
	return (uint32_t)[self socket];
}

- (NSPort *)peerPort
{
	NSSocketPort *peer;

	if (_peerSocket < 0) {
		return nil;		/* handed over already, or there was never a pair */
	}
	peer = [[NSSocketPort alloc] initWithProtocolFamily:AF_UNIX
						 socketType:SOCK_STREAM
						   protocol:0
						     socket:_peerSocket];
	if (peer == nil) {
		return nil;
	}
	/* THE END IS HANDED OVER WITH ITS OWNERSHIP, so exactly one object closes it and a peer that outlives this
	 * port keeps working. */
	[peer fnTakeOwnership];
	_peerSocket = -1;
	return [peer autorelease];
}

- (void)dealloc
{
	if (_peerSocket >= 0) {
		close(_peerSocket);		/* an end nobody asked for is this port's to close */
		_peerSocket = -1;
	}
	[super dealloc];
}

@end
