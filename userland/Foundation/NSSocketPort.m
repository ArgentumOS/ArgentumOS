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
#import <Foundation/NSPortMessage.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSString.h>
#include <arpa/inet.h>		/* htonl/ntohl: the frame is written in NETWORK byte order */
#include <errno.h>
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
- (void)fnTakeOwnership;
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
	/* `shouldListen`, NOT `listen`: the unqualified name in this scope is the LIBC FUNCTION, whose address is
	 * never null, so this condition was always true and the ephemeral-port path was taken even when the caller
	 * had NOT asked to listen. The line below used the parameter correctly; this one did not. */
	if (shouldListen && storage.ss_family == AF_INET &&
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

- (void)fnTakeOwnership
{
	/* THIS PORT BECOMES RESPONSIBLE FOR ITS DESCRIPTOR. A descriptor the CALLER made is the caller's to close,
	 * which is the right rule until the caller HANDS IT OVER — and that hand is the only reason this door
	 * exists. The machinery is the same one -fnCloseSocket uses; what changes is who owns the socket. */
	if (_socket >= 0) {
		_ownsSocket = YES;
	}
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

/* ===================================================================================================
 * THE MESSAGE TRANSPORT (§62.53)
 *
 * THE FRAME IS OURS (§11.6.1 D2) BECAUSE APPLE'S IS NOT PUBLISHED, and it is deliberately the simplest shape
 * that can carry what a message is: an eight-byte header in NETWORK BYTE ORDER — the message id and the
 * payload length — and then the payload, which is a count followed by each component's length and bytes.
 * Network order rather than the host's, because a message that works between two processes on this machine
 * should not stop working between two machines.
 *
 * PARTIAL ARRIVALS ARE THE NORMAL CASE, NOT AN ERROR: a socket read may deliver half a frame, so what has
 * arrived is kept in `_incoming` and a frame is delivered only when all of it is there. ONE READ PER
 * READINESS REPORT is the rule — the run loop reports readability again while data remains — because a
 * second read on a socket with nothing left would BLOCK inside a run-loop callback, which is how a loop
 * stops responding.
 *
 * A FRAME TOO LARGE TO BELIEVE IS REFUSED BY INVALIDATING THE PORT rather than by allocating for it: the
 * payload length comes off the wire, and a length that pretends to be a gigabyte must not become one.
 * =================================================================================================== */

#define FN_PORT_HEADER_BYTES	8u
#define FN_PORT_PAYLOAD_LIMIT	(4u * 1024u * 1024u)

/* THE FRAME, OR NIL WHEN A COMPONENT CANNOT BE WRITTEN DOWN. A component that is not data — Apple's other
 * documented kind is a port, which means a port RIGHT — is refused at this door rather than silently dropped. */
static NSData *fn_port_frame(uint32_t msgid, NSArray *components)
{
	NSMutableData *payload = [[NSMutableData alloc] init];
	NSMutableData *frame = [[NSMutableData alloc] init];
	uint32_t header[2];
	uint32_t encoded;
	NSUInteger i;

	encoded = htonl((uint32_t)[components count]);
	[payload appendBytes:&encoded length:sizeof(encoded)];
	for (i = 0; i < [components count]; i++) {
		id one = [components objectAtIndex:i];

		if (![one isKindOfClass:[NSData class]]) {
			[payload release];
			[frame release];
			return nil;
		}
		encoded = htonl((uint32_t)[one length]);
		[payload appendBytes:&encoded length:sizeof(encoded)];
		[payload appendData:one];
	}
	header[0] = htonl(msgid);
	header[1] = htonl((uint32_t)[payload length]);
	[frame appendBytes:header length:sizeof(header)];
	[frame appendData:payload];
	[payload release];
	return [frame autorelease];
}

/* ---- the message transport ---------------------------------------------- */

- (BOOL)fnWriteFrame:(NSData *)frame
{
	const unsigned char *bytes = (const unsigned char *)[frame bytes];
	NSUInteger length = [frame length];
	NSUInteger written = 0;

	while (written < length) {
		ssize_t n = write(_socket, bytes + written, length - written);

		if (n > 0) {
			written += (NSUInteger)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		return NO;		/* EPIPE, EAGAIN, EBADF: the message did not go */
	}
	return YES;
}

- (BOOL)sendBeforeDate:(NSDate *)date
		 msgid:(NSUInteger)msgid
	    components:(NSMutableArray *)components
		  from:(NSPort *)receivePort
	      reserved:(NSUInteger)headerSpaceReserved
{
	NSData *frame;
	BOOL sent;

	(void)date;		/* a socket write here is blocking and short: there is no deadline to honour */
	(void)receivePort;	/* the return address is the RECEIVER's business in this transport */
	(void)headerSpaceReserved;
	if (_socket < 0 || ![self isValid]) {
		return NO;
	}
	frame = fn_port_frame((uint32_t)msgid, components);
	if (frame == nil) {
		return NO;
	}
	sent = [self fnWriteFrame:frame];
	if (!sent && errno == EPIPE) {
		/* THE PEER IS GONE, AND A PORT WHOSE PEER IS GONE IS NOT A PORT: Apple's NSConnection notices this
		 * too, and the honest report is an invalid port rather than a silent NO on every future send. */
		[self invalidate];
	}
	return sent;
}

/* DELIVER EVERY COMPLETE FRAME IN THE BUFFER, in arrival order, and keep any partial one. */
- (void)fnDeliverMessages
{
	while ([_incoming length] >= FN_PORT_HEADER_BYTES) {
		const unsigned char *bytes = (const unsigned char *)[_incoming bytes];
		NSUInteger available = [_incoming length];
		NSUInteger offset = FN_PORT_HEADER_BYTES;
		NSUInteger consumed;
		uint32_t header[2];
		uint32_t msgid;
		uint32_t payloadLength;
		uint32_t count;
		uint32_t i;
		NSMutableArray *components;
		NSPortMessage *message;

		memcpy(header, bytes, sizeof(header));
		msgid = ntohl(header[0]);
		payloadLength = ntohl(header[1]);
		if (payloadLength > FN_PORT_PAYLOAD_LIMIT) {
			[self invalidate];
			return;
		}
		if (available < FN_PORT_HEADER_BYTES + (NSUInteger)payloadLength) {
			return;		/* the rest is still on its way */
		}
		consumed = FN_PORT_HEADER_BYTES + (NSUInteger)payloadLength;
		components = [[NSMutableArray alloc] init];
		if (payloadLength < sizeof(uint32_t)) {
			/* A PAYLOAD THAT CANNOT HOLD ITS OWN COUNT is not a frame this library wrote; it is dropped
			 * whole rather than parsed optimistically. */
			[self fnDropPrefix:consumed];
			[components release];
			continue;
		}
		memcpy(&count, bytes + offset, sizeof(count));
		count = ntohl(count);
		offset += sizeof(count);
		for (i = 0; i < count; i++) {
			uint32_t length;

			if (offset + sizeof(length) > consumed) {
				break;
			}
			memcpy(&length, bytes + offset, sizeof(length));
			length = ntohl(length);
			offset += sizeof(length);
			if (offset + (NSUInteger)length > consumed) {
				break;
			}
			[components addObject:[NSData dataWithBytes:bytes + offset length:(NSUInteger)length]];
			offset += (NSUInteger)length;
		}
		message = [[NSPortMessage alloc] initWithSendPort:self receivePort:self components:components];
		[message setMsgid:msgid];
		if (_delegate != nil && [_delegate respondsToSelector:@selector(handlePortMessage:)]) {
			[_delegate handlePortMessage:message];
		}
		[message release];
		[components release];
		[self fnDropPrefix:consumed];
		[self fnDropInvalidated];	/* the delegate may have invalidated this port while handling it */
		if (![self isValid]) {
			return;
		}
	}
}

- (void)fnDropPrefix:(NSUInteger)consumed
{
	NSUInteger remaining = [_incoming length] - consumed;
	NSMutableData *rest;

	if (remaining == 0) {
		[_incoming setLength:0];
		return;
	}
	/* SLICED INTO A NEW BUFFER rather than compacted in place, so this uses only the methods this library's
	 * NSMutableData is known to have: a transport that hung on an unimplemented slicing door would be a
	 * mystery, and a copy of at most one frame is not what makes this slow. */
	rest = [[NSMutableData alloc] initWithBytes:(const unsigned char *)[_incoming bytes] + consumed
					    length:remaining];
	[_incoming release];
	_incoming = rest;
}

- (void)fnDropInvalidated
{
	if (![self isValid] && _incoming != nil) {
		[_incoming release];
		_incoming = nil;
	}
}

- (void)portDidBecomeReadable
{
	unsigned char chunk[4096];
	ssize_t got;

	if (_socket < 0 || ![self isValid]) {
		return;
	}
	got = read(_socket, chunk, sizeof(chunk));
	if (got <= 0) {
		/* ZERO IS A CLOSED PEER and a negative is an error: neither is a message, so the port reports
		 * nothing. Apple's ports deliver "the port died" through the connection above them, and that is
		 * where this belongs rather than in a message nobody sent. */
		return;
	}
	if (_incoming == nil) {
		_incoming = [[NSMutableData alloc] init];
	}
	[_incoming appendBytes:chunk length:(NSUInteger)got];
	[self fnDeliverMessages];
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
	[_incoming release];		/* a half-received frame dies with the port that was receiving it */
	[super dealloc];
}


- (instancetype)initRemoteWithProtocolFamily:(int)family
				  socketType:(int)type
				    protocol:(int)protocol
				     address:(NSData *)address
{
	/* THE MIRROR OF THE LOCAL INITIALISER: socket(2), then CONNECT(2) to the address rather than bind(2), which
	 * is what the header's own description of this door says. */
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (![self fnMakeSocket:family type:type protocol:protocol]) {
		[self release];
		return nil;
	}
	if (connect(_socket, (const struct sockaddr *)[address bytes], (socklen_t)[address length]) != 0) {
		[self release];
		return nil;
	}
	_address = [fn_sockaddr_data(_socket, NO) retain];
	return self;
}

- (instancetype)initRemoteWithTCPPort:(unsigned short)port host:(NSString *)hostName
{
	/* RESOLVE, THEN DELEGATE: the first getaddrinfo answer becomes the address the remote initialiser connects
	 * to, so there is one connect path and not two. */
	struct addrinfo hints;
	struct addrinfo *answer = NULL;
	char service[16];
	NSData *address;
	int family, type, protocol;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(service, sizeof(service), "%u", (unsigned)port);
	if (getaddrinfo([hostName UTF8String], service, &hints, &answer) != 0 || answer == NULL) {
		return nil;
	}
	address = [NSData dataWithBytes:answer->ai_addr length:(NSUInteger)answer->ai_addrlen];
	family = answer->ai_family;
	type = answer->ai_socktype;
	protocol = answer->ai_protocol;
	freeaddrinfo(answer);
	return [self initRemoteWithProtocolFamily:family socketType:type protocol:protocol address:address];
}
@end
