/*
 * coregraphics_layer — the offscreen surface, and the round trip that makes it worth having.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT IS CHECKED HERE IS THE ROUND TRIP AND NOT THE SIGNATURES: something is drawn into the layer's own
 * context, the layer is then drawn into another context, and the DESTINATION'S PIXELS are read to see where it
 * landed. A layer that stored a size and a context but never got its contents out would pass every pointer
 * check and fail these.
 *
 * THE LAYER'S CONTENT IS ASYMMETRIC ON PURPOSE — the left half of its surface is painted and the right half is
 * not — so "where did it land" is a question about X ALONE. A symmetric fill would make the answer the same
 * whichever way the rows of the two surfaces happened to run, and a vertical flip between them would go
 * unnoticed.
 */
#include <CoreGraphics/CGLayer.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGImage.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-LAYER %-64s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 8
#define H 8
#define STRIDE (W * 4)

/* The memory order of this library's one bitmap chart: blue, green, red, alpha. */
static unsigned char pixel_at(const unsigned char *buf, int x, int y, int c)
{
	return buf[(size_t)y * STRIDE + (size_t)x * 4 + (size_t)c];
}

static int is_red(const unsigned char *buf, int x, int y)
{
	return pixel_at(buf, x, y, 3) == 0xFF && pixel_at(buf, x, y, 2) == 0xFF
	       && pixel_at(buf, x, y, 1) == 0x00 && pixel_at(buf, x, y, 0) == 0x00;
}

static int is_clear(const unsigned char *buf, int x, int y)
{
	return pixel_at(buf, x, y, 3) == 0x00;
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef dest = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
						  kCGImageAlphaPremultipliedFirst
						  | kCGBitmapByteOrder32Little);
	CGLayerRef layer;
	CGContextRef inside;

	check("a destination context is made", dest != NULL);

	layer = CGLayerCreateWithContext(dest, CGSizeMake(W, H), NULL);
	check("a layer is made over that context", layer != NULL);
	check("...and CGLayerGetSize answers what was asked",
	      layer != NULL && CGLayerGetSize(layer).width == W && CGLayerGetSize(layer).height == H);
	inside = CGLayerGetContext(layer);
	check("...and it has a context of its own, not the one it was made from",
	      inside != NULL && inside != dest);

	/* THE SURFACE IS ROUNDED UP TO WHOLE PIXELS while the SIZE keeps the units it was asked in — the two are
	 * different questions and both are answered. */
	{
		CGLayerRef fractional = CGLayerCreateWithContext(dest, CGSizeMake(8.5, 4.25), NULL);
		CGContextRef fc = CGLayerGetContext(fractional);

		check("a fractional size is kept as asked, and its surface is rounded UP",
		      fractional != NULL && CGLayerGetSize(fractional).width == 8.5
		      && CGLayerGetSize(fractional).height == 4.25
		      && CGBitmapContextGetWidth(fc) == 9 && CGBitmapContextGetHeight(fc) == 5);
		CGLayerRelease(fractional);
	}

	/* --- nothing to draw into, and nothing to draw ------------------------------------------- */
	check("a layer with no area is refused", CGLayerCreateWithContext(dest, CGSizeMake(0, 8), NULL) == NULL);
	check("...and one made from no context is refused", CGLayerCreateWithContext(NULL, CGSizeMake(8, 8),
										    NULL) == NULL);
	check("the retain and release doors are NULL-SAFE, as the header says",
	      CGLayerRetain(NULL) == NULL && CGLayerGetSize(NULL).width == 0.0);
	CGLayerRelease(NULL);
	check("...which is what makes the NULL release above a no-op rather than a crash", 1);

	/* --- THE ROUND TRIP --------------------------------------------------------------------- */
	{
		unsigned char *const c = canvas;
		size_t i;

		/* The layer's own context is what gets drawn into: the LEFT HALF of its surface, full height. */
		CGContextSetRGBFillColor(inside, 1.0, 0.0, 0.0, 1.0);
		CGContextFillRect(inside, CGRectMake(0, 0, 4, H));
		memset(canvas, 0, sizeof canvas);
		CGContextDrawLayerInRect(dest, CGRectMake(0, 0, W, H), layer);
		check("drawing the layer in its own size puts its LEFT half on the left",
		      is_red(c, 0, 0) && is_red(c, 3, 0) && is_clear(c, 4, 0) && is_clear(c, 7, 0));

		/* AND THE LAYER STILL HOLDS ITS CONTENT: a layer is not consumed by being drawn. */
		memset(canvas, 0, sizeof canvas);
		CGContextDrawLayerAtPoint(dest, CGPointMake(2, 2), layer);
		check("drawing it AT A POINT moves it by exactly that point",
		      is_clear(c, 0, 0) && is_clear(c, 1, 0) && is_red(c, 2, 0) && is_red(c, 5, 0)
		      && is_clear(c, 6, 0));
		printf("CG-LAYER %-64s at (0,1,2,5,6) = %d,%d,%d,%d,%d\n", "...readout",
		       is_red(c, 0, 0), is_red(c, 1, 0), is_red(c, 2, 0), is_red(c, 5, 0), is_red(c, 6, 0));

		/* SCALING IS WHAT THE HEADER PROMISES ("the contents are scaled, if necessary, to fit"): the same
		 * layer into HALF THE WIDTH reaches half as far. THE RECTANGLE IS FULL-HEIGHT ON PURPOSE — a
		 * rectangle shorter than the surface leaves part of it outside the draw, and this library's bitmap
		 * rows do not run the same way as its y axis, so a short rectangle would make the answer depend on
		 * which row was read rather than on the scaling. Reading a full-height band asks the question in
		 * X ALONE. */
		memset(canvas, 0, sizeof canvas);
		CGContextDrawLayerInRect(dest, CGRectMake(0, 0, 4, H), layer);
		check("...and drawing it into half the WIDTH SCALES it there",
		      is_red(c, 0, 0) && is_red(c, 1, 0) && is_clear(c, 2, 0) && is_clear(c, 3, 0));

		/* the layer's own surface is untouched by all of that */
		{
			CGContextRef again = CGLayerGetContext(layer);

			memset(canvas, 0, sizeof canvas);
			CGContextDrawLayerInRect(dest, CGRectMake(0, 0, W, H), layer);
			check("...and after three draws the layer's own content is unchanged",
			      again == inside && is_red(c, 0, 0) && is_clear(c, 4, 0));
		}
		(void)i;
	}

	/* --- the identity, and the reference count ---------------------------------------------- */
	check("the layer's type id is its own", CGLayerGetTypeID() != (CGTypeID)0
	      && CGLayerGetTypeID() != CGImageGetTypeID()
	      && CGLayerGetTypeID() != (CGTypeID)0);
	{
		CGLayerRef held = CGLayerRetain(layer);

		CGLayerRelease(layer);
		check("a retained layer survives one release and still answers",
		      held == layer && CGLayerGetSize(held).width == W);
		CGLayerRelease(held);
	}

	CGContextRelease(dest);
	CGColorSpaceRelease(rgb);
	printf("CG-LAYER: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
