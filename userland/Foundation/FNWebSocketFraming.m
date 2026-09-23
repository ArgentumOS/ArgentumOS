/*
 * FNWebSocketFraming.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * RFC 6455's byte layer, and every branch below is a rule with a section number rather than a judgement call.
 * MRC, like every file in this library.
 */
#import <Foundation/FNWebSocketFraming.h>
#include <string.h>

/* §5.2: the payload length's THREE encodings - 7 bits, then 126 + 16 bits, then 127 + 64 bits. The middle one
 * is the trap: 126 and 127 are not lengths, they are the markers that the length follows. */
#define FN_WS_LEN16	126
#define FN_WS_LEN64	127

NSInteger FNWebSocketParseFrame(const uint8_t *bytes, size_t length, BOOL expectMasked,
				FNWebSocketFrame *frame)
{
	size_t need = 2;
	size_t headerLength;
	uint64_t payloadLength;
	uint8_t first, second, opcode, len7;
	BOOL fin, masked;

	if(length < need) {
		return 0;
	}
	first = bytes[0];
	second = bytes[1];

	fin = (first & 0x80) != 0;
	if((first & 0x70) != 0) {
		return -1;	/* RSV1-3 set: no extension was negotiated (§59 forbids offering one) */
	}
	opcode = first & 0x0F;
	switch(opcode) {
	case FNWebSocketOpcodeContinuation:
	case FNWebSocketOpcodeText:
	case FNWebSocketOpcodeBinary:
	case FNWebSocketOpcodeClose:
	case FNWebSocketOpcodePing:
	case FNWebSocketOpcodePong:
		break;
	default:
		return -1;	/* §5.2: a reserved opcode */
	}

	masked = (second & 0x80) != 0;
	if(masked != expectMasked) {
		return -1;	/* §5.1: a server's frames may not be masked, a client's must be */
	}
	len7 = second & 0x7F;
	if(len7 < FN_WS_LEN16) {
		payloadLength = len7;
	} else if(len7 == FN_WS_LEN16) {
		need += 2;
		if(length < need) {
			return 0;
		}
		payloadLength = ((uint64_t)bytes[2] << 8) | (uint64_t)bytes[3];
	} else {
		int i;

		need += 8;
		if(length < need) {
			return 0;
		}
		payloadLength = 0;
		for(i = 0; i < 8; i++) {
			payloadLength = (payloadLength << 8) | (uint64_t)bytes[2 + i];
		}
		if(payloadLength > 0x7FFFFFFFFFFFFFFFULL) {
			return -1;	/* §5.2: the most significant bit must be 0 */
		}
	}

	/* §5.5: A CONTROL FRAME IS NEVER FRAGMENTED AND NEVER LONGER THAN 125 BYTES. Both halves matter - a control
	 * frame that could be split would sit in the middle of a message and have to be reassembled first, and one
	 * that could be huge would be a way to make a peer buffer without limit. */
	if((opcode & 0x8) != 0 && (!fin || payloadLength > 125)) {
		return -1;
	}

	if(masked) {
		need += 4;
		if(length < need) {
			return 0;
		}
		memcpy(frame->maskKey, bytes + need - 4, 4);
	}
	headerLength = need;
	if(length < headerLength + (size_t)payloadLength) {
		return 0;	/* the header is here and the payload is not: ask again */
	}

	frame->fin = fin;
	frame->opcode = (FNWebSocketOpcode)opcode;
	frame->masked = masked;
	frame->payloadLength = payloadLength;
	frame->payload = bytes + headerLength;
	frame->frameLength = headerLength + (size_t)payloadLength;
	return (NSInteger)frame->frameLength;
}

void FNWebSocketApplyMask(uint8_t *payload, size_t length, const uint8_t maskKey[_Nonnull 4])
{
	size_t i;

	for(i = 0; i < length; i++) {
		payload[i] ^= maskKey[i % 4];
	}
}

NSData *FNWebSocketCreateFrame(BOOL fin, FNWebSocketOpcode opcode, const uint8_t *payload, size_t length,
			       const uint8_t *maskKeyOrNULL)
{
	NSMutableData *frame = [[NSMutableData alloc] initWithCapacity:length + 14];
	uint8_t header[14];
	size_t headerLength = 2;

	header[0] = (uint8_t)((fin ? 0x80 : 0x00) | (uint8_t)(opcode & 0x0F));
	if(length < FN_WS_LEN16) {
		header[1] = (uint8_t)(length & 0x7F);
	} else if(length <= 0xFFFF) {
		header[1] = FN_WS_LEN16;
		header[2] = (uint8_t)((length >> 8) & 0xFF);
		header[3] = (uint8_t)(length & 0xFF);
		headerLength += 2;
	} else {
		int i;

		header[1] = FN_WS_LEN64;
		for(i = 0; i < 8; i++) {
			header[2 + i] = (uint8_t)((length >> (8 * (7 - i))) & 0xFF);
		}
		headerLength += 8;
	}
	if(maskKeyOrNULL != NULL) {
		header[1] |= 0x80;	/* the client's direction */
		memcpy(header + headerLength, maskKeyOrNULL, 4);
		headerLength += 4;
	}
	[frame appendBytes:header length:headerLength];
	if(length > 0) {
		NSUInteger before = [frame length];

		[frame appendBytes:payload length:length];
		if(maskKeyOrNULL != NULL) {
			FNWebSocketApplyMask((uint8_t *)[frame mutableBytes] + before, length, maskKeyOrNULL);
		}
	}
	return frame;
}

BOOL FNWebSocketClosePayloadIsSendable(const uint8_t *payload, size_t length)
{
	uint64_t code;

	if(length == 0) {
		return YES;	/* a close with no code at all is legal (§5.5.1) */
	}
	if(payload == NULL || length < 2) {
		/* A LENGTH THAT CLAIMS A CODE MUST COME WITH SOMEWHERE TO READ IT. The first version checked only the
		 * length, so a caller that passed NULL with a nonzero length - which the TASK does, whose reason may be
		 * nil - made this dereference address zero. FOUND BY THE TASK'S PROBE CRASHING (SIGSEGV), because slice
		 * 2's own probe never tried that combination: it tested 0, 1 and valid codes, and every one of those has
		 * a payload to look at. */
		return NO;
	}
	code = ((uint64_t)payload[0] << 8) | (uint64_t)payload[1];
	/* §7.4.1: these three are RESERVED for a reader to report what happened to it. Sending one would be a
	 * close frame whose meaning is "this is what I received", which no endpoint can be telling the other. */
	if(code == 1005 || code == 1006 || code == 1015) {
		return NO;
	}
	return YES;
}
