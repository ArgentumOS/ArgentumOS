/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSDistantObjectRequest — A REQUEST A CONNECTION'S DELEGATE MAY ANSWER ITSELF (§62.91), and the last piece the
 * port family owed.
 *
 * **A REQUEST LIKE THIS IS MADE BY THE CONNECTION, NEVER BY A CALLER** — Apple says so in words ("you should
 * never create these objects directly"), and the shape follows: there is no public initialiser here, only the
 * four doors that describe the request and the one that answers it. A connection hands one to its delegate
 * through `-connection:handleRequest:`; a delegate that answers YES owns the reply.
 *
 * `-replyWithException:` IS THE ONLY DOOR THAT DOES ANYTHING, and its two arms are the whole feature:
 *   * **nil** — the reply is the INVOCATION'S RETURN VALUE, which is what the connection would have sent if the
 *     delegate had not intercepted it;
 *   * **an exception** — it CROSSES THE WIRE and is RAISED AT THE DESTINATION, which is why this unit began by
 *     giving `NSException` the `NSCoding` conformance Apple declares and this library was missing: the wire
 *     refuses an unarchivable object on the sending side, so an exception could not have crossed without it.
 *
 * AND A REQUEST THAT IS NEVER ANSWERED IS NOT AN ERROR: Apple's contract lets a delegate hold a request as long
 * as it likes, and a client that waits too long is told so ("the service did not answer") rather than hanging.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSConnection;
@class NSException;
@class NSInvocation;

@interface NSDistantObjectRequest : NSObject
{
@private
	id _connection;		/* NOT retained: the connection owns the request while it is being handled */
	id _conversation;	/* NOT retained, as the connection holds it too */
	id _invocation;		/* retained: the call being answered */
	id _replyName;		/* retained: WHERE the answer goes (a port cannot be carried, §62.56) */
	BOOL _replied;		/* one reply per request — see the note on the door */
}

/* THE REQUEST'S OWN FACTS: the connection it arrived on, the conversation it belongs to, and the call. */
- (NSConnection *)connection;
- (nullable id)conversation;
- (NSInvocation *)invocation;

/* ANSWER IT. See the file's note for the two arms — and note that answering TWICE is a programming error and
 * raises: a request has one reply, and a second would be a second answer to a question that was asked once. */
- (void)replyWithException:(nullable NSException *)exception;

@end

NS_ASSUME_NONNULL_END
