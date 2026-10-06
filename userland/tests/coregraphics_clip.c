/*
 * coregraphics_clip — the clip doors, starting with the set of rectangles.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CHECK THAT DECIDES THE IMPLEMENTATION IS THE OVERLAP: two rectangles that overlap are ONE region, so the
 * union is inside and a fill covers both. AN IMPLEMENTATION THAT INTERSECTED THE CLIP ONCE PER RECTANGLE WOULD
 * FAIL IT — its overlap would mean "inside both" and the two outer halves would be clipped away — and that is
 * the reading Apple's sentence ("a path consisting of all rects") forbids.
 *
 * AND THE RESET IS CHECKED, because it is an explicit clause: after the door, the context's path is EMPTY, so
 * a path built before the call is gone rather than being filled afterwards.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGPath.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-CLIP %-66s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 12
#define H 12
#define STRIDE (W * 4)

static unsigned char alpha_at(const unsigned char *buf, int x)
{
	return buf[(size_t)x * 4 + 3];	/* row 0, and this chart is BGRA */
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGRect two[2];

	check("a context is made", c != NULL);

	/* --- two overlapping rectangles are ONE region ---------------------------------------------------- */
	two[0] = CGRectMake(0, 0, 6, H);
	two[1] = CGRectMake(4, 0, 6, H);	/* overlapping the first by two columns */
	memset(canvas, 0, sizeof canvas);
	CGContextClipToRects(c, two, 2);
	CGContextSetRGBFillColor(c, 1, 0, 0, 1);
	CGContextFillRect(c, CGRectMake(0, 0, W, H));
	check("a fill covers BOTH rectangles", alpha_at(canvas, 0) == 0xFF && alpha_at(canvas, 9) == 0xFF);
	check("...and their OVERLAP too — the union, not the intersection",
	      alpha_at(canvas, 4) == 0xFF && alpha_at(canvas, 5) == 0xFF);
	check("...while outside them nothing is painted",
	      alpha_at(canvas, 10) == 0x00 && alpha_at(canvas, 11) == 0x00);
	printf("CG-CLIP %-66s x=0,4,9,11 = %u,%u,%u,%u\n", "...readout", alpha_at(canvas, 0),
	       alpha_at(canvas, 4), alpha_at(canvas, 9), alpha_at(canvas, 11));

	/* --- the path is reset, which is Apple's explicit clause ----------------------------------------- */
	{
		CGMutablePathRef empty = CGPathCreateMutable();
		CGPathRef copy;

		CGContextBeginPath(c);
		CGContextMoveToPoint(c, 1, 1);
		CGContextAddLineToPoint(c, 9, 1);
		CGContextClipToRects(c, two, 1);
		copy = CGContextCopyPath(c);
		check("the context's path is EMPTY afterwards, as Apple's note says",
		      copy != NULL && CGPathEqualToPath(copy, (CGPathRef)empty));
		CGPathRelease(copy);
		CGPathRelease((CGPathRef)empty);
	}

	/* --- and no rectangles adds no clip, on a context that has nothing to intersect with ------------ */
	{
		/* A CLIP IS CUMULATIVE, SO THIS NEEDS A CONTEXT OF ITS OWN: the sections above have already
		 * intersected this one, and "adds no clip" can only be seen where there was none to begin with.
		 * The first version of this check reused the context and failed, correctly. */
		unsigned char fresh[STRIDE * H];
		CGContextRef c2 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
							kCGImageAlphaPremultipliedFirst
							| kCGBitmapByteOrder32Little);

		memset(fresh, 0, sizeof fresh);
		CGContextClipToRects(c2, NULL, 0);
		CGContextSetRGBFillColor(c2, 0, 0, 1, 1);
		CGContextFillRect(c2, CGRectMake(0, 0, W, H));
		check("an empty set of rectangles adds no clip, so a fresh context still fills everywhere",
		      alpha_at(fresh, 0) == 0xFF && alpha_at(fresh, 11) == 0xFF);
		check("...and the clip it did not add is checked AND the context above is still bounded by ITS clip",
		      alpha_at(canvas, 11) == 0x00);
		CGContextRelease(c2);
	}
	check("a NULL context is a no-op rather than a crash", (CGContextClipToRects(NULL, two, 2), 1));

	CGContextRelease(c);
	CGColorSpaceRelease(rgb);
	printf("CG-CLIP: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
