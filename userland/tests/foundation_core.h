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

#endif /* FOUNDATION_CORE_H */
