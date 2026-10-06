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

	/* --- CLIP TO A MASK: THE TWO RULES, ON ONE BYTE PATTERN ------------------------------------- */
	{
		/* A GRAY IMAGE AND AN IMAGE MASK OVER THE SAME BYTES, FULL HEIGHT SO THE QUESTION IS IN X
		 * ALONE. The image keeps what is WHITE (the alpha itself), the mask keeps what is BLACK (the
		 * inverse alpha) — so the two halves of the canvas must be painted by the two doors. */
		static const unsigned char half_and_half[W * H];
		unsigned char dark[W * H];
		CGDataProviderRef dp;
		CGImageRef gray_mask;
		CGImageRef image_mask;
		CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
		size_t i;

		for (i = 0; i < sizeof dark; i++) {
			dark[i] = 0;	/* replaced below: left half dark, right half light */
		}
		for (i = 0; i < sizeof dark; i++) {
			dark[i] = ((i % W) < W / 2) ? 0 : 0xFF;
		}
		(void)half_and_half;
		dp = CGDataProviderCreateWithData(NULL, dark, sizeof dark, NULL);
		gray_mask = CGImageCreate(W, H, 8, 8, W, gray, kCGImageAlphaNone, dp, NULL, false,
					  kCGRenderingIntentDefault);
		image_mask = CGImageMaskCreate(W, H, 8, 8, W, dp, NULL, false);

		{
			unsigned char fresh[STRIDE * H];
			CGContextRef c3 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
								kCGImageAlphaPremultipliedFirst
								| kCGBitmapByteOrder32Little);

			CGContextClipToMask(c3, CGRectMake(0, 0, W, H), gray_mask);
			CGContextSetRGBFillColor(c3, 1, 0, 0, 1);
			CGContextFillRect(c3, CGRectMake(0, 0, W, H));
			printf("CG-CLIP %-66s gray mask x=1,10 = %u,%u\n", "...readout", alpha_at(fresh, 1),
			       alpha_at(fresh, 10));
			check("a GRAY IMAGE as a mask keeps what is WHITE: the right half paints",
			      alpha_at(fresh, 10) == 0xFF && alpha_at(fresh, 1) == 0x00);

			memset(fresh, 0, sizeof fresh);
			c3 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
						   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
			CGContextClipToMask(c3, CGRectMake(0, 0, W, H), image_mask);
			CGContextSetRGBFillColor(c3, 1, 0, 0, 1);
			CGContextFillRect(c3, CGRectMake(0, 0, W, H));
			check("...and an IMAGE MASK over the SAME BYTES keeps what is BLACK, the inverse alpha",
			      alpha_at(fresh, 1) == 0xFF && alpha_at(fresh, 10) == 0x00);

			/* AND THE MASK IS MAPPED INTO THE RECTANGLE: the same gray image asked for in the canvas's
			 * LEFT HALF puts its white half — the second quarter of the canvas — where it belongs. */
			memset(fresh, 0, sizeof fresh);
			c3 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
						   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
			CGContextClipToMask(c3, CGRectMake(0, 0, W / 2, H), gray_mask);
			CGContextSetRGBFillColor(c3, 1, 0, 0, 1);
			CGContextFillRect(c3, CGRectMake(0, 0, W, H));
			/* THREE REGIONS, AND THE THIRD IS THE ONE THAT SAYS WHAT "ADDED" MEANS: the mask is
			 * INTERSECTED with the clipping area, so outside the rectangle the area is UNCHANGED —
			 * still fully inside — and a check that expected the right of the rectangle to be empty
			 * was reading the door as REPLACING the clip. The first version of this check failed for
			 * exactly that reason. */
			printf("CG-CLIP %-66s mapped x=1,4,8 = %u,%u,%u\n", "...readout", alpha_at(fresh, 1),
			       alpha_at(fresh, W / 4 + 1), alpha_at(fresh, 8));
			check("...and into the RECTANGLE it was given: the rect's dark half is out, its white half in",
			      alpha_at(fresh, 1) == 0x00 && alpha_at(fresh, W / 4 + 1) == 0xFF);
			check("...while OUTSIDE the rectangle the clipping area is unchanged, which is what "
			      "\"added to the clip\" means", alpha_at(fresh, 8) == 0xFF);

			/* THE INTERSECTION WITH WHAT IS ALREADY CLIPPED — asked of a fresh context, because a clip
			 * is cumulative and a check that reuses one is a check on everything before it. */
			memset(fresh, 0, sizeof fresh);
			c3 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
						   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
			{
				CGRect right = CGRectMake(W / 2, 0, W / 2, H);

				CGContextClipToRect(c3, right);
				CGContextClipToMask(c3, CGRectMake(0, 0, W, H), gray_mask);
				CGContextSetRGBFillColor(c3, 1, 0, 0, 1);
				CGContextFillRect(c3, CGRectMake(0, 0, W, H));
				check("a mask intersects what was already clipped: the right half still paints",
				      alpha_at(fresh, 10) == 0xFF && alpha_at(fresh, 1) == 0x00);
				/* AND A SECOND MASK THAT KEEPS THE OTHER HALF LEAVES NOTHING */
				CGContextClipToMask(c3, CGRectMake(0, 0, W, H), image_mask);
				memset(fresh, 0, sizeof fresh);
				CGContextFillRect(c3, CGRectMake(0, 0, W, H));
				check("...and a second mask keeping the OTHER half intersects to nothing",
				      alpha_at(fresh, 1) == 0x00 && alpha_at(fresh, 10) == 0x00);
			}

			/* AND THE PATH SURVIVES, WHICH IS WHERE THIS DOOR DIFFERS FROM ClipToRects */
			{
				CGMutablePathRef kept = CGPathCreateMutable();
				CGPathRef copy;

				CGPathAddRect(kept, NULL, CGRectMake(1, 1, 3, 3));
				CGContextBeginPath(c3);
				CGContextAddRect(c3, CGRectMake(1, 1, 3, 3));
				CGContextClipToMask(c3, CGRectMake(0, 0, W, H), gray_mask);
				copy = CGContextCopyPath(c3);
				check("the context's path SURVIVES a clip to a mask, unlike a clip to rectangles",
				      copy != NULL && CGPathEqualToPath(copy, (CGPathRef)kept));
				CGPathRelease(copy);
				CGPathRelease((CGPathRef)kept);
			}
			CGContextRelease(c3);
		}

		/* --- and what an image mask may not be -------------------------------------------------- */
		{
			unsigned char fresh[STRIDE * H];
			CGContextRef c4 = CGBitmapContextCreate(fresh, W, H, 8, STRIDE, rgb,
								kCGImageAlphaPremultipliedFirst
								| kCGBitmapByteOrder32Little);
			/* A COLORED IMAGE NEEDS A COLORED IMAGE'S WORTH OF BYTES: the first version of this
			 * section handed the 8-bit mask's provider to a 32-bit chart, CGImageCreate correctly
			 * refused it, and the check then passed VACUOUSLY against a NULL mask. A probe that
			 * cannot tell a refusal from a success is worse than no probe. */
			static const unsigned char rgb_bytes[W * H * 4];
			CGDataProviderRef color_dp = CGDataProviderCreateWithData(NULL, rgb_bytes,
									  sizeof rgb_bytes, NULL);
			CGImageRef colored = CGImageCreate(W, H, 8, 32, W * 4, rgb,
							   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
							   color_dp, NULL, false, kCGRenderingIntentDefault);

			check("...and the colored image the refusal is about really was made", colored != NULL);

			CGContextClipToMask(c4, CGRectMake(0, 0, W, H), colored);
			memset(fresh, 0, sizeof fresh);
			CGContextSetRGBFillColor(c4, 1, 0, 0, 1);
			CGContextFillRect(c4, CGRectMake(0, 0, W, H));
			check("a COLORED image is refused as a mask, and the clip is left as it was",
			      alpha_at(fresh, 1) == 0xFF && alpha_at(fresh, 10) == 0xFF);
			CGContextClipToMask(NULL, CGRectMake(0, 0, 1, 1), gray_mask);
			CGContextClipToMask(c4, CGRectMake(0, 0, 1, 1), NULL);
			check("a NULL context or a NULL mask is a no-op rather than a crash", 1);
			CGImageRelease(colored);
			CGDataProviderRelease(color_dp);
			CGContextRelease(c4);
		}

		CGImageRelease(image_mask);
		CGImageRelease(gray_mask);
		CGDataProviderRelease(dp);
		CGColorSpaceRelease(gray);
	}

	CGContextRelease(c);
	CGColorSpaceRelease(rgb);
	printf("CG-CLIP: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
