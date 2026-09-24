/*
 * CGPaint — turning "what colour is this point?" into something pixman can composite.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE LOOP. Every gradient, shading and pattern in this library is drawn by walking the pixels of a
 * device rectangle, mapping each one back into user space, and asking the paint what colour is
 * there. That is not the fastest possible design — pixman has gradient sources of its own — and it
 * is the one this library took, for a reason worth stating rather than leaving to be discovered:
 * THE EXTENSION SEMANTICS ARE THE HARD PART AND THEY ARE ALREADY OURS. `CGGradientDrawingOptions`
 * extends one end or the other or neither, independently; pixman's repeat modes extend both ends or
 * neither, so a caller asking for one end would need a second pass over a clipped half-plane. The
 * parameter is computed here anyway to know which side of the ramp a point is on, so the sample is
 * one more arithmetic step on a value that already exists.
 */
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGPaint_internal.h>

#include <stdlib.h>

/* THE ONE CLAMP IN THE LIBRARY, and it moved here from CGContext.c with the packer it serves. */
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

uint32_t cg_premultiplied_pixel(CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	uint32_t a8 = (uint32_t)(cg_clamp01(a) * 255.0 + 0.5);
	uint32_t r8 = (uint32_t)(cg_clamp01(r * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t g8 = (uint32_t)(cg_clamp01(g * cg_clamp01(a)) * 255.0 + 0.5);
	uint32_t b8 = (uint32_t)(cg_clamp01(b * cg_clamp01(a)) * 255.0 + 0.5);

	/* PIXMAN_a8r8g8b8 SPELLS ITS NAME FROM THE 32-BIT WORD: alpha in bits 31…24, then red, green,
	 * blue. In memory on a little-endian machine that is B, G, R, A — which is exactly the
	 * `PremultipliedFirst | ByteOrder32Little` chart the contexts are built with. See
	 * CGBitmapContext.h for the binding itself and CGImage.c's `cg_image_layout` for the
	 * general matrix; this function is the single stop for a colour that is not an image. */
	return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}

pixman_image_t *cg_paint_image(int px, int py, int w, int h, CGAffineTransform ctm, CGFloat alpha,
			       cg_paint_eval_fn eval, void *info)
{
	CGAffineTransform inv;
	pixman_image_t *image;
	uint32_t *bits;
	int stride;
	int i;
	int j;

	if (eval == NULL || w <= 0 || h <= 0) {
		return NULL;
	}
	/* THE IMAGE ALLOCATES ITS OWN BYTES — `pixman_image_create_bits` with a NULL buffer — so the
	 * buffer's lifetime IS the image's and there is nothing for a caller to free separately. The
	 * stride is asked for rather than assumed: pixman may pad a row, and indexing with `w * 4`
	 * where the image actually strides wider would draw a picture sheared by the difference. */
	image = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, NULL, 0);
	if (image == NULL) {
		return NULL;
	}
	bits = pixman_image_get_data(image);
	stride = pixman_image_get_stride(image);
	inv = CGAffineTransformInvert(ctm);

	for (j = 0; j < h; j++) {
		uint32_t *row = (uint32_t *)((char *)bits + (size_t)j * (size_t)stride);

		for (i = 0; i < w; i++) {
			/* THE PIXEL'S CENTRE, NOT ITS CORNER. A shape drawn at integer coordinates
			 * covers pixel (n) from n to n+1, so the pixel's representative point is its
			 * centre; sampling the corner would shift the whole paint half a pixel and make
			 * a one-pixel-wide band land on the boundary instead of inside. */
			CGFloat dx = (CGFloat)(px + i) + 0.5;
			CGFloat dy = (CGFloat)(py + j) + 0.5;
			CGFloat ux = inv.a * dx + inv.c * dy + inv.tx;
			CGFloat uy = inv.b * dx + inv.d * dy + inv.ty;
			CGFloat rgba[4] = { 0.0, 0.0, 0.0, 0.0 };

			eval(info, ux, uy, rgba);
			row[i] = cg_premultiplied_pixel(rgba[0], rgba[1], rgba[2], rgba[3] * alpha);
		}
	}
	/* NO REPEAT: the image covers exactly the rectangle that was asked for, and a sample from
	 * outside it should read transparent rather than wrap. */
	pixman_image_set_repeat(image, PIXMAN_REPEAT_NONE);
	return image;
}
