/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAutoreleasePool.h — the boundary of an object-lifetime region (W2h, §14).
 *
 * THE POOL IS A STACK ENTRY, and the runtime already implements the stack: `-init` pushes and
 * `-drain` pops. This class is the boundary a program can SEE.
 *
 * THREE THINGS IT IS NOT, named rather than discovered: `+addObject:` is DEPRECATED by Apple and
 * therefore out (§11.5); `-retain` is REFUSED ("Cannot retain an autorelease pool" — a retainable
 * pool would outlive the region whose objects it holds); and a SECOND `-drain` or `-release` is a
 * NO-OP, because popping the runtime's stack twice would take apart whatever pushed next.
 */
#ifndef FOUNDATION_NSAUTORELEASEPOOL_H
#define FOUNDATION_NSAUTORELEASEPOOL_H

#import <foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSAutoreleasePool : NSObject
{
	void *_token;		/* the runtime's pool handle, from the push */
	BOOL _discarded;	/* a pool pops ONCE, however it is ended */
}

- (void)drain;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSAUTORELEASEPOOL_H */
