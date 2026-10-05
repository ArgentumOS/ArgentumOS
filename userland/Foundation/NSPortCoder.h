/*
 * NSPortCoder.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSPortCoder` — A CODER THAT GOES SOMEWHERE (§62.56). It is the object a distributed-objects message is built
 * with: objects are added to it, and `-dispatch` sends them.
 *
 * HOW IT CARRIES AN OBJECT, STATED BECAUSE IT IS THE WHOLE DESIGN: an object is written by the KEYED ARCHIVER and
 * ONE OBJECT BECOMES ONE COMPONENT of the message — and a component is `NSData`, which is what this library's
 * transport can carry (§62.53). Nothing here invents a wire format for objects; the archive is the format and the
 * coder is the envelope.
 *
 * SO ANY `NSCoding` OBJECT CAN CROSS, and so can a nil COMPONENT, because the archive's objects table has a `$null`
 * slot at index 0 and the reader answers nil for it. A nil ARGUMENT is a different question and it is still refused
 * — by `NSConnection`, not here — because the message's crate is an array, and an array cannot hold a nil at all.
 * THE OBVIOUS QUESTION — "WHY NOT ALWAYS?" — HAS A MEASURED ANSWER WORTH THE PARAGRAPH, because this coder spent one
 * unit refusing what it now carries: its first version used the property list, on the strength of a probe that had
 * failed with the library's own words,
 *
 *     +[NSKeyedArchiver unarchiveObjectWithData:] is not implemented
 *
 * which was read as a stub in the library. §62.57 re-measured it: `+unarchiveObjectWithData:` is declared on
 * `NSKeyedUnarchiver` and implemented on it, the call had been made on `NSKeyedArchiver`, which does not declare it,
 * and the message was the library CORRECTLY naming the class it was sent to. The class in the message was the
 * diagnosis; the property-list carrier and the nil refusal were a boundary built on a wrong cause.
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
