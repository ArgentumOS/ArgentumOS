/*
 * NSURLSessionWebSocketMessage.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE WEBSOCKET MESSAGE AS A VALUE: what the payload is, and which kind it is. Apple's published page, read
 * 2026-09-22, and its own words are the contract this file implements:
 *
 *   "The message can be initialized with data or string. If initialized with data, the string property will be
 *    nil and vice versa."
 *
 * SO THE TWO PAYLOAD PROPERTIES ARE MUTUALLY EXCLUSIVE, AND ONE OF THEM IS ALWAYS nil. That is worth stating
 * because the plausible alternative would be WRONG in a way a caller would not notice until it mattered: a data
 * message does NOT hand its bytes back as a string, and a string message does NOT hand its text back as UTF-8
 * data. A caller that wants the bytes of a TEXT message asks for `string` and encodes it itself.
 *
 * WHAT THIS CLASS IS NOT: it is not the task and it is not the framing. A message is a VALUE - two initialisers
 * and three read-only properties - and the RFC 6455 layer that puts it on a wire belongs to
 * NSURLSessionWebSocketTask (slice 3 of §59). NOTHING IN THIS FILE TOUCHES A SOCKET.
 *
 * AND THE TYPE ENUM IS NOT HERE, deliberately: Apple publishes NSURLSessionWebSocketMessageType on
 * NSURLSession's page, so it is declared in NSURLSession.h - the same fidelity rule that put
 * NSURLSessionWebSocketCloseCode on the task's page (§59).
 */
#ifndef _FOUNDATION_NSURLSESSIONWEBSOCKETMESSAGE_H
#define _FOUNDATION_NSURLSESSIONWEBSOCKETMESSAGE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURLSession.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSURLSessionWebSocketMessage : NSObject

/* THE TWO WAYS TO MAKE ONE, AND THEY ARE THE ONLY TWO: a binary message from data, a text message from a
 * string. There is no initialiser that takes both, because a message holds one or the other. */
- (instancetype)initWithData:(NSData *)data;
- (instancetype)initWithString:(NSString *)string;

/* WHICH KIND IT IS, AND THE TWO PAYLOADS. The one that does not apply is nil - see the header above, where
 * that rule is quoted from the page rather than inferred. */
@property (readonly) NSURLSessionWebSocketMessageType type;
@property (readonly, nullable) NSData *data;
@property (readonly, nullable) NSString *string;

@end

NS_ASSUME_NONNULL_END

#endif /* _FOUNDATION_NSURLSESSIONWEBSOCKETMESSAGE_H */
