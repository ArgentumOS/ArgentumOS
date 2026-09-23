/*
 * NSURLSessionWebSocketTask.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CLOSE CODES, AND ONLY THE CLOSE CODES SO FAR. Apple publishes NSURLSessionWebSocketCloseCode on
 * NSURLSessionWebSocketTask's page, so this header is its home - but the TASK ITSELF LANDS IN SLICE 3 OF §59,
 * and nothing here declares it: a header that declared the class before its doors exist would make the family
 * ledger read "shipped" for a class that is not, which is the one thing that ledger is for.
 *
 * WHERE THE NUMBERS COME FROM, AND IT IS NOT APPLE. RFC 6455 §7.4.1 DEFINES 1000-1015, and these names are
 * Apple's spellings of exactly those codes, so the values below are THE RFC'S - a standard, not a vendor's
 * choice. The one exception is `...Invalid`, which is NOT in the RFC: Apple documents the name and nothing
 * else, so its value is OURS under D2 (§11.6) - and it is 0 for the reason every container needs: it is what a
 * reader that has no close code at all starts from, and what an unknown one becomes. That matches the only
 * published account of the behaviour rather than being guessed from it.
 *
 * THREE OF THESE ARE RESERVED BY THE RFC AND MUST NEVER BE SENT IN A CLOSE FRAME (1005 noStatusReceived,
 * 1006 abnormalClosure, 1015 tlsHandshakeFailure): they EXIST for a reader to report what happened to it, not
 * for a writer to say. The framing layer enforces that (slice 2); the constants are here because a caller
 * reads them.
 */
#ifndef _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H
#define _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H

#import <Foundation/NSObject.h>
/* THE CLASS AND THE MESSAGE IT CARRIES ARE A PAIR, so this header imports it; the MESSAGE header imports
 * NSURLSession.h for the type enum Apple publishes there, which is safe because NSURLSession.h only FORWARD
 * DECLARES this class (its three factories return one, and a pointer needs no definition). No cycle. */
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionWebSocketMessage.h>

NS_ASSUME_NONNULL_BEGIN

@class NSURLSession;

typedef NS_ENUM(NSInteger, NSURLSessionWebSocketCloseCode) {
	NSURLSessionWebSocketCloseCodeInvalid = 0,		/* ours (D2): no close code, or one we do not know */
	NSURLSessionWebSocketCloseCodeNormalClosure = 1000,	/* the RFC's */
	NSURLSessionWebSocketCloseCodeGoingAway = 1001,
	NSURLSessionWebSocketCloseCodeProtocolError = 1002,
	NSURLSessionWebSocketCloseCodeUnsupportedData = 1003,
	NSURLSessionWebSocketCloseCodeNoStatusReceived = 1005,	/* reserved: reportable, never sent */
	NSURLSessionWebSocketCloseCodeAbnormalClosure = 1006,	/* reserved: reportable, never sent */
	NSURLSessionWebSocketCloseCodeInvalidFramePayloadData = 1007,
	NSURLSessionWebSocketCloseCodePolicyViolation = 1008,
	NSURLSessionWebSocketCloseCodeMessageTooBig = 1009,
	NSURLSessionWebSocketCloseCodeMandatoryExtensionMissing = 1010,
	NSURLSessionWebSocketCloseCodeInternalServerError = 1011,
	NSURLSessionWebSocketCloseCodeTLSHandshakeFailure = 1015	/* reserved: reportable, never sent */
};

/* A MESSAGE-ORIENTED TASK OVER RFC 6455, and a NSURLSessionTask like any other: resume, cancel, suspend, an
 * identifier, and the completed-with-error ending all come from the base. WHAT IT ADDS IS A CONNECTION - it holds
 * a NSURLSessionStreamTask (§59: the stream is the substrate, which is why `wss:` costs nothing new) and speaks
 * the framing over it. WHAT IS OURS RATHER THAN APPLE'S IS SAID WHERE IT HAPPENS: the default below, and the
 * certificate rule it inherits from the stream. */
@interface NSURLSessionWebSocketTask : NSURLSessionTask

/* "The maximum number of bytes to buffer before the receive call fails with an error ... includes the sum of all
 * bytes from continuation frames." THE DEFAULT IS OURS (D2): Apple documents the property and not its default,
 * and the two published accounts of it disagree - so it is one megabyte, chosen and written down rather than
 * inferred. */
@property NSInteger maximumMessageSize;

/* WHY IT ENDED, AND THE PEER'S OWN WORDS: the close code from the wire (or our own, when we were the one that
 * closed), and the reason data the peer sent. `...Invalid` until a close arrives. */
@property (readonly) NSURLSessionWebSocketCloseCode closeCode;
@property (readonly, nullable) NSData *closeReason;

/* THE FOUR DOORS. Every completion handler is nullable, because a caller may not care. `_Nullable` is spelled
 * INSIDE each block parameter rather than as a bare `nullable`, which the compiler refuses in a block's parameter
 * list - ¶53.1's lesson, learned once already in this library. */
- (void)sendMessage:(NSURLSessionWebSocketMessage *)message
  completionHandler:(nullable void (^)(NSError * _Nullable error))completionHandler;
/* ONE CALL, ONE MESSAGE, and a listener re-arms from inside its own handler - Apple's interface, and the reason
 * this task never calls a receive handler twice by itself. The message is nil only when there is an error. */
- (void)receiveMessageWithCompletionHandler:(void (^)(NSURLSessionWebSocketMessage * _Nullable message,
						     NSError * _Nullable error))completionHandler;
/* The pong goes to THIS handler, and multiple pings are answered IN THE ORDER they were sent. */
- (void)sendPingWithPongReceiveHandler:(nullable void (^)(NSError * _Nullable error))pongReceiveHandler;
/* A CLOSE FRAME carrying a code and an optional reason - as against plain `-cancel`, which is a cancellation
 * with neither. 1005, 1006 and 1015 are REFUSED here: §7.4.1 reserves them for reporting what happened to us. */
- (void)cancelWithCloseCode:(NSURLSessionWebSocketCloseCode)closeCode reason:(nullable NSData *)reason;

/* FNX: THE SESSION'S OWN DOOR, beside the session's own doors in this library - a task is created BY a session,
 * so its initialiser is not public and the session calls this. IT BELONGS IN THIS HEADER RATHER THAN THE
 * IMPLEMENTATION because NSURLSession.m has to be able to SEE it: the first version declared it in the .m, and
 * the session could not call what it could not see. (NSURLSessionTask.h's own fn note is the convention.) */
- (instancetype)fnInitWithURL:(NSURL *)url
		    protocols:(nullable NSArray *)protocols
		   identifier:(NSUInteger)identifier;

@end

/* THE DELEGATE'S TWO DOORS ARE THE ONLY PLACE A CALLER LEARNS THE TWO THINGS THE TASK ITSELF CANNOT SAY: which
 * SUBPROTOCOL the server chose (the task asked for a list; the server picks), and WHY the connection ended - the
 * close code and the peer's reason, which arrive on the wire and nowhere else. Both optional, as Apple's are. */
@protocol NSURLSessionWebSocketDelegate <NSObject>
@optional
- (void)URLSession:(NSURLSession *)session
   webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
didOpenWithProtocol:(nullable NSString *)protocol;
- (void)URLSession:(NSURLSession *)session
   webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
  didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
	   reason:(nullable NSData *)reason;
@end

NS_ASSUME_NONNULL_END

#endif /* _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H */
