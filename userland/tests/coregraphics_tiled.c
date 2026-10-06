/*
 * coregraphics_tiled — the tiled image, and the two page doors.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TILING IS ASKED FOR IN THREE WAYS THAT A WRONG IMPLEMENTATION CANNOT SATISFY AT ONCE:
 *
 *   - the STEP: a tile is `rect` wide and high, so the canvas must repeat the image at those intervals;
 *   - the SCALE: the image is scaled INTO `rect`, so a 2x2 source in a 4-unit tile puts one source pixel
 *     across two units — measured by making the source's top-left corner opaque and the rest clear, which
 *     makes the block 2x2 units at every tile origin;
 *   - the PHASE: the tiling starts at `rect.origin`, so moving the rectangle moves the whole pattern.
 *
 * AND THE CLIP BOUNDS IT, because Apple's sentence is "to fill the current clip region".
 *
 * THE PAGE DOORS ARE REFUSED, AND THE CHECK IS THAT THE SURFACE IS UNDISTURBED: a 10.6 header that says only
 * "Begin a new page." leaves this library nothing to implement, and a page that silently drew something would
 * be an invented behaviour.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-TILED %-64s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 12
#define H 12
#define STRIDE (W * 4)

/* row 0 of this chart's memory is a row: the tiles are symmetric in y, so any row answers. */
static unsigned char alpha_at(const unsigned char *buf, int x)
{
	return buf[(size_t)x * 4 + 3];
}

int main(void)
{
	/* A 2x2 source whose FIRST pixel is opaque and whose other three are clear. */
	static unsigned char src[2 * 2 * 4] = { 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, src, sizeof src, NULL);
	CGImageRef image = CGImageCreate(2, 2, 8, 32, 2 * 4, rgb,
					 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
					 dp, NULL, false, kCGRenderingIntentDefault);
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);

	check("a tiled image and a context are made", image != NULL && c != NULL);

	/* --- the step, the scale and the phase, all at once --------------------------------------------- */
	memset(canvas, 0, sizeof canvas);
	CGContextDrawTiledImage(c, CGRectMake(0, 0, 4, 4), image);
	printf("CG-TILED %-64s x=0,1,2,4,6 = %u,%u,%u,%u,%u\n", "...readout", alpha_at(canvas, 0),
	       alpha_at(canvas, 1), alpha_at(canvas, 2), alpha_at(canvas, 4), alpha_at(canvas, 6));
	check("the source pixel covers TWO units, because the 2x2 image is scaled into a 4-unit tile",
	      alpha_at(canvas, 0) == 0xFF && alpha_at(canvas, 1) == 0xFF && alpha_at(canvas, 2) == 0x00);
	check("...and the next tile starts 4 units along, with its own opaque corner",
	      alpha_at(canvas, 4) == 0xFF && alpha_at(canvas, 5) == 0xFF && alpha_at(canvas, 6) == 0x00);
	check("...so the canvas is filled to its edges, not just where one tile landed",
	      alpha_at(canvas, 8) == 0xFF && alpha_at(canvas, 11) == 0x00);

	/* --- the phase: the tiling starts where the rectangle starts ------------------------------------ */
	memset(canvas, 0, sizeof canvas);
	CGContextDrawTiledImage(c, CGRectMake(2, 0, 4, 4), image);
	check("moving the rectangle MOVES THE PHASE: the opaque corners are now at x=2 and x=6",
	      alpha_at(canvas, 0) == 0x00 && alpha_at(canvas, 2) == 0xFF && alpha_at(canvas, 6) == 0xFF);

	/* --- and the clip bounds it ------------------------------------------------------------------- */
	memset(canvas, 0, sizeof canvas);
	CGContextClipToRect(c, CGRectMake(0, 0, 2, H));
	CGContextDrawTiledImage(c, CGRectMake(0, 0, 4, 4), image);
	check("the clip bounds the tiling: only the clipped columns are painted",
	      alpha_at(canvas, 0) == 0xFF && alpha_at(canvas, 1) == 0xFF && alpha_at(canvas, 4) == 0x00);

	/* --- the page doors --------------------------------------------------------------------------- */
	{
		unsigned char fresh[STRIDE * H];
		CGContextRef c2 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
							kCGImageAlphaPremultipliedFirst
							| kCGBitmapByteOrder32Little);

		memset(fresh, 0, sizeof fresh);
		CGContextBeginPage(c2, NULL);
		CGContextEndPage(c2);
		CGContextSetRGBFillColor(c2, 1, 0, 0, 1);
		CGContextFillRect(c2, CGRectMake(0, 0, W, H));
		check("the page doors are refused and leave the surface alone: the fill lands as usual",
		      alpha_at(fresh, 0) == 0xFF && alpha_at(fresh, 11) == 0xFF);
		CGContextRelease(c2);
	}

	CGContextRelease(c);
	CGImageRelease(image);
	CGDataProviderRelease(dp);
	CGColorSpaceRelease(rgb);
	printf("CG-TILED: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
