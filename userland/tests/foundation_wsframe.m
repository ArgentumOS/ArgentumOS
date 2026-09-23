/*
 * foundation_wsframe — RFC 6455'S BYTE LAYER (§59 slice 2): the frame codec, as pure functions.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NO SOCKET, NO HANDSHAKE, NO SERVER. That is the whole reason the codec is a layer: a frame parser that only
 * existed inside the task could only be tested through a connection and a peer, and a failure would name a
 * connection. Here the probe hands it bytes.
 *
 * WHAT IT PINS, AND THE TRAPS EACH CHECK EXISTS FOR:
 *   * the ROUND TRIP, both directions of the mask rule (a client's frames are masked, a server's must not be);
 *   * THE THREE LENGTH ENCODINGS (§5.2), because 126 and 127 are MARKERS and not lengths - the single most
 *     common way a hand-written codec is wrong, and it is wrong only for payloads nobody tries;
 *   * the CONTROL FRAME RULES (§5.5): never fragmented, never longer than 125 bytes;
 *   * THE INCREMENTAL CONTRACT: a frame that has not fully arrived is 0 ("ask again"), not an error and not a
 *     shorter frame - a reader that mistook it for one would drop the rest of the message;
 *   * THE NO-COPY PROPERTY: the frame points into the caller's buffer;
 *   * and the CLOSE PAYLOAD'S ONE NON-BYTE RULE: 1005, 1006 and 1015 are REPORTABLE AND NEVER SENDABLE (§7.4.1).
 *
 * ARC, like every probe: no release calls anywhere, which is a lesson this family already learned by failing to
 * compile once.
 */
#import <Foundation/Foundation.h>
/* AND THE INTERNAL UNIT IS NAMED EXPLICITLY, WHICH IS WHAT "NOT PUBLIC API" MEANS IN PRACTICE: FNWebSocketFraming
 * is DELIBERATELY not in Foundation.h, so a probe of the byte layer has to ask for it - and nobody reaching for
 * the toolkit can stumble into it. (Learned by compiling: the first version of this probe imported the umbrella
 * and the compiler said, of every type in the codec, "undeclared identifier".) */
#import <Foundation/FNWebSocketFraming.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-WSFRAME %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-WSFRAME %s FAIL: %s\n", name, [[why description] UTF8String]);
	}
}

/* A frame built by hand, so the probe is not only testing the codec against itself. */
static NSData *fnRawFrame(uint8_t first, uint8_t second, const uint8_t *extra, size_t extraLength,
			  const uint8_t *payload, size_t payloadLength)
{
	NSMutableData *built = [NSMutableData data];

	[built appendBytes:&first length:1];
	[built appendBytes:&second length:1];
	if(extraLength > 0) {
		[built appendBytes:extra length:extraLength];
	}
	if(payloadLength > 0) {
		[built appendBytes:payload length:payloadLength];
	}
	return built;
}

int main(void)
{
	const uint8_t key[4] = { 0xA1, 0xB2, 0xC3, 0xD4 };
	const uint8_t text[5] = { 'h', 'e', 'l', 'l', 'o' };
	FNWebSocketFrame frame;
	NSInteger consumed;
	NSData *masked;
	NSData *unmasked;
	NSData *partial;
	const uint8_t *bytes;
	uint8_t closeCode[2];

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE ROUND TRIP -------------------------------------------------------------------------------- */
	masked = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeText, text, sizeof(text), key);
	unmasked = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeText, text, sizeof(text), NULL);

	consumed = FNWebSocketParseFrame([unmasked bytes], [unmasked length], NO, &frame);
	check("a-text-frame-parses-back-to-its-payload",
	      consumed == (NSInteger)[unmasked length] && frame.fin && frame.opcode == FNWebSocketOpcodeText &&
	      !frame.masked && frame.payloadLength == 5 && memcmp(frame.payload, text, 5) == 0,
	      [NSString stringWithFormat:@"consumed=%ld fin=%d op=%d masked=%d len=%llu",
	       (long)consumed, (int)frame.fin, (int)frame.opcode, (int)frame.masked,
	       (unsigned long long)frame.payloadLength]);

	/* THE MASK IS ON THE WIRE, WHICH IS THE ONLY PLACE IT MATTERS: the client's frame carries the bit and a key,
	 * and its payload bytes on the wire are NOT the plaintext. */
	bytes = [masked bytes];
	consumed = FNWebSocketParseFrame(bytes, [masked length], YES, &frame);
	check("and-a-client-frame-carries-the-mask-and-a-key",
	      consumed > 0 && frame.masked && (bytes[1] & 0x80) != 0 &&
	      memcmp(frame.payload, text, 5) != 0,
	      @"the client's frame must be masked and its payload must not be readable on the wire");
	{
		/* AND THE INVERSE IS THE CALLER'S TO APPLY, WHICH IS THE DESIGN AND NOT A GAP: the parser COPIES NOTHING,
		 * so the frame's payload IS the masked bytes - unmasking is the second half of §5.3, and it is asserted
		 * here rather than assumed. (The first version of this check expected the parser to have done it, and
		 * failed: a probe that asserts the wrong half of a contract reports a codec bug that is not there.) */
		uint8_t recovered[5];

		memcpy(recovered, frame.payload, 5);
		FNWebSocketApplyMask(recovered, 5, frame.maskKey);
		check("and-the-mask-is-its-own-inverse",
		      consumed > 0 && frame.payloadLength == 5 && memcmp(recovered, text, 5) == 0,
		      @"masking is XOR with the key, so unmasking is the same operation and must return the plaintext");
	}

	/* --- THE THREE LENGTH ENCODINGS (§5.2) -------------------------------------------------------------- */
	{
		NSMutableData *big = [NSMutableData data];
		NSMutableData *huge = [NSMutableData data];
		uint8_t filler[125];

		memset(filler, 'x', sizeof(filler));
		[big appendBytes:filler length:126];	/* the 16-bit form */
		[big setLength:126];
		[huge appendBytes:filler length:125];
		[huge increaseLengthBy:(70000 - 125)];	/* the 64-bit form */

		{
			NSData *f = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeBinary, [big bytes], 126, NULL);
			NSInteger got = FNWebSocketParseFrame([f bytes], [f length], NO, &frame);

			check("a-126-byte-payload-uses-the-16-bit-encoding",
			      got > 0 && ((const uint8_t *)[f bytes])[1] == 126 && frame.payloadLength == 126,
			      [NSString stringWithFormat:@"marker=%d length=%llu",
			       (int)((const uint8_t *)[f bytes])[1], (unsigned long long)frame.payloadLength]);
		}
		{
			NSData *f = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeBinary, [huge bytes], 70000, NULL);
			NSInteger got = FNWebSocketParseFrame([f bytes], [f length], NO, &frame);

			check("a-70000-byte-payload-uses-the-64-bit-encoding",
			      got > 0 && ((const uint8_t *)[f bytes])[1] == 127 && frame.payloadLength == 70000,
			      [NSString stringWithFormat:@"marker=%d length=%llu",
			       (int)((const uint8_t *)[f bytes])[1], (unsigned long long)frame.payloadLength]);
		}
	}
	{
		NSData *small = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeBinary, text, 5, NULL);

		check("and-a-small-payload-uses-the-7-bit-one",
		      ((const uint8_t *)[small bytes])[1] == 5,
		      @"125 and below are the length itself; 126 and 127 are markers");
	}

	/* --- THE DIRECTION RULES AND THE RESERVED SPACE (§5.1, §5.2, §5.5) ---------------------------------- */
	check("a-server-frame-may-not-be-masked",
	      FNWebSocketParseFrame([masked bytes], [masked length], NO, &frame) < 0,
	      @"§5.1: a client must refuse a masked frame from a server");
	check("and-a-client-frame-must-be",
	      FNWebSocketParseFrame([unmasked bytes], [unmasked length], YES, &frame) < 0,
	      @"§5.1: a server must refuse an unmasked frame from a client");
	{
		NSData *reserved = fnRawFrame(0x83, 0x00, NULL, 0, NULL, 0);	/* opcode 0x3 */

		check("a-reserved-opcode-is-refused",
		      FNWebSocketParseFrame([reserved bytes], [reserved length], NO, &frame) < 0,
		      @"§5.2 reserves 0x3-0x7 and 0xB-0xF, and a codec that accepted one would be naming what it cannot mean");
	}
	{
		NSData *fragmentedPing = fnRawFrame(0x09, 0x00, NULL, 0, NULL, 0);	/* FIN=0, ping */
		/* A LONG CONTROL FRAME, BUILT AS ONE: FIN + ping (0x89), the 126 marker, 126 as the 16-bit length, and
		 * 126 payload bytes. (The first version of this check handed the parser an all-zeroes buffer and called
		 * it a long control frame - which is opcode 0, a CONTINUATION, length 0, and perfectly legal, so the
		 * check failed while the codec was right. A probe has to build the thing it means.) */
		NSMutableData *longControl = [NSMutableData data];
		uint8_t controlHeader[4] = { 0x89, 126, 0x00, 126 };
		uint8_t controlBody[126];

		memset(controlBody, 'p', sizeof(controlBody));
		[longControl appendBytes:controlHeader length:sizeof(controlHeader)];
		[longControl appendBytes:controlBody length:sizeof(controlBody)];

		check("a-control-frame-may-not-be-fragmented",
		      FNWebSocketParseFrame([fragmentedPing bytes], [fragmentedPing length], NO, &frame) < 0,
		      @"§5.5: a control frame is never fragmented");
		check("and-may-not-be-longer-than-125-bytes",
		      FNWebSocketParseFrame([longControl bytes], [longControl length], NO, &frame) < 0,
		      @"§5.5: and never longer, so a peer cannot make a reader buffer without limit");
	}

	/* --- THE INCREMENTAL CONTRACT, AND THE NO-COPY PROPERTY --------------------------------------------- */
	partial = [masked subdataWithRange:NSMakeRange(0, 3)];
	check("a-frame-that-has-not-arrived-is-not-yet-a-frame",
	      FNWebSocketParseFrame([partial bytes], [partial length], YES, &frame) == 0,
	      @"0 means ASK AGAIN: a reader that read this as an error, or as a smaller frame, would lose the rest");
	consumed = FNWebSocketParseFrame([masked bytes], [masked length], YES, &frame);
	check("and-the-frame-points-into-the-buffer-it-was-given",
	      consumed > 0 && frame.payload == (const uint8_t *)[masked bytes] + ([masked length] - 5),
	      @"nothing is copied: the payload is a window on the caller's bytes, which is what a read loop needs");

	/* --- THE CLOSE PAYLOAD'S ONE NON-BYTE RULE (§7.4.1) ------------------------------------------------- */
	closeCode[0] = (uint8_t)(1000 >> 8);
	closeCode[1] = (uint8_t)(1000 & 0xFF);
	check("a-close-code-may-be-sent", FNWebSocketClosePayloadIsSendable(closeCode, 2),
	      @"1000 is the ordinary close and it is sendable");
	closeCode[0] = (uint8_t)(1005 >> 8);
	closeCode[1] = (uint8_t)(1005 & 0xFF);
	{
		BOOL noStatus = FNWebSocketClosePayloadIsSendable(closeCode, 2);

		closeCode[0] = (uint8_t)(1006 >> 8);
		closeCode[1] = (uint8_t)(1006 & 0xFF);
		{
			BOOL abnormal = FNWebSocketClosePayloadIsSendable(closeCode, 2);

			closeCode[0] = (uint8_t)(1015 >> 8);
			closeCode[1] = (uint8_t)(1015 & 0xFF);
			check("and-the-three-reserved-codes-may-not",
			      !noStatus && !abnormal && !FNWebSocketClosePayloadIsSendable(closeCode, 2),
			      @"§7.4.1 reserves 1005, 1006 and 1015 for a READER to report what happened to IT");
		}
	}
	check("and-an-empty-close-payload-means-no-code-at-all",
	      FNWebSocketClosePayloadIsSendable(NULL, 0) && !FNWebSocketClosePayloadIsSendable(closeCode, 1),
	      @"empty is legal; a payload that is neither empty nor carries a code is not");
	/* A LENGTH THAT CLAIMS A CODE MUST COME WITH SOMEWHERE TO READ IT - the case this probe MISSED, and the
	 * omission cost the task's probe a SIGSEGV: FNWebSocketClosePayloadIsSendable(NULL, 2) dereferenced address
	 * zero, because the first version checked the length and not the pointer. The check above passes NULL with
	 * length ZERO (legal) and length ONE (refused before any read), so the dangerous combination - NULL with a
	 * length that promises two bytes - was the one nobody tried. It is tried here. */
	check("and-a-length-that-promises-a-code-needs-a-payload-to-read-it",
	      !FNWebSocketClosePayloadIsSendable(NULL, 2) && !FNWebSocketClosePayloadIsSendable(NULL, 126),
	      @"found by slice 3b's probe crashing: NULL with a real length must be refused, not dereferenced");

	printf("FOUNDATION-WSFRAME RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-WSFRAME-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-WSFRAME DONE\n");
	return failc ? 1 : 0;
}
