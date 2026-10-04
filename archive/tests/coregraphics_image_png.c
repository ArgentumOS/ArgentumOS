/*
 * coregraphics_image_png — a real PNG decodes, draws, and is refused when it should be.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FIXTURES ARE IN THIS FILE AS BYTES, deliberately: a committed PNG plus a path would need a
 * fixture convention and would depend on the working directory, and the two images are 80 and 79
 * bytes. They were generated once, outside the repository, with zlib — which is how a decoder can be
 * tested without using the code under test to build its own input.
 *
 * THE IMAGE IS CHOSEN SO THE DECODE IS READABLE: row 0 opaque red, row 1 WHITE AT 50% ALPHA, row 2
 * green, row 3 blue. The white row is the one that matters — a straight sample of 255 must come back
 * as 128 after the premultiply this library's format requires, so an implementation that forgot to
 * premultiply (or that premultiplied twice) fails on a number rather than on a picture.
 *
 * AND IT IS CHECKED BY DRAWING, not by reading the decoder's buffer: `CGContextDrawImage` puts the
 * image on a surface this probe already knows how to read, so the check covers the whole path a
 * caller uses — decode, then draw — rather than the half of it a decoder-only test would reach.
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

/* 4x4 RGBA — COLOUR TYPE 6, 8 bits per channel, straight samples: red / white@50% / green / blue.
 *
 * THE COLOUR TYPE IS THE PART WORTH RECORDING. My first version of this fixture declared colour type
 * 2 (truecolour, NO alpha channel) while the row data it encoded was 4-tuples, so the 128 was
 * truncated at generation time and the "50% alpha" row arrived opaque — and the probe then failed
 * against a library that was right. A decoder can only report the alpha it was given. */
static const unsigned char png_8bit[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
	0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
	0x1b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x19, 0x03, 0xc9, 0xff, 0x0d, 0xc8, 0x18, 0x2c, 0x86, 0x02, 0xd1,
	0x74, 0xfc, 0x07, 0x00, 0x31, 0xab, 0x25, 0xdd, 0xc1, 0xa6, 0x81, 0x33,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

/* The same 4x4 at 16 bits per channel — the one that must be REFUSED. */
static const unsigned char png_16bit[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
	0x10, 0x02, 0x00, 0x00, 0x00, 0x76, 0x03, 0xd5, 0x6a, 0x00, 0x00, 0x00,
	0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xff, 0x9f, 0x01,
	0x08, 0x30, 0x49, 0xac, 0x82, 0xf8, 0xa5, 0xfe, 0xe3, 0x00, 0x00, 0x01,
	0xc5, 0x2f, 0xd1, 0xae, 0xd5, 0xf4, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x49,
	0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

static void check(const char *name, int ok)
{
	printf("CG-PNG %-60s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want)
{
	printf("CG-PNG %-60s %s (got %g, want %g)\n", name, got == want ? "ok" : "FAIL", got, want);
	if (got != want) {
		failures++;
	}
}

static CGImageRef decode(const unsigned char *bytes, size_t size)
{
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, bytes, size, NULL);

	if (provider == NULL) {
		return NULL;
	}
	return CGImageCreateWithPNGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
}

static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * CGBitmapContextGetBytesPerRow(c) + (size_t)x * 4, 4);
}

int main(void)
{
	static unsigned char buf[W * H * 4];
	CGImageRef image;
	CGContextRef c;

	/* --- a real PNG decodes, with the chart this library's format promises ----------------- */
	image = decode(png_8bit, sizeof(png_8bit));
	check("an 8-bit PNG decodes", image != NULL);
	check_num("...to its own width", (double)CGImageGetWidth(image), 4.0);
	check_num("...and height", (double)CGImageGetHeight(image), 4.0);
	check_num("...at 32 bits per pixel", (double)CGImageGetBitsPerPixel(image), 32.0);
	check_num("...tightly packed at 4 bytes per pixel", (double)CGImageGetBytesPerRow(image), 16.0);
	check("...premultiplied, which is the format these contexts draw",
	      CGImageGetAlphaInfo(image) == kCGImageAlphaPremultipliedFirst);
	check("...in device RGB, whatever the PNG said",
	      CGImageGetColorSpace(image) == CGColorSpaceCreateDeviceRGB());

	/* --- and it DRAWS, which is where the bytes can be read back --------------------------- */
	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), image);
	/* THE 4x4 IMAGE ON AN 8x8 SURFACE IS TWO PIXELS PER IMAGE PIXEL, and the image's row 0 is the
	 * top of the rect, so rows come in pairs: 0-1 red, 2-3 the white row, 4-5 green, 6-7 blue. */
	pixel(c, 4, 0, p);
	check_num("the drawn image is red at the top, as its first row is", (double)p[2], 255.0);
	pixel(c, 4, 1, p);
	check_num("...and its second surface row repeats it", (double)p[2], 255.0);
	pixel(c, 4, 2, p);
	/* THIS IS THE CHECK THAT PROVES THE PREMULTIPLY, and the number needs both halves of the path
	 * to be read: the decoder turns the straight sample 255 into 128 (255 * 128/255, rounded), and
	 * then DRAWING that pixel composites it over an EMPTY surface once more by its own alpha, so
	 * 128 * 128/255 lands at 64. A decoder that forgot to premultiply would leave 255 in the
	 * channel and draw 128 here; one that premultiplied twice would give 32. */
	check_num("THE 50%-ALPHA WHITE ROW IS PREMULTIPLIED, and then composited by its own alpha: red",
		  (double)p[2], 64.0);
	pixel(c, 4, 3, p);
	check_num("...green, the same number for the same reason", (double)p[1], 64.0);
	pixel(c, 4, 4, p);
	check_num("...then the green row of the PNG", (double)p[1], 255.0);
	pixel(c, 4, 7, p);
	check_num("...and its last row, blue, at the bottom", (double)p[0], 255.0);
	CGImageRelease(image);
	CGContextRelease(c);

	/* --- and what must be refused is refused ---------------------------------------------- */
	{
		CGFloat decode_array[8] = { 0 };
		CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, png_8bit,
									 sizeof(png_8bit), NULL);

		check("a decode array is refused rather than ignored",
		      CGImageCreateWithPNGDataProvider(provider, decode_array, false,
						       kCGRenderingIntentDefault) == NULL);
		CGDataProviderRelease(provider);
	}
	check("a 16-bit PNG is refused rather than downshifted",
	      decode(png_16bit, sizeof(png_16bit)) == NULL);
	{
		/* THE SAME BYTES WITH A WRECKED SIGNATURE: an image library that guessed here would be
		 * guessing at every caller's data. */
		unsigned char broken[sizeof(png_8bit)];

		memcpy(broken, png_8bit, sizeof(png_8bit));
		broken[1] = 'X';
		check("bytes that are not a PNG are refused", decode(broken, sizeof(broken)) == NULL);
	}

	printf("CG-PNG: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
