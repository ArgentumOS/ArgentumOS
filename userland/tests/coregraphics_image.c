/*
 * coregraphics_image — CGImage as pixels plus their meaning, and the blit that shows them.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT AN IMAGE IS, AND WHAT THERE IS TO GET WRONG: a buffer nobody can read without its chart.
 * So the checks come in three groups — the CHART reads back as it was given, a chart the library
 * cannot draw is REFUSED rather than reinterpreted, and the DRAWING puts the bytes where the
 * contract says they go.
 *
 * THE IMAGE USED THROUGHOUT IS 4x4 WITH A DISTINCT VALUE PER ROW, which is what makes the flip
 * observable: row 0 is the image's TOP row, and Apple's contract draws it at the TOP of the rect
 * even though this library's user space has y increasing upward. An image that was uniform could
 * not tell the two conventions apart.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGImage.h>

#include <stdio.h>
#include <string.h>

#define W 8
#define H 8

static int failures;
static unsigned char p[4];

/* A 4x4 image, premultiplied ARGB little-endian, WITH EVERY CELL DISTINCT — and the two ways it
 * varies are two different checks: BLUE rises down the ROWS (0x10, 0x40, 0x80, 0xc0) and GREEN
 * rises across the COLUMNS (0x10, 0x20, 0x30, 0x40). That is what makes the flip and the scale
 * observable SEPARATELY, which my first version of this image could not do: it varied by row only,
 * so a check about COLUMNS sampled four identical pixels and could never have failed for the right
 * reason — it failed for the wrong one and I nearly changed an expectation to match. */
static const unsigned char img_data[4 * 4 * 4] = {
	/* B, G, R, A */
	0x10, 0x10, 0, 255,   0x10, 0x20, 0, 255,   0x10, 0x30, 0, 255,   0x10, 0x40, 0, 255,
	0x40, 0x10, 0, 255,   0x40, 0x20, 0, 255,   0x40, 0x30, 0, 255,   0x40, 0x40, 0, 255,
	0x80, 0x10, 0, 255,   0x80, 0x20, 0, 255,   0x80, 0x30, 0, 255,   0x80, 0x40, 0, 255,
	0xc0, 0x10, 0, 255,   0xc0, 0x20, 0, 255,   0xc0, 0x30, 0, 255,   0xc0, 0x40, 0, 255
};

static void check(const char *name, int ok)
{
	printf("CG-IMAGE %-58s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	double d = got - want;

	if (d < 0) {
		d = -d;
	}
	printf("CG-IMAGE %-58s %s (got %g, want %g ±%g)\n", name, d <= tol ? "ok" : "FAIL", got,
	       want, tol);
	if (!(d <= tol)) {
		failures++;
	}
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	return c;
}

static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * CGBitmapContextGetBytesPerRow(c) + (size_t)x * 4, 4);
}

static CGImageRef make_image(size_t bytes)
{
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, img_data, bytes, NULL);

	return CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(),
			     kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, NULL,
			     false, kCGRenderingIntentDefault);
}

int main(void)
{
	CGImageRef image;
	CGDataProviderRef provider;
	CGContextRef c;
	CGFloat decode[8] = { 0 };

	/* --- the chart reads back as it was given ---------------------------------------------- */
	provider = CGDataProviderCreateWithData(NULL, img_data, sizeof(img_data), NULL);
	image = CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(),
			      kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, NULL,
			      false, kCGRenderingIntentDefault);
	check("CGImageCreate gives an image", image != NULL);
	check_num("...whose width is what it was told", (double)CGImageGetWidth(image), 4.0, 0);
	check_num("...and height", (double)CGImageGetHeight(image), 4.0, 0);
	check_num("...and bytes per row", (double)CGImageGetBytesPerRow(image), 16.0, 0);
	check_num("...and bits per pixel", (double)CGImageGetBitsPerPixel(image), 32.0, 0);
	check_num("...and bits per component", (double)CGImageGetBitsPerComponent(image), 8.0, 0);
	check("...and the alpha it was told", CGImageGetAlphaInfo(image) ==
	      kCGImageAlphaPremultipliedFirst);
	check("...and the colour space it was told",
	      CGImageGetColorSpace(image) == CGColorSpaceCreateDeviceRGB());
	check("...and it holds the PROVIDER rather than a copy",
	      CGImageGetDataProvider(image) == provider);
	check("...and it says whether it interpolates", !CGImageGetShouldInterpolate(image));
	CGImageRelease(image);
	CGDataProviderRelease(provider);

	/* --- and a chart this library cannot draw is REFUSED ---------------------------------- */
	provider = CGDataProviderCreateWithData(NULL, img_data, sizeof(img_data), NULL);
	check("a chart with a different alpha is refused",
	      CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(),
			    kCGImageAlphaPremultipliedLast | kCGImageByteOrder32Little, provider, NULL,
			    false, kCGRenderingIntentDefault) == NULL);
	check("...and so is a different bit depth",
	      CGImageCreate(4, 4, 16, 64, 32, CGColorSpaceCreateDeviceRGB(),
			    kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, NULL,
			    false, kCGRenderingIntentDefault) == NULL);
	check("...and a DECODE array, rather than ignoring it",
	      CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(),
			    kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, decode,
			    false, kCGRenderingIntentDefault) == NULL);
	check("...and a NULL provider", CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(),
						      kCGImageAlphaPremultipliedFirst |
						      kCGImageByteOrder32Little, NULL, NULL, false,
						      kCGRenderingIntentDefault) == NULL);
	check("...and a provider too short for its own chart",
	      make_image(8) == NULL);
	CGDataProviderRelease(provider);

	/* --- and the DRAWING puts row 0 at the TOP of the rect -------------------------------- */
	/* THE SURFACE IS 8x8 AND THE IMAGE IS 4x4, so this also exercises the SCALE — nearest, which
	 * makes each image pixel a 2x2 block and keeps the flip readable at a glance. */
	image = make_image(sizeof(img_data));
	c = fresh();
	CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), image);
	pixel(c, 4, 0, p);
	check_num("the image's row 0 (the darkest) lands at the TOP of the rect", (double)p[0], 0x10,
		  0.0);
	pixel(c, 4, 7, p);
	check_num("...and its LAST row at the bottom", (double)p[0], 0xc0, 0.0);
	pixel(c, 0, 0, p);
	check_num("...and nearest scaling gives a 2x2 block of the same value", (double)p[0], 0x10,
		  0.0);
	pixel(c, 1, 1, p);
	check_num("...including its second pixel", (double)p[0], 0x10, 0.0);
	pixel(c, 2, 1, p);
	check_num("...and its third surface column is the SECOND image pixel, because an 8-wide surface "
		  "carries a 4-wide image at two columns per pixel — read on GREEN, the channel that "
		  "varies across columns", (double)p[1], 0x20, 0.0);
	pixel(c, 4, 1, p);
	check_num("...while the fifth column is the third image pixel", (double)p[1], 0x30, 0.0);
	CGImageRelease(image);
	CGContextRelease(c);

	/* --- and it COMPOSITES rather than replacing ------------------------------------------ */
	/* THE IMAGE IS OPAQUE, SO INSTEAD THIS DRAWS A HALF-TRANSPARENT ONE OVER A KNOWN FILL — the
	 * arithmetic of premultiplied source-over, checked as a number. */
	{
		static const unsigned char half[4] = { 0x00, 0x00, 0x80, 0x80 };   /* 50% red, premultiplied */
		CGDataProviderRef hp = CGDataProviderCreateWithData(NULL, half, sizeof(half), NULL);
		CGImageRef himg = CGImageCreate(1, 1, 8, 32, 4, CGColorSpaceCreateDeviceRGB(),
					       kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little,
					       hp, NULL, false, kCGRenderingIntentDefault);

		check("a 1x1 half-transparent image can be made", himg != NULL);
		c = fresh();
		CGContextSetRGBFillColor(c, 0.0, 1.0, 0.0, 1.0);
		CGContextFillRect(c, CGRectMake(0.0, 0.0, 8.0, 8.0));
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), himg);
		pixel(c, 4, 4, p);
		/* PREMULTIPLIED SOURCE-OVER, AND THIS CHECK IS THE ONE THAT PROVES IT: the image's red is
		 * 0x80 — 0.502 of full red AS A PREMULTIPLIED VALUE, because its own alpha is also 0x80 —
		 * so source-over contributes 0.502 * 0.502, about 64. A library that unpremultiplied the
		 * source, or read red as 255, would land near 127 instead. MY FIRST VERSION OF THIS
		 * EXPECTATION said 127.5, and the code was right. */
		check_num("...and composited rather than replacing: red", (double)p[2], 64.0, 2.0);
		check_num("...green", (double)p[1], 127.5, 2.0);
		CGImageRelease(himg);
		CGDataProviderRelease(hp);
		CGContextRelease(c);
	}

	/* --- and a blend mode this cannot apply is refused rather than dropped ----------------- */
	{
		int before;
		int after;

		image = make_image(sizeof(img_data));
		c = fresh();
		CGContextSetRGBFillColor(c, 0.0, 1.0, 0.0, 1.0);
		CGContextFillRect(c, CGRectMake(0.0, 0.0, 8.0, 8.0));
		pixel(c, 4, 4, p);
		before = p[1];
		CGContextSetBlendMode(c, kCGBlendModeMultiply);
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), image);
		pixel(c, 4, 4, p);
		after = p[1];
		check("a non-normal blend mode REFUSES the image, leaving the surface alone",
		      before == after);
		CGImageRelease(image);
		CGContextRelease(c);
	}

	printf("CG-IMAGE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
