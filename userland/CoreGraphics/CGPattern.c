/*
 * CGPattern — the object, the cell rendered from the caller's callback, and the tile.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ONE PIECE OF REAL GEOMETRY HERE IS THE TILE LOOKUP, and it is worth stating in one place
 * because it is where a pattern goes wrong quietly. Given a point in the drawing context's user
 * space, this library has to find the point of the CELL that belongs there:
 *
 *   1. THE PHASE COMES OFF FIRST, IN USER SPACE, because that is where `CGContextSetPatternPhase`
 *      says it is. The convention taken here is that a phase of (px, py) moves the pattern's origin
 *      to that spot in user space — so the point measured FROM the origin is `user - phase`.
 *   2. THE MATRIX GOES THE OTHER WAY — by its INVERSE — because `matrix` maps pattern space ONTO
 *      user space, and what is wanted is the opposite direction. A rotated matrix therefore rotates
 *      the tiles, which is the visible half of the no-distortion rule in CGPattern.h.
 *   3. THE STEP WRAPS THE RESULT. The tile is `xStep` by `yStep` and the cell is `bounds.size`,
 *      which may be SMALLER — the difference is a gap that `cg_pattern_sample` reports as transparent
 *      rather than as a stretched or wrapped cell. Wrapping is `fmod` brought into 0…step, written
 *      with the negative case because a caller's point is as likely to be left of the origin as
 *      right of it, and a bare `fmod` gives a negative there.
 *
 * AND THE CELL'S OWN ROWS RUN THE OTHER WAY. A bitmap's row 0 is its TOP, while pattern space has y
 * increasing upward like every other space in this library — so the row for a cell coordinate `cy` is
 * `height - 1 - floor(cy)`, and that subtraction is the one line in this file that a probe can catch
 * being absent (a cell whose top and bottom quadrants differ would come out upside down).
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPaint_internal.h>
#include <CoreGraphics/CGPattern.h>
#include <CoreGraphics/CGPattern_internal.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CGPattern {
	int refcount;
	void *info;
	CGRect bounds;
	CGAffineTransform matrix;
	CGFloat x_step;
	CGFloat y_step;
	CGPatternCallbacks callbacks;
};

/* THE CELL: THE BITMAP THE CALLBACK PAINTS INTO, AND THE PIXELS IT LEFT THERE. The bitmap context is
 * released after the callback returns — its bytes are the caller's buffer and outlive it — but the
 * context must exist while the callback draws, which is why the two live together in this struct. */
struct cg_pattern_cell {
	unsigned char *data;
	int width;
	int height;
	int stride;
};

CGPatternRef CGPatternCreate(void *info, CGRect bounds, CGAffineTransform matrix, CGFloat xStep,
			     CGFloat yStep, CGPatternTiling tiling, bool isColored,
			     const CGPatternCallbacks *callbacks)
{
	CGPatternRef pattern;

	if (callbacks == NULL) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate needs callbacks; without a drawPattern "
				"there is no cell to tile\n");
		return NULL;
	}
	if (callbacks->version != 0) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate was given callbacks of version %u; this "
				"library knows version 0\n", callbacks->version);
		return NULL;
	}
	if (callbacks->drawPattern == NULL) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate needs a drawPattern callback\n");
		return NULL;
	}
	/* THE STENCIL REFUSAL, ARGUED IN CGPattern.h: an uncoloured pattern's cell is drawn in the
	 * context's colour AT DRAW TIME, so honouring it means threading the fill state into the callback
	 * and re-rendering the cell per colour. Refused by name rather than drawn as a coloured one, which
	 * would be a cell that ignores the colour it was told to use. */
	if (!isColored) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate does not implement a STENCIL pattern — an "
				"uncoloured cell must be drawn in the context's fill colour at draw "
				"time, and this library will not draw one that ignores it\n");
		return NULL;
	}
	/* AND THE TILING REFUSAL, for the same reason one level down: the two constant-spacing values ask
	 * for spacing held constant in DEVICE space, which is a different lattice and a different
	 * picture. */
	if (tiling != kCGPatternTilingNoDistortion) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate implements kCGPatternTilingNoDistortion "
				"only; a constant-spacing tiling holds the spacing constant in DEVICE "
				"space and would draw a different lattice than the one this library "
				"tiles\n");
		return NULL;
	}
	if (bounds.size.width <= 0.0 || bounds.size.height <= 0.0) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate needs a cell with a size; a bounds with no "
				"area has no pixels to tile\n");
		return NULL;
	}
	if (xStep <= 0.0 || yStep <= 0.0) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate needs a positive step on both axes\n");
		return NULL;
	}
	/* A STEP SMALLER THAN THE CELL IN A DIRECTION MAKES THE TILES OVERLAP, and which overdraws which
	 * would then depend on the order this library walks its pixels — a picture that looks deliberate
	 * and is an accident. Apple's own documentation gives the larger step as the requirement. */
	if (xStep < bounds.size.width || yStep < bounds.size.height) {
		fprintf(stderr, "CG-REFUSE: CGPatternCreate was given a step (%g, %g) smaller than the "
				"cell (%g, %g), so the tiles would overlap\n", (double)xStep,
			(double)yStep, (double)bounds.size.width, (double)bounds.size.height);
		return NULL;
	}
	pattern = calloc(1, sizeof(struct CGPattern));
	if (pattern == NULL) {
		return NULL;
	}
	pattern->refcount = 1;
	pattern->info = info;
	pattern->bounds = bounds;
	pattern->matrix = matrix;
	pattern->x_step = xStep;
	pattern->y_step = yStep;
	pattern->callbacks = *callbacks;
	return pattern;
}

CGPatternRef CGPatternRetain(CGPatternRef pattern)
{
	if (pattern != NULL) {
		pattern->refcount++;
	}
	return pattern;
}

void CGPatternRelease(CGPatternRef pattern)
{
	if (pattern == NULL) {
		return;
	}
	if (--pattern->refcount > 0) {
		return;
	}
	/* THE CALLER'S `info` GOES BACK THROUGH THEIR OWN CALLBACK, exactly once, only when the last
	 * reference goes — the same rule `CGFunctionRelease` follows, and what makes a pattern safe to
	 * hold in two contexts' graphics states at once. */
	if (pattern->callbacks.releaseInfo != NULL) {
		pattern->callbacks.releaseInfo(pattern->info);
	}
	free(pattern);
}

/* ------------------------------------------------------------------------- */
/* the cell                                                                  */
/* ------------------------------------------------------------------------- */

cg_pattern_cell *cg_pattern_render_cell(CGPatternRef pattern)
{
	cg_pattern_cell *cell;
	CGContextRef ctx;
	int width;
	int height;

	if (pattern == NULL || pattern->callbacks.drawPattern == NULL) {
		return NULL;
	}
	if (pattern->bounds.size.width >= 1e7 || pattern->bounds.size.height >= 1e7) {
		return NULL;
	}
	/* THE CELL IS A WHOLE NUMBER OF PIXELS, CEILED: a cell of 2.5 units of pattern space must be
	 * three pixels wide or the right-hand column would be dropped, and a pattern whose cells overlap
	 * by a fraction of a pixel is a visible seam. */
	width = (int)ceil(pattern->bounds.size.width);
	height = (int)ceil(pattern->bounds.size.height);
	if (width <= 0 || height <= 0) {
		return NULL;
	}
	cell = calloc(1, sizeof(cg_pattern_cell));
	if (cell == NULL) {
		return NULL;
	}
	cell->width = width;
	cell->height = height;
	cell->stride = width * 4;
	cell->data = calloc((size_t)height * (size_t)cell->stride, 1);
	if (cell->data == NULL) {
		free(cell);
		return NULL;
	}
	ctx = CGBitmapContextCreate(cell->data, (size_t)width, (size_t)height, 8, (size_t)cell->stride,
				    CGColorSpaceCreateDeviceRGB(),
				    kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	if (ctx == NULL) {
		free(cell->data);
		free(cell);
		return NULL;
	}
	/* PATTERN SPACE'S `bounds.origin` LANDS AT THE BITMAP'S LOWER-LEFT, so a callback drawing at the
	 * cell's own coordinates — which is what `bounds` is FOR — draws where it meant to. The bitmap's
	 * user space already has y increasing upward, the same as pattern space, so no flip is needed
	 * here: the flip is between the bitmap's ROWS and its user space, and `cg_pattern_sample` is the
	 * one place that deals with it. */
	CGContextTranslateCTM(ctx, -pattern->bounds.origin.x, -pattern->bounds.origin.y);
	pattern->callbacks.drawPattern(pattern->info, ctx);
	CGContextRelease(ctx);
	return cell;
}

void cg_pattern_cell_free(cg_pattern_cell *cell)
{
	if (cell == NULL) {
		return;
	}
	free(cell->data);
	free(cell);
}

/* ------------------------------------------------------------------------- */
/* the tile                                                                  */
/* ------------------------------------------------------------------------- */

/* `fmod` BROUGHT INTO 0…STEP, because a bare one is negative for a negative input and a pattern is
 * sampled as often to the left of its origin as to the right. */
static CGFloat cg_wrap(CGFloat v, CGFloat step)
{
	CGFloat w = fmod(v, step);

	if (w < 0.0) {
		w += step;
	}
	return w;
}

void cg_pattern_sample(CGPatternRef pattern, cg_pattern_cell *cell, CGSize phase, CGFloat x,
		       CGFloat y, CGFloat rgba[4])
{
	CGAffineTransform inv;
	CGFloat ux;
	CGFloat uy;
	CGFloat cx;
	CGFloat cy;
	int col;
	int row;
	unsigned char *px;
	double a;

	rgba[0] = 0.0;
	rgba[1] = 0.0;
	rgba[2] = 0.0;
	rgba[3] = 0.0;
	if (pattern == NULL || cell == NULL) {
		return;
	}
	/* (1) THE PHASE IN USER SPACE, (2) THE MATRIX BY ITS INVERSE — see the file's header note. */
	inv = CGAffineTransformInvert(pattern->matrix);
	ux = x - phase.width;
	uy = y - phase.height;
	cx = inv.a * ux + inv.c * uy + inv.tx - pattern->bounds.origin.x;
	cy = inv.b * ux + inv.d * uy + inv.ty - pattern->bounds.origin.y;
	/* (3) THE STEP WRAPS, AND THE CELL IS WHAT IS LEFT INSIDE THE TILE: the region between the cell
	 * and the step is a GAP, not a repeat. */
	cx = cg_wrap(cx, pattern->x_step);
	cy = cg_wrap(cy, pattern->y_step);
	if (cx >= (CGFloat)cell->width || cy >= (CGFloat)cell->height) {
		return;
	}
	col = (int)cx;
	/* THE ROW IS MEASURED FROM THE TOP AND THE CELL FROM THE BOTTOM — see the file's header note. */
	row = cell->height - 1 - (int)cy;
	if (col < 0 || col >= cell->width || row < 0 || row >= cell->height) {
		return;
	}
	px = cell->data + (size_t)row * (size_t)cell->stride + (size_t)col * 4u;
	/* THE CELL'S BYTES ARE PREMULTIPLIED AND A PAINT'S SAMPLES ARE STRAIGHT, so this is the one place
	 * that undoes it. A fully transparent pixel has no colour to divide by and is reported as the
	 * transparent sample it is. */
	a = (double)px[3] / 255.0;
	if (a <= 0.0) {
		return;
	}
	rgba[0] = (CGFloat)((double)px[2] / 255.0 / a);
	rgba[1] = (CGFloat)((double)px[1] / 255.0 / a);
	rgba[2] = (CGFloat)((double)px[0] / 255.0 / a);
	rgba[3] = (CGFloat)a;
}
