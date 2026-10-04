/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSocketPort — A PORT THAT IS A BSD SOCKET. docs/design/foundation-plan.md W6b and §43.
 *
 * THE ONLY LIVE CONCRETE PORT, and that is why `+[NSPort port]` answers one. Its siblings are struck
 * (§11.5): NSMachPort and NSMessagePort were deprecated with the Mach/message port system they exist for,
 * and NSSocketPortNameServer with the name server. What is left is a real socket with a real descriptor —
 * usable for both local and remote communication, which is what Apple's own overview says it is for — and
 * `-socket` hands the descriptor out, so the network layer can use the port as an ADDRESS with an object
 * around it.
 *
 * WHAT EACH INITIALIZER ACTUALLY DOES, because "as a local TCP/IP socket of type SOCK_STREAM" is a shape
 * and not an implementation:
 *
 *   -init                                    socket + bind to `INADDR_ANY` ON AN EPHEMERAL PORT + listen,
 *                                            so `-address` answers a port a peer can reach;
 *   -initWithTCPPort:                        the same with the port asked for, and SO_REUSEADDR so a
 *                                            restart is not a failure;
 *   -initWithProtocolFamily:...:address:     socket + bind to the GIVEN address;
 *   -initWithRemoteWithTCPPort:host:         socket + CONNECT to the named host and port;
 *   -initWithRemoteWithProtocolFamily:...:   socket + CONNECT to the given address;
 *   -initWithProtocolFamily:...:socket:      WRAPS a descriptor the CALLER made, and does not own it.
 *
 * THE OWNERSHIP SPLIT IS DELIBERATE AND IT MIRRORS NSFileHandle: a port that CREATED its descriptor closes
 * it on `-invalidate`, and a port handed one does not — the caller that made it is the caller that closes
 * it. Pinned by the probe, because "who closes this" is the question every descriptor wrapper has to
 * answer out loud.
 *
 * `-address` IS THE ADDRESS OF THE END THIS PORT IS, taken from `getsockname(2)` for a local port and from
 * `getpeername(2)` for a remote one, WRAPPED AS THE RAW `struct sockaddr` IN AN NSData — Apple's shape,
 * and the reason `-protocolFamily` and `-socketType` sit beside it: the bytes alone do not say how to read
 * them.
 *
 * TWO MEASURED KERNEL FACTS A CONSUMER OF THIS CLASS NEEDS, both found by the probe's first run (§43):
 *
 *   * `-init` PICKS ITS OWN EPHEMERAL PORT, because `bind(2)` to port 0 on this system returns 0 and the
 *     `listen(2)` that follows returns -1 — so "bind an ephemeral port and listen" is not expressible
 *     here. A port is chosen out of the IANA ephemeral range and the bind is retried upward from there.
 *   * A LISTENING PORT CANNOT BE A RUN-LOOP SOURCE HERE: `select(2)` does not report a listening
 *     descriptor as readable while a peer waits on it, even though `accept(2)` returns that connection.
 *     A CONNECTED one is a source like any other — which is what the probe's scheduling checks use — so
 *     a server on this system has to notice connections by asking `accept(2)` rather than by waiting.
 *
 * THE HOST NAME IS RESOLVED WITH `getaddrinfo(3)`, which answers a numeric address with no lookup at all —
 * so `initWithRemoteWithTCPPort:host:` on "127.0.0.1" touches no network, which is what makes the probe's
 * connect check runnable in a guest with no NIC.
 */

#ifndef FOUNDATION_NSSOCKETPORT_H
#define FOUNDATION_NSSOCKETPORT_H

#import <Foundation/NSPort.h>

@class NSData;
@class NSDate;
@class NSPort;
@class NSString;
@class NSMutableData;

NS_ASSUME_NONNULL_BEGIN

@interface NSSocketPort : NSPort
{
	NSSocketNativeHandle _socket;	/* the descriptor, or -1 */
	int _family;			/* AF_INET / AF_INET6 / AF_UNIX … */
	int _type;			/* SOCK_STREAM / SOCK_DGRAM */
	int _protocol;			/* the protocol argument of socket(2) */
	BOOL _ownsSocket;		/* NO for a descriptor the caller made */
	NSData *_address;		/* the raw struct sockaddr of this end */
	NSMutableData *_incoming;	/* a message being received, possibly in pieces (see the .m) */
}

/* A local TCP/IP socket of type SOCK_STREAM: bound on an ephemeral port and listening. */
- (instancetype)init;

/* The same, bound to `port` rather than to an ephemeral one. NULL when the bind fails — the port is in
 * use, or the caller may not — which is Apple's `init?` shape and the only honest answer. */
- (nullable instancetype)initWithTCPPort:(unsigned short)port;

/* "A local socket with the provided arguments": socket(2), then bind(2) to `address`. */
- (nullable instancetype)initWithProtocolFamily:(int)family
				    socketType:(int)type
				      protocol:(int)protocol
				       address:(NSData *)address;

/* "A previously created local socket": no socket(2), no bind(2), no OWNERSHIP. */
- (nullable instancetype)initWithProtocolFamily:(int)family
				    socketType:(int)type
				      protocol:(int)protocol
					socket:(NSSocketNativeHandle)socket;

/* A TCP/IP socket that CONNECTS to a remote host. `hostName` may be a name or a numeric address and may
 * be nil, which means the loopback address. */
- (nullable instancetype)initWithRemoteWithTCPPort:(unsigned short)port
					      host:(nullable NSString *)hostName;

/* "A remote socket with the provided arguments": socket(2), then connect(2) to `address`. */
- (nullable instancetype)initWithRemoteWithProtocolFamily:(int)family
					       socketType:(int)type
						 protocol:(int)protocol
						  address:(NSData *)address;

/* THE MESSAGE TRANSPORT (§62.53), AND THE DECLARATION IS NOT REPEATED HERE. What this class overrides is the
 * INTERNAL form - `-sendBeforeDate:msgid:components:from:reserved:` - and NSPort's public door already routes
 * into it with a message id of 0 (see NSPort.m), so the public method is INHERITED and a redeclaration said
 * something the compiler then reported as an implementation that does not exist. The delegate in `-setDelegate:`
 * is handed every complete message this port receives, which is what makes a scheduled socket port a message
 * endpoint rather than only a readable descriptor; the frame itself is the .m's business and is stated there. */

/* The raw struct sockaddr of this end, as an NSData. */
- (NSData *)address;

- (int)protocol;
- (int)protocolFamily;
- (NSSocketNativeHandle)socket;
- (int)socketType;


/* §63.217: THE REMOTE PAIR — the MIRROR of the local initialisers, socket(2) then CONNECT(2) to the address
 * instead of bind(2). The header already described the address form in those words. */
- (nullable instancetype)initRemoteWithProtocolFamily:(int)family
					   socketType:(int)type
					     protocol:(int)protocol
					      address:(NSData *)address;
- (nullable instancetype)initRemoteWithTCPPort:(unsigned short)port
					 host:(nullable NSString *)hostName;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSOCKETPORT_H */
