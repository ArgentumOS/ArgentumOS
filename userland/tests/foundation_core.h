/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_core — the F0 acceptance probe for the Foundation
 * (docs/design/foundation-plan.md §5).
 *
 * TWO TRANSLATION UNITS, and they are also the two OWNERSHIP regimes, which is
 * the point of F0:
 *
 *   foundation_core_support.m  MRR   Counter, and the lifetime exercises
 *   foundation_core.m          ARC   the identity/pool checks and main
 *
 * [retain]/[release]/[autorelease] cannot even be SENT from an ARC file, so the
 * MRR half is exposed as C functions and the ARC half calls them — the seam the
 * library itself lives on.
 */

#ifndef FOUNDATION_CORE_H
#define FOUNDATION_CORE_H

#import <foundation/Foundation.h>

@interface Counter : NSObject
{
	int _value;
}
- (int)value;
- (void)setValue:(int)v;
- (int)marker;			/* defined in the support unit */
@end

/* --- the MRR half (support unit) --------------------------------------- */

/* How many Counter objects have been deallocated so far. */
int foundation_core_deallocs(void);

/* alloc -> init -> retain -> release -> release reaches -dealloc exactly once. */
int foundation_core_lifecycle(void);

/* isEqual: is identity and hash is the pointer, for a fresh object. */
int foundation_core_equality(void);

/*
 * THE FORWARDING PROBES (stage F, second half). TWO classes, because forwarding
 * has two paths and they are wired differently:
 *
 *   FastForwarder overrides -forwardingTargetForSelector:, which the runtime
 *   consults BEFORE it hands a call over (objc_proxy_lookup) — the message is
 *   re-looked-up on the real object and NO invocation is built at all.
 *
 *   SlowForwarder overrides -forwardInvocation:, so the call arrives as an
 *   NSInvocation whose arguments were captured from the register file (that is
 *   what ninvoke_amd64.S is for). It re-invokes on a real Counter, which is what
 *   "-forwardInvocation: with arguments" has to mean.
 *
 * The forwarded methods are DECLARED here and NOT implemented: that is the point
 * of the classes, and the declaration is also what registers the selector's type,
 * which is how -methodSignatureForSelector: can answer for them.
 */
@interface FastForwarder : NSObject
{
	Counter *_backing;		/* the object it forwards to */
}
- (int)marker;			/* forwarded, not implemented */
@end

@interface SlowForwarder : NSObject
{
	Counter *_backing;
	unsigned long _forwarded;
}
- (int)value;			/* forwarded, not implemented */
- (void)setValue:(int)v;	/* forwarded, not implemented */
- (unsigned long)forwardedCount;
@end

#endif /* FOUNDATION_CORE_H */
