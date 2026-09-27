/*
 * NSConnection.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSConnection` — DISTRIBUTED OBJECTS, ON THIS SYSTEM'S OWN TRANSPORT (§62.56). Apple struck the family by name in
 * §11.5 and §62.24 put it back; this is the last thing the port family owed.
 *
 * WHAT A CONNECTION IS HERE: a RECEIVE PORT and a SEND PORT (§62.53's transport), a ROOT OBJECT to answer for, and
 * the name it is published under (§62.54's registry). The connection is the receive port's DELEGATE, so a message
 * arriving is a request being served — no polling, no thread, the run loop of whoever scheduled the port.
 *
 * THE REPLY GOES TO A NAME, AND THAT IS THE DESIGN'S ONE NON-OBVIOUS CHOICE. A message cannot carry a port (§62.53
 * refuses a component that is not data), so the requester cannot hand over "send the answer here" as a port. It
 * registers a name for its own receive port instead and puts the NAME in the request; the service looks the name up
 * and sends the answer there. **A NAME SERVER IS EXACTLY THE MECHANISM FOR THAT**, which is why this works with
 * what §62.54 landed and needs nothing new.
 *
 * THE BOUNDARY, STATED PLAINLY: only OBJECT arguments and OBJECT results cross (see `NSDistantObject.h`), and the
 * registry is PER-PROCESS, so both ends of a connection live in the same process. A connection to another process
 * would need the name server to publish an address and a service to accept it — the accept path §62.54 named as
 * out of its reach. What is here is real, and it is in-process.
 *
 * FOUR NAMES COME WITH IT, AND ONE OF THEM IS A REFUSAL WORTH READING: `NSConnectionDidInitializeNotification` and
 * `NSConnectionDidDieNotification` are posted when a connection is made and when it is invalidated,
 * `NSConnectionReplyMode` is the run-loop mode a waiting client runs — AND EVERY CONNECTION'S PORT IS WATCHED IN
 * IT, service and client alike, because an in-process exchange is one wait: the client running that mode is what
 * pumps the service. A service in the default mode would deadlock against a client waiting in reply mode, and `NSFailedAuthenticationException` EXISTS AND IS NEVER RAISED: nothing in this library
 * authenticates, because a connection is a socket pair the process already holds.
 */

#ifndef FOUNDATION_NSCONNECTION_H
#define FOUNDATION_NSCONNECTION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSPort.h>

@class NSPort, NSPortNameServer, NSDistantObject, NSRunLoop, NSString;

NS_ASSUME_NONNULL_BEGIN

/* Posted when a connection is made, and when one is invalidated; the notification's object is the connection. */
extern NSString *const NSConnectionDidInitializeNotification;
extern NSString *const NSConnectionDidDieNotification;

/* THE MODE A REPLY IS AWAITED IN. A waiting client runs its run loop in this mode, and the serving side of the
 * same process keeps serving in it: what makes a request/answer exchange possible without a thread. */
extern NSString *const NSConnectionReplyMode;

/* NAMED, AND NEVER RAISED — see the header note. */
extern NSString *const NSFailedAuthenticationException;

@interface NSConnection : NSObject <NSPortDelegate>
{
	NSPort *_receivePort;
	NSPort *_sendPort;
	id _rootObject;			/* retained: the object this connection answers for */
	NSString *_name;		/* the name it was published under, if any */
	BOOL _valid;
	BOOL _waitingForReply;
	id _replyValue;
}

+ (nullable NSConnection *)connectionWithReceivePort:(nullable NSPort *)receivePort
					    sendPort:(nullable NSPort *)sendPort;

/* THE SERVICE SIDE: make a connection that answers for `rootObject`, and publish it under `name`. */
+ (nullable NSConnection *)serviceConnectionWithName:(NSString *)name
					  rootObject:(id)rootObject
				     usingNameServer:(NSPortNameServer *)server;
+ (nullable NSConnection *)serviceConnectionWithName:(NSString *)name rootObject:(id)rootObject;

/* THE CLIENT SIDE: find a published connection by name and answer a proxy for its root object. */
+ (nullable id)rootProxyForConnectionWithRegisteredName:(NSString *)name
						   host:(nullable NSString *)hostName
					usingNameServer:(NSPortNameServer *)server;
+ (nullable id)rootProxyForConnectionWithRegisteredName:(NSString *)name
						   host:(nullable NSString *)hostName;

- (instancetype)initWithReceivePort:(nullable NSPort *)receivePort sendPort:(nullable NSPort *)sendPort;

- (nullable NSPort *)receivePort;
- (nullable NSPort *)sendPort;

- (BOOL)registerName:(NSString *)name withNameServer:(NSPortNameServer *)server;
- (BOOL)registerName:(NSString *)name;

- (nullable id)rootObject;
- (void)setRootObject:(nullable id)anObject;

/* The client's proxy for the far root object. */
- (nullable NSDistantObject *)rootProxy;

- (void)addRunLoop:(NSRunLoop *)runLoop;
- (void)removeRunLoop:(NSRunLoop *)runLoop;

- (void)invalidate;
- (BOOL)isValid;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCONNECTION_H */
