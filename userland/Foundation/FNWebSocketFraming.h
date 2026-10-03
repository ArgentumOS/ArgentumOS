/*
 * FNWebSocketFraming.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNWebSocketFraming — RFC 6455's framing, as PURE FUNCTIONS OVER BYTES. §59 slice 2, internal.
 *
 * THIS IS NOT PUBLIC API AND IS NOT IN Foundation.h: Apple has no such type, and it is here so that the framing
 * is a LAYER rather than a paragraph of code inside a task. The reason is the one that makes it testable at
 * all - a frame codec that exists only as "whatever the socket layer happens to do" can only be tested
 * through a socket, a handshake and a peer; these functions take bytes and return bytes, so the probe
 * drives them directly and a failure names a frame rather than a connection. (FNPointerTable is the precedent
 * for the placement and the words "not public API" are its.)
 *
 * WHAT IS IN HERE: the frame header (FIN, the opcode, the mask bit, and the THREE length encodings the RFC
 * defines), the masking operation (§5.3), the construction of a frame, and the close payload's one rule that is
 * not a byte rule. WHAT IS NOT: fragmentation across frames, the interleaved control frames of §5.4, and the
 * message size limit - those are STATE, and they are the assembler's (slice 2b), because a parse of one frame
 * has nothing to remember.
 *
 * THE PARSER COPIES NOTHING AND THE FRAME POINTS INTO YOUR BUFFER, deliberately: a WebSocket reader is a
 * streaming machine, and a codec that allocated per frame would make the caller's read loop the place where the
 * allocations pile up. `payload` is therefore valid exactly as long as the bytes passed in are.
 */
#ifndef FOUNDATION_FNWEBSOCKETFRAMING_H
#define FOUNDATION_FNWEBSOCKETFRAMING_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>

NS_ASSUME_NONNULL_BEGIN

/* §5.2's opcodes. The reserved ones (0x3-0x7 data, 0xB-0xF control) are NOT here, because a codec that could
 * name one would be a codec that could accept one. */
typedef NS_ENUM(uint8_t, FNWebSocketOpcode) {
	FNWebSocketOpcodeContinuation = 0x0,
	FNWebSocketOpcodeText = 0x1,
	FNWebSocketOpcodeBinary = 0x2,
	FNWebSocketOpcodeClose = 0x8,
	FNWebSocketOpcodePing = 0x9,
	FNWebSocketOpcodePong = 0xA
};

typedef struct {
	BOOL fin;
	FNWebSocketOpcode opcode;
	BOOL masked;
	uint64_t payloadLength;
	uint8_t maskKey[4];	/* meaningful only when masked */
	const uint8_t *payload;	/* INTO THE CALLER'S BUFFER - nothing is copied */
	size_t frameLength;	/* header plus payload: what the caller may consume */
} FNWebSocketFrame;

/* PARSE ONE FRAME. Three answers, and the middle one is the contract that makes streaming work:
 *   > 0  the frame is whole; that many bytes were consumed and `frame` describes it;
 *   = 0  NOT YET: more bytes are needed, and the caller should ask again after reading more - nothing is
 *        consumed and nothing is remembered (there is no partial-frame state here, on purpose);
 *   < 0  a PROTOCOL ERROR: a reserved opcode, a set RSV bit (no extension was negotiated, §59's boundary), a
 *        masked frame from a server or an unmasked one from a client (§5.1), a fragmented control frame, or a
 *        control frame longer than 125 bytes (§5.5).
 * `expectMasked` is the DIRECTION's rule rather than a hint: a server's frames may not be masked and a client's
 * must be. */
NSInteger FNWebSocketParseFrame(const uint8_t *bytes, size_t length, BOOL expectMasked,
				FNWebSocketFrame * _Nonnull frame);

/* §5.3: XOR the payload with the 4-byte key, cycling. MASKING AND UNMASKING ARE THE SAME OPERATION, which is
 * why there is one function and not two. (The array parameter carries its OWN specifier, because this header is
 * inside an NS_ASSUME_NONNULL region: pointers inherit nonnull there and ARRAYS DO NOT - a distinction the
 * compiler enforces with -Werror, which is how this line came to be written twice.) */
void FNWebSocketApplyMask(uint8_t *payload, size_t length, const uint8_t maskKey[_Nonnull 4]);

/* BUILD ONE FRAME. `maskKeyOrNULL` decides the direction: NULL builds an unmasked (server) frame, a key builds
 * a masked (client) one - and the key is the CALLER's, so that the task can supply randomness and a test can
 * supply a constant. Returns +1; the caller releases it. */
NSData *FNWebSocketCreateFrame(BOOL fin, FNWebSocketOpcode opcode, const uint8_t * _Nullable payload,
			       size_t length, const uint8_t * _Nullable maskKeyOrNULL);

/* MAY THIS CLOSE PAYLOAD BE SENT? The one rule here that is not a byte rule: §7.4.1 RESERVES 1005
 * (no status received), 1006 (abnormal closure) and 1015 (TLS handshake failure) so that a READER can report
 * what happened to it - they must never be sent in a close frame. An empty payload is legal and means "no
 * code". A payload shorter than two bytes is not a code at all and is refused with the same answer. */
BOOL FNWebSocketClosePayloadIsSendable(const uint8_t * _Nullable payload, size_t length);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNWEBSOCKETFRAMING_H */
