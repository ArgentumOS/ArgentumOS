/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPath.c — the path container, the walker, and the flattener.
 *
 * THE PATH IS AN ARRAY OF ELEMENTS — move, line, quadratic, cubic, close — and the
 * structure is not incidental. The FILL RULE needs subpaths walked separately (a move
 * starts one, and the implicit closing edge belongs to one), and CURVES need their
 * CONTROL POINTS kept rather than rounded off at the door: `CGPathGetBoundingBox` is
 * defined to include them while `CGPathGetPathBoundingBox` is the tight box of the
 * curve, so a path that flattened on the way in could not answer either question
 * honestly, and a caller's flatness choice would be taken away from them.
 *
 * NOTHING HERE KNOWS ABOUT THE CTM. A path is in the coordinates it was built in; only
 * the context transforms it, when it is drawn. That is why `CGPathAddRect` must NOT use
 * `CGRectApplyAffineTransform`: that function returns the BOUNDING BOX of the transformed
 * rectangle, which under a rotation is not the transformed rectangle at all. The four
 * corners go through the transform one at a time, so a rotated rectangle stays a rotated
 * quadrilateral.
 *
 * THE FLATTENER IS PUBLIC — `CGPathCreateCopyByFlattening` — and it is also what the two
 * BOXES and the two consumers (the fill in CGContext.c, the stroker in CGPathStroke.c)
 * use. One subdivision, in one place: a second one would be a second answer to "where is
 * this curve", which is the class of bug this tree keeps finding.
 */
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGPath_internal.h>

/* THE RENAME TABLE FOR THE THREE PATH OPERATIONS THAT ARE OURS NOW (2026-10-05). They were public,
 * and `CGPathCreateCopyByStrokingPath`/`ByDashingPath` are macOS 10.7 while `ByFlattening` is 13.0
 * — out of era for a 10.6-era surface — so the PUBLIC DECLARATIONS are gone and the code lives
 * under the names in CGPath_internal.h. The table is here so this file's definition and call sites
 * read as one thing; what the surface cares about is the compiled name, and `nm` is the check. */
#define CGPathCreateCopyByFlattening cg_path_create_flattened_copy

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The deepest a subdivision may go. Sixteen halvings of a curve that is still not flat
 * is a curve with a degenerate chord (both endpoints equal, so the flatness test's
 * denominator is zero) — and the answer there is to stop, not to recurse until the stack
 * says so. */
#define CG_FLATTEN_MAX_DEPTH 16

/* What a caller who asks for "flat" without a number gets, and what a caller who passes
 * zero or less gets. Apple's page does not say what a non-positive flatness means; this
 * tree takes it as "use the default" rather than as "subdivide forever". */
#define CG_FLATTEN_DEFAULT 0.1

typedef struct cg_element {
	int type;         /* one of kCGPathElement… */
	int npts;         /* 1 for a move or a line, 2 for a quad, 3 for a cubic, 0 for a close */
	CGFloat pts[6];   /* x,y per point */
} cg_element;

struct CGPath {
	int refcount;
	cg_element *elems;
	int count;
	int cap;
};

/* ------------------------------------------------------------------------- */
/* the container                                                             */
/* ------------------------------------------------------------------------- */

static int cg_push(struct CGPath *path, int type, int npts, const CGFloat *pts)
{
	if (path->count == path->cap) {
		int want = path->cap ? path->cap * 2 : 16;
		cg_element *grown = realloc(path->elems, (size_t)want * sizeof(cg_element));

		if (grown == NULL) {
			return 0;
		}
		path->elems = grown;
		path->cap = want;
	}
	path->elems[path->count].type = type;
	path->elems[path->count].npts = npts;
	memcpy(path->elems[path->count].pts, pts, (size_t)npts * 2 * sizeof(double));
	path->count++;
	return 1;
}

/* The endpoint of an element, and the origin for an empty path — which is what
 * `CGPathGetCurrentPoint` states the current point of an empty path is. */
static CGPoint cg_endpoint(const cg_element *e)
{
	if (e->npts == 0) {
		return CGPointZero;
	}
	return CGPointMake(e->pts[(e->npts - 1) * 2], e->pts[(e->npts - 1) * 2 + 1]);
}

static CGPoint cg_point(const CGFloat *p)
{
	return CGPointMake(p[0], p[1]);
}

static void cg_transform_in_place(int npts, CGFloat *pts, const CGAffineTransform *m)
{
	int i;

	if (m == NULL) {
		return;
	}
	for (i = 0; i < npts; i++) {
		CGPoint p = CGPointApplyAffineTransform(cg_point(&pts[i * 2]), *m);

		pts[i * 2] = p.x;
		pts[i * 2 + 1] = p.y;
	}
}

CGMutablePathRef CGPathCreateMutable(void)
{
	struct CGPath *path = calloc(1, sizeof(struct CGPath));

	if (path != NULL) {
		path->refcount = 1;
	}
	return path;
}

CGPathRef CGPathRetain(CGPathRef cpath)
{
	struct CGPath *path = (struct CGPath *)cpath;

	if (path != NULL) {
		path->refcount++;
	}
	return cpath;
}

void CGPathRelease(CGPathRef cpath)
{
	struct CGPath *path = (struct CGPath *)cpath;

	if (path == NULL || path->refcount == 0) {
		return;
	}
	if (--path->refcount > 0) {
		return;
	}
	free(path->elems);
	free(path);
}

/* !! `CGPathCreateWithRect` STOOD HERE AND WAS REMOVED (2026-10-05): macOS 10.7, out of era, and
 * four lines over `CGPathAddRect` — so the era's spelling is `CGPathCreateMutable` followed by
 * `CGPathAddRect`, and that is what the curve probe now exercises BOTH halves of. */

/* ------------------------------------------------------------------------- */
/* building                                                                  */
/* ------------------------------------------------------------------------- */

void CGPathMoveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y)
{
	CGFloat pts[2] = { x, y };

	if (path == NULL) {
		return;
	}
	cg_transform_in_place(1, pts, m);
	cg_push(path, kCGPathElementMoveToPoint, 1, pts);
}

void CGPathAddLineToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y)
{
	CGFloat pts[2] = { x, y };

	if (path == NULL) {
		return;
	}
	cg_transform_in_place(1, pts, m);
	/* A LINE (OR A CURVE) WITH NO PRECEDING MOVE STARTS AT THE ORIGIN, which is the
	 * current point of an empty path. The origin is emitted as an explicit MOVE rather
	 * than left implicit, because the fill needs it as a vertex and the walker has to
	 * report it — which is also why the element stream always begins with a move. */
	if (path->count == 0) {
		CGFloat origin[2] = { 0.0, 0.0 };

		cg_push(path, kCGPathElementMoveToPoint, 1, origin);
	}
	cg_push(path, kCGPathElementAddLineToPoint, 1, pts);
}

void CGPathAddQuadCurveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat cpx,
			       CGFloat cpy, CGFloat x, CGFloat y)
{
	CGFloat pts[4] = { cpx, cpy, x, y };

	if (path == NULL) {
		return;
	}
	cg_transform_in_place(2, pts, m);
	if (path->count == 0) {
		CGFloat origin[2] = { 0.0, 0.0 };

		cg_push(path, kCGPathElementMoveToPoint, 1, origin);
	}
	cg_push(path, kCGPathElementAddQuadCurveToPoint, 2, pts);
}

void CGPathAddCurveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat cp1x,
			   CGFloat cp1y, CGFloat cp2x, CGFloat cp2y, CGFloat x, CGFloat y)
{
	CGFloat pts[6] = { cp1x, cp1y, cp2x, cp2y, x, y };

	if (path == NULL) {
		return;
	}
	cg_transform_in_place(3, pts, m);
	if (path->count == 0) {
		CGFloat origin[2] = { 0.0, 0.0 };

		cg_push(path, kCGPathElementMoveToPoint, 1, origin);
	}
	cg_push(path, kCGPathElementAddCurveToPoint, 3, pts);
}

void CGPathAddRect(CGMutablePathRef path, const CGAffineTransform *m, CGRect rect)
{
	CGPoint corners[4];
	int i;

	if (path == NULL) {
		return;
	}
	corners[0] = CGPointMake(rect.origin.x, rect.origin.y);
	corners[1] = CGPointMake(rect.origin.x + rect.size.width, rect.origin.y);
	corners[2] = CGPointMake(rect.origin.x + rect.size.width, rect.origin.y + rect.size.height);
	corners[3] = CGPointMake(rect.origin.x, rect.origin.y + rect.size.height);
	if (m != NULL) {
		for (i = 0; i < 4; i++) {
			corners[i] = CGPointApplyAffineTransform(corners[i], *m);
		}
	}
	CGPathMoveToPoint(path, NULL, corners[0].x, corners[0].y);
	for (i = 1; i < 4; i++) {
		CGPathAddLineToPoint(path, NULL, corners[i].x, corners[i].y);
	}
	CGPathCloseSubpath(path);
}

void CGPathCloseSubpath(CGMutablePathRef path)
{
	if (path != NULL) {
		cg_push(path, kCGPathElementCloseSubpath, 0, NULL);
	}
}

/* ------------------------------------------------------------------------- */
/* asking                                                                    */
/* ------------------------------------------------------------------------- */

int CGPathIsEmpty(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;

	return path == NULL || path->count == 0;
}

CGPoint CGPathGetCurrentPoint(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	int i;

	if (path == NULL) {
		return CGPointZero;
	}
	for (i = path->count - 1; i >= 0; i--) {
		const cg_element *e = &path->elems[i];

		/* A CLOSE CARRIES NO POINT, AND THE ANSWER IS BEHIND IT: closing a subpath puts
		 * the current point back at that subpath's START, so the search walks back to the
		 * move that opened it rather than reporting the last curve's endpoint. */
		if (e->type == kCGPathElementCloseSubpath) {
			int j;

			for (j = i - 1; j >= 0; j--) {
				if (path->elems[j].type == kCGPathElementMoveToPoint) {
					return cg_endpoint(&path->elems[j]);
				}
			}
			return CGPointZero;
		}
		if (e->npts > 0) {
			return cg_endpoint(e);
		}
	}
	return CGPointZero;
}

/* ------------------------------------------------------------------------- */
/* flattening                                                                */
/* ------------------------------------------------------------------------- */

static void cg_box_add(CGRect *box, int *seen, double x, double y)
{
	if (!*seen) {
		box->origin = CGPointMake(x, y);
		box->size = CGSizeZero;
		*seen = 1;
		return;
	}
	if (x < CGRectGetMinX(*box)) {
		box->size.width = CGRectGetMaxX(*box) - x;
		box->origin.x = x;
	}
	if (y < CGRectGetMinY(*box)) {
		box->size.height = CGRectGetMaxY(*box) - y;
		box->origin.y = y;
	}
	if (x > CGRectGetMaxX(*box)) {
		box->size.width = x - box->origin.x;
	}
	if (y > CGRectGetMaxY(*box)) {
		box->size.height = y - box->origin.y;
	}
}

/*
 * THE FLATNESS TEST, AND WHY IT IS WRITTEN THIS WAY: the two quantities d1 and d2 are
 * (twice) the area of the triangles the control points make with the chord, so dividing
 * them by the chord's length gives distances from the chord. The test below therefore
 * compares (d1 + d2)² against flatness² × (chord length)² — the same statement without a
 * square root or a division, which matters because this runs once per subdivision.
 *
 * A DEGENERATE CHORD (both endpoints equal, so the right-hand side is zero) cannot pass
 * unless the controls are exactly on the point, so such a curve subdivides to the DEPTH
 * LIMIT and is then accepted as a line. That is a shape whose own chord says nothing about
 * it, and stopping is the honest answer rather than recursing until the stack objects.
 */
static int cg_cubic_is_flat(const double *p, double flat)
{
	double dx = p[6] - p[0];
	double dy = p[7] - p[1];
	double d1 = (p[2] - p[6]) * dy - (p[3] - p[7]) * dx;
	double d2 = (p[4] - p[6]) * dy - (p[5] - p[7]) * dx;
	double sum = d1 + d2;

	return sum * sum <= flat * flat * (dx * dx + dy * dy);
}

/* de Casteljau at t = 0.5, which is the split that keeps the subdivision stable: both
 * halves are cubics of the same shape as the pieces they cover. */
static void cg_cubic_split(const double *p, double *left, double *right)
{
	double a[2], b[2], c[2], d[2], e[2], f[2];

	a[0] = (p[0] + p[2]) / 2.0;
	a[1] = (p[1] + p[3]) / 2.0;
	b[0] = (p[2] + p[4]) / 2.0;
	b[1] = (p[3] + p[5]) / 2.0;
	c[0] = (p[4] + p[6]) / 2.0;
	c[1] = (p[5] + p[7]) / 2.0;
	d[0] = (a[0] + b[0]) / 2.0;
	d[1] = (a[1] + b[1]) / 2.0;
	e[0] = (b[0] + c[0]) / 2.0;
	e[1] = (b[1] + c[1]) / 2.0;
	f[0] = (d[0] + e[0]) / 2.0;
	f[1] = (d[1] + e[1]) / 2.0;

	left[0] = p[0];
	left[1] = p[1];
	left[2] = a[0];
	left[3] = a[1];
	left[4] = d[0];
	left[5] = d[1];
	left[6] = f[0];
	left[7] = f[1];
	right[0] = f[0];
	right[1] = f[1];
	right[2] = e[0];
	right[3] = e[1];
	right[4] = c[0];
	right[5] = c[1];
	right[6] = p[6];
	right[7] = p[7];
}

static void cg_flatten_cubic(CGMutablePathRef out, const double *p, double flat, int depth)
{
	double left[8];
	double right[8];

	if (depth >= CG_FLATTEN_MAX_DEPTH || cg_cubic_is_flat(p, flat)) {
		CGPathAddLineToPoint(out, NULL, p[6], p[7]);
		return;
	}
	cg_cubic_split(p, left, right);
	cg_flatten_cubic(out, left, flat, depth + 1);
	cg_flatten_cubic(out, right, flat, depth + 1);
}

/* A QUADRATIC IS A CUBIC WHOSE CONTROLS ARE TWO THIRDS OF THE WAY TO THE SHOULDER, and
 * raising the degree here instead of writing a second subdivision means there is one
 * flattener to be right rather than two to agree. */
static void cg_quad_as_cubic(const double *q, double *p)
{
	p[0] = q[0];
	p[1] = q[1];
	p[2] = q[0] + 2.0 / 3.0 * (q[2] - q[0]);
	p[3] = q[1] + 2.0 / 3.0 * (q[3] - q[1]);
	p[4] = q[4] + 2.0 / 3.0 * (q[2] - q[4]);
	p[5] = q[5] + 2.0 / 3.0 * (q[3] - q[5]);
	p[6] = q[4];
	p[7] = q[5];
}

CGPathRef CGPathCreateCopyByFlattening(CGPathRef cpath, CGFloat flatness)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	CGMutablePathRef out;
	double flat = (flatness > 0.0) ? flatness : CG_FLATTEN_DEFAULT;
	int i;

	if (path == NULL) {
		return NULL;
	}
	out = CGPathCreateMutable();
	if (out == NULL) {
		return NULL;
	}
	for (i = 0; i < path->count; i++) {
		const cg_element *e = &path->elems[i];

		switch (e->type) {
		case kCGPathElementMoveToPoint:
			CGPathMoveToPoint(out, NULL, e->pts[0], e->pts[1]);
			break;
		case kCGPathElementAddLineToPoint:
			CGPathAddLineToPoint(out, NULL, e->pts[0], e->pts[1]);
			break;
		case kCGPathElementAddQuadCurveToPoint:
		{
			double p[8];

			cg_quad_as_cubic(e->pts, p);
			/* THE SUBDIVISION STARTS FROM THE CURVE'S START POINT, which is the path's
			 * current point at this element — read from the output path rather than
			 * tracked, so there is one answer to where the pen is. */
			{
				CGPoint cur = CGPathGetCurrentPoint((CGPathRef)out);

				p[0] = cur.x;
				p[1] = cur.y;
			}
			cg_flatten_cubic(out, p, flat, 0);
			break;
		}
		case kCGPathElementAddCurveToPoint:
		{
			double p[8];
			CGPoint cur = CGPathGetCurrentPoint((CGPathRef)out);

			p[0] = cur.x;
			p[1] = cur.y;
			p[2] = e->pts[0];
			p[3] = e->pts[1];
			p[4] = e->pts[2];
			p[5] = e->pts[3];
			p[6] = e->pts[4];
			p[7] = e->pts[5];
			cg_flatten_cubic(out, p, flat, 0);
			break;
		}
		case kCGPathElementCloseSubpath:
			CGPathCloseSubpath(out);
			break;
		default:
			break;
		}
	}
	return (CGPathRef)out;
}

/* ------------------------------------------------------------------------- */
/* the two boxes                                                             */
/* ------------------------------------------------------------------------- */

CGRect CGPathGetBoundingBox(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	CGRect box = CGRectNull;
	int seen = 0;
	int i, j;

	if (path == NULL || path->count == 0) {
		/* THE EMPTY PATH HAS NO EXTENT AND THIS TREE SAYS SO WITH THE NULL RECTANGLE — a
		 * statement, not a transcription: Apple's page does not reach this case in a form
		 * this tree can check without the SDK. It is also a choice that cannot bite a
		 * caller: null and zero agree on width and height, so anything measuring the box
		 * reads the same either way. */
		return CGRectNull;
	}
	/* THE CONTROL POINTS ARE INCLUDED, which is what makes this function's answer larger
	 * than the path for a curve — see CGPathGetPathBoundingBox for the other one. */
	for (i = 0; i < path->count; i++) {
		const cg_element *e = &path->elems[i];

		for (j = 0; j < e->npts; j++) {
			cg_box_add(&box, &seen, e->pts[j * 2], e->pts[j * 2 + 1]);
		}
	}
	return box;
}

CGRect CGPathGetPathBoundingBox(CGPathRef cpath)
{
	/* THE TIGHT BOX IS THE FLATTENED PATH'S BOX, and using the flattener rather than a
	 * closed form for the extremes is deliberate: whatever the rasterizer will draw, this
	 * reports. A closed form for a cubic's extrema is thirty lines of derivative roots
	 * that would have to agree with the subdivision — and the day they disagree, the box
	 * says one thing and the pixels another. */
	CGPathRef flat = CGPathCreateCopyByFlattening(cpath, CG_FLATTEN_DEFAULT);
	CGRect box;

	if (flat == NULL) {
		return CGRectNull;
	}
	box = CGPathGetBoundingBox(flat);
	CGPathRelease(flat);
	return box;
}

/* ------------------------------------------------------------------------- */
/* walking                                                                   */
/* ------------------------------------------------------------------------- */

void CGPathApply(CGPathRef cpath, void *info, CGPathApplierFunction function)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	CGPoint points[3];
	CGPathElement element;
	int i, j;

	if (path == NULL || function == NULL) {
		return;
	}
	element.points = points;
	for (i = 0; i < path->count; i++) {
		const cg_element *e = &path->elems[i];

		element.type = (CGPathElementType)e->type;
		for (j = 0; j < e->npts; j++) {
			points[j] = cg_point(&e->pts[j * 2]);
		}
		function(info, &element);
	}
}

/* ------------------------------------------------------------------------- */
/* Copies, and the three questions a path can answer about itself             */
/* ------------------------------------------------------------------------- */

/* ONE ELEMENT AT A TIME, BECAUSE THE TAIL IS NOT PART OF IT: `cg_element` carries room for three points and a
 * line uses one, so comparing whole elements would compare slots the path never wrote — which is also why the
 * comparison is not a `memcmp`. */
static int fn_elements_equal(const cg_element *a, const cg_element *b)
{
	int i;

	if (a->type != b->type || a->npts != b->npts) {
		return 0;
	}
	for (i = 0; i < a->npts * 2; i++) {
		if (a->pts[i] != b->pts[i]) {
			return 0;
		}
	}
	return 1;
}

static struct CGPath *fn_copy_of(CGPathRef path)
{
	/* `CGPathRef` IS A POINTER TO CONST, so a copy being built is spelled with the mutable
	 * struct — the same thing this file's other constructors do. */
	struct CGPath *copy;

	if (path == NULL) {
		return NULL;
	}
	copy = calloc(1, sizeof(struct CGPath));
	if (copy == NULL) {
		return NULL;
	}
	copy->refcount = 1;
	if (path->count > 0) {
		copy->elems = malloc(sizeof(cg_element) * (size_t)path->count);
		if (copy->elems == NULL) {
			free(copy);
			return NULL;
		}
		memcpy(copy->elems, path->elems, sizeof(cg_element) * (size_t)path->count);
		copy->count = path->count;
		copy->cap = path->count;
	}
	return copy;
}

CGPathRef CGPathCreateCopy(CGPathRef path)
{
	return (CGPathRef)fn_copy_of(path);
}

CGMutablePathRef CGPathCreateMutableCopy(CGPathRef path)
{
	return (CGMutablePathRef)fn_copy_of(path);
}

bool CGPathEqualToPath(CGPathRef path1, CGPathRef path2)
{
	int i;

	if (path1 == NULL || path2 == NULL) {
		return false;
	}
	if (path1 == path2) {
		return true;
	}
	if (path1->count != path2->count) {
		return false;
	}
	for (i = 0; i < path1->count; i++) {
		if (!fn_elements_equal(&path1->elems[i], &path2->elems[i])) {
			return false;
		}
	}
	return true;
}

/* A RECTANGLE IS FOUR CORNERS THAT STEP ONE AXIS AT A TIME AND COME BACK. `CGPathAddRect` writes a move and
 * three lines and closes; a caller's own path may spell the same rectangle as four lines whose last returns to
 * the start. BOTH ARE READ HERE AND NOTHING ELSE IS: a path with a curve in it, a path with five corners, or a
 * degenerate one whose corners line up is not a rectangle. The corners may be wound either way and may start
 * at any corner, which is a READING — Apple's page says only that the path must be a rectangle. */
bool CGPathIsRect(CGPathRef path, CGRect *rect)
{
	CGFloat x[5];
	CGFloat y[5];
	int n = 0;
	int i;
	int closed = 0;
	CGFloat minx, maxx, miny, maxy;

	if (path == NULL) {
		return false;
	}
	for (i = 0; i < path->count; i++) {
		const cg_element *e = &path->elems[i];

		if (e->type == kCGPathElementCloseSubpath) {
			if (i != path->count - 1) {
				return false;	/* anything after the close is not part of one rectangle */
			}
			closed = 1;
			continue;
		}
		if (e->type != kCGPathElementMoveToPoint && e->type != kCGPathElementAddLineToPoint) {
			return false;
		}
		if (n >= 5) {
			return false;
		}
		x[n] = e->pts[0];
		y[n] = e->pts[1];
		n++;
	}
	if (n == 5 && x[4] == x[0] && y[4] == y[0]) {
		n = 4;	/* a fourth line that returns to the start IS the close */
	} else if (n != 4) {
		return false;
	}
	(void)closed;
	for (i = 0; i < 4; i++) {
		int j = (i + 1) % 4;

		if (!((x[i] == x[j]) != (y[i] == y[j]))) {
			return false;	/* consecutive corners must step exactly one axis */
		}
	}
	minx = maxx = x[0];
	miny = maxy = y[0];
	for (i = 1; i < 4; i++) {
		if (x[i] < minx) {
			minx = x[i];
		}
		if (x[i] > maxx) {
			maxx = x[i];
		}
		if (y[i] < miny) {
			miny = y[i];
		}
		if (y[i] > maxy) {
			maxy = y[i];
		}
	}
	if (minx == maxx || miny == maxy) {
		return false;	/* a degenerate rectangle is a line, not a rectangle */
	}
	if (rect != NULL) {
		rect->origin.x = minx;
		rect->origin.y = miny;
		rect->size.width = maxx - minx;
		rect->size.height = maxy - miny;
	}
	return true;
}

/* THE CROSSING TEST, OVER THE PATH'S OWN LINES.
 *
 * EVERY CURVE IS FLATTENED FIRST — the tree's own adaptive flattener, the one the stroker and the dasher use —
 * so a curve is a run of short lines by the time it is crossed, and this is one loop rather than one loop per
 * element type.
 *
 * THE TWO RULES DIFFER IN ONE SIGN. Even-odd counts how many edges the ray crosses; non-zero adds whether each
 * crossing runs upward or downward and asks whether the total is anything but zero. A COUNTER-CLOCKWISE INNER
 * RECTANGLE IS THE CASE THAT TELLS THEM APART: both rules then describe the same shape differently, and the
 * probe checks them as one pair.
 *
 * `m` IS APPLIED TO THE POINT RATHER THAN TO THE PATH — the same answer for an invertible map, and it keeps the
 * flattening tolerance measured in the path's own units, which is where the flattener was told it applies. */
bool CGPathContainsPoint(CGPathRef path, const CGAffineTransform *m, CGPoint point, bool eoFill)
{
	CGPathRef flat;
	int crossings = 0;
	int winding = 0;
	int i;
	double px = (double)point.x;
	double py = (double)point.y;
	double lx = 0.0;
	double ly = 0.0;
	double sx = 0.0;	/* the subpath's start: what a CLOSE returns to */
	double sy = 0.0;
	int have = 0;

	if (path == NULL) {
		return false;
	}
	flat = cg_path_create_flattened_copy(path, CG_FLATTEN_DEFAULT);
	if (flat == NULL) {
		return false;
	}
	if (m != NULL) {
		CGAffineTransform inv = CGAffineTransformInvert(*m);
		CGPoint p = CGPointApplyAffineTransform(CGPointMake((CGFloat)px, (CGFloat)py), inv);

		px = (double)p.x;
		py = (double)p.y;
	}
	for (i = 0; i < flat->count; i++) {
		const cg_element *e = &flat->elems[i];

		if (e->type == kCGPathElementMoveToPoint) {
			lx = (double)e->pts[0];
			ly = (double)e->pts[1];
			sx = lx;
			sy = ly;
			have = 1;
			continue;
		}
		/* A CLOSE IS AN EDGE — THE ONE BACK TO THE SUBPATH'S START — AND LEAVING IT OUT COSTS A
		 * CROSSING RATHER THAN A PIXEL: a rectangle written by `CGPathAddRect` ends in a close and not
		 * in a returning line, so without this the left edge of every rectangle was invisible to the
		 * ray. THE PROBE CAUGHT IT AS A HOLE THAT WAS NOT THERE: a point in the band between two nested
		 * rectangles came back OUTSIDE by both rules, because the inner rectangle's left edge — the one
		 * that makes the count even — was never counted. */
		if (e->type == kCGPathElementCloseSubpath && have) {
			double ax = lx, ay = ly;
			double bx = sx, by = sy;

			if ((ay <= py) != (by <= py)) {
				double t = (py - ay) / (by - ay);
				double xint = ax + t * (bx - ax);

				if (xint > px) {
					crossings++;
					winding += (by > ay) ? 1 : -1;
				}
			}
			lx = bx;
			ly = by;
			continue;
		}
		if (e->type == kCGPathElementAddLineToPoint && have) {
			double ax = lx, ay = ly;
			double bx = (double)e->pts[0], by = (double)e->pts[1];

			if ((ay <= py) != (by <= py)) {
				double t = (py - ay) / (by - ay);
				double xint = ax + t * (bx - ax);

				if (xint > px) {
					crossings++;
					winding += (by > ay) ? 1 : -1;
				}
			}
			lx = bx;
			ly = by;
		}
	}
	CGPathRelease(flat);
	return eoFill ? (crossings % 2) != 0 : winding != 0;
}
