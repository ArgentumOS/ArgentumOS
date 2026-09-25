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

/* A 2x1 STRIP: BLACK TEXEL, WHITE TEXEL. Drawn into a wide rect it is the smallest thing that can
 * tell a sampler from a sampler — with NEAREST every destination pixel is one of the two source
 * values, and with BILINEAR the pixels between the texel centres take intermediate ones. Nothing
 * else about it can vary, which is what makes the check below a check about SAMPLING. */
static const unsigned char strip_data[2 * 4] = {
	/* B, G, R, A */
	0, 0, 0, 255,   255, 255, 255, 255
};

static CGImageRef make_strip(void)
{
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, strip_data, sizeof(strip_data),
								 NULL);

	return CGImageCreate(2, 1, 8, 32, 8, CGColorSpaceCreateDeviceRGB(),
			     kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, NULL,
			     false, kCGRenderingIntentDefault);
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
	/* THE MATRIX WIDENED, SO THIS CHECK MOVED RATHER THAN DISAPPEARED. A chart with
	 * `kCGImageAlphaPremultipliedLast` used to be REFUSED, and now it is accepted, drawn, and
	 * covered by its own check below — the rule the Foundation probes follow applies here too:
	 * landing the code must move the assertion that asserted it was missing.
	 *
	 * WHAT IS STILL REFUSED IS STILL CHECKED, each for a reason: a bit depth that would have to be
	 * SCALED, a `bitsPerPixel` that disagrees with the channels the chart actually has, and CMYK,
	 * whose four COLOUR components would need conversion rather than reordering. */
	check("a bit depth that would have to be scaled is refused",
	      CGImageCreate(4, 4, 16, 64, 32, CGColorSpaceCreateDeviceRGB(),
			    kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, provider, NULL,
			    false, kCGRenderingIntentDefault) == NULL);
	check("...and a bitsPerPixel that disagrees with the chart's own channels",
	      CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceRGB(), kCGImageAlphaNone,
			    provider, NULL, false, kCGRenderingIntentDefault) == NULL);
	check("...and CMYK, which the blit cannot reorder into RGB",
	      CGImageCreate(4, 4, 8, 32, 16, CGColorSpaceCreateDeviceCMYK(),
			    kCGImageAlphaNone | kCGImageByteOrder32Big, provider, NULL, false,
			    kCGRenderingIntentDefault) == NULL);
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

	/* --- and the WIDENED MATRIX, proven by DRAWING rather than by construction ------------- */
	/* EVERY CHART BELOW WAS UNBUILDABLE BEFORE THIS SLICE. Numbers, not shapes: a grey chart has
	 * one channel and three names for it, a `PremultipliedLast` chart is the other end of the same
	 * 32-bit layout, and a `Last` chart promises samples that are NOT premultiplied — so the
	 * sampler owes the multiply and half-transparent white must land at 64, the same number the
	 * PNG path produces for the same reason. */
	{
		/* GREY: ONE BYTE PER PIXEL. Four different greys, so a wrong stride or a wrong map shows
		 * up as the wrong pixel rather than as a plausible flat fill. */
		static const unsigned char grey[4] = { 0x00, 0x40, 0x80, 0xff };
		CGDataProviderRef gp = CGDataProviderCreateWithData(NULL, grey, sizeof(grey), NULL);
		CGImageRef gi = CGImageCreate(4, 1, 8, 8, 4, CGColorSpaceCreateDeviceGray(),
					      kCGImageAlphaNone, gp, NULL, false,
					      kCGRenderingIntentDefault);

		check("a GREY chart — one byte per pixel and no alpha — can be built", gi != NULL);
		c = fresh();
		/* THE RECT IS THE WHOLE SURFACE, AND THE READ IS THREE ROWS IN. A 4x1 image drawn into a
		 * 4x4 rect lands in USER space y 0..4, and this context's CTM flips y — so the DEVICE rows
		 * 0..3 sit at the BOTTOM of that rect and a read at (1,0) falls outside it entirely, which
		 * is how the first version of this check got 0 with everything else correct. Drawing into
		 * the full 8x8 surface makes the mapping one image pixel to two device columns, so column 1
		 * is still image pixel 0 and column 3 is pixel 1. */
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), gi);
		pixel(c, 3, 0, p);
		check("...and it draws as GREY: blue, green and red all equal", p[0] == p[1] && p[1] == p[2]);
		check_num("...at the value that pixel held", (double)p[1], 64.0, 0.0);
		CGImageRelease(gi);
		CGDataProviderRelease(gp);
		CGContextRelease(c);
	}
	{
		/* PREMULTIPLIEDLAST | 32Little: the byte order reverses on the way in, so this is
		 * R, G, B, A in memory — the same pixels the library's own format holds, reached from
		 * the other spelling. Drawing red must give red AND NOT BLUE. */
		static const unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };
		CGDataProviderRef rp = CGDataProviderCreateWithData(NULL, rgba, sizeof(rgba), NULL);
		CGImageRef ri = CGImageCreate(1, 1, 8, 32, 4, CGColorSpaceCreateDeviceRGB(),
					      kCGImageAlphaPremultipliedLast | kCGImageByteOrder32Little, rp,
					      NULL, false, kCGRenderingIntentDefault);

		check("a kCGImageAlphaPremultipliedLast chart is ACCEPTED now, not refused", ri != NULL);
		c = fresh();
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), ri);
		pixel(c, 0, 0, p);
		check_num("...and its redchannel is red", (double)p[2], 255.0, 0.0);
		check_num("...while its blue channel is NOT", (double)p[0], 0.0, 0.0);
		CGImageRelease(ri);
		CGDataProviderRelease(rp);
		CGContextRelease(c);
	}
	{
		/* STRAIGHT ALPHA: `kCGImageAlphaLast` samples are NOT premultiplied, so the sampler owes
		 * the multiply — and the proof is the same 64 the premultiplied PNG case gives, arrived at
		 * from the opposite direction. A library that forgot would land at 128.
		 *
		 * AND THE FIXTURE IS `A, B, G, R`, WHICH IS THE PART THAT FOOLED ME: `AlphaLast` is the
		 * LOGICAL order, and `kCGImageByteOrder32Little` REVERSES it in memory, so the alpha's byte
		 * is the FIRST one. My first version of this fixture wrote R, G, B, A — the reading one
		 * expects from the name — and the alpha came back as 255. The code was right, the fixture
		 * was wrong, and the check below is what said so. */
		static const unsigned char straight[4] = { 0x80, 0xff, 0xff, 0xff };
		CGDataProviderRef sp = CGDataProviderCreateWithData(NULL, straight, sizeof(straight), NULL);
		CGImageRef si = CGImageCreate(1, 1, 8, 32, 4, CGColorSpaceCreateDeviceRGB(),
					      kCGImageAlphaLast | kCGImageByteOrder32Little, sp, NULL, false,
					      kCGRenderingIntentDefault);

		check("a kCGImageAlphaLast chart — STRAIGHT alpha — can be built", si != NULL);
		c = fresh();
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), si);
		pixel(c, 0, 0, p);
		check_num("...and the sampler PREMULTIPLIED it: half-transparent white lands at 64, not 128",
			  (double)p[2], 64.0, 0.0);
		check_num("...with its alpha preserved", (double)p[3], 128.0, 0.0);
		CGImageRelease(si);
		CGDataProviderRelease(sp);
		CGContextRelease(c);
	}

	/* --- THE INTERPOLATION QUALITY, WHICH IS A REAL CHOICE HERE AND NOT A STORED FLAG ---------- */
	/* THE TWO SAMPLERS ARE DISTINGUISHED BY THE PIXELS BETWEEN TEXEL CENTRES, and the checks are a
	 * PAIR: the same strip drawn twice, once under each quality, with the middle columns asserted to
	 * be intermediate in one and a source value in the other. A check on one quality alone would
	 * pass for a library that ignored the setting entirely — which is the shape of a silent no-op. */
	{
		CGImageRef strip = make_strip();

		c = fresh();
		check("a fresh context samples NEAREST: the quality starts at kCGInterpolationNone",
		      CGContextGetInterpolationQuality(c) == kCGInterpolationNone);
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), strip);
		pixel(c, 3, 4, p);
		{
			int near3 = p[2];

			pixel(c, 4, 4, p);
			check("...and NEAREST gives only the source values: columns 3 and 4 are 0 and 255 "
			      "with nothing between them",
			      (near3 == 0 || near3 == 255) && (p[2] == 0 || p[2] == 255));
		}
		CGContextRelease(c);

		c = fresh();
		CGContextSetInterpolationQuality(c, kCGInterpolationMedium);
		check("kCGInterpolationMedium is accepted and reads back as itself",
		      CGContextGetInterpolationQuality(c) == kCGInterpolationMedium);
		CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), strip);
		pixel(c, 3, 4, p);
		{
			int bil3 = p[2];

			pixel(c, 4, 4, p);
			check_num("...and BILINEAR puts an intermediate value at column 3", (double)bil3, 96.0,
				  12.0);
			check_num("...and another at column 4", (double)p[2], 159.0, 12.0);
			check("...which is strictly between the two source values, where NEAREST could not "
			      "produce anything", bil3 > 0 && bil3 < 255);
		}
		/* THE ENDS ARE STILL THE ENDS: an interpolator that lightened or darkened the edge texels
		 * would pass the two checks above. */
		pixel(c, 0, 4, p);
		check_num("...and the first column is still the black texel", (double)p[2], 0.0, 0.0);
		pixel(c, 7, 4, p);
		check_num("...and the last is still the white one", (double)p[2], 255.0, 0.0);
		CGContextRelease(c);

		/* AND THE TWO FILTERS THIS LIBRARY DOES NOT HAVE ARE REFUSED, LEAVING THE QUALITY ALONE. */
		c = fresh();
		CGContextSetInterpolationQuality(c, kCGInterpolationMedium);
		CGContextSetInterpolationQuality(c, kCGInterpolationLow);
		check("kCGInterpolationLow is REFUSED rather than mapped onto one of the two samplers",
		      CGContextGetInterpolationQuality(c) == kCGInterpolationMedium);
		CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
		check("...and so is kCGInterpolationHigh",
		      CGContextGetInterpolationQuality(c) == kCGInterpolationMedium);
		CGContextSetInterpolationQuality(c, kCGInterpolationDefault);
		check("...while kCGInterpolationDefault is accepted",
		      CGContextGetInterpolationQuality(c) == kCGInterpolationDefault);
		CGContextRelease(c);

		CGImageRelease(strip);
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

	/* --- AND THE CLIP'S MASK HALF REACHES THE HAND-WRITTEN BLIT, WHICH IS NOT A pixman COMPOSITE --- */
	/* THIS IS THE ONE OF THE THREE THAT COULD EASILY HAVE BEEN MISSED AND NOT BE NOTICED: `DrawImage`
	 * composites pixel by pixel in a loop rather than through pixman, so it consults the clip's mask
	 * itself. A mask that worked for fills and gradients and silently not here would be the worst
	 * version of all.
	 *
	 * THE PATH IS THE USER-SPACE TRIANGLE (0,0),(8,0),(0,8), whose DEVICE form (the CTM flips y) covers
	 * the half-surface py >= px — so (7,1) is far outside it and (1,7) is inside. DEVICE coordinates on
	 * purpose: this probe's `pixel()` reads the raw row. */
	{
		CGImageRef im = make_image(sizeof(img_data));
		CGContextRef c2 = fresh();

		CGContextBeginPath(c2);
		CGContextMoveToPoint(c2, 0.0, 0.0);
		CGContextAddLineToPoint(c2, 8.0, 0.0);
		CGContextAddLineToPoint(c2, 0.0, 8.0);
		CGContextClosePath(c2);
		CGContextClip(c2);
		CGContextDrawImage(c2, CGRectMake(0.0, 0.0, 8.0, 8.0), im);
		pixel(c2, 7, 1, p);
		check("an image drawn under a CURVED clip does not reach a pixel outside it", p[3] == 0);
		pixel(c2, 1, 7, p);
		check("...and it DOES reach one inside the same clip", p[3] != 0);
		CGImageRelease(im);
		CGContextRelease(c2);
	}

	/* --- AND A PREMULTIPLIED-LAST IMAGE, WHICH IS THE FORMAT AN APPKIT REP DECLARES ----------------- */
	/* EVERY OTHER IMAGE IN THIS PROBE IS PREMULTIPLIED-FIRST LITTLE-ENDIAN, the format this tree's own
	 * bitmap contexts use — so nothing here had ever asked whether the blit reads a
	 * kCGImageAlphaPremultipliedLast image AS THE FORMAT SAYS. It matters because NSBitmapImageRep's
	 * -CGImage declares exactly that, and an AppKit probe found a BLUE fill landing nowhere while WHITE
	 * landed: a colour-dependent difference is a CHANNEL difference, which points here.
	 *
	 * THE PIXEL IS OPAQUE BLUE (R=0, G=0, B=255, A=255) — deliberately NOT white, because white is the
	 * one colour that cannot tell a channel-order bug from correct behaviour: every channel says the
	 * same thing. That is how the AppKit probe missed it, and it is why this check exists. */
	{
		static const unsigned char last_rgba[2 * 2 * 4] = {
			0, 0, 255, 255,   0, 0, 0, 0,
			0, 0, 0, 0,       0, 0, 0, 0
		};
		CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, last_rgba,
								     sizeof(last_rgba), NULL);
		CGImageRef im = CGImageCreate(2, 2, 8, 32, 8, CGColorSpaceCreateDeviceRGB(),
					      kCGImageAlphaPremultipliedLast, prov, NULL, false,
					      kCGRenderingIntentDefault);

		check("an image can be built in the PREMULTIPLIED-LAST format", im != NULL);
		if (im != NULL) {
			CGContextRef c = fresh();

			CGContextDrawImage(c, CGRectMake(0.0, 0.0, 8.0, 8.0), im);
			/* THE DESTINATION IS PREMULTIPLIED-FIRST LITTLE-ENDIAN, so memory is B, G, R, A. Image
			 * pixel (0,0) is drawn 4x4 into the rect's TOP-LEFT, which is rows 0..3, cols 0..3. */
			pixel(c, 0, 0, p);
			/* THE FOUR CHANNELS ARE CHECKED ONE BY ONE rather than as one boolean, so the gate's own
			 * output NAMES what arrived where — which is the measurement that locates the fault. The
			 * source pixel is RGBA (0, 0, 255, 255); the destination's memory is B, G, R, A. */
			check_num("...arriving with B intact (destination byte 0)", (double)p[0], 255.0, 0.0);
			check_num("...with G intact (byte 1)", (double)p[1], 0.0, 0.0);
			check_num("...with R intact (byte 2)", (double)p[2], 0.0, 0.0);
			check_num("...and with ALPHA intact (byte 3)", (double)p[3], 255.0, 0.0);
			/* THE OTHER HALF OF THE PAIR: the transparent pixels must STAY transparent, so a check
			 * that painted everything would not pass here by accident. */
			pixel(c, 7, 7, p);
			check("...while the transparent ones stay transparent", p[0] == 0 && p[3] == 0);
			CGContextRelease(c);
			CGImageRelease(im);
		}
		CGDataProviderRelease(prov);
	}

	printf("CG-IMAGE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
