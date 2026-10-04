/*
 * NSProtocolChecker.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSProtocolChecker` — A PROXY THAT ANSWERS ONLY FOR ITS PROTOCOL (§62.55). Apple deprecated it with the port
 * family, and §62.24's policy put it back; it is the last self-contained class of that family.
 *
 * WHAT IT IS FOR: a server hands out a checker instead of its real object, so a client can call the methods the
 * protocol declares and NOTHING ELSE. The filter is the whole class — a selector outside the protocol never
 * reaches the target — and it is built on two things this library already has: `NSProxy` and the forwarding
 * machinery (`NSMethodSignature` + `NSInvocation`, whose two paths have been proved end to end).
 *
 * THE FILTER ASKS THE PROTOCOL ITSELF, through `protocol_getMethodDescription`, ONCE FOR A REQUIRED SELECTOR AND
 * ONCE FOR AN OPTIONAL ONE. That is worth stating because the usual shortcut — "does the target respond to it?" —
 * is not the same question: a target responds to plenty of methods its protocol never mentions, and forwarding one
 * of those is exactly what a checker exists to prevent.
 *
 * A REFUSED SELECTOR IS REFUSED AT THE DOOR THE RUNTIME ASKS, WHICH IS `-methodSignatureForSelector:` — AND IT
 * RAISES THERE RATHER THAN ANSWERING NIL. The nil answer is what Apple's shape implies, and MEASURING IT TOOK A
 * SIGBUS: this runtime's forwarding path does not survive a missing signature, so the probe's first run crashed at
 * exactly the refusal. Raising an `NSInvalidArgumentException` from that door is the same refusal reached before
 * the machinery that cannot carry it, and it is what a caller sees either way. `-target`'s own methods are never
 * invoked. An optional selector the target does not implement is refused the same way, because a checker is a VIEW
 * of what the target can actually do.
 *
 * ONE MEASURED FACT ABOUT THIS LIBRARY, FOUND BY THIS UNIT: `NSProxy` IS A ROOT CLASS WITH NO `-init`, so a
 * checker's initializer must not call `[super init]` — `+alloc` is the whole of construction for a proxy here.
 *
 * THE TARGET IS RETAINED, DELIBERATELY. Apple's documentation does not say, so it is ours to decide (§11.6.1 D2)
 * and the reason is the failure mode: a checker that forwarded into a deallocated target would be a proxy into
 * freed memory, which is worse than the cycle it avoids. A target that owns its own checker should let go of one
 * of the two references.
 */

#ifndef FOUNDATION_NSPROTOCOLCHECKER_H
#define FOUNDATION_NSPROTOCOLCHECKER_H

#import <Foundation/NSProxy.h>

@class NSObject;

NS_ASSUME_NONNULL_BEGIN

@interface NSProtocolChecker : NSProxy
{
	NSObject *_target;		/* retained: see the header */
	Protocol *_protocol;		/* not retained — protocol objects belong to the runtime */
}

+ (nullable id)protocolCheckerWithTarget:(NSObject *)anObject protocol:(Protocol *)aProtocol;
- (instancetype)initWithTarget:(NSObject *)anObject protocol:(Protocol *)aProtocol;

- (NSObject *)target;
- (Protocol *)protocol;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPROTOCOLCHECKER_H */
