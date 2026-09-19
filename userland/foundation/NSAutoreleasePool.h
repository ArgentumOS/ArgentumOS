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
 * THE PRIVATE MARKER BELOW IS NOT OPTIONAL, and that is a measured fact rather than a rumour.
 * libobjc2's arc.mm looks this class up BY NAME and then asks whether it implements
 * `-_ARCCompatibleAutoreleasePool`: if it DOES, the runtime keeps its fast ARC pool path and never
 * instantiates this class; if it does NOT, the runtime switches to its legacy GNUstep-style path and
 * binds `+new`, `-release` and `+addObject:` on this class instead — and the first autorelease under
 * that path reaches an unimplemented `+addObject:` and HALTS THE GUEST with a kernel dump. That was
 * measured, twice, before the cause was read out of the runtime. The selector's signature is
 * irrelevant here; its PRESENCE is the whole contract, and `+addObject:` staying out is safe
 * precisely because this marker keeps that path from being taken.
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

- (void)_ARCCompatibleAutoreleasePool;	/* THE RUNTIME ASKS FOR THIS BY NAME — see above */
- (void)drain;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSAUTORELEASEPOOL_H */
