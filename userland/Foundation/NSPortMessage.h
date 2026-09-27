/*
 * NSPortMessage.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSPortMessage` — THE OBJECT A PORT SENDS AND RECEIVES (§62.53). Apple deprecated the whole port family at 10.13
 * and §11.5 struck it here BY NAME, which is why `NSPort.h` still says that `-sendBeforeDate:components:from:reserved:`
 * "has no component type to carry". **§62.24's policy retired the deprecation ground as a strike**, so the names
 * came back onto the work list and this unit is the policy reversing that decision: with a message to carry, the
 * send door has a component type, and the delegate has a protocol to be typed by.
 *
 * THE COMPONENTS ARE `NSData`. Apple's documentation says a component may be a port or an `NSData`, and a MESSAGE IS
 * CARRIED AS BYTES over the socket that is this library's transport — so a component that is not data cannot be
 * written down, and one that IS a port is REFUSED at the door that would have to send it rather than silently
 * dropped. A port in a message means a port RIGHT, which is a kernel feature this system's transport does not have.
 */

#ifndef FOUNDATION_NSPORTMESSAGE_H
#define FOUNDATION_NSPORTMESSAGE_H

#import <Foundation/NSObject.h>

@class NSPort, NSDate, NSArray, NSMutableArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSPortMessage : NSObject
{
	NSPort *_sendPort;
	NSPort *_receivePort;
	NSMutableArray *_components;
	unsigned int _msgid;
}

/* Either port may be nil, which is Apple's own contract for a message that is being built before it is sent or
 * described as it was received. */
- (instancetype)initWithSendPort:(nullable NSPort *)sendPort
		     receivePort:(nullable NSPort *)receivePort
		      components:(nullable NSArray *)components;

- (nullable NSArray *)components;
- (nullable NSPort *)receivePort;
- (nullable NSPort *)sendPort;

- (unsigned int)msgid;
- (void)setMsgid:(unsigned int)msgid;

/* Answers NO when the message cannot be sent — no send port, an invalid one, or a component that is not data — and
 * the reason is on the port's side rather than in a status code, which is Apple's shape for this door. */
- (BOOL)sendBeforeDate:(NSDate *)date;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPORTMESSAGE_H */
