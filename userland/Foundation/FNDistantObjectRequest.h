/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNDistantObjectRequest.h — THE SEAM BETWEEN A CONNECTION AND THE REQUESTS IT HANDS OUT (§62.91). INTERNAL.
 *
 * TWO things live here that are not public API, and both exist because the two classes must cooperate without
 * either reaching into the other: the connection MAKES a request (Apple forbids a caller doing so), and the
 * request sends its answer THROUGH the connection (the reply goes to a name, which only the connection knows how
 * to reach). A public initialiser would be an API Apple says not to use; the alternative — one class reading the
 * other's storage — is the mistake §62.79 recorded.
 */

#import <Foundation/NSObjCRuntime.h>
/* THE CATEGORY BELOW NAMES `NSConnection`, AND A CATEGORY NEEDS THE CLASS ITSELF — a forward declaration
 * defines no category (§62.91's first compile said so). No cycle: NSConnection.h does not import this. */
#import <Foundation/NSConnection.h>

NS_ASSUME_NONNULL_BEGIN

@class NSException;
@class NSInvocation;
@class NSDistantObjectRequest;

@interface NSDistantObjectRequest (FNPrivate)

/* MADE BY THE CONNECTION. `conversation` may be nil (a connection whose delegate never made one has none to
 * name), and `replyName` is where the answer must go. */
- (instancetype)fnInitWithConnection:(NSConnection *)connection
			conversation:(nullable id)conversation
			  invocation:(NSInvocation *)invocation
			   replyName:(NSString *)replyName;
@end

@interface NSConnection (FNPrivate)

/* ONE PLACE THAT SENDS AN ANSWER, used by both the ordinary path and a delegate's `-replyWithException:` — so
 * the wire has one shape rather than two that could drift. */
- (void)fnReplyToName:(NSString *)replyName
		value:(nullable id)value
		error:(nullable NSString *)error
	    exception:(nullable NSException *)exception;
@end

NS_ASSUME_NONNULL_END
