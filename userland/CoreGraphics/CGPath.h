/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPath.h — the path: a sequence of subpaths of LINES, and the walker.
 *
 * WHAT C2 HAS AND WHAT IT DOES NOT. Lines only: move, line, rectangle, close. The
 * curve constructors (`CGPathAddCurveToPoint`, `CGPathAddQuadCurveToPoint`,
 * `CGPathAddArc`, `CGPathAddRoundedRect`, …) are deliberately ABSENT rather than
 * declared-and-ignored, because a path that silently flattened a curve wrongly is
 * worse than a symbol a caller cannot call: the drawing would be plausible and the
 * geometry would be a guess. They arrive in C3 with the flattener that makes them
 * real.
 *
 * A NOTE ON THIS HEADER'S PROVENANCE, because it is unusual for this tree: the C0
 * ledger (docs/reference/coregraphics-apple-surface.txt) DOES NOT CONTAIN the
 * CGMutablePath family. `CGPathCreateMutable`, `CGPathMoveToPoint`,
 * `CGPathAddLineToPoint`, `CGPathCloseSubpath` and `CGMutablePathRef` have no rows in
 * it at all, while the COPYING family (`CGPathCreateMutableCopy`, …) does. MEASURED
 * 2026-09-20 against Apple's own index: the CoreGraphics index JSON contains no node
 * titled `CGPathCreateMutable` — and no `method`-typed node either — so the gap is in
 * the source this ledger is built from, not in the sweep's kind filter. The names in
 * this file are Apple's, taken from their documentation, and the ledger simply cannot
 * see them to credit them.
 */
#ifndef CORE_GRAPHICS_CGPATH_H
#define CORE_GRAPHICS_CGPATH_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGGeometry.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Apple's own declaration: the immutable reference is CONST and the mutable one is
 * not, and the same struct backs both. That is why a `CGMutablePathRef` can be
 * passed where a `CGPathRef` is wanted and not the other way round — the const is the
 * whole enforcement. */
typedef const struct CGPath *CGPathRef;
typedef struct CGPath *CGMutablePathRef;

/* Creating. */
CGMutablePathRef CGPathCreateMutable(void);
CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform *m);
CGPathRef CGPathRetain(CGPathRef path);
void CGPathRelease(CGPathRef path);

/* Building. `m` is applied to the point (or to the rectangle's four corners) BEFORE
 * it is added; a NULL `m` adds it unchanged. */
void CGPathMoveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y);
void CGPathAddLineToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y);
void CGPathAddRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect);
void CGPathCloseSubpath(CGMutablePathRef path);

/* Asking. */
int CGPathIsEmpty(CGPathRef path);
CGPoint CGPathGetCurrentPoint(CGPathRef path);
CGRect CGPathGetBoundingBox(CGPathRef path);
CGRect CGPathGetPathBoundingBox(CGPathRef path);

/* Walking. THE TWO BOXES DIFFER FOR CURVES ONLY — `CGPathGetBoundingBox` includes a
 * curve's control points and `CGPathGetPathBoundingBox` is the tight box of the path
 * itself — so for the line-only paths C2 can build they are the same rectangle. Both
 * are implemented here because the difference is a promise this header makes to the
 * C3 that adds curves, not a detail it can defer. */
typedef enum {
	kCGPathElementMoveToPoint,
	kCGPathElementAddLineToPoint,
	kCGPathElementAddQuadCurveToPoint,
	kCGPathElementAddCurveToPoint,
	kCGPathElementCloseSubpath
} CGPathElementType;

/* `points` is an array owned by the caller of the applier and valid only for the
 * duration of the call: 1 point for a move, 1 for a line, 2 for a quad curve, 3 for a
 * cubic, 0 for a close. C2 only ever produces the first three of those cases. */
typedef struct CGPathElement {
	CGPathElementType type;
	CGPoint *points;
} CGPathElement;

typedef void (*CGPathApplierFunction)(void *info, const CGPathElement *element);

void CGPathApply(CGPathRef path, void *info, CGPathApplierFunction function);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGPATH_H */
