/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPathStroke.c — the stroker: a stroke becomes a PATH whose non-zero fill is the
 * stroke (C3).
 *
 * THE DESIGN, WHICH IS THE REASON THIS FILE IS SHORT: a stroke does not need a boolean
 * union of its pieces if every piece is emitted with the SAME ORIENTATION. Overlap then
 * has winding 2 instead of 0, and the NON-ZERO fill takes the union for free — the same
 * mechanism that makes two nested squares drawn the same way a solid square (see
 * CGContext.c). So the stroker emits quadrilaterals, joint wedges and cap fans, all
 * positively oriented, and the rasterizer C2 already has does the rest.
 *
 * WHAT EACH PIECE IS:
 *   * one QUADRILATERAL per segment, of the line's width, centred on the segment;
 *   * at every interior vertex — and at every vertex of a CLOSED subpath — a JOIN on the
 *     OUTER side: a BEVEL as the triangle between the two outer corners and the vertex;
 *     a MITER as the triangle out to the intersection of the two offset edges, FALLING
 *     BACK TO THE BEVEL when that intersection is further than the miter limit allows
 *     (also as a triangle — two shapes for one case would be two chances to disagree);
 *     a ROUND join as a fan around the vertex. The INNER side needs nothing: the two
 *     quadrilaterals already overlap there.
 *   * at the ends of an OPEN subpath, a CAP: butt adds nothing, square adds a
 *     width-by-half-width rectangle, round adds a half-disc fan. A closed subpath has
 *     NO caps, which is a property the probe checks by comparing a closed stroke under
 *     two different cap settings.
 *
 * ALL THE WORK IS DONE IN THE SPACE THE TRANSFORM PUTS THE PATH IN, and the pieces are
 * built with plain doubles: nothing here knows about pixman, fixed point, or a surface.
 * A stroked path is geometry; where it lands is the context's business.
 */
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGPath_internal.h>

/* THE RENAME TABLE — see CGPath.c for the argument in full: the three `CopyBy…` path operations
 * are internal as of 2026-10-05 because they are 10.7-13.0 API and this is a 10.6-era surface.
 * This file DEFINES the stroker and CALLS the flattener, so it needs both names. */
#define CGPathCreateCopyByStrokingPath cg_path_create_stroked_copy
#define CGPathCreateCopyByFlattening cg_path_create_flattened_copy

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* M_PI IS NOT PORTABLE ACROSS THE TWO COMPILERS THIS TREE USES for the same file (the
 * host's glibc and the guest's musl), so the constant is spelled out. */
#define CG_STROKE_PI 3.14159265358979323846
/* Steps per half turn for the round caps and joins: a quality choice, and the only one
 * in this file. Finer than the traffic needs, because a round cap that reads as a
 * polygon is a bug report about the rasterizer. */
#define CG_STROKE_ARC_STEPS 32

typedef struct {
	CGMutablePathRef out;
	double half;         /* lineWidth / 2 */
	CGLineCap cap;
	CGLineJoin join;
	double miter_limit;  /* miterLimit × half */
	CGAffineTransform ctm;
	int have_ctm;

	double *pts;         /* the current subpath, in stroking space */
	int n;
	int cap_pts;
	int closed;
} cg_stroke;

/* ------------------------------------------------------------------------- */
/* emitting a piece                                                          */
/* ------------------------------------------------------------------------- */

/*
 * EMIT ONE PIECE WITH A POSITIVE SIGNED AREA, reversing it if it came out negative.
 * This single function is what makes the union trick work: the quadrilateral of a
 * segment has the same handedness as its direction, and an outer wedge's handedness
 * FLIPS with the turn direction, so without this the pieces would wind against each
 * other and the overlaps would cancel into holes.
 */
static void cg_piece(CGMutablePathRef out, const double *p, int n)
{
	double area = 0.0;
	int i;

	for (i = 0; i < n; i++) {
		int j = (i + 1) % n;

		area += p[i * 2] * p[j * 2 + 1] - p[j * 2] * p[i * 2 + 1];
	}
	if (n < 3) {
		return;
	}
	CGPathMoveToPoint(out, NULL, p[0], p[1]);
	if (area >= 0.0) {
		for (i = 1; i < n; i++) {
			CGPathAddLineToPoint(out, NULL, p[i * 2], p[i * 2 + 1]);
		}
	} else {
		for (i = n - 1; i >= 1; i--) {
			CGPathAddLineToPoint(out, NULL, p[i * 2], p[i * 2 + 1]);
		}
	}
	CGPathCloseSubpath(out);
}

/* The unit LEFT normal of a → b, or 0 for a degenerate segment (which has no direction
 * and therefore no stroke: two identical points are a dot, not a line). */
static int cg_normal(double ax, double ay, double bx, double by, double *nx, double *ny, double *ux,
		     double *uy)
{
	double dx = bx - ax;
	double dy = by - ay;
	double len = sqrt(dx * dx + dy * dy);

	if (len == 0.0) {
		return 0;
	}
	*ux = dx / len;
	*uy = dy / len;
	*nx = -dy / len;
	*ny = dx / len;
	return 1;
}

/* ------------------------------------------------------------------------- */
/* the pieces                                                                */
/* ------------------------------------------------------------------------- */

static void cg_segment_quad(cg_stroke *st, double ax, double ay, double bx, double by, double ext_a,
			    double ext_b)
{
	double nx, ny, ux, uy;
	double a0x, a0y, b1x, b1y;
	double p[8];

	if (!cg_normal(ax, ay, bx, by, &nx, &ny, &ux, &uy)) {
		return;
	}
	/* THE SQUARE CAP IS AN EXTENSION OF THE SEGMENT'S OWN QUADRILATERAL, in the
	 * segment's direction — the same shape a butt cap would have, pushed out by half
	 * the width. */
	a0x = ax - ux * ext_a;
	a0y = ay - uy * ext_a;
	b1x = bx + ux * ext_b;
	b1y = by + uy * ext_b;

	p[0] = a0x + nx * st->half;
	p[1] = a0y + ny * st->half;
	p[2] = b1x + nx * st->half;
	p[3] = b1y + ny * st->half;
	p[4] = b1x - nx * st->half;
	p[5] = b1y - ny * st->half;
	p[6] = a0x - nx * st->half;
	p[7] = a0y - ny * st->half;
	cg_piece(st->out, p, 4);
}

static void cg_round_fan(cg_stroke *st, double cx, double cy, double a0, double a1, int with_centre)
{
	double p[(CG_STROKE_ARC_STEPS + 2) * 2];
	int steps = CG_STROKE_ARC_STEPS;
	double da = (a1 - a0) / steps;
	int n = 0;
	int i;

	if (with_centre) {
		p[0] = cx;
		p[1] = cy;
		n = 1;
	}
	for (i = 0; i <= steps; i++) {
		p[n * 2] = cx + st->half * cos(a0 + da * i);
		p[n * 2 + 1] = cy + st->half * sin(a0 + da * i);
		n++;
	}
	cg_piece(st->out, p, n);
}

static void cg_join(cg_stroke *st, double px, double py, double vx, double vy, double nx_, double ny_)
{
	double n1x, n1y, u1x, u1y;
	double n2x, n2y, u2x, u2y;
	double cross;
	double outer;
	double a1x, a1y, a2x, a2y;
	double p[6];

	if (!cg_normal(px, py, vx, vy, &n1x, &n1y, &u1x, &u1y)) {
		return;
	}
	if (!cg_normal(vx, vy, nx_, ny_, &n2x, &n2y, &u2x, &u2y)) {
		return;
	}
	cross = u1x * u2y - u1y * u2x;
	if (cross == 0.0) {
		/* Straight or doubling back: a doubling-back joint is covered by the two
		 * quadrilaterals, and this is where a naive stroker grows a spike. */
		return;
	}
	/* THE OUTER SIDE IS THE ONE THE PATH TURNS AWAY FROM. A left turn (cross > 0) puts it
	 * on the right, which is the LEFT normal negated. */
	outer = (cross > 0.0) ? -1.0 : 1.0;
	a1x = vx + outer * n1x * st->half;
	a1y = vy + outer * n1y * st->half;
	a2x = vx + outer * n2x * st->half;
	a2y = vy + outer * n2y * st->half;

	if (st->join == kCGLineJoinRound) {
		double a0 = atan2(a1y - vy, a1x - vx);
		double a1 = atan2(a2y - vy, a2x - vx);

		/* THE SHORT WAY AROUND, which for a join is the outer arc: the two outer
		 * corners are within a half turn of each other on that side by construction. */
		while (a1 - a0 > CG_STROKE_PI) {
			a1 -= 2.0 * CG_STROKE_PI;
		}
		while (a0 - a1 > CG_STROKE_PI) {
			a1 += 2.0 * CG_STROKE_PI;
		}
		cg_round_fan(st, vx, vy, a0, a1, 1);
		return;
	}
	if (st->join == kCGLineJoinMiter) {
		/* The intersection of the two OFFSET edges, by solving them as lines. */
		double ex = a2x - a1x;
		double ey = a2y - a1y;
		double den = u1x * u2y - u1y * u2x;

		if (den != 0.0) {
			double t = (ex * u2y - ey * u2x) / den;
			double mx = a1x + t * u1x;
			double my = a1y + t * u1y;

			/* THE MITER LIMIT IS APPLE'S DOCUMENTED FALLBACK, not a heuristic: past
			 * `miterLimit` half-widths from the vertex, the join is CLIPPED to a bevel
			 * rather than grown into a spike, and a path of many small turns otherwise
			 * grows spikes a screen long. */
			if (sqrt((mx - vx) * (mx - vx) + (my - vy) * (my - vy)) <= st->miter_limit) {
				/* THE MITER PIECE IS THE QUADRILATERAL (a1, m, a2, v). THE TRIPLE
				 * (a1, m, a2) IS NOT THE BEVEL PLUS A TIP — it is a sliver BESIDE a notch,
				 * and the union of the two triangles is the miter. MEASURED, and the
				 * measurement is what found it: with the triple, a miter join covered LESS
				 * than a bevel — 5929 against 6056 coverage units — because the bevel
				 * triangle (a1, v, a2) is 0.707·√2/2 ≈ 0.5 px² while the tip triangle is
				 * 0.293 px². The comment below used to say the bevel triangle was "strictly
				 * contained in the miter", which is exactly backwards. THIS SURFACED ONLY
				 * WHEN THE CROSSING SPLIT MADE THE SWEEP ACCURATE ENOUGH TO SEE IT — the
				 * unsplit sweep had been hiding the error behind its own. */
				double q[8];

				q[0] = a1x;
				q[1] = a1y;
				q[2] = mx;
				q[3] = my;
				q[4] = a2x;
				q[5] = a2y;
				q[6] = vx;
				q[7] = vy;
				cg_piece(st->out, q, 4);
				return;
			}
		}
	}
	/* BEVEL, and the miter's fallback: the triangle the two corners and the VERTEX, which
	 * fills the notch between the two quadrilaterals. It is NOT contained in the miter's
	 * tip triangle — they are complementary, and the miter is their union — which is the
	 * distinction the measurement above is about. */
	p[0] = a1x;
	p[1] = a1y;
	p[2] = a2x;
	p[3] = a2y;
	p[4] = vx;
	p[5] = vy;
	cg_piece(st->out, p, 3);
}

static void cg_cap(cg_stroke *st, double vx, double vy, double ux, double uy, int at_start)
{
	if (st->cap != kCGLineCapRound) {
		return;
	}
	/* THE OUTWARD DIRECTION IS OPPOSITE THE SEGMENT AT THE START AND ALONG IT AT THE END
	 * — and the half-disc's fan runs from the left normal through the outward direction
	 * to the right normal, which is a half turn either way. */
	{
		double a0 = atan2(uy, ux);

		if (!at_start) {
			a0 += CG_STROKE_PI;
		}
		cg_round_fan(st, vx, vy, a0, a0 + CG_STROKE_PI, 1);
	}
}

static void cg_stroke_subpath(cg_stroke *st)
{
	int n = st->n;
	int segments = st->closed ? n : n - 1;
	int i;

	if (n < 2) {
		st->n = 0;
		return;
	}
	for (i = 0; i < segments; i++) {
		int j = (i + 1) % n;
		double ext_a = (st->cap == kCGLineCapSquare && !st->closed && i == 0) ? st->half : 0.0;
		double ext_b =
			(st->cap == kCGLineCapSquare && !st->closed && i == segments - 1) ? st->half : 0.0;

		cg_segment_quad(st, st->pts[i * 2], st->pts[i * 2 + 1], st->pts[j * 2], st->pts[j * 2 + 1],
				ext_a, ext_b);
	}
	/* JOINS: at every vertex of a closed subpath, and at every INTERIOR vertex of an
	 * open one — the two ends of an open subpath get caps instead. */
	for (i = 0; i < n; i++) {
		int interior = st->closed || (i > 0 && i < n - 1);

		if (!interior) {
			continue;
		}
		cg_join(st, st->pts[((i + n - 1) % n) * 2], st->pts[((i + n - 1) % n) * 2 + 1], st->pts[i * 2],
			st->pts[i * 2 + 1], st->pts[((i + 1) % n) * 2], st->pts[((i + 1) % n) * 2 + 1]);
	}
	if (!st->closed) {
		double ux, uy, nx, ny;

		if (cg_normal(st->pts[0], st->pts[1], st->pts[2], st->pts[3], &nx, &ny, &ux, &uy)) {
			cg_cap(st, st->pts[0], st->pts[1], ux, uy, 1);
		}
		if (cg_normal(st->pts[(n - 2) * 2], st->pts[(n - 2) * 2 + 1], st->pts[(n - 1) * 2],
			      st->pts[(n - 1) * 2 + 1], &nx, &ny, &ux, &uy)) {
			cg_cap(st, st->pts[(n - 1) * 2], st->pts[(n - 1) * 2 + 1], ux, uy, 0);
		}
	}
	st->n = 0;
}

/* ------------------------------------------------------------------------- */
/* walking the input path                                                    */
/* ------------------------------------------------------------------------- */

static int cg_stroke_push(cg_stroke *st, double x, double y)
{
	if (st->n == st->cap_pts) {
		int want = st->cap_pts ? st->cap_pts * 2 : 16;
		double *grown = realloc(st->pts, (size_t)want * 2 * sizeof(double));

		if (grown == NULL) {
			return 0;
		}
		st->pts = grown;
		st->cap_pts = want;
	}
	st->pts[st->n * 2] = x;
	st->pts[st->n * 2 + 1] = y;
	st->n++;
	return 1;
}

static void cg_stroke_element(void *info, const CGPathElement *element)
{
	cg_stroke *st = info;
	CGPoint p;

	switch (element->type) {
	case kCGPathElementMoveToPoint:
		cg_stroke_subpath(st);
		p = element->points[0];
		if (st->have_ctm) {
			p = CGPointApplyAffineTransform(p, st->ctm);
		}
		cg_stroke_push(st, p.x, p.y);
		break;
	case kCGPathElementAddLineToPoint:
		p = element->points[0];
		if (st->have_ctm) {
			p = CGPointApplyAffineTransform(p, st->ctm);
		}
		if (st->n == 0) {
			cg_stroke_push(st, 0.0, 0.0);
		}
		cg_stroke_push(st, p.x, p.y);
		break;
	case kCGPathElementCloseSubpath:
		if (st->n > 0) {
			double sx = st->pts[0];
			double sy = st->pts[1];

			st->closed = 1;
			cg_stroke_subpath(st);
			cg_stroke_push(st, sx, sy);
		}
		break;
	default:
		break;
	}
}

CGPathRef CGPathCreateCopyByStrokingPath(CGPathRef path, const CGAffineTransform *transform,
					CGFloat lineWidth, CGLineCap lineCap, CGLineJoin lineJoin,
					CGFloat miterLimit)
{
	cg_stroke st;
	CGMutablePathRef out;

	if (path == NULL || lineWidth <= 0.0) {
		/* A STROKE OF ZERO WIDTH IS NOTHING, and that is a real case: Apple's default
		 * line width is 1, but a caller may set 0, and a zero-width stroke drawn as a
		 * hairline would be a shape this tree cannot justify. */
		return CGPathCreateMutable();
	}
	memset(&st, 0, sizeof(st));
	out = CGPathCreateMutable();
	if (out == NULL) {
		return NULL;
	}
	st.out = out;
	st.half = lineWidth / 2.0;
	st.cap = lineCap;
	st.join = lineJoin;
	st.miter_limit = (miterLimit > 0.0 ? miterLimit : 10.0) * st.half;
	if (transform != NULL) {
		st.ctm = *transform;
		st.have_ctm = 1;
	}
	/* A CURVE IS FLATTENED BEFORE THE WALKER SEES IT, so `cg_stroke_element` handles only
	 * moves, lines and closes: ONE subdivision in the tree (CGPath.c), and the stroker's
	 * joins and caps then get line segments exactly as they do for a hand-built polyline.
	 *
	 * THE TOLERANCE IS DIVIDED BY THE TRANSFORM'S SCALE, because the transform is applied
	 * to the flattened points: subdividing in the path's units and then scaling the result
	 * by ten would draw a curve ten times coarser than the caller asked for. The scale is
	 * the same upper bound the fill uses — four numbers added, no square root. */
	{
		double a = st.ctm.a, b = st.ctm.b, cc = st.ctm.c, d = st.ctm.d;
		double scale = 1.0;
		CGPathRef flat;

		if (st.have_ctm) {
			scale = (a < 0 ? -a : a) + (b < 0 ? -b : b) + (cc < 0 ? -cc : cc) +
				(d < 0 ? -d : d);
			if (scale < 1e-6) {
				scale = 1.0;
			}
		}
		flat = CGPathCreateCopyByFlattening(path, 0.1 / scale);
		if (flat != NULL) {
			CGPathApply(flat, &st, cg_stroke_element);
			CGPathRelease(flat);
		}
	}
	cg_stroke_subpath(&st);
	free(st.pts);
	return (CGPathRef)out;
}
