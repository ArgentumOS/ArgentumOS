/*
 * coregraphics_layers — transparency layers, and the difference they make to a number.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A TRANSPARENCY LAYER IS NOT A CONVENIENCE AND ITS EFFECT IS NOT COSMETIC: Apple's header says the layer's
 * contents are composited into a separate buffer and the result composited into the context ONCE, "using the
 * global alpha of the context", and that inside the layer the global alpha is 1. SO THE MEASURABLE DIFFERENCE
 * IS IN AN OVERLAP:
 *
 *   WITHOUT a layer, at global alpha 0.5, two overlapping half-alpha shapes give 25% where one covers the
 *   surface and 43.75% where both do, because each is attenuated by the outer alpha before it blends.
 *
 *   WITH a layer, the same two shapes blend inside the group at full strength — 50% and 75% — and the whole
 *   result is attenuated ONCE, giving 25% and 37.5%.
 *
 * The single-covered area is 25% either way; THE OVERLAP IS THE NUMBER THAT TELLS THE TWO APART, and a
 * "transparency layer" that merely saved and restored the state would produce 112 where this must produce 96.
 *
 * EVERY SAMPLE IS ON A FULL-HEIGHT BAND, because this library's bitmap rows do not run the way its y axis
 * does and a check that names a row would be checking the row arithmetic.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-LAYERS %-62s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 8
#define H 8
#define STRIDE (W * 4)

static unsigned char alpha_at(const unsigned char *buf, int x)
{
	return buf[(size_t)x * 4 + 3];	/* row 0, and this chart is BGRA */
}

static void two_bands(CGContextRef c)
{
	CGContextSetRGBFillColor(c, 1, 0, 0, 0.5);
	CGContextFillRect(c, CGRectMake(0, 0, 4, H));
	CGContextFillRect(c, CGRectMake(2, 0, 4, H));
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	unsigned char plain_single, plain_overlap, group_single, group_overlap;

	check("a context is made", c != NULL && CGBitmapContextGetData(c) == canvas);

	/* --- the same two shapes, without a layer ------------------------------------------------ */
	CGContextSetAlpha(c, 0.5);
	memset(canvas, 0, sizeof canvas);
	two_bands(c);
	plain_single = alpha_at(canvas, 0);
	plain_overlap = alpha_at(canvas, 3);
	printf("CG-LAYERS %-62s plain: single=%u overlap=%u\n", "...readout", plain_single, plain_overlap);
	check("without a layer the outer alpha attenuates each shape, so the overlap is 43.75%",
	      plain_single > 0x38 && plain_single < 0x48 && plain_overlap > 0x68 && plain_overlap < 0x78);

	/* --- and the same two shapes inside one ------------------------------------------------ */
	memset(canvas, 0, sizeof canvas);
	CGContextBeginTransparencyLayer(c, NULL);
	/* THE THREE EXCEPTIONS CANNOT BE READ BACK — this library has no `CGContextGetAlpha` to ask — SO THE
	 * OVERLAP'S NUMBER AT THE END IS WHAT PROVES THEM: an alpha of 1 inside the layer is the only thing
	 * that makes the inner blend reach 75% before the outer 0.5 is applied once. */
	two_bands(c);
	CGContextEndTransparencyLayer(c);
	group_single = alpha_at(canvas, 0);
	group_overlap = alpha_at(canvas, 3);
	printf("CG-LAYERS %-62s group: single=%u overlap=%u\n", "...readout", group_single, group_overlap);
	check("...and the layer's contents blend at FULL strength and are attenuated once, so 37.5%",
	      group_single > 0x38 && group_single < 0x48 && group_overlap > 0x58 && group_overlap < 0x68);
	check("THE NUMBER THAT TELLS THEM APART IS THE OVERLAP: 96 with a layer, 112 without",
	      group_overlap < plain_overlap - 8);

	/* --- the state comes back -------------------------------------------------------------- */
	{
		memset(canvas, 0, sizeof canvas);
		CGContextSetRGBFillColor(c, 0, 0, 0, 1);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("...and the OUTER alpha is back afterwards, so the next fill is attenuated again",
		      alpha_at(canvas, 2) > 0x70 && alpha_at(canvas, 2) < 0x90);
	}

	/* --- a layer bounded by a rectangle ------------------------------------------------------ */
	{
		memset(canvas, 0, sizeof canvas);
		CGContextSetAlpha(c, 1.0);
		CGContextBeginTransparencyLayerWithRect(c, CGRectMake(0, 0, 4, H), NULL);
		CGContextSetRGBFillColor(c, 0, 1, 0, 1);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));	/* the WHOLE surface, inside a bounded layer */
		CGContextEndTransparencyLayer(c);
		check("a layer bounded by a rectangle keeps the drawing inside it",
		      alpha_at(canvas, 0) == 0xFF && alpha_at(canvas, 3) == 0xFF
		      && alpha_at(canvas, 4) == 0x00 && alpha_at(canvas, 7) == 0x00);
	}

	/* --- nesting, and the stack staying balanced --------------------------------------------- */
	{
		memset(canvas, 0, sizeof canvas);
		CGContextBeginTransparencyLayer(c, NULL);
		CGContextBeginTransparencyLayer(c, NULL);
		CGContextEndTransparencyLayer(c);
		CGContextEndTransparencyLayer(c);
		CGContextSetRGBFillColor(c, 0, 0, 0, 1);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("two nested layers end cleanly, and drawing after them lands on the context again",
		      alpha_at(canvas, 2) == 0xFF);
	}

	/* --- and what is refused ---------------------------------------------------------------- */
	{
		CGContextEndTransparencyLayer(c);	/* an end with nothing to end */
		CGContextSetRGBFillColor(c, 1, 1, 1, 1);
		memset(canvas, 0, sizeof canvas);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("an end with no begin is refused rather than corrupting the surface",
		      alpha_at(canvas, 2) == 0xFF);
		CGContextSetBlendMode(c, kCGBlendModeMultiply);
		memset(canvas, 0, sizeof canvas);
		CGContextBeginTransparencyLayer(c, NULL);
		two_bands(c);
		CGContextEndTransparencyLayer(c);
		check("a layer under a NON-NORMAL blend is refused by name, and leaves the surface alone",
		      alpha_at(canvas, 3) == 0x00);
		CGContextSetBlendMode(c, kCGBlendModeNormal);
	}

	CGContextRelease(c);
	CGColorSpaceRelease(rgb);
	printf("CG-LAYERS: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
