/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPath.h — the path: subpaths of lines and CURVES, the walker, and the flattener.
 *
 * WHAT IS HERE NOW: move, line, RECTANGLE, quadratic and cubic curves, close, the ARC
 * family (`CGPathAddArc`, `CGPathAddEllipseInRect`, `CGPathAddRoundedRect` and the two
 * `CGPathCreateWith…` forms) — plus `CGPathCreateCopyByFlattening`, which is how a curve
 * becomes the lines everything else in this tree draws. THE CURVE CONSTRUCTORS USED TO BE
 * ABSENT ON PURPOSE and the paragraph that said so is gone, because they are here: the
 * reason for waiting was that a path that silently flattened a curve wrongly is worse than
 * one that cannot be built, and the answer was to keep the CONTROL POINTS in the path — so
 * a caller keeps their flatness choice, and the two bounding boxes below can honestly
 * differ. The arcs came the same way, built ON those cubics.
 *
 * WHAT IS STILL ABSENT: `CGPathAddArcToPoint`, the "round off this corner between two
 * segments" form. It is a different construction — tangent lines to a circle that fits
 * between them — rather than a design question, and it is not declared until it works,
 * which is this header's rule.
 *
 * A NOTE ON THIS HEADER'S PROVENANCE, because it is unusual for this tree: the C0
 * ledger (docs/reference/coregraphics-apple-surface.txt) DOES NOT CONTAIN the
 * CGMutablePath family. `CGPathCreateMutable`, `CGPathMoveToPoint`,
 * `CGPathAddLineToPoint`, `CGPathCloseSubpath` and `CGMutablePathRef` have no rows in
 * it at all, while the COPYING family (`CGPathCreateMutableCopy`, …) does. Likewise
 * `CGPathAddCurveToPoint` and `CGPathAddQuadCurveToPoint` have no rows. MEASURED
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
 *
 * AND IT HAPPENED AGAIN, WITH `bool`: `CGPathAddArc`'s `clockwise` is a `bool`, and this
 * header named the type without `<stdbool.h>` — so the arc constructor's own header failed
 * to compile in every translation unit that had not happened to include it first. The
 * lesson is the same one, which is the point of writing it down twice: a header that names
 * a type includes the header that defines it, and the SECOND time is what proves it was a
 * lesson rather than an accident.
 */
#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>
#include <stdbool.h>

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

/* Building. `m` is applied to the point (or to the rectangle's four corners) BEFORE it is
 * added; a NULL `m` adds it unchanged. A LINE OR A CURVE WITH NO PRECEDING MOVE starts at
 * the origin, which is `CGPathGetCurrentPoint`'s statement about an empty path. */
void CGPathMoveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y);
void CGPathAddLineToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y);
/* The control point of a quadratic, then its endpoint. */
void CGPathAddQuadCurveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat cpx,
			       CGFloat cpy, CGFloat x, CGFloat y);
/* Two control points, then the endpoint, in drawing order. */
void CGPathAddCurveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat cp1x,
			   CGFloat cp1y, CGFloat cp2x, CGFloat cp2y, CGFloat x, CGFloat y);
void CGPathAddRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect);
void CGPathCloseSubpath(CGMutablePathRef path);

/* THE ARC FAMILY, ALL OF IT BUILT ON CUBICS (CGPathArc.c). `clockwise` IS IN THE PATH'S OWN
 * SPACE — a positive sweep is counter-clockwise there — and EQUAL ANGLES ADD NOTHING, which
 * is this tree's statement about a case Apple's page does not define. An arc CONTINUES the
 * open subpath (with a line from the current point when there is one), while an ellipse or
 * a rounded rectangle STARTS A SUBPATH of its own; a caller who wants either behaviour the
 * other way moves first.
 *
 * `CGPathAddRoundedRect` CLAMPS its two radii to half the rectangle, also this tree's
 * statement: an unclamped radius means arcs that overlap each other and corners turned
 * inside out, and half is exactly where the four corners meet. A zero radius is the plain
 * rectangle. */
void CGPathAddArc(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y,
		  CGFloat radius, CGFloat startAngle, CGFloat endAngle, bool clockwise);
void CGPathAddEllipseInRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect);
CGPathRef CGPathCreateWithEllipseInRect(CGRect rect, const CGAffineTransform *m);
void CGPathAddRoundedRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect,
			  CGFloat cornerWidth, CGFloat cornerHeight);
CGPathRef CGPathCreateWithRoundedRect(CGRect rect, CGFloat cornerWidth, CGFloat cornerHeight,
				      const CGAffineTransform *m);

/*
 * FLATTENING: THE CURVES AS LINES, AND THE ONE PLACE THAT DECISION IS MADE.
 *
 * `flatness` is the greatest distance a line is allowed to stray from the curve it
 * replaces, in the path's own units. A value of zero or less — or the number a caller who
 * does not want to think about it passes — means `0.1`, which is this tree's answer because
 * Apple's page does not define the non-positive case.
 *
 * IT IS PUBLIC BECAUSE BOTH BOXES AND BOTH CONSUMERS USE IT: `CGPathGetPathBoundingBox` is
 * this path's box, the fill in CGContext.c and the stroker in CGPathStroke.c flatten through
 * it too. ONE SUBDIVISION, IN ONE PLACE — a second one would be a second answer to "where
 * is this curve", and a bounding box that disagreed with the pixels is exactly the kind of
 * bug that survives every review.
 *
 * THE SUBDIVERSION IS ADAPTIVE (de Casteljau at the midpoint, recursing until the control
 * points are within the tolerance of the chord, to a depth limit that catches degenerate
 * chords). A fixed number of segments would be wrong for a small curve and wrong for a
 * large one, in opposite directions.
 */
CGPathRef CGPathCreateCopyByFlattening(CGPathRef path, CGFloat flatness);

/* Asking. `CGPathGetBoundingBox` INCLUDES THE CONTROL POINTS, so for a curve it is bigger
 * than the path; `CGPathGetPathBoundingBox` is the tight box of the curve itself, taken
 * from the flattened path so that it agrees with what will be drawn. For a line-only path
 * they are the same rectangle — which is what the C2 probe asserted — and for a cubic the
 * difference is one of the checks in the curve probe. */
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

/* Walking. `points` is an array owned by the caller of the applier and valid only for the
 * duration of the call: 1 point for a move, 1 for a line, 2 for a quadratic, 3 for a cubic,
 * 0 for a close — AND ALL FIVE ARRIVE, because the path keeps its curves. */
typedef enum {
	kCGPathElementMoveToPoint,
	kCGPathElementAddLineToPoint,
	kCGPathElementAddQuadCurveToPoint,
	kCGPathElementAddCurveToPoint,
	kCGPathElementCloseSubpath
} CGPathElementType;

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
