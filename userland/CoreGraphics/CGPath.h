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
 * AND THE CORNER CAME TOO: `CGPathAddArcToPoint`, the "round off this corner between two
 * segments" form — a circle tangent to both legs, drawn between its two tangency points — so
 * this header no longer has a list of shapes it is waiting for. The rule it followed still
 * stands, and is worth stating once: NOTHING IS DECLARED UNTIL IT WORKS, which is why every
 * entry in this file arrived together with the check that shows it does.
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
 * declarations below, the stroker's among them —
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
/* !! `CGPathCreateWithRect` STOOD HERE AND WAS REMOVED (2026-10-05): it is macOS 10.7, out of
 * era, and it was four lines over `CGPathAddRect` (allocate, add, return) — so the era's
 * spelling is that pair, which is what a caller of this surface writes. */
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
void CGPathAddRoundedRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect,
			  CGFloat cornerWidth, CGFloat cornerHeight);
/* !! THE TWO `CGPathCreateWith…` FACTORIES STOOD HERE AND WERE REMOVED (2026-10-05):
 * `CGPathCreateWithEllipseInRect` is macOS 10.7 and `CGPathCreateWithRoundedRect` is 10.9, out of
 * era for this duplication. THEY WERE THIN WRAPPERS OVER THE TWO `Add…` FORMS DIRECTLY ABOVE —
 * allocate a mutable path, call the adder — so nothing was lost with them: a caller of the era
 * this surface targets writes `CGPathCreateMutable` + `CGPathAddEllipseInRect`, which is what they
 * did anyway. */

/* ROUND OFF THE CORNER AT (x1,y1) BETWEEN THE CURRENT POINT AND (x2,y2): a straight line to
 * the tangency point, then the arc to the second tangency point, WHICH IS WHERE THE PATH
 * CONTINUES. A ZERO RADIUS is a line to the corner (Apple's own statement). A radius too
 * large for the legs is REDUCED rather than clamped at the tangent point — this tree's
 * statement, and the same answer Cairo reaches — because a clamped tangent point leaves an
 * arc that touches neither leg. */
void CGPathAddArcToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x1, CGFloat y1,
			 CGFloat x2, CGFloat y2, CGFloat radius);

/* !! DASHING AND FLATTENING STOOD HERE, AND BOTH ARE INTERNAL NOW (2026-10-05). Their public
 * declarations were `CGPathCreateCopyByDashingPath` (macOS 10.7) and `CGPathCreateCopyByFlattening`
 * (13.0) — out of era for a 10.6-era surface — and the code lives in CGPath_internal.h as
 * `cg_path_create_dashed_copy` and `cg_path_create_flattened_copy`.
 *
 * NEITHER NAME WAS LOAD-BEARING FOR A CALLER OF THE ERA, AND BOTH PIECES OF MACHINERY ARE STILL
 * LOAD-BEARING FOR THIS LIBRARY, which is why they are internalised rather than deleted:
 *
 *   * DASHING IS REACHED THROUGH `CGContextSetLineDash` (10.0), the verb an application of the era
 *     calls; the dasher exists so that verb can draw. `lengths` is a cycle alternating ON and OFF
 *     from `phase`, the output is separate subpaths because the pens really do go up and down, and
 *     it is LINES because a dash boundary falls between points on a curve. Three cases this tree
 *     decided because Apple's page leaves them open: AN ODD COUNT IS DOUBLED (a cyclic pattern with
 *     an odd element count would fall out of step with its own alternation), A TOTAL LENGTH OF ZERO
 *     IS A SOLID LINE, and A NEGATIVE LENGTH IS READ AS ITS MAGNITUDE.
 *   * FLATTENING IS REACHED THROUGH `CGContextClip`, `CGPathGetPathBoundingBox`, the fill and the
 *     stroker, and through the AppKit's `-containsPoint:`. ONE SUBDIVISION, IN ONE PLACE — a
 *     second one would be a second answer to "where is this curve", and a box that disagreed with
 *     the pixels is the kind of bug that survives every review. The subdivision is ADAPTIVE (de
 *     Casteljau at the midpoint, recursing until the control points are within the tolerance of
 *     the chord, to a depth limit that catches degenerate chords); `flatness` of zero or less means
 *     0.1, this tree's answer because Apple's page leaves the non-positive case open.
 *
 * A CALLER OF THIS SURFACE WRITES `CGContextSetLineDash` AND `CGPathGetPathBoundingBox`, which is
 * what the era had. */

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
/* !! THE STROKING DECLARATION STOOD HERE AND IS INTERNAL NOW (2026-10-05):
 * `CGPathCreateCopyByStrokingPath` is macOS 10.7, out of era, and the code is
 * `cg_path_create_stroked_copy` in CGPath_internal.h. THE COMMENT ABOVE IS KEPT BECAUSE IT IS THE
 * BEHAVIOUR THE FUNCTION STILL HAS, and the ONE DEVIATION it states is the reason the requirement
 * is kept rather than deleted: the returned path is a set of overlapping ORIENTED pieces and must
 * be filled NON-ZERO — an even-odd fill of it is not the stroke, which a stroke that doubles back
 * shows, where the even-odd rule paints nothing at all. `CGContextStrokePath` (10.0) is the caller
 * that fills it that way, and THAT is the verb of this era. */

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

/* ------------------------------------------------------------------------- */
/* Copies, and the three questions a path can answer about itself             */
/* ------------------------------------------------------------------------- */

/* THE ELEMENTS ARE COPIED, THE BYTES ARE NOT SHARED, and the two doors differ only in what the result may be
 * used for: a copy is for reading, a MUTABLE copy is for carrying on from. Mutability is a TYPE here rather
 * than a flag — `CGMutablePathRef` is what the mutation doors take — which is Apple's own arrangement and the
 * reason an "immutable" path needs no runtime flag to protect it. */
CGPathRef CGPathCreateCopy(CGPathRef path);
CGMutablePathRef CGPathCreateMutableCopy(CGPathRef path);

/* EQUAL MEANS THE SAME ELEMENTS, not the same picture: two paths that draw one outline by different routes are
 * NOT equal, and this compares what Apple's page calls the path's elements — each element's TYPE, its POINT
 * COUNT and the coordinates it actually uses. The unused slots beyond `npts` are not part of an element, which
 * is why this is not a `memcmp`. */
bool CGPathEqualToPath(CGPathRef path1, CGPathRef path2);

/* IS THIS PATH A RECTANGLE, AND WHICH ONE? True for four axis-aligned corners that close — the shape
 * `CGPathAddRect` builds, however it was wound and from whichever corner it started — with the rectangle
 * written through `rect` when that is not NULL. Anything else is false, including a path that only LOOKS like
 * a rectangle on screen (a curve approximating one, or a fifth corner). */
bool CGPathIsRect(CGPathRef path, CGRect *rect);

/* IS THIS POINT INSIDE THE PATH, BY THE EVEN-ODD RULE OR THE NON-ZERO ONE? The two rules disagree exactly
 * where a path overlaps itself, which is the choice the parameter makes; and `m`, when given, is applied to
 * the path's points before the test — Apple's sentence — so a singular matrix collapses the path rather than
 * sending the question somewhere undefined. */
bool CGPathContainsPoint(CGPathRef path, const CGAffineTransform *m, CGPoint point, bool eoFill);

#ifdef __cplusplus
}
#endif

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGPathGetTypeID(void);

#endif /* CORE_GRAPHICS_CGPATH_H */
