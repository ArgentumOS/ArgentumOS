/*
 * FNWebSocketAssembler.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNWebSocketAssembler — THE STATE HALF OF THE FRAMING: frames in, messages and control frames out. §59 slice 2b,
 * internal, and the companion of FNWebSocketFraming for a reason worth stating: THE CODEC REMEMBERS NOTHING AND
 * THIS REMEMBERS EVERYTHING THAT MATTERS. A parse is a function of bytes; a MESSAGE is a function of history -
 * which fragments belong together, what type the message is, how much of it there is, and whether a control frame
 * has arrived in the middle of it.
 *
 * NOT PUBLIC API AND NOT IN Foundation.h (FNPointerTable's precedent). The task composes it in slice 3.
 *
 * THE ONE RULE THAT SHAPES THIS INTERFACE IS §5.4: A CONTROL FRAME MAY ARRIVE IN THE MIDDLE OF A FRAGMENTED
 * MESSAGE AND MUST BE ANSWERED WHEN IT ARRIVES, not after the message finishes. So feeding a frame can hand back
 * a CONTROL FRAME rather than a message - and the message it interrupted goes on being assembled as if nothing
 * had happened. A reader that queued the control frame behind the message would stall a peer's ping for as long
 * as the message takes, which for a large message is the whole point of the ping.
 *
 * AND WHAT A BROKEN PEER GETS IS AN ANSWER, NOT A CRASH: a continuation with nothing to continue, a second
 * message started before the first finished, or a message past maximumMessageSize are ERRORS here, because
 * RFC 6455 has exactly one thing to say about all three (1002, protocol error) and the task says it.
 */
#ifndef FOUNDATION_FNWEBSOCKETASSEMBLER_H
#define FOUNDATION_FNWEBSOCKETASSEMBLER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>
#import <Foundation/FNWebSocketFraming.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, FNWebSocketAssemblyResult) {
	FNWebSocketAssemblyFragments = 0,	/* part of the current message: nothing to hand back yet */
	FNWebSocketAssemblyMessage = 1,		/* a WHOLE message is ready (outOpcode says which kind) */
	FNWebSocketAssemblyControl = 2,		/* a control frame arrived: answer it NOW (outOpcode says which) */
	FNWebSocketAssemblyError = -1		/* the peer broke a rule of §5.4, and 1002 is the answer */
};

@interface FNWebSocketAssembler : NSObject
{
	NSMutableData *_payload;
	FNWebSocketOpcode _type;
	BOOL _inMessage;
	NSUInteger _maximumMessageSize;
}

/* THE DEFAULT IS OURS (D2), LIKE THE MESSAGE-TYPE VALUES WERE: Apple documents `maximumMessageSize` and NOT its
 * default, and the only published account of the value contradicts the only other one. A megabyte is chosen
 * because it is what the credible report says AND because it is a sane ceiling for a reader that must hold the
 * whole message before it can hand it over: the property's own documentation describes exactly this buffer. */
- (instancetype)init;

- (NSUInteger)maximumMessageSize;
- (void)setMaximumMessageSize:(NSUInteger)bytes;

/* FEED ONE PARSED FRAME. The out-parameters are +1 when they are set (the caller owns them) and untouched
 * otherwise; `outData` carries a message's payload or a control frame's, and `outOpcode` its kind. (BOTH LEVELS
 * of an out-parameter need their specifier inside an NS_ASSUME_NONNULL region - `NSData **` is a pointer to a
 * pointer, and the region covers both - which is another line this family learned from -Werror.) */
- (FNWebSocketAssemblyResult)feedFrame:(const FNWebSocketFrame *)frame
			       outOpcode:(FNWebSocketOpcode * _Nonnull)outOpcode
				 outData:(NSData * _Nonnull * _Nonnull)outData;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNWEBSOCKETASSEMBLER_H */
