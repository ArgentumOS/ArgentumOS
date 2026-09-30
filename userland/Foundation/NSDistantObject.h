/*
 * NSDistantObject.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSDistantObject` — A PROXY FOR AN OBJECT AT THE OTHER END OF A CONNECTION (§62.56).
 *
 * WHAT CROSSES A MESSAGE, STATED BECAUSE IT IS WHAT THE PROXY CAN DO: an object argument and an object result, and
 * NOTHING ELSE. A method whose arguments or result are numbers, structures or pointers is REFUSED BY THE PROXY —
 * raising, with the method named — because this library's coder carries OBJECTS (through its own archiver, §62.56's
 * `NSPortCoder`), and an encoder that accepted a `double` and sent its bytes would be inventing a wire format for
 * something the archiver already has one for.
 *
 * THE PROXY HAS TWO SHAPES, AND BOTH ARE APPLE'S: a proxy whose `target` is a LOCAL object forwards to it directly
 * (that is how a service hands out something it owns), and a proxy with no target sends the invocation over its
 * connection and waits for the reply — which it does by RUNNING THE RUN LOOP IN `NSConnectionReplyMode`, which is
 * what that mode is for.
 *
 * A CLIENT'S PROXY MUST BE TOLD ITS PROTOCOL — `-setProtocolForProxy:` — and that is a requirement rather than a
 * nicety: an invocation is built from a METHOD SIGNATURE, and nothing but the protocol can describe a method the
 * far side implements. Without one the proxy answers no signature and the runtime reports an unrecognised selector,
 * which is the honest answer to "call this method I have not described".
 *
 * A PROXY IS NOT ARCHIVABLE HERE. Apple's `+distantObjectWithCoder:` would rebuild one from bytes, and there is
 * nothing in those bytes that could re-establish a connection: the name server is per-process (§62.54), so a
 * connection cannot be rebuilt in another process from a message. The door is NOT DECLARED rather than declared and
 * hollow, and a caller who needs to pass a reference to a remote object passes its NAME and asks the name server.
 */

#ifndef FOUNDATION_NSDISTANTOBJECT_H
#define FOUNDATION_NSDISTANTOBJECT_H

#import <Foundation/NSProxy.h>

@class NSConnection, NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSDistantObject : NSProxy
{
	NSObject *_target;		/* the LOCAL object this stands for, or nil when it stands for the far one */
	/* RETAINED, WHICH THIS COMMENT GOT WRONG UNTIL §63.19: it said "not retained: a proxy does not keep its
	 * connection alive", while both initializers retain it and NSConnection's own -dealloc says "the proxy
	 * holds it (NSDistantObject.h says so)". A proxy without its transport is a proxy that cannot call, so the
	 * RETAIN is the code's answer and the comment is now the code's too. */
	NSConnection *_connection;
	Protocol *_protocol;		/* not retained — protocol objects belong to the runtime */
}

/* The LOCAL shape: `object` is in this process and calls go straight to it. */
- (instancetype)initWithLocal:(nullable id)object connection:(NSConnection *)connection;

/* The other shape: `target` nil means "the connection's root object on the far side", and calls travel. */
- (instancetype)initWithTarget:(nullable id)target connection:(NSConnection *)connection;

/* THE TWO CLASS-SIDE FACTORIES (§63.19), which are the initializers above with Apple's own spelling: each
 * answers an AUTORELEASED proxy, which is the house rule for a factory whose name begins with neither `alloc`,
 * `new` nor `copy`. They were the class's last two open rows beyond its coder door. */
+ (nullable id)proxyWithLocal:(nullable id)object connection:(NSConnection *)connection;
+ (nullable id)proxyWithTarget:(nullable id)target connection:(NSConnection *)connection;

- (nullable NSConnection *)connectionForProxy;
- (void)setProtocolForProxy:(nullable Protocol *)aProtocol;
- (nullable Protocol *)protocolForProxy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDISTANTOBJECT_H */
