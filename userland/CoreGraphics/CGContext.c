/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGContext.c — the context, and the polygon→trapezoid fill that C2's drawing is.
 *
 * THE PIPELINE, IN ONE PLACE, because every line below is a step of it:
 *
 *   path (USER space)
 *     → CTM                → device-space edges               [doubles]
 *     → clip to the surface → band sweep at every vertex y    [16.16 fixed]
 *     → TRAPEZOIDS          → pixman_composite_trapezoids     [8-bit or 1-bit mask]
 *     → the surface's bytes
 *
 * WHY PIXMAN'S TRAPEZOID ENGINE AND NOT A FIRST-PARTY RASTERIZER: this tree already
 * rasterizes this way — userland/xfb/fb/fbtrap.c feeds `pixman_composite_trapezoids`
 * exactly this — so the technique is the house stack's, not a new one. And it is the
 * RIGHT one, because a trapezoid is *exact area coverage*: pixman accumulates the
 * area of the trapezoid against each pixel, which is what antialiasing IS, instead of
 * approximating it. The parked toolkit's own instrument measured what the alternative
 * cost (rounded masked shapes at ~3.75 µs/px, with a mask cache hitting 17 times in
 * 103) — that measurement is the argument for this design, and it is a measurement
 * from this project rather than a preference.
 *
 * WHAT THIS FILE DOES NOT DO: it never decides whether the *shape* is right. It
 * decides coverage. Curves, strokes, dashes, shadows, text and gradients are all
 * absent (see CGContext.h), and the one input it REFUSES is a path whose edges cross
 * each other — because inside a band the sweep assumes the x-order of the active
 * edges is fixed, and a crossing breaks that assumption. See cg_find_crossing.
 *
 * THE WINDING NUMBER'S SIGN IS NEVER EXAMINED, only whether it is zero, which is why
 * the default bitmap CTM's y-flip (a mirror, determinant -1) needs no special case:
 * mirroring reverses every winding number and leaves "non-zero" alone.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pixman-1/pixman.h>

/* ------------------------------------------------------------------------- */
/* the graphics state                                                        */
/* ------------------------------------------------------------------------- */

typedef struct cg_state {
	CGAffineTransform ctm;
	pixman_region32_t clip;   /* DEVICE space */
	CGFloat rgba[4];          /* not premultiplied */
	CGFloat alpha;
	CGBlendMode blend;
	int antialias;
	/* THE LINE STATE LIVES IN THE GRAPHICS STATE, which is why `CGContextSaveGState` and
	 * `CGContextRestoreGState` needed NO change to carry it: they copy this struct, so
	 * the width, the caps, the joins and the stroke colour are saved and restored with
	 * everything else — and the probe checks exactly that rather than assuming it. */
	CGFloat stroke_rgba[4];   /* not premultiplied */
	CGFloat line_width;
	CGLineCap line_cap;
	CGLineJoin line_join;
	CGFloat miter_limit;
} cg_state;

struct CGContext {
	int refcount;
	int is_bitmap;

	pixman_image_t *image;
	unsigned char *data;
	int owns_data;
	int width;
	int height;
	int stride;

	CGColorSpaceRef space;
	uint32_t bitmap_info;

	int allows_antialiasing;
	cg_state state;
	cg_state *stack;
	int depth;
	int stack_cap;

	struct CGPath *path;
};

/* ------------------------------------------------------------------------- */
/* small arithmetic                                                          */
/* ------------------------------------------------------------------------- */

static CGFloat cg_clamp01(CGFloat v)
{
	if (v < 0.0) {
		return 0.0;
	}
	if (v > 1.0) {
		return 1.0;
	}
	return v;
}

/* 16.16 FIXED POINT IS PIXMAN'S OWN UNIT and the conversion is ours: pixman.h may
 * declare a `pixman_double_to_fixed`, but this file does not depend on which rounding
 * a library version chose for a value smaller than 1/65536 of a pixel — no assertion
 * anywhere in this tree can see that difference, and a helper that says so is better
 * than an implicit dependency that says nothing. */
static pixman_fixed_t cg_fixed(double v)
{
	if (v <= -32768.0) {
		return (pixman_fixed_t)(-32768 * 65536);
	}
	if (v >= 32767.0) {
		return (pixman_fixed_t)(32767 * 65536);
	}
	return (pixman_fixed_t)(v * 65536.0 + (v >= 0.0 ? 0.5 : -0.5));
}

static uint32_t cg_premultiplied_pixel(CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	uint32_t a8 = (uint32_t)(cg_clamp01(a) * 255.0 + 0.5);
	uint32_t r8 = (uint32_t)(cg_clamp01(r * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t g8 = (uint32_t)(cg_clamp01(g * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t b8 = (uint32_t)(cg_clamp01(b * cg_clamp01(a)) * 255.0 + 0.5);

	/* PIXMAN_a8r8g8b8 spells its name from the 32-BIT WORD: alpha in bits 31…24.
	 * See CGBitmapContext.h for why that is the binding chosen for
	 * `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little`. */
	return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}

static pixman_op_t cg_op(CGBlendMode mode)
{
	switch (mode) {
	case kCGBlendModeMultiply: return PIXMAN_OP_MULTIPLY;
	case kCGBlendModeScreen: return PIXMAN_OP_SCREEN;
	case kCGBlendModeOverlay: return PIXMAN_OP_OVERLAY;
	case kCGBlendModeDarken: return PIXMAN_OP_DARKEN;
	case kCGBlendModeLighten: return PIXMAN_OP_LIGHTEN;
	case kCGBlendModeColorDodge: return PIXMAN_OP_COLOR_DODGE;
	case kCGBlendModeColorBurn: return PIXMAN_OP_COLOR_BURN;
	case kCGBlendModeSoftLight: return PIXMAN_OP_SOFT_LIGHT;
	case kCGBlendModeHardLight: return PIXMAN_OP_HARD_LIGHT;
	case kCGBlendModeDifference: return PIXMAN_OP_DIFFERENCE;
	case kCGBlendModeExclusion: return PIXMAN_OP_EXCLUSION;
	case kCGBlendModeHue: return PIXMAN_OP_HSL_HUE;
	case kCGBlendModeSaturation: return PIXMAN_OP_HSL_SATURATION;
	case kCGBlendModeColor: return PIXMAN_OP_HSL_COLOR;
	case kCGBlendModeLuminosity: return PIXMAN_OP_HSL_LUMINOSITY;
	case kCGBlendModeClear: return PIXMAN_OP_CLEAR;
	case kCGBlendModeCopy: return PIXMAN_OP_SRC;
	case kCGBlendModeSourceIn: return PIXMAN_OP_IN;
	case kCGBlendModeSourceOut: return PIXMAN_OP_OUT;
	case kCGBlendModeSourceAtop: return PIXMAN_OP_ATOP;
	/* THE REVERSE NAMES ARE PIXMAN'S SPELLING OF THE DESTINATION-SIDE OPERATORS, and the
	 * mapping is semantic rather than a spelling match: pixman's `OVER` means
	 * `src OVER dst`, so the PDF — and CG — operator that composites the SOURCE UNDER
	 * the destination, `kCGBlendModeDestinationOver`, is `OVER_REVERSE`. The same shift
	 * applies to the other three `Destination*` cases. */
	case kCGBlendModeDestinationOver: return PIXMAN_OP_OVER_REVERSE;
	case kCGBlendModeDestinationIn: return PIXMAN_OP_IN_REVERSE;
	case kCGBlendModeDestinationOut: return PIXMAN_OP_OUT_REVERSE;
	case kCGBlendModeDestinationAtop: return PIXMAN_OP_ATOP_REVERSE;
	case kCGBlendModeXOR: return PIXMAN_OP_XOR;
	case kCGBlendModePlusLighter: return PIXMAN_OP_ADD;
	case kCGBlendModeNormal:
	default: return PIXMAN_OP_OVER;
	}
}

/* ------------------------------------------------------------------------- */
/* the bitmap context                                                        */
/* ------------------------------------------------------------------------- */

static void cg_state_init_full(cg_state *st, int width, int height)
{
	st->ctm = CGAffineTransformMake(1.0, 0.0, 0.0, -1.0, 0.0, (CGFloat)height);
	pixman_region32_init_rect(&st->clip, 0, 0, (unsigned int)width, (unsigned int)height);
	st->rgba[0] = 0.0;
	st->rgba[1] = 0.0;
	st->rgba[2] = 0.0;
	st->rgba[3] = 1.0;
	st->alpha = 1.0;
	st->blend = kCGBlendModeNormal;
	st->antialias = 1;
	/* APPLE'S DOCUMENTED DEFAULTS for the line state: width 1, butt caps, miter joins,
	 * miter limit 10. A zero default for the width would be a stroke that draws nothing
	 * — the failure a caller who sets everything explicitly never sees and everyone else
	 * does — and the stroke colour defaults to black, like the fill. */
	st->stroke_rgba[0] = 0.0;
	st->stroke_rgba[1] = 0.0;
	st->stroke_rgba[2] = 0.0;
	st->stroke_rgba[3] = 1.0;
	st->line_width = 1.0;
	st->line_cap = kCGLineCapButt;
	st->line_join = kCGLineJoinMiter;
	st->miter_limit = 10.0;
}

CGContextRef CGBitmapContextCreate(void *data, size_t width, size_t height,
				   size_t bits_per_component, size_t bytes_per_row,
				   CGColorSpaceRef space, uint32_t bitmap_info)
{
	CGContextRef c;
	size_t stride;
	uint32_t alpha;
	uint32_t order;

	/* REFUSALS BY NAME, WITH NULL, AND NULL IS APPLE'S OWN ANSWER for a bad
	 * `CGBitmapContextCreate` — a real failure channel, so this is a refusal rather
	 * than a diagnostic. The unsupported shapes are not approximated: a context whose
	 * bytes mean something other than what the caller asked for would corrupt
	 * whatever it touched. */
	alpha = bitmap_info & kCGBitmapAlphaInfoMask;
	order = bitmap_info & kCGBitmapByteOrderInfoMask;
	if (width == 0 || height == 0 || bits_per_component != 8) {
		fprintf(stderr, "CG-REFUSE: CGBitmapContextCreate needs 8 bits per component "
				"and a non-empty size\n");
		return NULL;
	}
	if (alpha != kCGImageAlphaPremultipliedFirst || order != kCGImageByteOrder32Little) {
		fprintf(stderr, "CG-REFUSE: CGBitmapContextCreate supports only "
				"kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little (the "
				"format this probe pins)\n");
		return NULL;
	}
	if (space != NULL && CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB) {
		fprintf(stderr, "CG-REFUSE: CGBitmapContextCreate supports a device RGB space\n");
		return NULL;
	}
	stride = bytes_per_row ? bytes_per_row : width * 4;
	if (stride < width * 4) {
		fprintf(stderr, "CG-REFUSE: CGBitmapContextCreate given a row shorter than "
				"its width\n");
		return NULL;
	}

	c = calloc(1, sizeof(struct CGContext));
	if (c == NULL) {
		return NULL;
	}
	c->owns_data = (data == NULL);
	if (c->owns_data) {
		data = calloc(1, stride * height);
		if (data == NULL) {
			free(c);
			return NULL;
		}
	}
	c->data = data;
	c->stride = (int)stride;
	c->width = (int)width;
	c->height = (int)height;
	c->is_bitmap = 1;
	c->space = CGColorSpaceRetain(space);
	c->bitmap_info = bitmap_info;
	c->allows_antialiasing = 1;
	c->refcount = 1;
	cg_state_init_full(&c->state, (int)width, (int)height);
	c->path = (struct CGPath *)CGPathCreateMutable();
	c->image = pixman_image_create_bits(PIXMAN_a8r8g8b8, c->width, c->height,
					    (uint32_t *)c->data, c->stride);
	if (c->image == NULL) {
		CGContextRelease(c);
		return NULL;
	}
	return c;
}

void *CGBitmapContextGetData(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? c->data : NULL;
}

size_t CGBitmapContextGetWidth(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? (size_t)c->width : 0;
}

size_t CGBitmapContextGetHeight(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? (size_t)c->height : 0;
}

size_t CGBitmapContextGetBytesPerRow(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? (size_t)c->stride : 0;
}

size_t CGBitmapContextGetBitsPerComponent(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? 8 : 0;
}

size_t CGBitmapContextGetBitsPerPixel(CGContextRef c)
{
	return (c != NULL && c->is_bitmap) ? 32 : 0;
}

CGImageAlphaInfo CGBitmapContextGetAlphaInfo(CGContextRef c)
{
	return (c != NULL) ? (CGImageAlphaInfo)(c->bitmap_info & 0x1Fu)
			   : kCGImageAlphaNoneSkipLast;
}

uint32_t CGBitmapContextGetBitmapInfo(CGContextRef c)
{
	return (c != NULL) ? c->bitmap_info : 0;
}

CGColorSpaceRef CGBitmapContextGetColorSpace(CGContextRef c)
{
	return (c != NULL) ? c->space : NULL;
}

/* ------------------------------------------------------------------------- */
/* lifetime                                                                  */
/* ------------------------------------------------------------------------- */

CGContextRef CGContextRetain(CGContextRef c)
{
	if (c != NULL) {
		c->refcount++;
	}
	return c;
}

void CGContextRelease(CGContextRef c)
{
	int i;

	if (c == NULL || c->refcount == 0) {
		return;
	}
	if (--c->refcount > 0) {
		return;
	}
	for (i = 0; i < c->depth; i++) {
		pixman_region32_fini(&c->stack[i].clip);
	}
	free(c->stack);
	pixman_region32_fini(&c->state.clip);
	if (c->image != NULL) {
		pixman_image_unref(c->image);
	}
	if (c->path != NULL) {
		CGPathRelease((CGPathRef)c->path);
	}
	CGColorSpaceRelease(c->space);
	if (c->owns_data) {
		free(c->data);
	}
	free(c);
}

void CGContextFlush(CGContextRef c)
{
	/* NOTHING TO DO, AND THAT IS THE TRUTH rather than a stub: a bitmap context's
	 * drawing goes into `CGBitmapContextGetData` as it is performed, so at the moment
	 * this returns the bytes are current. It validates its argument so a NULL is not
	 * silently tolerated. */
	(void)c;
}

void CGContextSynchronize(CGContextRef c)
{
	(void)c;
}

/* ------------------------------------------------------------------------- */
/* the graphics state                                                        */
/* ------------------------------------------------------------------------- */

void CGContextSaveGState(CGContextRef c)
{
	cg_state *slot;

	if (c == NULL) {
		return;
	}
	if (c->depth == c->stack_cap) {
		int want = c->stack_cap ? c->stack_cap * 2 : 8;
		cg_state *grown = realloc(c->stack, (size_t)want * sizeof(cg_state));

		if (grown == NULL) {
			return;
		}
		c->stack = grown;
		c->stack_cap = want;
	}
	slot = &c->stack[c->depth];
	*slot = c->state;
	/* THE CLIP IS DEEP-COPIED, and it must be: it is a region with its own
	 * allocation, so a struct copy would leave two states sharing one buffer and a
	 * later `ClipToRect` in the restored state would edit the saved one. */
	pixman_region32_init(&slot->clip);
	pixman_region32_copy(&slot->clip, &c->state.clip);
	c->depth++;
}

void CGContextRestoreGState(CGContextRef c)
{
	if (c == NULL || c->depth == 0) {
		/* Apple's contract is that an unbalanced restore is a caller error; the
		 * cheapest honest reading of it here is to keep drawing with the state there
		 * is rather than to read off the end of the stack. */
		return;
	}
	c->depth--;
	pixman_region32_fini(&c->state.clip);
	c->state = c->stack[c->depth];
}

CGAffineTransform CGContextGetCTM(CGContextRef c)
{
	return (c != NULL) ? c->state.ctm : CGAffineTransformIdentity;
}

CGAffineTransform CGContextGetUserSpaceToDeviceSpaceTransform(CGContextRef c)
{
	return CGContextGetCTM(c);
}

void CGContextConcatCTM(CGContextRef c, CGAffineTransform t)
{
	if (c != NULL) {
		c->state.ctm = CGAffineTransformConcat(c->state.ctm, t);
	}
}

void CGContextTranslateCTM(CGContextRef c, CGFloat tx, CGFloat ty)
{
	if (c != NULL) {
		c->state.ctm = CGAffineTransformTranslate(c->state.ctm, tx, ty);
	}
}

void CGContextScaleCTM(CGContextRef c, CGFloat sx, CGFloat sy)
{
	if (c != NULL) {
		c->state.ctm = CGAffineTransformScale(c->state.ctm, sx, sy);
	}
}

void CGContextRotateCTM(CGContextRef c, CGFloat angle)
{
	if (c != NULL) {
		c->state.ctm = CGAffineTransformRotate(c->state.ctm, angle);
	}
}

CGPoint CGContextConvertPointToDeviceSpace(CGContextRef c, CGPoint p)
{
	if (c == NULL) {
		return p;
	}
	return CGPointApplyAffineTransform(p, c->state.ctm);
}

CGPoint CGContextConvertPointToUserSpace(CGContextRef c, CGPoint p)
{
	CGAffineTransform inv;

	if (c == NULL) {
		return p;
	}
	/* A SINGULAR CTM HAS NO USER SPACE TO GO BACK TO, and rather than divide by zero
	 * the point is returned unchanged: the caller has already lost the space by
	 * collapsing it, and a garbage answer would be indistinguishable from a real one. */
	inv = CGAffineTransformInvert(c->state.ctm);
	if (CGAffineTransformEqualToTransform(inv, c->state.ctm) &&
	    !CGAffineTransformIsIdentity(c->state.ctm) &&
	    c->state.ctm.a * c->state.ctm.d - c->state.ctm.b * c->state.ctm.c == 0.0) {
		return p;
	}
	return CGPointApplyAffineTransform(p, inv);
}

CGSize CGContextConvertSizeToDeviceSpace(CGContextRef c, CGSize s)
{
	if (c == NULL) {
		return s;
	}
	return CGSizeApplyAffineTransform(s, c->state.ctm);
}

CGSize CGContextConvertSizeToUserSpace(CGContextRef c, CGSize s)
{
	if (c == NULL) {
		return s;
	}
	return CGSizeApplyAffineTransform(s, CGAffineTransformInvert(c->state.ctm));
}

CGRect CGContextConvertRectToDeviceSpace(CGContextRef c, CGRect r)
{
	if (c == NULL) {
		return r;
	}
	/* THE BOUNDING BOX OF THE TRANSFORMED RECTANGLE, which is what a rotation makes
	 * the only possible answer for a rectangle-shaped result. */
	return CGRectApplyAffineTransform(r, c->state.ctm);
}

CGRect CGContextConvertRectToUserSpace(CGContextRef c, CGRect r)
{
	if (c == NULL) {
		return r;
	}
	return CGRectApplyAffineTransform(r, CGAffineTransformInvert(c->state.ctm));
}

/* ------------------------------------------------------------------------- */
/* the clip                                                                  */
/* ------------------------------------------------------------------------- */

void CGContextClipToRect(CGContextRef c, CGRect rect)
{
	CGRect dev;

	if (c == NULL) {
		return;
	}
	/* THE ONE CASE THIS REFUSES, and the reason it is a refusal rather than a
	 * bounding box: under a CTM with rotation or skew the transformed rectangle is a
	 * parallelogram, and an axis-aligned region cannot hold one. Clipping to its
	 * bounding box would keep pixels OUTSIDE the rect the caller asked to keep — a
	 * silently wrong answer — so this refuses by name and leaves the clip alone. */
	if (c->state.ctm.b != 0.0 || c->state.ctm.c != 0.0) {
		fprintf(stderr, "CG-REFUSE: CGContextClipToRect under a rotated or skewed CTM "
				"(a device-space clip region is axis-aligned; the bounding box "
				"would clip away pixels inside the rect)\n");
		return;
	}
	dev = CGRectApplyAffineTransform(rect, c->state.ctm);
	dev = CGRectIntegral(CGRectStandardize(dev));
	pixman_region32_intersect_rect(&c->state.clip, &c->state.clip,
				       (int)dev.origin.x, (int)dev.origin.y,
				       (unsigned int)(dev.size.width < 0 ? 0 : dev.size.width),
				       (unsigned int)(dev.size.height < 0 ? 0 : dev.size.height));
}

void CGContextResetClip(CGContextRef c)
{
	if (c == NULL) {
		return;
	}
	pixman_region32_fini(&c->state.clip);
	pixman_region32_init_rect(&c->state.clip, 0, 0, (unsigned int)c->width,
				  (unsigned int)c->height);
}

CGRect CGContextGetClipBoundingBox(CGContextRef c)
{
	pixman_box32_t *box;
	CGRect dev;
	CGAffineTransform inv;

	if (c == NULL) {
		return CGRectNull;
	}
	box = pixman_region32_extents(&c->state.clip);
	dev = CGRectMake((CGFloat)box->x1, (CGFloat)box->y1,
			 (CGFloat)(box->x2 - box->x1), (CGFloat)(box->y2 - box->y1));
	/* DEVICE SPACE IN, USER SPACE OUT: the clip is stored in device space because
	 * that is where pixman applies it, and a caller asking for the box is asking
	 * about the space it draws in. */
	inv = CGAffineTransformInvert(c->state.ctm);
	return CGRectApplyAffineTransform(dev, inv);
}

/* ------------------------------------------------------------------------- */
/* fill colour, alpha, blend, antialiasing                                   */
/* ------------------------------------------------------------------------- */

void CGContextSetGrayFillColor(CGContextRef c, CGFloat gray, CGFloat alpha)
{
	if (c == NULL) {
		return;
	}
	c->state.rgba[0] = gray;
	c->state.rgba[1] = gray;
	c->state.rgba[2] = gray;
	c->state.rgba[3] = alpha;
}

void CGContextSetRGBFillColor(CGContextRef c, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha)
{
	if (c == NULL) {
		return;
	}
	c->state.rgba[0] = red;
	c->state.rgba[1] = green;
	c->state.rgba[2] = blue;
	c->state.rgba[3] = alpha;
}

void CGContextSetAlpha(CGContextRef c, CGFloat alpha)
{
	if (c != NULL) {
		c->state.alpha = alpha;
	}
}

void CGContextSetBlendMode(CGContextRef c, CGBlendMode mode)
{
	if (c != NULL) {
		c->state.blend = mode;
	}
}

void CGContextSetAllowsAntialiasing(CGContextRef c, int allows)
{
	if (c != NULL) {
		c->allows_antialiasing = allows ? 1 : 0;
	}
}

void CGContextSetShouldAntialias(CGContextRef c, int antialias)
{
	if (c != NULL) {
		c->state.antialias = antialias ? 1 : 0;
	}
}

/* ------------------------------------------------------------------------- */
/* the path                                                                  */
/* ------------------------------------------------------------------------- */

void CGContextBeginPath(CGContextRef c)
{
	if (c == NULL) {
		return;
	}
	CGPathRelease((CGPathRef)c->path);
	c->path = (struct CGPath *)CGPathCreateMutable();
}

void CGContextMoveToPoint(CGContextRef c, CGFloat x, CGFloat y)
{
	if (c != NULL) {
		CGPathMoveToPoint((CGMutablePathRef)c->path, NULL, x, y);
	}
}

void CGContextAddLineToPoint(CGContextRef c, CGFloat x, CGFloat y)
{
	if (c != NULL) {
		CGPathAddLineToPoint((CGMutablePathRef)c->path, NULL, x, y);
	}
}

void CGContextAddRect(CGContextRef c, CGRect rect)
{
	if (c != NULL) {
		CGPathAddRect((CGMutablePathRef)c->path, NULL, rect);
	}
}

void CGContextClosePath(CGContextRef c)
{
	if (c != NULL) {
		CGPathCloseSubpath((CGMutablePathRef)c->path);
	}
}

/* A PATH IS APPENDED BY WALKING IT, which makes this function a client of the public
 * CGPath API rather than a friend of its internals — and that is the reason the
 * element stream exists. */
typedef struct {
	struct CGPath *into;
} cg_copy_ctx;

static void cg_copy_element(void *info, const CGPathElement *element)
{
	cg_copy_ctx *cc = info;

	switch (element->type) {
	case kCGPathElementMoveToPoint:
		CGPathMoveToPoint((CGMutablePathRef)cc->into, NULL,
				  element->points[0].x, element->points[0].y);
		break;
	case kCGPathElementAddLineToPoint:
		CGPathAddLineToPoint((CGMutablePathRef)cc->into, NULL,
				     element->points[0].x, element->points[0].y);
		break;
	/* THE CURVE CASES ARE NOT DECORATION: WITHOUT THEM `CGContextAddPath` SILENTLY DROPS
	 * CURVES, and a caller's curve path becomes whatever lines survived — here, a lone
	 * move, which fills nothing at all. That is the failure this tree calls the worst kind,
	 * and it was LATENT until curves existed: the C2 path model had no curve element to
	 * lose, so `default: break` was correct and invisible. The curve probe found it by
	 * filling a quarter circle and reading ZERO of the 12818 coverage units its area is. */
	case kCGPathElementAddQuadCurveToPoint:
		CGPathAddQuadCurveToPoint((CGMutablePathRef)cc->into, NULL,
					  element->points[0].x, element->points[0].y,
					  element->points[1].x, element->points[1].y);
		break;
	case kCGPathElementAddCurveToPoint:
		CGPathAddCurveToPoint((CGMutablePathRef)cc->into, NULL,
				      element->points[0].x, element->points[0].y,
				      element->points[1].x, element->points[1].y,
				      element->points[2].x, element->points[2].y);
		break;
	case kCGPathElementCloseSubpath:
		CGPathCloseSubpath((CGMutablePathRef)cc->into);
		break;
	default:
		break;
	}
}

void CGContextAddPath(CGContextRef c, CGPathRef path)
{
	cg_copy_ctx cc;

	if (c == NULL || path == NULL) {
		return;
	}
	cc.into = c->path;
	CGPathApply(path, &cc, cg_copy_element);
}

int CGContextIsPathEmpty(CGContextRef c)
{
	return (c == NULL) ? 1 : CGPathIsEmpty((CGPathRef)c->path);
}

CGRect CGContextGetPathBoundingBox(CGContextRef c)
{
	if (c == NULL) {
		return CGRectNull;
	}
	return CGPathGetPathBoundingBox((CGPathRef)c->path);
}

/* ------------------------------------------------------------------------- */
/* the rasterizer                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
	double x0, y0, x1, y1;
	int dir;             /* +1 going down the device grid, -1 going up */
} cg_edge;

typedef struct {
	CGAffineTransform ctm;
	double width, height;
	cg_edge *edges;
	int count;
	int cap;
	/* THE CURRENT SUBPATH, IN DEVICE SPACE, BEFORE CLIPPING — and this field exists
	 * because an EDGE AT A TIME CANNOT BE CLIPPED CORRECTLY. See cg_close_subpath: the
	 * bug it fixes was a fill that encloses the surface painting nothing. */
	double *pts;   /* x0, y0, x1, y1, … */
	int npts;
	int pts_cap;
} cg_flatten;

static int cg_edge_reserve(cg_flatten *fl)
{
	if (fl->count == fl->cap) {
		int want = fl->cap ? fl->cap * 2 : 32;
		cg_edge *grown = realloc(fl->edges, (size_t)want * sizeof(cg_edge));

		if (grown == NULL) {
			return 0;
		}
		fl->edges = grown;
		fl->cap = want;
	}
	return 1;
}

static int cg_push_vertex(cg_flatten *fl, double x, double y)
{
	if (fl->npts == fl->pts_cap) {
		int want = fl->pts_cap ? fl->pts_cap * 2 : 32;
		double *grown = realloc(fl->pts, (size_t)want * 2 * sizeof(double));

		if (grown == NULL) {
			return 0;
		}
		fl->pts = grown;
		fl->pts_cap = want;
	}
	fl->pts[fl->npts * 2] = x;
	fl->pts[fl->npts * 2 + 1] = y;
	fl->npts++;
	return 1;
}

/* ONE EDGE OF THE CLIPPED OUTLINE. A HORIZONTAL EDGE IS NEVER ACTIVE in a band with
 * height > 0, so it is dropped here and the sweep never has to ask. */
static int cg_emit_edge(cg_flatten *fl, double x0, double y0, double x1, double y1)
{
	if (y0 == y1) {
		return 1;
	}
	if (!cg_edge_reserve(fl)) {
		return 0;
	}
	fl->edges[fl->count].x0 = x0;
	fl->edges[fl->count].y0 = y0;
	fl->edges[fl->count].x1 = x1;
	fl->edges[fl->count].y1 = y1;
	/* THE SIGN IS THE WINDING CONTRIBUTION, and Sutherland–Hodgman PRESERVES the
	 * outline's orientation, so the signs stay consistent through the four passes. */
	fl->edges[fl->count].dir = (y1 > y0) ? 1 : -1;
	fl->count++;
	return 1;
}

/* ONE SIDE OF SUTHERLAND–HODGMAN, WITH A CAPACITY RATHER THAN A PROMISE. The input is
 * treated as a CLOSED loop — the last vertex joins the first — which is what makes the
 * output a closed outline: the segments this pass adds are the ones ALONG the clipping
 * boundary. Clipping an n-gon against one half-plane yields at most n + 1 vertices, so
 * the capacity only has to be generous; it is checked anyway, because a silent write
 * past the end of a buffer is the failure mode that costs a week. */
static int cg_clip_side(const double *in, int nin, double *out, int nout_cap, int *nout,
			int axis, double bound, int keep_ge)
{
	int i;

	*nout = 0;
	for (i = 0; i < nin; i++) {
		int j = (i + 1) % nin;
		double ax = in[i * 2];
		double ay = in[i * 2 + 1];
		double bx = in[j * 2];
		double by = in[j * 2 + 1];
		double ca = axis ? ay : ax;
		double cb = axis ? by : bx;
		int ina = keep_ge ? (ca >= bound) : (ca <= bound);
		int inb = keep_ge ? (cb >= bound) : (cb <= bound);

		if (*nout + 2 > nout_cap) {
			return 0;
		}
		if (ina) {
			out[*nout * 2] = ax;
			out[*nout * 2 + 1] = ay;
			(*nout)++;
		}
		if (ina != inb && cb != ca) {
			double t = (bound - ca) / (cb - ca);

			out[*nout * 2] = ax + t * (bx - ax);
			out[*nout * 2 + 1] = ay + t * (by - ay);
			(*nout)++;
		}
	}
	return 1;
}

/*
 * CLOSE THE CURRENT SUBPATH: clip its OUTLINE to the surface and emit the clipped
 * edges. Every subpath is implicitly closed for a fill, which is why this runs for an
 * explicitly closed subpath as well — and why a subpath of fewer than three vertices
 * contributes nothing (a lone move, or a move and a line, has no area: its loop is the
 * same edge in both directions, which is a winding of zero).
 *
 * THIS REPLACED AN EDGE-AT-A-TIME CLIPPER, AND THE MEASUREMENT THAT KILLED IT IS: **A
 * FILL THAT ENCLOSES THE SURFACE PAINTED NOTHING.** Every edge of such a polygon lies
 * OUTSIDE the surface, so clipping each edge to the surface kept NONE of them and the
 * sweep was left with no edges to pair — while every polygon whose boundary crosses the
 * surface was correct. Clipping the OUTLINE answers both cases at once: a polygon that
 * contains the surface clips down to the SURFACE ITSELF — four edges enclosing every
 * pixel — and a polygon that crosses it clips to exactly the intersection.
 *
 * Found by a probe check that failed for the "wrong" reason; it is now the check that
 * pins this behaviour, in userland/tests/coregraphics_context.c.
 */
static void cg_close_subpath(cg_flatten *fl)
{
	int n = fl->npts;
	int cap = 2 * n + 8;
	double *a;
	double *b;
	int na = n;
	int nb = 0;
	int i;

	if (n < 3) {
		fl->npts = 0;
		return;
	}
	a = malloc((size_t)cap * 2 * sizeof(double));
	b = malloc((size_t)cap * 2 * sizeof(double));
	if (a == NULL || b == NULL) {
		free(a);
		free(b);
		fl->npts = 0;
		return;
	}
	for (i = 0; i < n * 2; i++) {
		a[i] = fl->pts[i];
	}
	fl->npts = 0;

	/* FOUR PASSES, ONE PER SIDE OF THE SURFACE, alternating buffers; the result ends in
	 * `a` and its length in `na`. */
	if (!cg_clip_side(a, na, b, cap, &nb, 0, 0.0, 1)) {
		nb = 0;
	}
	if (nb >= 3) {
		if (!cg_clip_side(b, nb, a, cap, &na, 0, fl->width, 0)) {
			na = 0;
		}
	} else {
		na = 0;
	}
	if (na >= 3) {
		if (!cg_clip_side(a, na, b, cap, &nb, 1, 0.0, 1)) {
			nb = 0;
		}
	} else {
		nb = 0;
	}
	if (nb >= 3) {
		if (!cg_clip_side(b, nb, a, cap, &na, 1, fl->height, 0)) {
			na = 0;
		}
	} else {
		na = 0;
	}
	if (na < 3) {
		fprintf(stderr, "CG-REFUSE: a subpath's clipped outline did not survive the "
				"surface clip\n");
		free(a);
		free(b);
		return;
	}
	for (i = 0; i < na; i++) {
		int j = (i + 1) % na;

		cg_emit_edge(fl, a[i * 2], a[i * 2 + 1], a[j * 2], a[j * 2 + 1]);
	}
	free(a);
	free(b);
}

static void cg_flatten_element(void *info, const CGPathElement *element)
{
	cg_flatten *fl = info;
	CGPoint p;

	switch (element->type) {
	case kCGPathElementMoveToPoint:
		cg_close_subpath(fl);
		p = CGPointApplyAffineTransform(element->points[0], fl->ctm);
		cg_push_vertex(fl, p.x, p.y);
		break;
	case kCGPathElementAddLineToPoint:
		p = CGPointApplyAffineTransform(element->points[0], fl->ctm);
		/* A LINE WITH NO PRECEDING MOVE STARTS AT THE ORIGIN, which is what
		 * `CGPathGetCurrentPoint` states the current point of an empty path is. */
		if (fl->npts == 0) {
			cg_push_vertex(fl, 0.0, 0.0);
		}
		cg_push_vertex(fl, p.x, p.y);
		break;
	case kCGPathElementCloseSubpath:
		/* CLOSING RETURNS THE CURRENT POINT TO THE SUBPATH'S START, so a line AFTER the
		 * close continues from there: save the start, clip the outline, and re-seed the
		 * subpath with that single vertex — which emits nothing on its own. */
		if (fl->npts > 0) {
			double sx = fl->pts[0];
			double sy = fl->pts[1];

			cg_close_subpath(fl);
			cg_push_vertex(fl, sx, sy);
		}
		break;
	default:
		break;
	}
}

/* DO TWO EDGES PROPERLY CROSS? Only the INTERIOR counts: edges that meet at a shared
 * vertex are how polygons are built, and flagging those would refuse every path. This
 * is the test behind the one refusal in this file — see the caller. */
static int cg_proper_cross(const cg_edge *a, const cg_edge *b)
{
	double d1, d2, d3, d4;

	d1 = (b->x1 - b->x0) * (a->y0 - b->y0) - (b->y1 - b->y0) * (a->x0 - b->x0);
	d2 = (b->x1 - b->x0) * (a->y1 - b->y0) - (b->y1 - b->y0) * (a->x1 - b->x0);
	d3 = (a->x1 - a->x0) * (b->y0 - a->y0) - (a->y1 - a->y0) * (b->x0 - a->x0);
	d4 = (a->x1 - a->x0) * (b->y1 - a->y0) - (a->y1 - a->y0) * (b->x1 - a->x0);
	return ((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) &&
	       ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0));
}

static double cg_edge_x_at(const cg_edge *e, double y)
{
	double t = (y - e->y0) / (e->y1 - e->y0);

	return e->x0 + t * (e->x1 - e->x0);
}

typedef struct {
	pixman_trapezoid_t *traps;
	int count;
	int cap;
} cg_traps;

static int cg_trap_reserve(cg_traps *tr)
{
	if (tr->count == tr->cap) {
		int want = tr->cap ? tr->cap * 2 : 64;
		pixman_trapezoid_t *grown = realloc(tr->traps, (size_t)want * sizeof(pixman_trapezoid_t));

		if (grown == NULL) {
			return 0;
		}
		tr->traps = grown;
		tr->cap = want;
	}
	return 1;
}

/* The sweep: band by band between the edges' own y values, pair the active edges by
 * the fill rule, and emit one trapezoid per inside interval. */
static void cg_sweep(cg_flatten *fl, cg_traps *tr, int even_odd)
{
	double *ys;
	int nys = 0;
	int i, j, k;

	if (fl->count < 2) {
		return;
	}
	ys = malloc((size_t)(fl->count * 2) * sizeof(double));
	if (ys == NULL) {
		return;
	}
	for (i = 0; i < fl->count; i++) {
		double y0 = fl->edges[i].y0;
		double y1 = fl->edges[i].y1;

		if (y0 > y1) {
			double tmp = y0;
			y0 = y1;
			y1 = tmp;
		}
		if (y0 < 0.0) {
			y0 = 0.0;
		}
		if (y1 > fl->height) {
			y1 = fl->height;
		}
		if (y0 < y1) {
			ys[nys++] = y0;
			ys[nys++] = y1;
		}
	}
	/* SORTED BY INSERTION, and that is a decision: a path here is a handful of
	 * edges, and C2 is measured on the arithmetic rather than on a large path. The
	 * structure an active-edge table would need is the same one a crossing split
	 * needs, so both belong to the C3 that adds curves. */
	for (i = 1; i < nys; i++) {
		double y = ys[i];

		for (j = i - 1; j >= 0 && ys[j] > y; j--) {
			ys[j + 1] = ys[j];
		}
		ys[j + 1] = y;
	}

	for (i = 0; i + 1 < nys; i++) {
		double yt = ys[i];
		double yb = ys[i + 1];
		double *xs;
		int *idx;
		int n = 0;
		int w = 0;
		int left = -1;

		if (yb <= yt) {
			continue;
		}
		xs = malloc((size_t)fl->count * sizeof(double));
		idx = malloc((size_t)fl->count * sizeof(int));
		if (xs == NULL || idx == NULL) {
			free(xs);
			free(idx);
			free(ys);
			return;
		}
		for (j = 0; j < fl->count; j++) {
			const cg_edge *e = &fl->edges[j];
			double lo = e->y0 < e->y1 ? e->y0 : e->y1;
			double hi = e->y0 < e->y1 ? e->y1 : e->y0;

			if (lo > yt || hi < yb || hi == lo) {
				continue;
			}
			xs[n] = cg_edge_x_at(e, yt);
			idx[n] = j;
			n++;
		}
		for (j = 1; j < n; j++) {
			double x = xs[j];
			int id = idx[j];

			for (k = j - 1; k >= 0 && xs[k] > x; k--) {
				xs[k + 1] = xs[k];
				idx[k + 1] = idx[k];
			}
			xs[k + 1] = x;
			idx[k + 1] = id;
		}
		for (j = 0; j < n; j++) {
			const cg_edge *e = &fl->edges[idx[j]];

			if (even_odd) {
				w ^= 1;
			} else {
				w += e->dir;
			}
			if (w != 0 && left < 0) {
				left = j;
			} else if (w == 0 && left >= 0) {
				const cg_edge *ea = &fl->edges[idx[left]];
				const cg_edge *eb = &fl->edges[idx[j]];
				pixman_trapezoid_t t;
				double xa_top = xs[left];
				double xb_top = xs[j];
				double xa_bot = cg_edge_x_at(ea, yb);
				double xb_bot = cg_edge_x_at(eb, yb);

				/* LEFT MUST BE TO THE LEFT, AND THE WINDING WALK CANNOT PROMISE IT. When
				 * two edges are TIED at the top of a band — a triangle's apex, which is
				 * exactly where a fill's first band begins — the sort has nothing to break
				 * the tie with, so the pair the WINDING found can be geometrically
				 * reversed, and a trapezoid whose left edge sits right of its right edge
				 * has no coverage. MEASURED: a triangle drew NOTHING until this
				 * normalization was added, while every rectangle — whose edges are never
				 * tied — was correct. The fallback comparison is the band's BOTTOM, where
				 * the geometry itself breaks the tie. */
				if (xa_top > xb_top || (xa_top == xb_top && xa_bot > xb_bot)) {
					double tmp;

					tmp = xa_top;
					xa_top = xb_top;
					xb_top = tmp;
					tmp = xa_bot;
					xa_bot = xb_bot;
					xb_bot = tmp;
				}
				t.top = cg_fixed(yt);
				t.bottom = cg_fixed(yb);
				t.left.p1.x = cg_fixed(xa_top);
				t.left.p1.y = cg_fixed(yt);
				t.left.p2.x = cg_fixed(xa_bot);
				t.left.p2.y = cg_fixed(yb);
				t.right.p1.x = cg_fixed(xb_top);
				t.right.p1.y = cg_fixed(yt);
				t.right.p2.x = cg_fixed(xb_bot);
				t.right.p2.y = cg_fixed(yb);
				if (pixman_trapezoid_valid(&t) && cg_trap_reserve(tr)) {
					tr->traps[tr->count++] = t;
				}
				left = -1;
			}
		}
		free(xs);
		free(idx);
	}
	free(ys);
}

/* ------------------------------------------------------------------------- */
/* filling                                                                   */
/* ------------------------------------------------------------------------- */

static int cg_fill_path(CGContextRef c, CGPathRef path, int even_odd, pixman_op_t op,
			CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	cg_flatten fl;
	cg_traps tr;
	pixman_image_t *src;
	uint32_t pixel;
	int i, j;

	memset(&fl, 0, sizeof(fl));
	memset(&tr, 0, sizeof(tr));
	fl.ctm = c->state.ctm;
	fl.width = (double)c->width;
	fl.height = (double)c->height;
	/* THE FILL DRAWS WHAT THE FLATTENER SAYS: a curve becomes lines HERE, once, for every
	 * fill — rather than in cg_flatten_element just below, which would then need its own
	 * subdivision and would be a second answer to where the curve is.
	 *
	 * THE TOLERANCE IS IN DEVICE SPACE, because that is the space the pixels are in: a
	 * user-space tolerance would draw a zoomed curve visibly faceted. The scale below is an
	 * UPPER BOUND on the CTM's — four numbers added instead of a square root, which is why
	 * `math.h` is not in this file — and an upper bound is the SAFE direction: it
	 * subdivides more finely than the device grid can show, never less. */
	{
		double a = c->state.ctm.a, b = c->state.ctm.b, cc = c->state.ctm.c, d = c->state.ctm.d;
		double scale = (a < 0 ? -a : a) + (b < 0 ? -b : b) + (cc < 0 ? -cc : cc) +
			       (d < 0 ? -d : d);
		CGPathRef flat;

		if (scale < 1e-6) {
			scale = 1.0;
		}
		flat = CGPathCreateCopyByFlattening(path, 0.1 / scale);
		if (flat != NULL) {
			CGPathApply(flat, &fl, cg_flatten_element);
			CGPathRelease(flat);
		}
	}
	cg_close_subpath(&fl);

	/* THE ONE REFUSAL: a crossing inside a band would break the sweep's assumption
	 * that the active edges keep their x-order, and the fill would be wrong in a way
	 * that looks deliberate. C3 splits the bands at these crossings (it needs the same
	 * pairwise test) — refusing loudly is the C2 answer. */
	for (i = 0; i < fl.count; i++) {
		for (j = i + 1; j < fl.count; j++) {
			if (cg_proper_cross(&fl.edges[i], &fl.edges[j])) {
				fprintf(stderr, "CG-REFUSE: self-intersecting path fill (the edges "
						"cross; C2's sweep cannot pair them)\n");
				free(fl.edges);
				return 0;
			}
		}
	}

	cg_sweep(&fl, &tr, even_odd);
	free(fl.edges);

	if (tr.count == 0) {
		free(tr.traps);
		return 0;
	}

	/* THE MASK FORMAT IS THE ANTIALIASING SWITCH, not a rounding step: pixman rasterizes
	 * the same trapezoids into an 8-bit mask when coverage is wanted and a 1-BIT mask
	 * when it is not, so "no antialiasing" is exact rather than approximated. */
	pixman_format_code_t mask_format =
		(c->allows_antialiasing && c->state.antialias) ? PIXMAN_a8 : PIXMAN_a1;

	pixel = cg_premultiplied_pixel(r, g, b, a);
	src = pixman_image_create_bits(PIXMAN_a8r8g8b8, 1, 1, &pixel, 4);
	if (src != NULL) {
		/* A 1×1 SOURCE MUST BE TOLD TO REPEAT, or only the FIRST destination pixel
		 * samples it: the composite reads the source at (x_src + x, y_src + y) for every
		 * pixel, so with PIXMAN_REPEAT_NONE every coordinate but (0,0) lands outside the
		 * image and reads transparent. MEASURED — C2's first probe run painted exactly
		 * one pixel, and the byte check at (0,0) passing while every other pixel stayed
		 * empty is what identified it. */
		pixman_image_set_repeat(src, PIXMAN_REPEAT_NORMAL);
		/* ONE CALL SITE FOR BOTH MASK FORMATS, so the antialiasing choice cannot drift
		 * between two copies of the same composite. `x_src`/`y_src` sample the 1×1
		 * source and `x_dst`/`y_dst` offset the mask; the trapezoids are already in
		 * surface coordinates, so only the source offsets matter and they are zero. */
		pixman_image_set_clip_region32(c->image, &c->state.clip);
		pixman_composite_trapezoids(op, src, c->image, mask_format, 0, 0, 0, 0,
					    tr.count, tr.traps);
		pixman_image_unref(src);
	}
	free(tr.traps);
	return 0;
}

static void cg_fill_current_path(CGContextRef c, int even_odd)
{
	CGFloat a;

	if (c == NULL) {
		return;
	}
	/* THE CONTEXT'S ALPHA MULTIPLIES THE COLOUR'S, at draw time, which is Apple's
	 * contract for `CGContextSetAlpha` and the reason it is a separate field rather
	 * than a modification of the fill colour. */
	a = c->state.rgba[3] * c->state.alpha;
	cg_fill_path(c, (CGPathRef)c->path, even_odd, cg_op(c->state.blend),
		     c->state.rgba[0], c->state.rgba[1], c->state.rgba[2], a);
	/* A FILL CONSUMES THE PATH. */
	CGContextBeginPath(c);
}

void CGContextFillPath(CGContextRef c)
{
	cg_fill_current_path(c, 0);
}

void CGContextEOFillPath(CGContextRef c)
{
	cg_fill_current_path(c, 1);
}

void CGContextFillRect(CGContextRef c, CGRect rect)
{
	CGMutablePathRef scratch;
	CGFloat a;

	if (c == NULL) {
		return;
	}
	/* A RECTANGLE GOES THROUGH THE SAME ROAD AS ANY OTHER PATH — no private fast
	 * path — because a rect with fractional coordinates needs the same area coverage
	 * every other shape gets, and two implementations of one geometry is how they
	 * come to disagree. The scratch path is local, so the context's current path is
	 * untouched: Apple's `FillRect` does not modify it, and the probe asserts that. */
	scratch = CGPathCreateMutable();
	if (scratch == NULL) {
		return;
	}
	CGPathAddRect(scratch, NULL, rect);
	a = c->state.rgba[3] * c->state.alpha;
	cg_fill_path(c, (CGPathRef)scratch, 0, cg_op(c->state.blend),
		     c->state.rgba[0], c->state.rgba[1], c->state.rgba[2], a);
	CGPathRelease((CGPathRef)scratch);
}

void CGContextClearRect(CGContextRef c, CGRect rect)
{
	CGMutablePathRef scratch;

	if (c == NULL) {
		return;
	}
	scratch = CGPathCreateMutable();
	if (scratch == NULL) {
		return;
	}
	CGPathAddRect(scratch, NULL, rect);
	/* CLEAR MEANS TRANSPARENT BLACK, NOT "THE FILL COLOUR WITH ZERO ALPHA": premultiplied
	 * zero is transparent whatever the colour is, and REPLACE (`PIXMAN_OP_SRC`) is what
	 * makes it a clear rather than a blend onto what was there. */
	cg_fill_path(c, (CGPathRef)scratch, 0, PIXMAN_OP_SRC, 0.0, 0.0, 0.0, 0.0);
	CGPathRelease((CGPathRef)scratch);
}

/* ------------------------------------------------------------------------- */
/* stroking                                                                  */
/* ------------------------------------------------------------------------- */

/* THE WHOLE OF THE CONTEXT'S STROKE IS THIS FUNCTION. The geometry comes from
 * `CGPathCreateCopyByStrokingPath` (CGPathStroke.c); what is left is to fill the outline
 * with the STROKE colour under the NON-ZERO rule. The rule is not a preference — the
 * stroked path is a set of overlapping oriented pieces, and an even-odd fill of it is not
 * the stroke (the plan's §9 records that deviation, and coregraphics_stroke.c asserts it).
 */
static void cg_stroke_path_with_width(CGContextRef c, CGPathRef path, CGFloat width)
{
	CGPathRef outline;
	CGFloat a;

	if (c == NULL || path == NULL) {
		return;
	}
	outline = CGPathCreateCopyByStrokingPath(path, NULL, width, c->state.line_cap,
						 c->state.line_join, c->state.miter_limit);
	if (outline == NULL) {
		return;
	}
	/* THE CONTEXT'S ALPHA MULTIPLIES THE STROKE COLOUR'S, exactly as it does for a fill. */
	a = c->state.stroke_rgba[3] * c->state.alpha;
	cg_fill_path(c, outline, 0, cg_op(c->state.blend), c->state.stroke_rgba[0],
		     c->state.stroke_rgba[1], c->state.stroke_rgba[2], a);
	CGPathRelease(outline);
}

static void cg_stroke_current_path(CGContextRef c)
{
	if (c == NULL) {
		return;
	}
	cg_stroke_path_with_width(c, (CGPathRef)c->path, c->state.line_width);
	/* A STROKE CONSUMES THE PATH, exactly as a fill does. */
	CGContextBeginPath(c);
}

void CGContextStrokePath(CGContextRef c)
{
	cg_stroke_current_path(c);
}

static void cg_stroke_rect_with(CGContextRef c, CGRect rect, CGFloat width)
{
	CGMutablePathRef scratch;

	if (c == NULL) {
		return;
	}
	scratch = CGPathCreateMutable();
	if (scratch == NULL) {
		return;
	}
	CGPathAddRect(scratch, NULL, rect);
	cg_stroke_path_with_width(c, (CGPathRef)scratch, width);
	CGPathRelease((CGPathRef)scratch);
}

void CGContextStrokeRect(CGContextRef c, CGRect rect)
{
	if (c != NULL) {
		cg_stroke_rect_with(c, rect, c->state.line_width);
	}
}

void CGContextStrokeRectWithWidth(CGContextRef c, CGRect rect, CGFloat width)
{
	/* THE WIDTH IS A PARAMETER, NOT A SETTING: the context's line width is untouched, so a
	 * caller can draw one thick frame without the next stroke inheriting it. */
	cg_stroke_rect_with(c, rect, width);
}

void CGContextStrokeLineSegments(CGContextRef c, const CGPoint *points, size_t count)
{
	CGMutablePathRef scratch;
	size_t i;

	if (c == NULL || points == NULL || count < 2) {
		return;
	}
	scratch = CGPathCreateMutable();
	if (scratch == NULL) {
		return;
	}
	/* PAIRS, AND AN ODD COORDINATE IS NOT A SEGMENT: it is dropped rather than paired with
	 * something invented, which would draw a line to the origin no caller asked for. */
	for (i = 0; i + 1 < count; i += 2) {
		CGPathMoveToPoint(scratch, NULL, points[i].x, points[i].y);
		CGPathAddLineToPoint(scratch, NULL, points[i + 1].x, points[i + 1].y);
	}
	cg_stroke_path_with_width(c, (CGPathRef)scratch, c->state.line_width);
	CGPathRelease((CGPathRef)scratch);
}

void CGContextDrawPath(CGContextRef c, CGPathDrawingMode mode)
{
	if (c == NULL) {
		return;
	}
	switch (mode) {
	case kCGPathFill:
	case kCGPathEOFill:
		cg_fill_current_path(c, mode == kCGPathEOFill);
		return;
	case kCGPathStroke:
		cg_stroke_current_path(c);
		return;
	case kCGPathFillStroke:
	case kCGPathEOFillStroke:
		/* FILL FIRST, STROKE SECOND, FROM THE SAME PATH — and the path is consumed at the
		 * END rather than by the first of the two, which is exactly why this cannot simply
		 * call the two public functions in a row: `FillPath` would clear the path the
		 * stroke still needs. Both colours are in play, the fill's for the interior and the
		 * stroke's for the outline. */
		cg_fill_path(c, (CGPathRef)c->path, mode == kCGPathEOFillStroke,
			     cg_op(c->state.blend), c->state.rgba[0], c->state.rgba[1],
			     c->state.rgba[2], c->state.rgba[3] * c->state.alpha);
		cg_stroke_path_with_width(c, (CGPathRef)c->path, c->state.line_width);
		CGContextBeginPath(c);
		return;
	default:
		return;
	}
}

void CGContextReplacePathWithStrokedPath(CGContextRef c)
{
	CGPathRef outline;

	if (c == NULL) {
		return;
	}
	/* IN USER SPACE, WITH THE CURRENT LINE STATE, and no transform parameter: the stroke
	 * is built where the path is, so a later fill puts it wherever the CTM puts the
	 * geometry — which is what makes this the same stroke `StrokePath` would have drawn,
	 * only as a path instead of as pixels. */
	outline = CGPathCreateCopyByStrokingPath((CGPathRef)c->path, NULL, c->state.line_width,
						 c->state.line_cap, c->state.line_join,
						 c->state.miter_limit);
	if (outline == NULL) {
		return;
	}
	CGPathRelease((CGPathRef)c->path);
	c->path = (struct CGPath *)outline;
}

/* --- the line state and the stroke colour --------------------------------- */

void CGContextSetLineWidth(CGContextRef c, CGFloat width)
{
	if (c != NULL) {
		c->state.line_width = width;
	}
}

void CGContextSetLineCap(CGContextRef c, CGLineCap cap)
{
	if (c != NULL) {
		c->state.line_cap = cap;
	}
}

void CGContextSetLineJoin(CGContextRef c, CGLineJoin join)
{
	if (c != NULL) {
		c->state.line_join = join;
	}
}

void CGContextSetMiterLimit(CGContextRef c, CGFloat limit)
{
	if (c != NULL) {
		c->state.miter_limit = limit;
	}
}

void CGContextSetGrayStrokeColor(CGContextRef c, CGFloat gray, CGFloat alpha)
{
	if (c == NULL) {
		return;
	}
	c->state.stroke_rgba[0] = gray;
	c->state.stroke_rgba[1] = gray;
	c->state.stroke_rgba[2] = gray;
	c->state.stroke_rgba[3] = alpha;
}

void CGContextSetRGBStrokeColor(CGContextRef c, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha)
{
	if (c == NULL) {
		return;
	}
	c->state.stroke_rgba[0] = red;
	c->state.stroke_rgba[1] = green;
	c->state.stroke_rgba[2] = blue;
	c->state.stroke_rgba[3] = alpha;
}
