/*
 * CGPath_internal — THE PATH OPERATIONS THAT ARE OURS NOW, and the reason they are not in CGPath.h.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THREE FUNCTIONS STOOD IN THE PUBLIC HEADER UNTIL 2026-10-05 AND ARE INTERNAL HERE, WHICH IS A
 * STATEMENT ABOUT THE ERA RATHER THAN ABOUT THE CODE. `CGPathCreateCopyByStrokingPath` and
 * `CGPathCreateCopyByDashingPath` are macOS 10.7 and `CGPathCreateCopyByFlattening` is 13.0; this
 * duplication is a 10.6-era surface, so a caller of that era cannot name them.
 *
 * AND THEY CANNOT SIMPLY BE DELETED, WHICH IS WHY THEY ARE HERE RATHER THAN GONE: THE 10.6-ERA
 * VERBS ARE BUILT ON THEM. `CGContextStrokePath` (10.0) is the stroker, `CGContextSetLineDash`
 * (10.0) is the dasher, `CGContextClip` and `CGPathGetPathBoundingBox` flatten, and the AppKit's
 * `-containsPoint:` (NSBezierPath) is a crossing count over a flattened path. Removing the
 * machinery would have removed working 10.6 behaviour along with a name.
 *
 * SO THE RULE THIS FILE ENFORCES IS THE ONE THE LEDGER USES: what is `shipped` is what a PUBLIC
 * header declares, and an Apple name that no public header declares is not part of this surface.
 * These three are `cg_`-prefixed like every other internal helper in the library (CGPaint.c's
 * `cg_paint_*`, CGColorSpace.c's `cg_colorspace_*`), which is also what keeps them out of any
 * caller's namespace.
 *
 * THE CALLERS STILL SPELL THE APPLE NAMES, AND THAT IS DELIBERATE AND VISIBLE: each of the five
 * translation units that use them carries a three-line RENAME TABLE at the top, so the call site
 * and the definition read as one thing and a reader who greps for the old name finds the table
 * with its reason on it. What matters for the surface is the COMPILED name, and `nm` on the built
 * library is where that is checked: no `CGPathCreateCopyBy…` symbol is exported.
 */
#ifndef CORE_GRAPHICS_CGPATH_INTERNAL_H
#define CORE_GRAPHICS_CGPATH_INTERNAL_H

#include <CoreGraphics/CGPath.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A flattened copy of `path` — every curve replaced by lines within `flatness` of it. The
 * flattener is the adaptive one (de Casteljau at the midpoint, recursing until the control points
 * are within the tolerance of the chord) and the tolerance is in USER space, divided by the CTM's
 * scale by the callers that have one. */
CGPathRef cg_path_create_flattened_copy(CGPathRef path, CGFloat flatness);

/* The OUTLINE of `path` as a fillable path: caps, joins, the miter limit and the line width
 * applied. The result is a set of overlapping ORIENTED pieces and must be filled NON-ZERO — that
 * deviation is stated in CGPath.h where the public stroking semantics live. */
CGPathRef cg_path_create_stroked_copy(CGPathRef path, const CGAffineTransform *transform,
				      CGFloat lineWidth, CGLineCap lineCap, CGLineJoin lineJoin,
				      CGFloat miterLimit);

/* `path` broken into the dashes `lengths` describes, with the phase applied first. An odd element
 * count is DOUBLED, a total of zero is a SOLID line and a negative length is read as its
 * magnitude — the three cases this tree decided because Apple's page leaves them open. */
CGPathRef cg_path_create_dashed_copy(CGPathRef path, const CGAffineTransform *transform,
				     CGFloat phase, const CGFloat *lengths, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGPATH_INTERNAL_H */
