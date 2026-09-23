/*
 * FNWebSocketAssembler.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * MRC, like every file in this library: the out-parameters are +1 for the caller, and `dealloc` releases what
 * this object owns.
 */
#import <Foundation/FNWebSocketAssembler.h>

/* THE MEGABYTE, AND WHY IT IS HERE RATHER THAN IN THE HEADER: it is a value somebody may want to argue with, and
 * the argument belongs where the property is set. Apple documents the property and not the default (D2). */
#define FN_WS_DEFAULT_MAX_MESSAGE	(1024 * 1024)

@interface FNWebSocketAssembler ()
- (void)abandonMessage;
@end

@implementation FNWebSocketAssembler

/* ONE PLACE THAT CLEARS THE MESSAGE STATE, SO NO ERROR PATH CAN LEAVE THE ASSEMBLER HALF-WAY THROUGH A MESSAGE
 * WHOSE CONNECTION IS OVER ANYWAY - the task's answer to all three §5.4 faults is the same (1002 and close). */
- (void)abandonMessage
{
	_inMessage = NO;
	[_payload setLength:0];
}

- (instancetype)init
{
	self = [super init];
	if(self == nil) {
		return nil;
	}
	_payload = [[NSMutableData alloc] init];
	_maximumMessageSize = FN_WS_DEFAULT_MAX_MESSAGE;
	_type = FNWebSocketOpcodeText;
	_inMessage = NO;
	return self;
}

- (void)dealloc
{
	[_payload release];
	[super dealloc];
}

- (NSUInteger)maximumMessageSize
{
	return _maximumMessageSize;
}

- (void)setMaximumMessageSize:(NSUInteger)bytes
{
	_maximumMessageSize = bytes;
}

- (FNWebSocketAssemblyResult)feedFrame:(const FNWebSocketFrame *)frame
			       outOpcode:(FNWebSocketOpcode * _Nonnull)outOpcode
				 outData:(NSData * _Nonnull * _Nonnull)outData
{
	/* A CONTROL FRAME IS ANSWERED WHERE IT ARRIVES (§5.4), and the message it interrupted is untouched: neither
	 * its payload nor its type is disturbed, which is what `a-ping-in-the-middle-is-answered-while-the-message-
	 * waits` exists to prove. */
	if((frame->opcode & 0x8) != 0) {
		*outOpcode = frame->opcode;
		*outData = [[NSData alloc] initWithBytes:frame->payload length:(NSUInteger)frame->payloadLength];
		return FNWebSocketAssemblyControl;
	}

	if(frame->opcode == FNWebSocketOpcodeContinuation) {
		if(!_inMessage) {
			[self abandonMessage];
			return FNWebSocketAssemblyError;	/* §5.4: nothing to continue */
		}
	} else {
		if(_inMessage) {
			/* A PROTOCOL ERROR ABANDONS THE MESSAGE, AND THE FIRST VERSION DID NOT - which a probe caught: its
			 * checks share one assembler, and a peer's fault left the next scenario reading the previous
			 * scenario's state. Every error path here clears now, because the task's answer to all three is the
			 * same (1002 and close) and a connection that is over must not leave state that says otherwise. */
			[self abandonMessage];
			return FNWebSocketAssemblyError;	/* §5.4: the last message was not finished */
		}
		_type = frame->opcode;
		_inMessage = YES;
		[_payload setLength:0];
	}

	/* THE LIMIT COUNTS THE WHOLE MESSAGE, NOT ONE FRAME (§59, and Apple's own words for the property: "includes
	 * the sum of all bytes from continuation frames"). So the check is against what has ACCUMULATED plus what
	 * just arrived - which is why a peer cannot get past it by sending the same total in small pieces. */
	if([_payload length] + (NSUInteger)frame->payloadLength > _maximumMessageSize) {
		[self abandonMessage];
		return FNWebSocketAssemblyError;
	}
	[_payload appendBytes:frame->payload length:(NSUInteger)frame->payloadLength];

	if(!frame->fin) {
		return FNWebSocketAssemblyFragments;
	}
	_inMessage = NO;
	*outOpcode = _type;
	*outData = [_payload copy];	/* +1: the message is the caller's, and the buffer goes on being reused */
	return FNWebSocketAssemblyMessage;
}

@end
