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

/*
 * SELF-CONTAINED, WHICH IT WAS NOT: `CGAffineTransform` appears in five of the
 * declarations below — `CGPathCreateWithRect`'s and the stroker's transform parameters —
 * and this header used to rely on its INCLUDERS including CGAffineTransform.h first.
 * CGPath.c and CGContext.h both happened to, so nothing failed until a new translation
 * unit (CGPathStroke.c) included CGPath.h alone and the compiler answered `unknown type
 * name 'CGAffineTransform'` in nine places at once. A header that names a type includes
 * the header that defines it.
 */
#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGAffineTransform.h>
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

/*
 * THE LINE STATE'S TYPES AND THE DRAWING MODE LIVE HERE, IN THE PATH'S HEADER, and that
 * is not an accident of convenience: the C0 ledger's own family column files all three
 * under "Opaque Types", which is CGPath's family, and `CGPathCreateCopyByStrokingPath`
 * below is the function that takes a cap and a join as parameters. Putting them here is
 * also what makes the includes work: CGContext.h includes CGPath.h (a context has a
 * path), so a caller including EITHER header sees all three types, and a cycle is
 * avoided. The case VALUES are this tree's, as everywhere Apple publishes names.
 */
typedef enum {
	kCGLineCapButt = 0,
	kCGLineCapRound = 1,
	kCGLineCapSquare = 2
} CGLineCap;

typedef enum {
	kCGLineJoinMiter = 0,
	kCGLineJoinRound = 1,
	kCGLineJoinBevel = 2
} CGLineJoin;

typedef enum {
	kCGPathFill = 0,
	kCGPathEOFill = 1,
	kCGPathStroke = 2,
	kCGPathFillStroke = 3,
	kCGPathEOFillStroke = 4
} CGPathDrawingMode;

/*
 * STROKING: THE OUTLINE OF A STROKE, AS A PATH.
 *
 * THE PATH THAT COMES BACK IS A SET OF ORIENTED PIECES — one quadrilateral per segment,
 * one wedge per join, one disc-fan per round cap — and ITS NON-ZERO FILL IS THE STROKE.
 * That is the whole design, and it is why there is no boolean union in this tree: every
 * piece is emitted with the SAME ORIENTATION, so wherever two pieces overlap the winding
 * number is 2 rather than 0, and the non-zero rule takes exactly their union.
 *
 * THE CONSEQUENCE A CALLER MUST KNOW: this path is for a NON-ZERO fill. Filling it
 * with the EVEN-ODD rule counts the overlaps as crossings and leaves HOLES where the
 * pieces cross — the corners of a closed subpath, the caps of a thick line. Apple's
 * returned path is an outline in the boolean sense and would tolerate either rule; this
 * one is a documented deviation (the plan's §9 records it), and `CGContextStrokePath`
 * and friends fill it the way it must be filled.
 *
 * `transform`, when not NULL, is applied to the path BEFORE stroking — so the line width
 * is in the TRANSFORMED space. That reading of Apple's parameter is this tree's
 * statement rather than a transcription: the SDK's headers are not readable here, and
 * the alternative (stroke first, transform the result) differs for any transform that
 * is not a similarity.
 */
CGPathRef CGPathCreateCopyByStrokingPath(CGPathRef path, const CGAffineTransform *transform,
					CGFloat lineWidth, CGLineCap lineCap, CGLineJoin lineJoin,
					CGFloat miterLimit);

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
