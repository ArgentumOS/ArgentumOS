/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPath.c — the path container and the walker (C2's line-only subset).
 *
 * THE PATH IS AN ARRAY OF SUBPATHS, EACH AN ARRAY OF POINTS, and the structure is not
 * incidental: the FILL RULE needs subpaths walked separately, because a move starts a
 * new one and the implicit closing edge belongs to one subpath. A flat point list
 * would make that unrecoverable at fill time.
 *
 * NOTHING HERE KNOWS ABOUT THE CTM. A path is in the coordinates it was built in;
 * only the context transforms it, when it is filled. That is why `CGPathAddRect` must
 * NOT use `CGRectApplyAffineTransform`: that function returns the BOUNDING BOX of the
 * transformed rectangle, which under a rotation is not the transformed rectangle at
 * all. The four corners go through the transform one at a time, so a rotated
 * rectangle stays a rotated quadrilateral.
 */
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGPath.h>

#include <stdlib.h>
#include <string.h>

typedef struct cg_subpath {
	CGFloat *pts;   /* x0, y0, x1, y1, … */
	int count;      /* point count, so 2 × count floats */
	int cap;        /* capacity in points */
	int closed;
} cg_subpath;

struct CGPath {
	int refcount;
	cg_subpath *subs;
	int count;
	int cap;
};

/* ------------------------------------------------------------------------- */

static int cg_reserve(void **buf, int *cap, int need, size_t elem)
{
	int want;
	void *grown;

	if (need <= *cap) {
		return 1;
	}
	want = *cap ? *cap * 2 : 8;
	while (want < need) {
		want *= 2;
	}
	grown = realloc(*buf, (size_t)want * elem);
	if (grown == NULL) {
		return 0;
	}
	*buf = grown;
	*cap = want;
	return 1;
}

static cg_subpath *cg_current_subpath(struct CGPath *path, int create)
{
	cg_subpath *sub;

	if (path->count == 0) {
		if (!create) {
			return NULL;
		}
		if (!cg_reserve((void **)&path->subs, &path->cap, 1, sizeof(cg_subpath))) {
			return NULL;
		}
		sub = &path->subs[path->count++];
		memset(sub, 0, sizeof(*sub));
		return sub;
	}
	return &path->subs[path->count - 1];
}

static int cg_add_point(struct CGPath *path, CGFloat x, CGFloat y)
{
	cg_subpath *sub = cg_current_subpath(path, 1);

	if (sub == NULL) {
		return 0;
	}
	/* A POINT PAIR, grown in point units so the arithmetic is not in bytes twice. */
	if (!cg_reserve((void **)&sub->pts, &sub->cap, sub->count + 1, 2 * sizeof(CGFloat))) {
		return 0;
	}
	sub->pts[sub->count * 2 + 0] = x;
	sub->pts[sub->count * 2 + 1] = y;
	sub->count++;
	sub->closed = 0;
	return 1;
}

/* ------------------------------------------------------------------------- */

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
	int i;

	if (path == NULL || path->refcount == 0) {
		return;
	}
	if (--path->refcount > 0) {
		return;
	}
	for (i = 0; i < path->count; i++) {
		free(path->subs[i].pts);
	}
	free(path->subs);
	free(path);
}

CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform *m)
{
	CGMutablePathRef path = CGPathCreateMutable();

	if (path != NULL) {
		CGPathAddRect(path, m, rect);
	}
	return path;
}

void CGPathMoveToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y)
{
	CGPoint p;

	if (path == NULL) {
		return;
	}
	p.x = x;
	p.y = y;
	if (m != NULL) {
		p = CGPointApplyAffineTransform(p, *m);
	}
	if (!cg_reserve((void **)&path->subs, &path->cap, path->count + 1, sizeof(cg_subpath))) {
		return;
	}
	memset(&path->subs[path->count], 0, sizeof(cg_subpath));
	path->count++;
	cg_add_point(path, p.x, p.y);
}

void CGPathAddLineToPoint(CGMutablePathRef path, const CGAffineTransform *m, CGFloat x, CGFloat y)
{
	CGPoint p;

	if (path == NULL) {
		return;
	}
	p.x = x;
	p.y = y;
	if (m != NULL) {
		p = CGPointApplyAffineTransform(p, *m);
	}
	/* A LINE WITH NO PRECEDING MOVE STARTS AT THE ORIGIN, which is what
	 * `CGPathGetCurrentPoint` states the current point of an empty path is. The
	 * starting point is added explicitly rather than left implicit, because the fill
	 * needs it as a vertex and the walker has to report it. */
	if (path->count == 0) {
		cg_add_point(path, 0.0, 0.0);
	}
	cg_add_point(path, p.x, p.y);
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
	cg_subpath *sub = cg_current_subpath(path, 0);

	if (sub != NULL) {
		sub->closed = 1;
	}
}

int CGPathIsEmpty(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;

	return path == NULL || path->count == 0;
}

CGPoint CGPathGetCurrentPoint(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	const cg_subpath *sub;

	if (path == NULL || path->count == 0) {
		return CGPointZero;
	}
	sub = &path->subs[path->count - 1];
	if (sub->count == 0) {
		return CGPointZero;
	}
	/* A CLOSED SUBPATH'S CURRENT POINT IS ITS START, which is the same statement as
	 * "the close added the segment back to the start" — and it is why the flag is
	 * kept rather than an explicit duplicate vertex being appended. */
	if (sub->closed) {
		return CGPointMake(sub->pts[0], sub->pts[1]);
	}
	return CGPointMake(sub->pts[(sub->count - 1) * 2], sub->pts[(sub->count - 1) * 2 + 1]);
}

/* Both boxes, and the difference is a promise to C3: a cubic curve leaves the box of
 * its CONTROL POINTS outside the box of the curve itself, so `GetBoundingBox` must
 * grow to include them and `GetPathBoundingBox` must not. For lines the two are the
 * same rectangle, and that is why implementing both now is honest rather than
 * premature. */
static CGRect cg_bound_box(CGPathRef cpath)
{
	const struct CGPath *path = (const struct CGPath *)cpath;
	CGRect box;
	int i, j;
	int seen = 0;

	if (path == NULL || path->count == 0) {
		/* THE EMPTY PATH HAS NO EXTENT AND THIS TREE SAYS SO WITH THE NULL RECTANGLE
		 * — a statement, not a transcription: Apple's page does not reach this case
		 * in a form this tree can check without the SDK. It is also a choice that
		 * cannot bite a caller: null and zero agree on width and height, so anything
		 * measuring the box reads the same either way. */
		return CGRectNull;
	}
	box = CGRectNull;
	for (i = 0; i < path->count; i++) {
		const cg_subpath *sub = &path->subs[i];

		for (j = 0; j < sub->count; j++) {
			CGFloat x = sub->pts[j * 2];
			CGFloat y = sub->pts[j * 2 + 1];

			if (!seen) {
				box.origin = CGPointMake(x, y);
				box.size = CGSizeZero;
				seen = 1;
				continue;
			}
			if (x < CGRectGetMinX(box)) {
				box.size.width = CGRectGetMaxX(box) - x;
				box.origin.x = x;
			}
			if (y < CGRectGetMinY(box)) {
				box.size.height = CGRectGetMaxY(box) - y;
				box.origin.y = y;
			}
			if (x > CGRectGetMaxX(box)) {
				box.size.width = x - box.origin.x;
			}
			if (y > CGRectGetMaxY(box)) {
				box.size.height = y - box.origin.y;
			}
		}
	}
	return box;
}

CGRect CGPathGetBoundingBox(CGPathRef path)
{
	return cg_bound_box(path);
}

CGRect CGPathGetPathBoundingBox(CGPathRef path)
{
	return cg_bound_box(path);
}

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
		const cg_subpath *sub = &path->subs[i];

		for (j = 0; j < sub->count; j++) {
			/* THE FIRST POINT OF A SUBPATH IS A MOVE; THE REST ARE LINES. That is
			 * how the element stream is defined, and it is also the only place the
			 * subpath structure becomes visible to a caller. */
			element.type = (j == 0) ? kCGPathElementMoveToPoint
						: kCGPathElementAddLineToPoint;
			points[0] = CGPointMake(sub->pts[j * 2], sub->pts[j * 2 + 1]);
			function(info, &element);
		}
		if (sub->closed && sub->count > 0) {
			element.type = kCGPathElementCloseSubpath;
			function(info, &element);
		}
	}
}
