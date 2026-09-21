/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSocketPort.m — the port that is a BSD socket. The design is in NSSocketPort.h; what is HERE is the
 * three things a socket wrapper can get wrong and this one states out loud.
 *
 * ONE: WHO CLOSES THE DESCRIPTOR. A port that CREATED it closes it (`_ownsSocket`), and a port handed one
 * does not. That is NSFileHandle's rule for `-initWithFileDescriptor:` applied to a socket, and the probe
 * pins both halves — the descriptor case is the one where a wrong answer is a double close in somebody
 * else's code.
 *
 * TWO: A FAILED INITIALIZER CLOSES WHAT IT MADE AND DOES NOT LEAK ITSELF. Under MRC that is `[self release]`
 * on every failure path, and the socket is closed before it, because an initializer that returns nil has
 * promised the caller nothing.
 *
 * THREE: THE REMOTE INITIALIZERS WALK `getaddrinfo(3)`'s ANSWER LIST. A host can resolve to several
 * addresses of two families, and the first socket that CONNECTS is the one kept; every earlier attempt is
 * closed. `getaddrinfo` on a numeric address does no lookup at all, which is what lets the probe's connect
 * check run in a guest with no network.
 */

#import <Foundation/NSSocketPort.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>		/* snprintf: the service name */
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* THE RAW struct sockaddr OF ONE END. Apple's `-address` is a Data holding exactly this, and the reason
 * `-protocolFamily`/`-socketType` sit beside it: the bytes do not say how to read themselves. */
static NSData *fn_sockaddr_data(NSSocketNativeHandle fd, BOOL peer)
{
	struct sockaddr_storage storage;
	socklen_t length = (socklen_t)sizeof(storage);
	int result;

	memset(&storage, 0, sizeof(storage));
	result = peer ? getpeername(fd, (struct sockaddr *)&storage, &length)
		      : getsockname(fd, (struct sockaddr *)&storage, &length);
	if (result != 0 || length == 0) {
		return nil;
	}
	return [NSData dataWithBytes:&storage length:(NSUInteger)length];
}

@interface NSSocketPort (FNPrivate)
- (BOOL)fnMakeSocket:(int)family type:(int)type protocol:(int)protocol;
- (BOOL)fnBind:(NSData *)address listen:(BOOL)shouldListen;
- (BOOL)fnBindEphemeral:(struct sockaddr_in *)local;
- (BOOL)fnConnect:(NSData *)address;
- (void)fnCloseSocket;
@end

@implementation NSSocketPort

/* ---- the four things the initializers are made of ------------------------ */

- (BOOL)fnMakeSocket:(int)family type:(int)type protocol:(int)protocol
{
	int fd = socket(family, type, protocol);

	if (fd < 0) {
		return NO;
	}
	[self fnCloseSocket];		/* a retry after a failed connect must not leak the attempt */
	_socket = fd;
	_family = family;
	_type = type;
	_protocol = protocol;
	_ownsSocket = YES;
	return YES;
}

- (BOOL)fnBind:(NSData *)address listen:(BOOL)shouldListen
{
	struct sockaddr_storage storage;
	socklen_t length = (socklen_t)[address length];
	int reuse = 1;

	if (length == 0 || length > (socklen_t)sizeof(storage) || _socket < 0) {
		return NO;
	}
	memset(&storage, 0, sizeof(storage));
	memcpy(&storage, [address bytes], (size_t)length);
	/* SO_REUSEADDR, because a server that is restarted is not a failure — and because the probe would
	 * otherwise be able to bind its port exactly once per guest boot. */
	(void)setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse));
	if (listen && storage.ss_family == AF_INET &&
	    ((struct sockaddr_in *)&storage)->sin_port == 0) {
		/* PORT 0 PLUS A LISTEN IS THE CASE THIS KERNEL CANNOT DO: see -fnBindEphemeral:. */
		return [self fnBindEphemeral:(struct sockaddr_in *)&storage];
	}
	if (bind(_socket, (struct sockaddr *)&storage, length) != 0) {
		return NO;
	}
	if (shouldListen && _type == SOCK_STREAM && listen(_socket, 16) != 0) {
		return NO;
	}
	return YES;
}

/* THE EPHEMERAL PORT IS CHOSEN HERE, AND THIS IS A MEASURED KERNEL FACT RATHER THAN A PREFERENCE (§43):
 * `bind(2)` to port 0 on this system RETURNS 0 and leaves the port unresolved, and the `listen(2)` that
 * follows RETURNS -1 — so "bind an ephemeral port and listen" is not expressible, and a server socket
 * needs a port picked before the bind. The RANGE is the rule: the IANA ephemeral range (49152-65535),
 * entered at a place that depends on the pid so two processes do not race for the same first candidate,
 * and walked upward for at most 256 tries. A table of ports would collide by definition. */
- (BOOL)fnBindEphemeral:(struct sockaddr_in *)local
{
	unsigned base = 49152u + ((unsigned)getpid() % 1024u);
	unsigned attempt;

	for (attempt = 0; attempt < 256u; attempt++) {
		unsigned candidate = 49152u + ((base - 49152u + attempt) % 16384u);

		local->sin_port = htons((unsigned short)candidate);
		if (bind(_socket, (struct sockaddr *)local, (socklen_t)sizeof(*local)) == 0) {
			return _type != SOCK_STREAM || listen(_socket, 16) == 0;
		}
	}
	return NO;
}

- (BOOL)fnConnect:(NSData *)address
{
	socklen_t length = (socklen_t)[address length];

	if (length == 0 || _socket < 0) {
		return NO;
	}
	return connect(_socket, (const struct sockaddr *)[address bytes], length) == 0;
}

/* CLOSES WHAT IT OWNS, AND ONLY THEN FORGETS IT. THE DISTINCTION IS THE BUG THE PROBE FOUND on the first
 * run: with `_socket = -1` unconditional, `-invalidate` made a port built around the CALLER's descriptor
 * report -1 — the port had forgotten a descriptor that is still open and still the caller's. A port lets
 * go of a descriptor exactly when it closes one. */
- (void)fnCloseSocket
{
	if (_socket >= 0 && _ownsSocket) {
		close(_socket);
		_socket = -1;
	}
}

/* ---- creating instances -------------------------------------------------- */

- (instancetype)init
{
	struct sockaddr_in local;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (![self fnMakeSocket:AF_INET type:SOCK_STREAM protocol:0]) {
		[self release];
		return nil;
	}
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = 0;			/* EPHEMERAL: the kernel picks, and -address reports it */
	if (![self fnBind:[NSData dataWithBytes:&local length:sizeof(local)] listen:YES]) {
		[self release];
		return nil;
	}
	_address = [fn_sockaddr_data(_socket, NO) retain];
	return self;
}

- (instancetype)initWithTCPPort:(unsigned short)port
{
	struct sockaddr_in local;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (![self fnMakeSocket:AF_INET type:SOCK_STREAM protocol:0]) {
		[self release];
		return nil;
	}
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(port);
	if (![self fnBind:[NSData dataWithBytes:&local length:sizeof(local)] listen:YES]) {
		[self release];
		return nil;
	}
	_address = [fn_sockaddr_data(_socket, NO) retain];
	return self;
}

- (instancetype)initWithProtocolFamily:(int)family
			    socketType:(int)type
			      protocol:(int)protocol
			       address:(NSData *)address
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (![self fnMakeSocket:family type:type protocol:protocol] ||
	    ![self fnBind:address listen:(type == SOCK_STREAM)]) {
		[self release];
		return nil;
	}
	_address = [fn_sockaddr_data(_socket, NO) retain];
	return self;
}

- (instancetype)initWithProtocolFamily:(int)family
			    socketType:(int)type
			      protocol:(int)protocol
				socket:(NSSocketNativeHandle)socket
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (socket < 0) {
		[self release];
		return nil;
	}
	_socket = socket;
	_family = family;
	_type = type;
	_protocol = protocol;
	_ownsSocket = NO;		/* THE CALLER MADE IT, SO THE CALLER CLOSES IT */
	_address = [fn_sockaddr_data(_socket, NO) retain];
	return self;
}

- (instancetype)initWithRemoteWithTCPPort:(unsigned short)port host:(NSString *)hostName
{
	struct addrinfo hints, *answer = NULL, *candidate;
	char portText[8];
	BOOL connected = NO;
	const char *host = (hostName != nil ? [hostName UTF8String] : "127.0.0.1");
	int resolved;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		/* a name may answer both, and the first that CONNECTS wins */
	hints.ai_socktype = SOCK_STREAM;
	snprintf(portText, sizeof(portText), "%u", (unsigned)port);
	resolved = getaddrinfo(host, portText, &hints, &answer);
	if (resolved != 0 || answer == NULL) {
		[self release];
		return nil;
	}
	for (candidate = answer; candidate != NULL; candidate = candidate->ai_next) {
		if ([self fnMakeSocket:candidate->ai_family
				  type:candidate->ai_socktype
			      protocol:candidate->ai_protocol] &&
		    [self fnConnect:[NSData dataWithBytes:candidate->ai_addr
						    length:candidate->ai_addrlen]]) {
			/* THE ADDRESS THE INITIALIZER CONNECTED TO, kept because that is what `-address` means for
			 * a remote port — and because asking the kernel instead is not the same question (see §43). */
			[_address release];
			_address = [[NSData alloc] initWithBytes:candidate->ai_addr
							  length:candidate->ai_addrlen];
			connected = YES;
			break;
		}
	}
	freeaddrinfo(answer);
	if (!connected) {
		[self fnCloseSocket];
		[self release];
		return nil;
	}
	return self;
}

- (instancetype)initWithRemoteWithProtocolFamily:(int)family
				       socketType:(int)type
					 protocol:(int)protocol
					  address:(NSData *)address
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (![self fnMakeSocket:family type:type protocol:protocol] || ![self fnConnect:address]) {
		[self fnCloseSocket];
		[self release];
		return nil;
	}
	[_address release];
	_address = [address copy];		/* the peer this port was built to talk to */
	return self;
}

/* ---- getting information ------------------------------------------------- */

- (NSData *)address
{
	return _address != nil ? _address : [NSData data];
}

- (int)protocol
{
	return _protocol;
}

- (int)protocolFamily
{
	return _family;
}

- (NSSocketNativeHandle)socket
{
	return _socket;
}

- (int)socketType
{
	return _type;
}

/* ---- being a run-loop source, and being taken apart ---------------------- */

- (void)scheduleInRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode
{
	[super scheduleInRunLoop:runLoop forMode:mode];
	if (_socket < 0) {
		return;
	}
	/* THE SEAM (W6a). One source per port, replaced rather than accumulated, so re-scheduling in the same
	 * mode cannot register the descriptor twice. THE LOOP HOLDS THE PORT WEAKLY — Apple's rule that a port
	 * must be invalidated before it is released is this rule seen from the caller's side. */
	[runLoop removeSourceForTarget:self];
	[runLoop addSourceForFileDescriptor:_socket
				       mode:mode
				   readable:YES
				     target:self
				   selector:@selector(portDidBecomeReadable)];
}

- (void)removeFromRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode
{
	[runLoop removeSourceForTarget:self];
	[super removeFromRunLoop:runLoop forMode:mode];
}

- (void)invalidate
{
	if (![self isValid]) {
		return;
	}
	/* UNREGISTER FIRST, THEN CLOSE. The run loop may be inside select(2) on this descriptor, and closing
	 * it under a pending watch is how the loop is handed EBADF. */
	[super invalidate];
	[self fnCloseSocket];
}

- (void)dealloc
{
	[self fnCloseSocket];
	[_address release];
	[super dealloc];
}

@end
