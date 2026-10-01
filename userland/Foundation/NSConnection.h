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
#import <Foundation/NSDate.h>	/* NSTimeInterval, for -replyTimeout */

@class NSPort, NSPortNameServer, NSDistantObject, NSDistantObjectRequest, NSRunLoop, NSString;
@class NSConnection;	/* the protocol below names it, and the class comes after */


NS_ASSUME_NONNULL_BEGIN

/* THE CONNECTION'S DELEGATE — AND **TWO OF APPLE'S DOORS ARE DECLARED, THREE ARE ABSENT WITH THEIR GROUNDS**,
 * which is the same discipline this header already applies to `NSFailedAuthenticationException`:
 *   * `-connection:shouldMakeNewConnection:` is NOT declared: there are no PARENT AND CHILD connections here.
 *     A child connection exists when a named service is contacted and forms one; this library's connections are
 *     socket pairs the process already holds (§62.56), so there is no child to allow or refuse.
 *   * the two AUTHENTICATION doors are NOT declared: `NSFailedAuthenticationException` "EXISTS AND IS NEVER
 *     RAISED" for the same reason — nothing in this library authenticates.
 * The two that ARE here are the two this library consults. */
@protocol NSConnectionDelegate <NSObject>
@optional

/* THE INTERCEPTION POINT: return YES to say the delegate has taken responsibility for the request (it will call
 * `-replyWithException:` — now or later), or NO to let the connection serve it as if no delegate existed. */
- (BOOL)connection:(NSConnection *)connection handleRequest:(NSDistantObjectRequest *)doreq;

/* THE CONVERSATION'S TOKEN, made once per connection when the first request arrives. Apple's default is an
 * `NSObject` instance, and that is what this library uses when the delegate does not answer. */
- (id)createConversationForConnection:(NSConnection *)connection;

@end


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
	id _delegate;			/* NOT retained: the delegate owns the connection, as Apple's does */
	id _conversation;		/* made once, lazily, when a request first arrives */
	NSTimeInterval _replyTimeout;	/* seconds a call waits for an answer; read by the sender, see -replyTimeout */
}

+ (nullable NSConnection *)connectionWithReceivePort:(nullable NSPort *)receivePort
					    sendPort:(nullable NSPort *)sendPort;

/* EVERY LIVE CONNECTION — Apple's `+allConnections`. THE REGISTRY IS NON-OWNING, and that is a decision rather
 * than an oversight: a registry that retained its members would be a leak this class created, because a live
 * connection could never reach `-dealloc` while the registry held it. Each connection withdraws its own entry
 * when it is invalidated or deallocated, and the answer keeps only the entries still `-isValid`. */
+ (NSArray *)allConnections;

/* THE DELEGATE, NOT RETAINED — the connection is the thing a delegate usually owns, so keeping it alive would
 * be the cycle Apple's `weak` delegate exists to avoid. */
- (nullable id <NSConnectionDelegate>)delegate;
- (void)setDelegate:(nullable id <NSConnectionDelegate>)anObject;

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

/* THE CLIENT'S CONNECTION ITSELF — where `-rootProxyForConnectionWithRegisteredName:` answers the PROXY, this
 * stops one step earlier and answers the connection the proxy travels over. Apple's doors, and a name nobody
 * published answers nil. THE ONE DIFFERENCE FROM APPLE IS STATED rather than left to be found: Apple's connection
 * here is a CHILD of the current thread's default connection and shares its receive port, and this library has no
 * parent and child connections (see the delegate note above; §62.56), so the connection is self-contained — a
 * send port to the service and its own receive port, watched in the reply mode. */
+ (nullable NSConnection *)connectionWithRegisteredName:(NSString *)name
						   host:(nullable NSString *)hostName
					usingNameServer:(NSPortNameServer *)server;
+ (nullable NSConnection *)connectionWithRegisteredName:(NSString *)name
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

/* HOW LONG A CALL WAITS FOR AN ANSWER — Apple's `replyTimeout`, in seconds, and this library READS it: it bounds
 * the run-loop wait the sender below runs, so the door is used rather than merely stored. THE DEFAULT IS 60.0,
 * because Apple's "the maximum delay" is not expressible as a bounded run-loop pump; a non-positive value means
 * the same default. */
- (NSTimeInterval)replyTimeout;
- (void)setReplyTimeout:(NSTimeInterval)timeout;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCONNECTION_H */
