/*
 * NSPortCoder.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSPortCoder` — A CODER THAT GOES SOMEWHERE (§62.56). It is the object a distributed-objects message is built
 * with: objects are added to it, and `-dispatch` sends them.
 *
 * HOW IT CARRIES AN OBJECT, STATED BECAUSE IT IS THE WHOLE DESIGN: an object is written as a PROPERTY LIST and ONE
 * OBJECT BECOMES ONE COMPONENT of the message — and a component is `NSData`, which is what this library's
 * transport can carry (§62.53). Nothing here invents a wire format for objects; the property-list serialiser is the
 * format and the coder is the envelope.
 *
 * THE KINDS A MESSAGE CAN CARRY ARE THEREFORE THE KINDS A PROPERTY LIST CAN WRITE — strings, numbers, dates, data,
 * arrays and dictionaries — and anything else is REFUSED WITH ITS CLASS NAMED (a nil among them: a property list
 * has no way to write down an absence). THE FIRST VERSION USED THE KEYED ARCHIVER AND THE PROBE MEASURED IT OUT:
 * `+[NSKeyedArchiver unarchiveObjectWithData:]` is a STUB here, so a coder built on it built messages nothing
 * could read.
 *
 * TWO OF APPLE'S DOORS REFUSE, WITH GROUNDS THAT COME FROM THE UNIT THAT MADE THEM IMPOSSIBLE:
 *
 *   * `-encodePortObject:` and `-decodePortObject:` are REFUSED — raising, not returning nil quietly — because a
 *     port in a message means a port RIGHT, and this library's transport refuses a component that is not data
 *     (§62.53). A caller who needs to hand a port to another process needs a mechanism this system does not have.
 *   * NOTHING HERE AUTHENTICATES, which is stated rather than implied: `NSFailedAuthenticationException` exists
 *     because callers name it, and no connection in this library ever raises it. A connection is a socket pair
 *     this process already holds; authentication is a feature this library does not provide.
 *
 * `-isBycopy` ANSWERS YES AND `-isByref` NO, and that is a fact about the transport rather than a preference:
 * this library's distributed objects send COPIES, because there is nothing in a message that could stand for a
 * proxy.
 */

#ifndef FOUNDATION_NSPORTCODER_H
#define FOUNDATION_NSPORTCODER_H

#import <Foundation/NSCoder.h>

@class NSPort, NSMutableArray, NSArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSPortCoder : NSCoder
{
	NSPort *_receivePort;
	NSPort *_sendPort;
	NSMutableArray *_components;
	NSUInteger _index;		/* where a decoder has read up to */
	BOOL _isBycopy;
}

+ (nullable id)portCoderWithReceivePort:(nullable NSPort *)receivePort
			       sendPort:(nullable NSPort *)sendPort
			     components:(nullable NSArray *)components;

- (instancetype)initWithReceivePort:(nullable NSPort *)receivePort
			   sendPort:(nullable NSPort *)sendPort
			 components:(nullable NSArray *)components;

/* Adds one object to the message, or reads the next one out of it. */
- (void)encodeObject:(nullable id)anObject;
- (nullable id)decodeObject;

/* Sends what has been added. Answers NO when the port cannot carry it. */
- (void)dispatch;

- (BOOL)isBycopy;
- (BOOL)isByref;

/* REFUSED — see the header: a port cannot be a component of this library's messages. */
- (void)encodePortObject:(NSPort *)aPort;
- (nullable NSPort *)decodePortObject;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPORTCODER_H */
