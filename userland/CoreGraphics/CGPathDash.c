/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPathDash.c — the dashing: a path cut into the pieces a dashed line draws.
 *
 * A DASH PATTERN IS A CYCLE OF LENGTHS THAT ALTERNATE ON AND OFF, starting ON, read
 * cyclically from `phase` — so the work is to walk the path's LINES, carry the pattern along
 * them, and emit a subpath for each ON piece. The pens go up and down as the pattern says,
 * which is why the result is a set of separate subpaths rather than one path with gaps.
 *
 * CURVES DO NOT SURVIVE DASHING, AND CANNOT: a dash boundary falls between points on a
 * curve, not at its ends, so the output is lines. The path is FLATTENED first (through the
 * one flattener in the tree, CGPath.c) — the same choice the stroker makes, for the same
 * reason: one subdivision, in one place.
 *
 * THE TRANSFORM IS APPLIED TO THE PATH, NOT TO THE RESULT, so the dash lengths are in the
 * transformed space — the convention `CGPathCreateCopyByStrokingPath` already set — and the
 * flattening tolerance is divided by the transform's scale so that it stays a DEVICE
 * tolerance however far the caller has zoomed.
 *
 * THREE CASES THIS TREE DECIDES BECAUSE APPLE'S PAGE DOES NOT:
 *   * AN ODD COUNT IS DOUBLED. A cyclic pattern whose element count is odd would fall out of
 *     step with its own alternation — the fourth element would be ON again after three — so
 *     the pattern is repeated once to make the cycle even, which is what the common
 *     implementations do.
 *   * A TOTAL LENGTH OF ZERO IS A SOLID LINE. A pattern of nothing but zeros asks for
 *     dashes of no length, which is not a dashed line; a copy is the honest answer.
 *   * A NEGATIVE LENGTH IS READ AS ITS MAGNITUDE, since a dash of negative length means
 *     nothing and refusing it would refuse a pattern a caller could reasonably build.
 */
#include <CoreGraphics/CGPath.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a pattern with more entries than this gets: NOTHING — see the refusal below. A
 * pattern is a handful of numbers in every real caller, and a stack array with a bound is
 * better than an allocation per dash. */
#define CG_DASH_MAX 32

typedef struct {
	CGMutablePathRef out;
	CGAffineTransform ctm;
	int have_ctm;
	double pat[CG_DASH_MAX * 2];
	int n;
	double left;
	int idx;
	int on;
	int pen;
	CGPoint cur;
	CGPoint start;
	int have;
} cg_dash;

static void cg_dash_advance(cg_dash *d)
{
	int guard = 0;

	/* ZERO-LENGTH ENTRIES ARE SKIPPED — an ON of no length adds nothing and an OFF of no
	 * length removes nothing — and the guard stops a pattern of nothing but zeros from
	 * spinning here. A total of zero never reaches this function: it is a solid line. */
	do {
		d->idx = (d->idx + 1) % d->n;
		d->on = !d->on;
		d->left = d->pat[d->idx];
		guard++;
	} while (d->left <= 0.0 && guard <= d->n + 1);
}

static CGPoint cg_dash_point(const cg_dash *d, double x, double y)
{
	CGPoint p = CGPointMake(x, y);

	if (d->have_ctm) {
		p = CGPointApplyAffineTransform(p, d->ctm);
	}
	return p;
}

static void cg_dash_segment(cg_dash *d, double x0, double y0, double x1, double y1)
{
	double dx = x1 - x0;
	double dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	double t = 0.0;

	if (len <= 0.0) {
		return;
	}
	while (t < len - 1e-12) {
		double step = d->left < (len - t) ? d->left : (len - t);
		double f0 = t / len;
		double f1 = (t + step) / len;

		if (step > 0.0) {
			CGPoint a = cg_dash_point(d, x0 + dx * f0, y0 + dy * f0);
			CGPoint b = cg_dash_point(d, x0 + dx * f1, y0 + dy * f1);

			if (d->on) {
				if (!d->pen) {
					CGPathMoveToPoint(d->out, NULL, a.x, a.y);
					d->pen = 1;
				}
				CGPathAddLineToPoint(d->out, NULL, b.x, b.y);
			} else {
				/* THE PEN GOES UP: an OFF piece ends the subpath, so the next ON piece
				 * starts with a move of its own. */
				d->pen = 0;
			}
		}
		t += step;
		d->left -= step;
		if (d->left <= 1e-12) {
			cg_dash_advance(d);
		}
	}
}

static void cg_dash_element(void *info, const CGPathElement *element)
{
	cg_dash *d = info;

	switch (element->type) {
	case kCGPathElementMoveToPoint:
		d->pen = 0;
		d->start = element->points[0];
		d->cur = element->points[0];
		d->have = 1;
		break;
	case kCGPathElementAddLineToPoint:
		if (!d->have) {
			d->start = CGPointZero;
			d->cur = CGPointZero;
			d->have = 1;
		}
		cg_dash_segment(d, d->cur.x, d->cur.y, element->points[0].x, element->points[0].y);
		d->cur = element->points[0];
		break;
	case kCGPathElementCloseSubpath:
		if (d->have) {
			cg_dash_segment(d, d->cur.x, d->cur.y, d->start.x, d->start.y);
			d->cur = d->start;
		}
		/* THE PEN COMES UP AT A CLOSE, AND THAT IS THE ONE PLACE A DASH CAN BE SPLIT. It is
		 * necessary: without it the next subpath's first ON piece would continue from THIS
		 * subpath's point and draw a line the caller never asked for. The pattern position
		 * carries on across the seam; only the join is lost. */
		d->pen = 0;
		break;
	default:
		break;
	}
}

CGPathRef CGPathCreateCopyByDashingPath(CGPathRef cpath, const CGAffineTransform *transform,
					CGFloat phase, const CGFloat *lengths, size_t count)
{
	cg_dash d;
	CGMutablePathRef out;
	CGPathRef flat;
	double total = 0.0;
	double scale = 1.0;
	int n;
	int i;

	/* NO PATTERN IS NO DASHING, and that is a copy rather than an empty path: a NULL array or
	 * a count of zero is how a caller says "solid". */
	if (cpath == NULL || lengths == NULL || count == 0) {
		return CGPathCreateCopyByFlattening(cpath, 0.1);
	}
	if (count > CG_DASH_MAX) {
		fprintf(stderr, "CG-REFUSE: a dash pattern longer than %d entries\n", CG_DASH_MAX);
		return CGPathCreateMutable();
	}
	/* AN ODD COUNT IS DOUBLED (see the header), which is what makes the cycle even and the
	 * alternation come out right. */
	n = ((int)count % 2) ? (int)count * 2 : (int)count;

	memset(&d, 0, sizeof(d));
	for (i = 0; i < n; i++) {
		double v = lengths[i % count];

		d.pat[i] = v < 0 ? -v : v;
	}
	for (i = 0; i < n; i++) {
		total += d.pat[i];
	}
	if (total <= 0.0) {
		/* A PATTERN OF NOTHING BUT ZEROS IS A SOLID LINE, not an infinity of zero-length
		 * dashes — see the header. */
		return CGPathCreateCopyByFlattening(cpath, 0.1);
	}
	d.n = n;
	d.idx = 0;
	d.on = 1;
	d.left = d.pat[0];

	/* THE PHASE WRAPS INTO THE PATTERN, so a caller may pass any number — including one the
	 * path's length made meaningless — and get the same dashes. */
	phase = fmod((double)phase, total);
	if (phase < 0.0) {
		phase += total;
	}
	{
		double left = phase;

		while (left > 1e-12) {
			if (left < d.left) {
				d.left -= left;
				left = 0.0;
			} else {
				left -= d.left;
				cg_dash_advance(&d);
			}
		}
	}

	out = CGPathCreateMutable();
	if (out == NULL) {
		return NULL;
	}
	d.out = out;

	/* THE FLATTENED PATH, WITH A TOLERANCE THAT STAYS A DEVICE ONE: the transform is applied
	 * to the flattened points as they are walked, so a scale of ten would otherwise dash a
	 * curve ten times coarser than asked for. The scale is the same upper bound the fill and
	 * the stroker use — four numbers added, no square root. */
	if (transform != NULL) {
		double a = transform->a, b = transform->b, c = transform->c, dd = transform->d;

		scale = (a < 0 ? -a : a) + (b < 0 ? -b : b) + (c < 0 ? -c : c) + (dd < 0 ? -dd : dd);
		if (scale < 1e-6) {
			scale = 1.0;
		}
		d.ctm = *transform;
		d.have_ctm = 1;
	}
	flat = CGPathCreateCopyByFlattening(cpath, 0.1 / scale);
	if (flat == NULL) {
		CGPathRelease((CGPathRef)out);
		return NULL;
	}
	CGPathApply(flat, &d, cg_dash_element);
	CGPathRelease(flat);
	return (CGPathRef)out;
}
