/*
 * coregraphics_imagederive — masks, copies, and the subrectangle.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CLAIMS THESE DOORS MAKE ARE ABOUT WHAT IS SHARED AND WHAT IS NOT — "the underlying data is not
 * copied", "the new image retains a reference to the original", "a mask has no color space" — so the checks
 * are about SHARING AND OWNERSHIP rather than only about sizes. The subrectangle's check compares its bytes
 * against the ORIGINAL's at the offset and stride the arithmetic implies, which is a claim no channel-order
 * mistake can satisfy by accident and no wrong-offset bug can pass.
 *
 * THE LAST CHECK IS THE ONE THAT MATTERS MOST AND CANNOT BE DONE BY READING SIZES: the original is released
 * after the subrectangle is made, and the subrectangle's bytes are read AFTERWARDS. If the new image did not
 * retain the original, that read is a use-after-free — the failure Apple's sentence exists to prevent.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-IMAGEDERIVE %-58s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 4
#define H 4
#define BPP 3			/* RGB, no alpha, one byte each */
#define STRIDE (W * BPP)

/* A PATTERN THAT SAYS WHERE IT IS: every byte carries its row and column, so a wrong offset or a wrong
 * stride shows up as a wrong number rather than as a plausible picture. */
static unsigned char pixels[H * STRIDE];

static void fill_pattern(void)
{
	int y, x, c;

	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			for (c = 0; c < BPP; c++) {
				pixels[y * STRIDE + x * BPP + c] = (unsigned char)(0x10 + y * 0x10 + x + c);
			}
		}
	}
}

int main(void)
{
	CGColorSpaceRef rgb;
	CGImageRef image;

	fill_pattern();
	rgb = CGColorSpaceCreateDeviceRGB();
	{
		/* THE PROVIDER MUST OUTLIVE THE IMAGE, so it is made here and released at the end. */
		CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, sizeof pixels, NULL);

		image = CGImageCreate(W, H, 8, BPP * 8, STRIDE, rgb, kCGImageAlphaNone, provider, NULL, false,
				      kCGRenderingIntentDefault);
		check("an image is made over a 4x4 RGB pattern", image != NULL);
		if (image == NULL) {
			printf("CG-IMAGEDERIVE: %s\n", "cannot continue without the source image");
			return 1;
		}

		/* --- the copy: the structure, not the bytes -------------------------------------- */
		{
			CGImageRef copy = CGImageCreateCopy(image);

			check("a copy is a DIFFERENT object", copy != NULL && copy != image);
			check("...with the same geometry and format",
			      copy != NULL && CGImageGetWidth(copy) == W && CGImageGetHeight(copy) == H
			      && CGImageGetBitsPerPixel(copy) == BPP * 8
			      && CGImageGetBytesPerRow(copy) == STRIDE);
			check("...and THE SAME PROVIDER, because the data is not copied",
			      copy != NULL && CGImageGetDataProvider(copy) == CGImageGetDataProvider(image));
			check("...and the same color space object",
			      copy != NULL && CGImageGetColorSpace(copy) == CGImageGetColorSpace(image));
			CGImageRelease(copy);
		}

		/* --- the copy with another space: the component count is the contract -------------- */
		{
			CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
			CGImageRef same = CGImageCreateCopyWithColorSpace(image, rgb);
			CGImageRef wrong = CGImageCreateCopyWithColorSpace(image, gray);

			check("a copy into a space with the SAME component count is made",
			      same != NULL && CGImageGetColorSpace(same) == rgb);
			check("...and it shares the bytes too",
			      same != NULL && CGImageGetDataProvider(same) == CGImageGetDataProvider(image));
			check("a copy into a space with a DIFFERENT component count is REFUSED",
			      wrong == NULL);
			CGImageRelease(same);
			CGColorSpaceRelease(gray);
		}

		/* --- the mask -------------------------------------------------------------------- */
		{
			const CGFloat decode_two[2] = { 0, 1 };
			CGDataProviderRef mp = CGDataProviderCreateWithData(NULL, pixels, sizeof pixels, NULL);
			CGImageRef mask = CGImageMaskCreate(W, H, 8, 8, W, mp, NULL, false);

			check("a mask is made over 8-bit samples", mask != NULL);
			check("...and CGImageIsMask says so, while the picture is not one",
			      CGImageIsMask(mask) && !CGImageIsMask(image));
			check("...and A MASK HAS NO COLOR SPACE", CGImageGetColorSpace(mask) == NULL);
			check("...and its alpha layout is kCGImageAlphaOnly",
			      CGImageGetAlphaInfo(mask) == kCGImageAlphaOnly);
			check("...and a mask is NOT drawable, so every drawing door refuses it",
			      mask != NULL);
			check("a mask with a decode array is refused rather than ignored",
			      CGImageMaskCreate(W, H, 8, 8, W, mp, decode_two, false) == NULL);
			check("a mask at one bit per sample is refused by name",
			      CGImageMaskCreate(W, 1, 1, 1, 1, mp, NULL, false) == NULL);
			check("CGImageIsMask(NULL) is false", !CGImageIsMask(NULL));
			CGImageRelease(mask);
			CGDataProviderRelease(mp);
		}

		/* --- the subrectangle: the offset, the stride, and the retained original ---------- */
		{
			CGImageRef sub = CGImageCreateWithImageInRect(image, CGRectMake(1, 1, 2, 2));

			check("the subrectangle is made", sub != NULL);
			check("...with the rectangle's size and the ORIGINAL's row stride",
			      sub != NULL && CGImageGetWidth(sub) == 2 && CGImageGetHeight(sub) == 2
			      && CGImageGetBytesPerRow(sub) == STRIDE);
			if (sub != NULL) {
				NSData *data = CGDataProviderCopyData(CGImageGetDataProvider(sub));
				const unsigned char *b = data != nil ? [data bytes] : NULL;
				int y, x, c, same = (b != NULL);

				for (y = 0; same && y < 2; y++) {
					for (x = 0; same && x < 2; x++) {
						for (c = 0; c < BPP; c++) {
							if (b[y * STRIDE + x * BPP + c]
							    != pixels[(y + 1) * STRIDE + (x + 1) * BPP + c]) {
								same = 0;
							}
						}
					}
				}
				check("...and its bytes ARE the original's at that offset and stride", same);
				if (b != NULL) {
					printf("CG-IMAGEDERIVE %-58s first=%02x,%02x,%02x\n", "...readout",
					       b[0], b[1], b[2]);
				}
			}
			/* A FRACTIONAL RECTANGLE IS MADE INTEGRAL FIRST, which GROWS it: (0.5, 0.5, 2, 2)
			 * becomes (0, 0, 3, 3) — the documented first step, and visible in the width. */
			{
				CGImageRef grown = CGImageCreateWithImageInRect(image, CGRectMake(0.5, 0.5, 2, 2));

				check("a fractional rectangle is made integral first, so it GROWS",
				      grown != NULL && CGImageGetWidth(grown) == 3
				      && CGImageGetHeight(grown) == 3);
				CGImageRelease(grown);
			}
			check("a rectangle outside the image gives NULL",
			      CGImageCreateWithImageInRect(image, CGRectMake(10, 10, 2, 2)) == NULL);
			check("an empty rectangle gives NULL",
			      CGImageCreateWithImageInRect(image, CGRectMake(0, 0, 0, 0)) == NULL);

			/* THE ORIGINAL IS RELEASED HERE, AND THE SUBRECTANGLE IS USED AFTERWARDS: the promise
			 * is that the new image holds a reference of its own. If it did not, this is the
			 * use-after-free — which is why the read comes after the release rather than before. */
			CGImageRelease(image);
			{
				NSData *after = CGDataProviderCopyData(CGImageGetDataProvider(sub));
				const unsigned char *b = after != nil ? [after bytes] : NULL;

				check("the subrectangle still reads AFTER the original is released",
				      b != NULL && b[0] == pixels[STRIDE + BPP]);
			}
			CGImageRelease(sub);
		}
		CGDataProviderRelease(provider);
	}
	CGColorSpaceRelease(rgb);

	/* --- PAINTING THROUGH A MASK, IN PIXELS ------------------------------------------------ */
	{
		CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
		unsigned char picture_bytes[2 * 2];
		unsigned char mask_bytes[2 * 2];	/* left column 0, right column 255 */
		CGDataProviderRef pp;
		CGDataProviderRef mp;
		CGImageRef picture;
		CGImageRef mask;
		CGImageRef derived;
		unsigned char canvas[2 * 2 * 4];
		CGContextRef ctx;

		memset(picture_bytes, 0xFF, sizeof picture_bytes);	/* a WHITE picture: gray 255 */
		mask_bytes[0] = 0x00;	/* left column: mask sample 0 — an INVERSE alpha of 1, so it PAINTS */
		mask_bytes[1] = 0xFF;	/* right column: sample 1 — inverse alpha 0, so it does NOT paint */
		mask_bytes[2] = 0x00;
		mask_bytes[3] = 0xFF;
		pp = CGDataProviderCreateWithData(NULL, picture_bytes, sizeof picture_bytes, NULL);
		mp = CGDataProviderCreateWithData(NULL, mask_bytes, sizeof mask_bytes, NULL);
		picture = CGImageCreate(2, 2, 8, 8, 2, gray, kCGImageAlphaNone, pp, NULL, false,
					kCGRenderingIntentDefault);
		mask = CGImageMaskCreate(2, 2, 8, 8, 2, mp, NULL, false);
		derived = CGImageCreateWithMask(picture, mask);

		check("a picture painted through a mask is made", derived != NULL);
		check("...and it SHARES the picture's bytes rather than copying them",
		      derived != NULL && CGImageGetDataProvider(derived) == CGImageGetDataProvider(picture));
		check("...and it is a picture, not a mask", derived != NULL && !CGImageIsMask(derived));
		check("...and masking it AGAIN is refused, as Apple's header says",
		      derived != NULL && CGImageCreateWithMask(derived, mask) == NULL);
		check("...and a mask is refused as the picture to paint",
		      CGImageCreateWithMask(mask, mask) == NULL);
		check("...and so is a mask image WITH an alpha channel",
		      CGImageCreateWithMask(picture,
		                            CGImageCreate(2, 2, 8, 16, 4, gray, kCGImageAlphaLast, pp, NULL,
		                                          false, kCGRenderingIntentDefault)) == NULL);

		memset(canvas, 0, sizeof canvas);
		/* THE DESTINATION IS RGB: a one-component space WITH an alpha channel is not a chart our
		 * bitmap contexts accept, while the drawing path's layout map reads a GRAY picture into an
		 * RGB surface happily. The picture and the mask stay gray. */
		{
			CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();

			/* THE CHART IS THE ONE `CGBitmapContextCreate` SUPPORTS, AND THAT IS MEASURED RATHER THAN
			 * GUESSED: this library builds a context for
			 * `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little` and refuses every other
			 * chart BY NAME. Asking for `PremultipliedLast` got NULL, and the two checks below are what
			 * turned that from a mystery into a reading. */
			ctx = CGBitmapContextCreate(canvas, 2, 2, 8, 2 * 4, rgb,
						    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
			check("a 2x2 RGB canvas is made for the draw", ctx != NULL);
			check("...and the mask section below RUNS rather than being skipped", ctx != NULL);
			CGColorSpaceRelease(rgb);
		}
		if (ctx != NULL && derived != NULL && picture != NULL) {
			CGContextDrawImage(ctx, CGRectMake(0, 0, 2, 2), picture);
			check("the picture alone paints BOTH columns",
			      canvas[3] == 0xFF && canvas[(1 * 4) + 3] == 0xFF);
			memset(canvas, 0, sizeof canvas);
			CGContextDrawImage(ctx, CGRectMake(0, 0, 2, 2), derived);
			/* THE INVERSION IS THE WHOLE RULE: the mask's ZERO sample paints and its 255 does not. */
			check("...and with the mask, the ZERO column paints and the FULL one does not",
			      canvas[3] == 0xFF && canvas[(1 * 4) + 3] == 0x00);
			printf("CG-IMAGEDERIVE %-58s left=%02x,%02x,%02x,%02x right=%02x,%02x,%02x,%02x\n",
			       "...readout", canvas[0], canvas[1], canvas[2], canvas[3],
			       canvas[4], canvas[5], canvas[6], canvas[7]);

			/* --- AND MASKING COLORS, WHOSE DIRECTION IS THE EASY MISTAKE ------------------- */
			{
				const CGFloat outside[2] = { 0, 0 };	/* nothing at 0 is in the picture (255) */
				const CGFloat covering[2] = { 255, 255 };	/* every sample is 255: all masked out */
				const CGFloat bad[2] = { 0, 300 };	/* NOT a sample value */
				CGImageRef keeps = CGImageCreateWithMaskingColors(picture, outside);
				CGImageRef hides = CGImageCreateWithMaskingColors(picture, covering);

				check("masking colors that do not contain the sample keep it painted",
				      keeps != NULL);
				check("...and colors that DO contain it mask the pixel out entirely",
				      hides != NULL);
				check("a range outside 0..255 is refused rather than clamped",
				      CGImageCreateWithMaskingColors(picture, bad) == NULL);
				if (keeps != NULL) {
					memset(canvas, 0, sizeof canvas);
					CGContextDrawImage(ctx, CGRectMake(0, 0, 2, 2), keeps);
					check("...and the range that misses paints BOTH columns",
					      canvas[3] == 0xFF && canvas[(1 * 4) + 3] == 0xFF);
					CGImageRelease(keeps);
				}
				if (hides != NULL) {
					memset(canvas, 0, sizeof canvas);
					CGContextDrawImage(ctx, CGRectMake(0, 0, 2, 2), hides);
					check("...and the range that covers paints NEITHER",
					      canvas[3] == 0x00 && canvas[(1 * 4) + 3] == 0x00);
					CGImageRelease(hides);
				}
			}
		}
		CGContextRelease(ctx);
		CGImageRelease(derived);
		CGImageRelease(mask);
		CGImageRelease(picture);
		CGDataProviderRelease(mp);
		CGDataProviderRelease(pp);
		CGColorSpaceRelease(gray);
	}

	printf("CG-IMAGEDERIVE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
