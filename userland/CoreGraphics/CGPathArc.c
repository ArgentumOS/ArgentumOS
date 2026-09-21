/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPathArc.c — the arcs, the ellipses and the rounded rectangles: the shapes that are
 * CURVES WITH A FORMULA rather than a curve someone typed in.
 *
 * EVERYTHING HERE IS CUBICS. An arc of at most a quarter turn is ONE cubic that touches
 * the arc at both ends and stays within ~0.03% of it, so a sweep is cut into segments no
 * bigger than that and each segment becomes one cubic whose control points lie on the
 * TANGENTS at its ends, 4/3·tan(θ/4) of the way along them. A wider arc is not more
 * accurate for being one cubic; it is less.
 *
 * THE ELLIPSE CASE IS THE SAME CONSTRUCTION WITH ITS OWN DERIVATIVE. For a circle the
 * tangent at parameter t is r·(−sin t, cos t); for an ellipse with radii rx and ry it is
 * (−rx·sin t, ry·cos t). Using that instead of squashing a circle afterwards is what makes
 * `CGPathAddRoundedRect`'s TWO corner radii honest — and an affine transform of the result
 * is still exact, because an affine map of a Bézier's control points IS the transformed
 * Bézier. That is why every function here passes its `m` through to `CGPathAddCurveToPoint`
 * rather than transforming afterwards.
 *
 * A NOTE ON `CGPathAddArcToPoint`, WHICH IS NOT HERE: it is the "round off this corner"
 * form, it is a different construction (tangent lines to a circle that fits between two
 * segments), and it is not written yet. It is not held back by a design question, and it
 * will arrive the same way this file did.
 */
#include <CoreGraphics/CGPath.h>

#include <math.h>

/* M_PI IS NOT PORTABLE ACROSS THE TWO COMPILERS THIS FILE IS BUILT BY (the host's glibc
 * and the guest's musl), so the constant is spelled out — the same call CGPathStroke.c
 * makes for the same reason. */
#define CG_ARC_PI 3.14159265358979323846

/*
 * ONE ELLIPTICAL ARC, AS `segments` CUBICS.
 *
 * `move_first` IS NOT DECORATION: an ellipse is four arcs that form ONE subpath, while an
 * arc a caller appends to a path continues whatever subpath is open. The flag is the whole
 * difference between the two, and getting it wrong turns an ellipse into four filled
 * quarter-pies — which is a shape, just not the one that was asked for.
 */
static void cg_arc_ellipse(CGMutablePathRef path, const CGAffineTransform *m, double cx,
			   double cy, double rx, double ry, double a0, double sweep, int move_first)
{
	int segments;
	double step;
	int i;

	if (sweep == 0.0 || rx == 0.0 || ry == 0.0) {
		return;
	}
	segments = (int)ceil(fabs(sweep) / (CG_ARC_PI / 2.0));
	if (segments < 1) {
		segments = 1;
	}
	step = sweep / segments;
	if (move_first) {
		CGPathMoveToPoint(path, m, cx + rx * cos(a0), cy + ry * sin(a0));
	} else {
		CGPathAddLineToPoint(path, m, cx + rx * cos(a0), cy + ry * sin(a0));
	}
	for (i = 0; i < segments; i++) {
		double t0 = a0 + step * i;
		double t1 = t0 + step;
		/* The control distance: 4/3·tan(θ/4), whose SIGN carries the direction, so a
		 * clockwise segment needs no second formula. */
		double k = 4.0 / 3.0 * tan(step / 4.0);
		double x0 = cx + rx * cos(t0);
		double y0 = cy + ry * sin(t0);
		double x1 = cx + rx * cos(t1);
		double y1 = cy + ry * sin(t1);

		CGPathAddCurveToPoint(path, m, x0 + k * (-rx * sin(t0)), y0 + k * (ry * cos(t0)),
				      x1 - k * (-rx * sin(t1)), y1 - k * (ry * cos(t1)), x1, y1);
	}
}

void CGPathAddArc(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y,
		  CGFloat radius, CGFloat startAngle, CGFloat endAngle, bool clockwise)
{
	double sweep = (double)endAngle - (double)startAngle;

	if (path == NULL) {
		return;
	}
	/* `clockwise` IS IN THE PATH'S OWN SPACE, so a positive sweep is counter-clockwise
	 * there — which on a bitmap context, whose user space has y upward, is counter-
	 * clockwise on the screen as well. The sweep is brought into [0, 2π) or (−2π, 0]
	 * according to the flag, and EQUAL ANGLES ARE A ZERO SWEEP THAT ADDS NOTHING: a
	 * statement of this tree's, because Apple's page does not define the case, and a caller
	 * who wants a whole circle has `CGPathAddEllipseInRect`. */
	if (clockwise) {
		while (sweep > 0.0) {
			sweep -= 2.0 * CG_ARC_PI;
		}
	} else {
		while (sweep < 0.0) {
			sweep += 2.0 * CG_ARC_PI;
		}
	}
	if (sweep == 0.0) {
		return;
	}
	/* AN ARC ON AN EMPTY PATH BEGINS A SUBPATH; AN ARC ON A PATH THAT HAS A CURRENT POINT
	 * CONTINUES IT, with a line from that point. The first half is not a detail — MEASURED:
	 * without it, `CGPathAddArc` on an empty path emitted a LINE FROM THE ORIGIN, which is
	 * what `CGPathAddLineToPoint` does with no preceding move, and a quarter pie came out
	 * 41 px² instead of 28 because the triangle from the origin was filled with it. The
	 * second half is the reading that makes "line, then round the corner, then line" mean
	 * what a caller thinks, and it is the reading `CGPathAddArcToPoint` will need. */
	cg_arc_ellipse(path, m, x, y, radius, radius, startAngle, sweep, CGPathIsEmpty(path));
}

void CGPathAddEllipseInRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect)
{
	double cx, cy, rx, ry;

	if (path == NULL) {
		return;
	}
	rect = CGRectStandardize(rect);
	cx = rect.origin.x + rect.size.width / 2.0;
	cy = rect.origin.y + rect.size.height / 2.0;
	rx = rect.size.width / 2.0;
	ry = rect.size.height / 2.0;
	if (rx <= 0.0 || ry <= 0.0) {
		return;
	}
	/* ONE SUBPATH OF FOUR ARCS, starting at angle 0 — the point (cx + rx, cy) — and the
	 * first of them moves, which is what makes it one subpath rather than four. */
	cg_arc_ellipse(path, m, cx, cy, rx, ry, 0.0, 2.0 * CG_ARC_PI, 1);
}

CGPathRef CGPathCreateWithEllipseInRect(CGRect rect, const CGAffineTransform *m)
{
	CGMutablePathRef path = CGPathCreateMutable();

	if (path != NULL) {
		CGPathAddEllipseInRect(path, m, rect);
	}
	return (CGPathRef)path;
}

void CGPathAddRoundedRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect,
			  CGFloat cornerWidth, CGFloat cornerHeight)
{
	double x0, y0, x1, y1, rx, ry;

	if (path == NULL) {
		return;
	}
	rect = CGRectStandardize(rect);
	x0 = rect.origin.x;
	y0 = rect.origin.y;
	x1 = x0 + rect.size.width;
	y1 = y0 + rect.size.height;
	rx = cornerWidth < 0 ? -cornerWidth : cornerWidth;
	ry = cornerHeight < 0 ? -cornerHeight : cornerHeight;
	/* THE RADII ARE CLAMPED TO HALF THE RECTANGLE, AND THAT IS THIS TREE'S STATEMENT:
	 * Apple's page does not say what a radius larger than the rectangle means, and the
	 * unclamped answer is four arcs that overlap each other — corners turned inside out.
	 * Half is exactly where the four corners meet and no further. */
	if (rx > rect.size.width / 2.0) {
		rx = rect.size.width / 2.0;
	}
	if (ry > rect.size.height / 2.0) {
		ry = rect.size.height / 2.0;
	}
	/* A ZERO RADIUS IS THE RECTANGLE, which is the one case here with no arc in it at
	 * all. */
	if (rx <= 0.0 || ry <= 0.0) {
		CGPathAddRect(path, m, rect);
		return;
	}
	/* CLOCKWISE IN USER SPACE, FROM THE BOTTOM-LEFT CORNER: the bottom edge, the
	 * bottom-right corner, the right edge, and so on round. Each corner is a quarter arc
	 * centred one radius in from the corner. */
	CGPathMoveToPoint(path, m, x0 + rx, y0);
	CGPathAddLineToPoint(path, m, x1 - rx, y0);
	cg_arc_ellipse(path, m, x1 - rx, y0 + ry, rx, ry, -CG_ARC_PI / 2.0, CG_ARC_PI / 2.0, 0);
	CGPathAddLineToPoint(path, m, x1, y1 - ry);
	cg_arc_ellipse(path, m, x1 - rx, y1 - ry, rx, ry, 0.0, CG_ARC_PI / 2.0, 0);
	CGPathAddLineToPoint(path, m, x0 + rx, y1);
	cg_arc_ellipse(path, m, x0 + rx, y1 - ry, rx, ry, CG_ARC_PI / 2.0, CG_ARC_PI / 2.0, 0);
	CGPathAddLineToPoint(path, m, x0, y0 + ry);
	cg_arc_ellipse(path, m, x0 + rx, y0 + ry, rx, ry, CG_ARC_PI, CG_ARC_PI / 2.0, 0);
	CGPathCloseSubpath(path);
}

CGPathRef CGPathCreateWithRoundedRect(CGRect rect, CGFloat cornerWidth, CGFloat cornerHeight,
				      const CGAffineTransform *m)
{
	CGMutablePathRef path = CGPathCreateMutable();

	if (path != NULL) {
		CGPathAddRoundedRect(path, m, rect, cornerWidth, cornerHeight);
	}
	return (CGPathRef)path;
}
