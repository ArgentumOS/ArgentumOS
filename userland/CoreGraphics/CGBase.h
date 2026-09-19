/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGBase.h — the base of this tree's CoreGraphics, and it is FIRST-PARTY.
 *
 * WHY THIS EXISTS AT ALL (user's decision, 2026-09-18): Apple's Foundation declares six
 * conversions between its geometry and CoreGraphics' — NSPointFromCGPoint and friends — plus
 * the macro that says the two type sets are identical. Those seven ledger rows cannot ship
 * without CG's VALUE TYPES, and the types are published interface: structs and a typedef,
 * no implementation to take. So this tree defines them, under Apple's spelling, in
 * `userland/CoreGraphics/` — which `-Iuserland` makes visible to every userland compile and
 * the kernel (which uses `-Iinclude` only) never sees.
 *
 * WHAT IS *NOT* HERE, and the line is deliberate: this is the VALUE TYPES only. CG's
 * function surface (CGPointMake, CGRectGetMinX, the affine transforms, CGColor, the drawing
 * contexts and everything they imply) is CG's own API and stays out — §12.6's rule that a
 * dependency is added rather than refused applies to it separately, and much of the drawing
 * half is already answered elsewhere in this tree by X11/Xfb.
 */
#ifndef CORE_GRAPHICS_CGBASE_H
#define CORE_GRAPHICS_CGBASE_H

/*
 * CGFloat IS DOUBLE ON 64-BIT, which is what Apple's is under __LP64__, and this tree is
 * 64-bit only (no 32-bit compatibility exists here at all).
 */
typedef double CGFloat;

#define CG_INLINE static inline
#ifndef CG_EXTERN
#define CG_EXTERN extern
#endif

#endif /* CORE_GRAPHICS_CGBASE_H */
