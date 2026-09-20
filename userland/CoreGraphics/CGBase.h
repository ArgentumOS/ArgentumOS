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
 * WHAT IS *NOT* HERE: this header is the VALUE TYPES only, and that was the right first step —
 * those seven ledger rows needed the types and nothing else, and CG's structs and a typedef are
 * published interface with no implementation to take.
 *
 * SUPERSEDED (2026-09, user's decision): CG's function surface and its DRAWING HALF are now to
 * be built, as a duplication of Apple's drawing API — docs/design/coregraphics-plan.md. The
 * earlier reasoning that "the drawing half is already answered elsewhere in this tree by
 * X11/Xfb" is what the plan KEEPS and what it changes: the display stays X11/Xfb, and CG's
 * drawing sits on top of it instead of replacing it. Do not read the paragraph below as the
 * current decision; it is this file's history.
 *
 * WHAT THE FIRST VERSION SAID, kept because its reasoning still binds the substrate: CG's
 * function surface (CGPointMake, CGRectGetMinX, the affine transforms, CGColor, the drawing
 * contexts and everything they imply) is CG's own API and stays out of THIS HEADER — §12.6's
 * rule that a dependency is added rather than refused applies to it separately, in the plan
 * named above.
 */
#ifndef CORE_GRAPHICS_CGBASE_H
#define CORE_GRAPHICS_CGBASE_H

/*
 * `<stddef.h>` FOR `size_t`, WHICH SEVERAL CG HEADERS TAKE — `CGBitmapContextCreate`'s
 * width, height and strides, `CGColorSpaceGetNumberOfComponents`. Apple's headers get
 * it transitively from CoreFoundation's; this tree has no CoreFoundation (the plan
 * retracted that, §1), so the header that IS the base states it instead of leaving
 * every other header to rediscover it — which is exactly what happened when C2's first
 * build failed on `unknown type name 'size_t'` in two headers at once, and then again on
 * `uint32_t` in `CGBitmapContextCreate`'s `bitmapInfo`. Both are stated here.
 */
#include <stddef.h>
#include <stdint.h>

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
