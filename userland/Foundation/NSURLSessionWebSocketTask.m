/*
 * NSURLSessionWebSocketTask.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TASK IS THREE THINGS WIRED TOGETHER, each of them already a tested layer: a NSURLSessionStreamTask that owns
 * the socket (§58, TLS included), FNWebSocketHandshake for the opening exchange (§59 slice 3a, tested against the
 * RFC's own worked example), and FNWebSocketAssembler for everything after it (§59 slices 2 and 2b). NOTHING HERE
 * PARSES A FRAME OR COMPUTES A DIGEST - it decides WHEN, and in what order.
 *
 * THE ORDER IS THE WHOLE DESIGN: resume opens a stream to the URL's host and port, asks for the upgrade, and -
 * only when the reply PROVES it is one - tells the delegate it is open. Then ONE READ PUMP runs for the life of
 * the connection: take what has arrived, parse as many whole frames out of it as there are, feed each to the
 * assembler, and act on what comes back - a message goes to the FIRST WAITING receive handler, a control frame is
 * answered AT ONCE (§5.4), and a close ends the task. Writes are the same framing in the other direction, masked,
 * because a client masks (§5.3).
 *
 * AND THREE THINGS THIS FILE LEARNED BY BEING READ RATHER THAN RECALLED, all of them now enforced above:
 *   * the state is `_wsState` and NOT `_state` - the base NSURLSessionTask already declares `_state`, and a
 *     subclass redeclaring it would be two states with one name;
 *   * `-cancel` sends a close frame with NO code and NO reason, because Apple says so in as many words ("if you
 *     call `cancel()` on the task instead of this method, it sends a cancellation frame with no close code or
 *     reason") - `-cancelWithCloseCode:reason:` is the door that carries them;
 *   * WRITES BEFORE THE HANDSHAKE ARE ENQUEUED, not refused: the same page says "reads/writes performed before
 *     the handshake completes are enqueued and executed afterward", so `-sendMessage:` and
 *     `-sendPingWithPongReceiveHandler:` queue their frames and their handlers.
 *
 * MRC, like every file in this library.
 */
#import <Foundation/NSURLSessionWebSocketTask.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLError.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLSessionStreamTask.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDictionary.h>	/* the error's userInfo is a DICTIONARY LITERAL, which needs the class */
#import <Foundation/FNWebSocketFraming.h>
#import <Foundation/FNWebSocketAssembler.h>
#import <Foundation/FNWebSocketHandshake.h>
#include <Block.h>		/* Block_copy/Block_release: MRC spells the copy a queued block needs */
#include <openssl/rand.h>
#include <string.h>

enum {
	FN_WS_IDLE = 0,		/* created: nothing has happened */
	FN_WS_UPGRADING = 1,	/* the request is written, the reply is not read */
	FN_WS_OPEN = 2,		/* the delegate has been told, the pump is running */
	FN_WS_CLOSING = 3,	/* a close frame went out or came in: draining */
	FN_WS_ENDED = 4		/* the ending has been delivered, once */
};

@interface NSURLSessionWebSocketTask ()
{
	NSString *_host;
	NSInteger _port;
	NSString *_path;
	NSArray *_protocols;
	NSString *_key;
	NSString *_negotiated;
	NSURLSessionStreamTask *_stream;
	FNWebSocketAssembler *_assembler;
	NSMutableData *_arrived;		/* bytes read and not yet parsed into frames */
	NSMutableArray *_receives;		/* waiting -receiveMessageWithCompletionHandler: blocks, in order */
	NSMutableArray *_pongs;			/* waiting -sendPingWithPongReceiveHandler: blocks, in order */
	NSMutableArray *_queued;		/* frames asked for BEFORE the handshake finished (§59: enqueued) */
	NSInteger _wsState;
	NSInteger _maximumMessageSize;
	NSInteger _closeCode;
	NSData *_closeReason;
	BOOL _weSentClose;
	BOOL _secure;			/* wss: - the upgrade must go over TLS, and this is what remembers the scheme */
}

- (instancetype)fnInitWithURL:(NSURL *)url protocols:(NSArray *)protocols identifier:(NSUInteger)identifier;
- (void)fnStartHandshake;
- (void)fnReadReply;
- (void)fnPump;
- (void)fnFlushQueued;
- (void)fnHandleMessage:(NSData *)payload ofKind:(FNWebSocketOpcode)opcode;
- (void)fnHandleControl:(NSData *)payload ofKind:(FNWebSocketOpcode)opcode;
- (void)fnSendFrame:(BOOL)fin opcode:(FNWebSocketOpcode)opcode payload:(const uint8_t *)payload length:(size_t)length;
- (void)fnWriteFrame:(NSData *)frame;
- (void)fnFlushQueued;
- (void)fnFlushWaitersWithError:(NSError *)error;
- (void)fnProtocolError:(NSString *)reason;
- (void)fnHopToDelegateQueue:(void (^)(void))block;
- (void)fnEndWithError:(NSError *)error;
- (NSError *)fnErrorWithCode:(NSInteger)code;
- (void)fnFail:(NSString *)reason closeCode:(NSInteger)closeCode;
@end

@implementation NSURLSessionWebSocketTask

- (instancetype)fnInitWithURL:(NSURL *)url protocols:(NSArray *)protocols identifier:(NSUInteger)identifier
{
	self = [super init];
	if(self == nil) {
		return nil;
	}
	_host = [[url host] copy];
	_secure = ([[url scheme] caseInsensitiveCompare:@"wss"] == NSOrderedSame);
	_port = [[url port] integerValue];
	if(_port == 0) {
		_port = _secure ? 443 : 80;
	}
	{
		NSString *path = [url path];

		/* THE PATH IS NEVER EMPTY, and the QUERY TRAVELS WITH IT: "GET  HTTP/1.1" is not a request, and a query
		 * dropped on the way to the upgrade would change what the server does. */
		if([path length] == 0) {
			path = @"/";
		}
		if([url query] != nil) {
			_path = [[NSString alloc] initWithFormat:@"%@?%@", path, [url query]];
		} else {
			_path = [path copy];
		}
	}
	_protocols = [protocols copy];
	_taskIdentifier = identifier;
	_priority = 0.5f;
	_wsState = FN_WS_IDLE;
	_state = NSURLSessionTaskStateSuspended;	/* THE BASE'S OWN IVAR: a new task is suspended */
	_maximumMessageSize = 1024 * 1024;		/* ours (D2): Apple documents the property, not its default */
	_closeCode = NSURLSessionWebSocketCloseCodeInvalid;
	_assembler = [[FNWebSocketAssembler alloc] init];
	[_assembler setMaximumMessageSize:(NSUInteger)_maximumMessageSize];
	_arrived = [[NSMutableData alloc] init];
	_receives = [[NSMutableArray alloc] init];
	_pongs = [[NSMutableArray alloc] init];
	_queued = [[NSMutableArray alloc] init];
	return self;
}

- (void)dealloc
{
	[_host release];
	[_path release];
	[_protocols release];
	[_key release];
	[_negotiated release];
	[_stream release];
	[_assembler release];
	[_arrived release];
	[_receives release];
	[_pongs release];
	[_queued release];
	[_closeReason release];
	[super dealloc];
}

- (NSInteger)maximumMessageSize
{
	return _maximumMessageSize;
}

- (void)setMaximumMessageSize:(NSInteger)bytes
{
	_maximumMessageSize = bytes;
	[_assembler setMaximumMessageSize:(NSUInteger)(bytes > 0 ? bytes : 0)];
}

- (NSURLSessionWebSocketCloseCode)closeCode
{
	return (NSURLSessionWebSocketCloseCode)_closeCode;
}

- (NSData *)closeReason
{
	return _closeReason;
}

/* --- THE STATE MACHINE ----------------------------------------------------------------------------- */

- (void)resume
{
	if(_state != NSURLSessionTaskStateSuspended || _wsState != FN_WS_IDLE) {
		return;
	}
	_state = NSURLSessionTaskStateRunning;
	_wsState = FN_WS_UPGRADING;
	[self fnStartHandshake];
}

/* A CANCEL SENDS A CLOSE FRAME WITH NO CODE AND NO REASON, which is Apple's own distinction: the door WITH a code
 * is -cancelWithCloseCode:reason:. And every handler still waiting is answered with NSURLErrorCancelled, because
 * a cancelled task must not leave a caller's block hanging. */
- (void)cancel
{
	if(_wsState == FN_WS_OPEN || _wsState == FN_WS_CLOSING) {
		[self fnSendFrame:YES opcode:FNWebSocketOpcodeClose payload:NULL length:0];
		_weSentClose = YES;
	}
	[self fnFlushWaitersWithError:[self fnErrorWithCode:NSURLErrorCancelled]];
	[super cancel];		/* the base's rule: Completed, with NSURLErrorCancelled */
}

- (void)suspend
{
	/* SAID RATHER THAN HALF-DONE, as the stream task says it: suspending a live connection would have to stop the
	 * reads the pump is already serving, and the substrate documents no such pause. The state moves; the
	 * connection is unaffected, which is a boundary of this row. */
	[super suspend];
}

- (void)fnStartHandshake
{
	NSURLSession *session = (NSURLSession *)_session;

	if(_host == nil || session == nil) {
		[self fnFail:@"the task has no host or no session" closeCode:NSURLSessionWebSocketCloseCodeInvalid];
		return;
	}
	/* THE SUBSTRATE IS A STREAM TASK (§59): the socket, its timeouts and - for a wss: URL - the TLS upgrade all
	 * come from a layer that is already verified, including the recorded deviation that certificates are not
	 * verified (there is no trust store yet). */
	_stream = [[session streamTaskWithHostName:_host port:_port] retain];
	if(_stream == nil) {
		[self fnFail:@"the session refused to make a stream" closeCode:NSURLSessionWebSocketCloseCodeInvalid];
		return;
	}
	[_stream resume];
	/* AND A wss: URL SECURES THE STREAM BEFORE THE UPGRADE GOES OUT - THE LINE THIS ROW EXISTED TO ADD. Without
	 * it the request would be written IN THE CLEAR to a port where a TLS server is waiting, which is not an
	 * unverified TLS path but an unimplemented one. THE ORDER NEEDS NO WAIT: -startSecureConnection is QUEUED on
	 * the stream, and the stream serves its operations SERIALLY, so the upgrade request queued below cannot leave
	 * before the TLS session is up. (Certificates are not verified - §58.1's recorded deviation, inherited.) */
	if(_secure) {
		[_stream startSecureConnection];
	}
	_key = [FNWebSocketCreateKey() copy];	/* the layer's +1, kept */
	{
		NSString *request = FNWebSocketUpgradeRequest(_host, _port, _path, _protocols, _key);
		NSData *bytes = [request dataUsingEncoding:NSUTF8StringEncoding];

		[_stream writeData:bytes timeout:15.0 completionHandler:^(NSError *error) {
			if(error != nil) {
				[self fnFail:@"the handshake request could not be sent" closeCode:NSURLSessionWebSocketCloseCodeInvalid];
			} else {
				[self fnReadReply];
			}
		}];
	}
}

- (void)fnReadReply
{
	[_stream readDataOfMinLength:1 maxLength:8192 timeout:15.0
		   completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		NSString *protocol = nil;
		NSString *reason = nil;

		(void)atEOF;
		if(error != nil || [data length] == 0) {
			[self fnFail:@"the server never finished its handshake reply"
			   closeCode:NSURLSessionWebSocketCloseCodeInvalid];
			return;
		}
		if(!FNWebSocketResponseIsUpgrade(data, _key, &protocol, &reason)) {
			/* the harness layer already learned why nothing is a reason */
			[self fnFail:(reason != nil ? reason : @"the reply was not an upgrade")
			   closeCode:NSURLSessionWebSocketCloseCodeInvalid];
			return;
		}
		/* REFUSING A SUBPROTOCOL WE NEVER OFFERED IS NOT PEDANTRY: the server just claimed one, and a claim
		 * outside our list means the two ends disagree about what they are speaking. */
		if(protocol != nil && (_protocols == nil || ![_protocols containsObject:protocol])) {
			[self fnFail:@"the server chose a subprotocol we never offered"
			   closeCode:NSURLSessionWebSocketCloseCodeInvalid];
			return;
		}
		_negotiated = [protocol copy];
		_wsState = FN_WS_OPEN;
		[self fnFlushQueued];
		{
			NSURLSession *session = (NSURLSession *)_session;
			id<NSURLSessionWebSocketDelegate> delegate = (id<NSURLSessionWebSocketDelegate>)[session delegate];
			NSString *chosen = [_negotiated retain];

			[self fnHopToDelegateQueue:^{
				if([delegate respondsToSelector:@selector(URLSession:webSocketTask:didOpenWithProtocol:)]) {
					[delegate URLSession:session webSocketTask:self didOpenWithProtocol:chosen];
				}
				[chosen release];
			}];
		}
		[self fnPump];
	}];
}

/* THE PUMP: READ, PARSE WHAT IS WHOLE, ACT ON IT. One read at a time, re-arming itself, and the parse loop below
 * is what lets one read deliver several frames - or half of one.
 *
 * AND THE READ IS BOUNDED IN TIME, WHICH IS NOT A DETAIL BUT THE WHOLE REASON THIS TASK CAN WRITE AT ALL. The
 * substrate's worker serves its operations SERIALLY, so a read that waits forever HOLDS THE WORKER, and every
 * write queued behind it waits forever too. The first version read with an infinite timeout and the symptom was
 * exactly that: the handshake succeeded, then the message never left the process - the pump had the worker and
 * would not let go. Half a second is long enough that the pump is not busy-spinning and short enough that a write
 * waits at most that long. */
#define FN_WS_PUMP_SLICE	0.5

- (void)fnPump
{
	if(_wsState != FN_WS_OPEN && _wsState != FN_WS_CLOSING) {
		return;
	}
	[_stream readDataOfMinLength:1 maxLength:16384 timeout:FN_WS_PUMP_SLICE
		   completionHandler:^(NSData *data, BOOL atEOF, NSError *error) {
		if(error != nil) {
			/* A SLICE THAT EXPIRED IS NOT A FAILURE: it is the pump handing the worker back so the write queue
			 * can drain, and it re-arms immediately. Only a real error ends the connection. */
			if([error code] == NSURLErrorTimedOut) {
				[self fnPump];
				return;
			}
			[self fnFail:@"the connection ended without a close frame"
			   closeCode:NSURLSessionWebSocketCloseCodeAbnormalClosure];
			return;
		}
		if(atEOF || [data length] == 0) {
			[self fnFail:@"the connection ended without a close frame"
			   closeCode:NSURLSessionWebSocketCloseCodeAbnormalClosure];
			return;
		}
		[_arrived appendData:data];
		for(;;) {
			FNWebSocketFrame frame;
			/* expectMasked:NO, BECAUSE THESE ARE THE SERVER'S FRAMES. §5.1's rule has two directions and the task
			 * writes one of them: OUR frames are masked (fnSendFrame passes a key) and THE PEER'S ARE NOT. The
			 * first version passed YES here - the direction it writes in - and the peer's own echo was then read
			 * as a protocol violation, which failed the task with the 1002 path (an error whose userInfo is empty,
			 * which is how the two failure sources were told apart). */
			NSInteger consumed = FNWebSocketParseFrame([_arrived bytes], [_arrived length], NO, &frame);

			if(consumed == 0) {
				break;		/* not yet whole: keep the bytes and read more */
			}
			if(consumed < 0) {
				[self fnProtocolError:@"the peer broke the framing rules"];
				return;
			}
			{
				FNWebSocketOpcode opcode = FNWebSocketOpcodeText;
				NSData *out = nil;

				switch([_assembler feedFrame:&frame outOpcode:&opcode outData:&out]) {
				case FNWebSocketAssemblyMessage:
					[self fnHandleMessage:out ofKind:opcode];
					break;
				case FNWebSocketAssemblyControl:
					[self fnHandleControl:out ofKind:opcode];
					break;
				case FNWebSocketAssemblyError:
					[self fnProtocolError:@"the peer broke the message rules"];
					return;
				default:
					break;		/* a fragment: the assembler is holding it */
				}
			}
			/* withBytes: is annotated nonnull even when length is 0, where nothing is read: a one-byte dummy
			 * says "no bytes" without lying to the compiler about the pointer. */
			[_arrived replaceBytesInRange:NSMakeRange(0, (NSUInteger)consumed)
					    withBytes:(const void *)"" length:0];
		}
		[self fnPump];
	}];
}

/* §5.4'S ANSWER TO A BROKEN PEER IS ONE CODE - 1002, protocol error - and it is sent before the task ends, so the
 * peer is told rather than just dropped. */
- (void)fnProtocolError:(NSString *)reason
{
	[self fnSendFrame:YES opcode:FNWebSocketOpcodeClose
		  payload:(const uint8_t *)"\x03\xEA" length:2];	/* 1002, big-endian */
	_weSentClose = YES;
	_closeCode = NSURLSessionWebSocketCloseCodeProtocolError;
	[self fnFlushWaitersWithError:[self fnErrorWithCode:NSURLErrorBadServerResponse]];
	[self fnEndWithError:[self fnErrorWithCode:NSURLErrorBadServerResponse]];
}

/* A MESSAGE GOES TO THE FIRST WAITING RECEIVE HANDLER, and a receive with no message keeps waiting - which is the
 * whole of the queue's contract, and why it is a queue rather than a variable. */
- (void)fnHandleMessage:(NSData *)payload ofKind:(FNWebSocketOpcode)opcode
{
	NSURLSessionWebSocketMessage *message = nil;
	void (^handler)(NSURLSessionWebSocketMessage *, NSError *);
	NSString *text;

	if(opcode == FNWebSocketOpcodeText) {
		text = [[NSString alloc] initWithData:payload encoding:NSUTF8StringEncoding];
		message = [[NSURLSessionWebSocketMessage alloc] initWithString:text];
		[text release];
	} else {
		message = [[NSURLSessionWebSocketMessage alloc] initWithData:payload];
	}
	if([_receives count] == 0) {
		/* NOBODY IS WAITING: the message is DROPPED, and that is Apple's interface rather than an omission - one
		 * call, one message. A client that wants a stream of them re-arms inside its handler. */
		[message release];
		return;
	}
	handler = Block_copy([_receives objectAtIndex:0]);
	[_receives removeObjectAtIndex:0];
	[self fnHopToDelegateQueue:^{
		handler(message, nil);
		Block_release(handler);		/* Block_copy/Block_release, NEVER -copy/-release: a block is not an object
					 * you message - the runtime reads its ISA, and a fault there is a null page
					 * with a garbage instruction pointer (tools' fn_block_mrc.m measured it) */
	}];
	[message release];
}

- (void)fnHandleControl:(NSData *)payload ofKind:(FNWebSocketOpcode)opcode
{
	if(opcode == FNWebSocketOpcodePing) {
		[self fnSendFrame:YES opcode:FNWebSocketOpcodePong
			  payload:(const uint8_t *)[payload bytes] length:[payload length]];
		return;
	}
	if(opcode == FNWebSocketOpcodePong) {
		void (^handler)(NSError *);

		if([_pongs count] == 0) {
			return;		/* a pong nobody asked for */
		}
		handler = Block_copy([_pongs objectAtIndex:0]);
		[_pongs removeObjectAtIndex:0];
		[self fnHopToDelegateQueue:^{
			handler(nil);
			Block_release(handler);
		}];
		return;
	}
	if(opcode == FNWebSocketOpcodeClose) {
		const uint8_t *bytes = (const uint8_t *)[payload bytes];
		NSData *reason;

		/* THE PEER'S CLOSE, AND THE TWO FACTS IN IT: the code and the reason, which are exactly what the delegate
		 * door exists to carry. An empty payload means "no code", which is legal (§5.5.1). */
		if([payload length] >= 2) {
			_closeCode = (NSInteger)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
			if([payload length] > 2) {
				[_closeReason release];
				_closeReason = [[payload subdataWithRange:NSMakeRange(2, [payload length] - 2)] retain];
			}
		}
		if(!_weSentClose) {
			/* ANSWER IN KIND, WITH OUR OWN CODE: echoing a RESERVED one (1005, 1006, 1015) would be a close frame
			 * §7.4.1 forbids, and the peer sent one only to report what happened to IT. */
			uint8_t normal[2] = { 0x03, 0xE8 };	/* 1000, normal closure */

			[self fnSendFrame:YES opcode:FNWebSocketOpcodeClose payload:normal length:2];
			_weSentClose = YES;
		}
		reason = [_closeReason retain];
		{
			NSURLSession *session = (NSURLSession *)_session;
			id<NSURLSessionWebSocketDelegate> delegate = (id<NSURLSessionWebSocketDelegate>)[session delegate];
			NSInteger code = _closeCode;

			[self fnHopToDelegateQueue:^{
				if([delegate respondsToSelector:@selector(URLSession:webSocketTask:didCloseWithCode:reason:)]) {
					[delegate URLSession:session webSocketTask:self
						 didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:reason];
				}
				[reason release];
			}];
		}
		[self fnFlushWaitersWithError:[self fnErrorWithCode:NSURLErrorNetworkConnectionLost]];
		[self fnEndWithError:nil];
	}
}

/* --- THE FOUR DOORS -------------------------------------------------------------------------------- */

- (void)sendMessage:(NSURLSessionWebSocketMessage *)message
  completionHandler:(void (^)(NSError *))completionHandler
{
	NSData *payload;
	FNWebSocketOpcode opcode;
	BOOL text = ([message type] == NSURLSessionWebSocketMessageTypeString);

	if(_wsState == FN_WS_ENDED || _state == NSURLSessionTaskStateCompleted) {
		if(completionHandler != nil) {
			completionHandler([self fnErrorWithCode:NSURLErrorCannotConnectToHost]);
		}
		return;
	}
	payload = text ? [[message string] dataUsingEncoding:NSUTF8StringEncoding] : [message data];
	opcode = text ? FNWebSocketOpcodeText : FNWebSocketOpcodeBinary;
	[self fnSendFrame:YES opcode:opcode payload:(const uint8_t *)[payload bytes] length:[payload length]];
	if(completionHandler != nil) {
		completionHandler(nil);
	}
}

- (void)receiveMessageWithCompletionHandler:(void (^)(NSURLSessionWebSocketMessage *, NSError *))completionHandler
{
	void (^copied)(NSURLSessionWebSocketMessage *, NSError *);

	if(completionHandler == nil) {
		return;
	}
	/* Block_copy AND NOT -copy: the array retains what it is handed, so the copy's +1 is released here and the
	 * array holds the only reference. (See the note where the handler is invoked for why blocks are never
	 * messaged for ownership.) */
	copied = Block_copy(completionHandler);
	[_receives addObject:copied];
	Block_release(copied);
}

- (void)sendPingWithPongReceiveHandler:(void (^)(NSError *))pongReceiveHandler
{
	void (^copied)(NSError *);

	if(_wsState == FN_WS_ENDED || _state == NSURLSessionTaskStateCompleted) {
		if(pongReceiveHandler != nil) {
			pongReceiveHandler([self fnErrorWithCode:NSURLErrorCannotConnectToHost]);
		}
		return;
	}
	if(pongReceiveHandler != nil) {
		copied = Block_copy(pongReceiveHandler);
		[_pongs addObject:copied];
		Block_release(copied);
	}
	[self fnSendFrame:YES opcode:FNWebSocketOpcodePing payload:NULL length:0];
}

- (void)cancelWithCloseCode:(NSURLSessionWebSocketCloseCode)closeCode reason:(NSData *)reason
{
	uint8_t payload[127];

	if(!FNWebSocketClosePayloadIsSendable([reason bytes], 2)) {
		/* A RESERVED CODE IS REFUSED RATHER THAN SENT (§7.4.1). The layer decides what is sendable; this door's
		 * job is to ask it rather than to know. */
		[self fnFail:@"that close code may be reported and never sent"
		   closeCode:NSURLSessionWebSocketCloseCodeInvalid];
		return;
	}
	payload[0] = (uint8_t)(((uint16_t)closeCode >> 8) & 0xFF);
	payload[1] = (uint8_t)((uint16_t)closeCode & 0xFF);
	_closeCode = (NSInteger)closeCode;
	if(reason != nil) {
		NSUInteger length = [reason length];

		if(length > 123) {
			length = 123;	/* a control frame's whole payload is 125 bytes, and two of them are the code */
		}
		[reason getBytes:payload + 2 length:length];
		[_closeReason release];
		_closeReason = [reason copy];
		[self fnSendFrame:YES opcode:FNWebSocketOpcodeClose payload:payload length:2 + length];
	} else {
		[self fnSendFrame:YES opcode:FNWebSocketOpcodeClose payload:payload length:2];
	}
	_weSentClose = YES;
	if(_wsState == FN_WS_OPEN) {
		_wsState = FN_WS_CLOSING;	/* the peer's close frame answers this one, and ends the task */
	}
}

/* --- PLUMBING -------------------------------------------------------------------------------------- */

/* A FRAME IS BUILT HERE AND WRITTEN, AND IF THE HANDSHAKE HAS NOT FINISHED IT IS QUEUED INSTEAD - because that
 * is what the page says: "reads/writes performed before the handshake completes are enqueued and executed
 * afterward". The queue is drained the moment the upgrade is proven. */
- (void)fnSendFrame:(BOOL)fin opcode:(FNWebSocketOpcode)opcode payload:(const uint8_t *)payload length:(size_t)length
{
	uint8_t maskBytes[4];
	NSData *mask;
	NSData *frame;

	if(_stream == nil) {
		return;
	}
	/* §5.3: A CLIENT'S FRAMES ARE MASKED, with a fresh key per frame. RAND_bytes is the source the handshake's
	 * nonce uses (and its failure mode is named there): a zero key is still CONSISTENT - the peer unmasks with
	 * the key it is sent - it is simply not unpredictable. */
	if(RAND_bytes(maskBytes, (int)sizeof(maskBytes)) != 1) {
		memset(maskBytes, 0, sizeof(maskBytes));
	}
	mask = [NSData dataWithBytes:maskBytes length:sizeof(maskBytes)];
	frame = FNWebSocketCreateFrame(fin, opcode, payload, length, (const uint8_t *)[mask bytes]);
	if(_wsState == FN_WS_OPEN || _wsState == FN_WS_CLOSING) {
		[self fnWriteFrame:frame];
	} else {
		[_queued addObject:frame];
	}
}

- (void)fnWriteFrame:(NSData *)frame
{
	[_stream writeData:frame timeout:15.0 completionHandler:^(NSError *error) {
		(void)error;
	}];
}

- (void)fnFlushQueued
{
	NSUInteger i;

	for(i = 0; i < [_queued count]; i++) {
		[self fnWriteFrame:[_queued objectAtIndex:i]];
	}
	[_queued removeAllObjects];
}

/* EVERY WAITING HANDLER IS ANSWERED WHEN THE TASK ENDS, because a caller's block must not be left hanging: the
 * receives get the error, and the pongs get it too - in the order they were asked for, which is the promise the
 * ping door makes. */
- (void)fnFlushWaitersWithError:(NSError *)error
{
	NSArray *receives = [_receives copy];
	NSArray *pongs = [_pongs copy];
	NSUInteger i;

	[_receives removeAllObjects];
	[_pongs removeAllObjects];
	for(i = 0; i < [receives count]; i++) {
		void (^handler)(NSURLSessionWebSocketMessage *, NSError *) = Block_copy([receives objectAtIndex:i]);

		[self fnHopToDelegateQueue:^{
			handler(nil, error);
			Block_release(handler);
		}];
	}
	for(i = 0; i < [pongs count]; i++) {
		void (^handler)(NSError *) = Block_copy([pongs objectAtIndex:i]);

		[self fnHopToDelegateQueue:^{
			handler(error);
			Block_release(handler);
		}];
	}
	[receives release];
	[pongs release];
}

/* THE HOP THE WHOLE SESSION USES (§52), and in MRC it is three things rather than one: the block is COPIED
 * because a queued block outlives the call, and the task is RETAINED because the queue's block refers to it. */
- (void)fnHopToDelegateQueue:(void (^)(void))block
{
	NSOperationQueue *queue = [(NSURLSession *)_session delegateQueue];
	void (^copied)(void);

	if(queue == nil) {
		block();
		return;
	}
	copied = Block_copy(block);
	[self retain];
	[queue addOperationWithBlock:^{
		copied();
		Block_release(copied);
		[self release];
	}];
}

/* THE ENDING, IN THE BASE'S OWN TERMS: fnProtocolDidFinishWithError: moves the state and keeps the error, and
 * then the delegate hears about it - once, which is what the state test at the top is for. */
- (void)fnEndWithError:(NSError *)error
{
	NSURLSession *session = (NSURLSession *)_session;

	if(_wsState == FN_WS_ENDED) {
		return;
	}
	_wsState = FN_WS_ENDED;
	[self fnProtocolDidFinishWithError:error];
	if(session == nil) {
		return;
	}
	{
		id delegate = [session delegate];

		if([delegate respondsToSelector:@selector(URLSession:task:didCompleteWithError:)]) {
			NSError *delivered = [[self error] retain];

			[self fnHopToDelegateQueue:^{
				[delegate URLSession:session task:self didCompleteWithError:delivered];
				[delivered release];
			}];
		}
	}
}

- (void)fnFail:(NSString *)reason closeCode:(NSInteger)closeCode
{
	NSError *error = [[NSError alloc] initWithDomain:NSURLErrorDomain
						   code:NSURLErrorBadServerResponse
					       userInfo:@{ NSLocalizedDescriptionKey: reason }];

	_closeCode = closeCode;
	[self fnFlushWaitersWithError:error];
	[self fnEndWithError:[error autorelease]];
}

- (NSError *)fnErrorWithCode:(NSInteger)code
{
	return [NSError errorWithDomain:NSURLErrorDomain code:code userInfo:nil];
}

@end
