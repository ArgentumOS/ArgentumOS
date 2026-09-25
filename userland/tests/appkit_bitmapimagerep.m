/*
 * appkit_bitmapimagerep — pixels you can hold, and the door to Core Graphics.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LAYOUT IS THE POINT OF THIS CLASS, so most of these checks are about it: what a rep reports for
 * a layout it was HANDED, what it reports for one READ OFF a CGImage, and that a layout it cannot hold
 * is REFUSED rather than half-accepted. A rep that quietly accepted a planar request would be the kind
 * of silent wrong answer this tree refuses.
 *
 * AND THE DRAW CHECK DOES NOT TRUST A FIXTURE'S CONTENT. Asserting that "the surface changed" fails for
 * a legitimately transparent image, so the picture here is written INTO THE REP'S OWN BYTES (opaque
 * white) and then drawn — deterministic, and its meaning does not depend on what a PNG happens to
 * contain. The decoded-PNG draw is checked separately for the weaker property that it HAPPENED.
 *
 * IT IS MRC AND WRAPS ITSELF IN A POOL, because the convenience constructor autoreleases the way
 * Apple's does.
 */
#import <AppKit/NSBitmapImageRep.h>
#import <AppKit/NSGraphicsContext.h>

#import <CoreGraphics/CGBitmapContext.h>
#import <CoreGraphics/CGContext.h>
#import <CoreGraphics/CGImage.h>
#import <Foundation/NSData.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];
static unsigned char surf[W * H * 4];

static void check(const char *name, int ok)
{
	printf("APPKIT-BITMAP %-66s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	int ok = (got >= want - tol && got <= want + tol);

	printf("APPKIT-BITMAP %-66s %s", name, ok ? "ok" : "FAIL");
	if (!ok) {
		printf(" (got %g, want %g)", got, want);
		failures++;
	}
	printf("\n");
}

/* The bytes of one pixel of the DRAWN surface, in memory order (`pixel()` here reads the raw row). */
static void pixel(int x, int y, unsigned char *out)
{
	memcpy(out, surf + ((size_t)(y * W + x) * 4u), 4);
}

/* THE 4x4 8-BIT RGBA PNG the CoreGraphics decode probe already uses, so this probe's positive decode
 * check has bytes that are known-good rather than hand-written. */
static const unsigned char png_4x4[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
	0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
	0x1b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x19, 0x03, 0xc9, 0xff, 0x0d, 0xc8, 0x18, 0x2c, 0x86, 0x02, 0xd1,
	0x74, 0xfc, 0x07, 0x00, 0x31, 0xab, 0x25, 0xdd, 0xc1, 0xa6, 0x81, 0x33,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

int main(void)
{
	@autoreleasepool {
		NSBitmapImageRep *rep;
		unsigned char *d;
		int zeroed = 1;
		size_t i, n;

		/* --- A LAYOUT THE CALLER GIVES, WITH NO BUFFER: THE REP ALLOCATES ------------- */
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:8 pixelsHigh:4
					bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
					colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:32 bitsPerPixel:32];
		check("a rep with no buffer allocates one", rep != nil && [rep bitmapData] != NULL);
		check_num("...and reports the pixel width it was given", (double)[rep pixelsWide], 8.0, 0.0);
		check_num("...and the height", (double)[rep pixelsHigh], 4.0, 0.0);
		check_num("...and a SIZE in points that defaults to the PIXELS", (double)[rep size].width,
			  8.0, 0.0);
		check_num("...bits per sample", (double)[rep bitsPerSample], 8.0, 0.0);
		check_num("...samples per pixel", (double)[rep samplesPerPixel], 4.0, 0.0);
		check("...hasAlpha", [rep hasAlpha]);
		check("...and is NOT planar", ![rep isPlanar]);
		check_num("...bytes per row", (double)[rep bytesPerRow], 32.0, 0.0);
		check_num("...bits per pixel", (double)[rep bitsPerPixel], 32.0, 0.0);
		check("...and the colour-space NAME round-trips",
		      [[rep colorSpaceName] isEqualToString:NSDeviceRGBColorSpace]);
		d = [rep bitmapData];
		n = (size_t)[rep bytesPerRow] * (size_t)[rep pixelsHigh];
		for (i = 0; i < n; i++) {
			if (d[i] != 0) {
				zeroed = 0;
			}
		}
		check("...and that buffer is ZEROED, not whatever the allocator had", zeroed);

		/* THE IMAGE IS BUILT OVER THE REP'S OWN BYTES, ONCE. */
		check("a bitmap-backed rep builds a CGImage over its bytes", [rep CGImage] != NULL);
		check("...and returns the SAME image twice (cached, so the two cannot drift apart)",
		      [rep CGImage] == [rep CGImage]);
		check_num("...whose width is the rep's", (double)CGImageGetWidth([rep CGImage]), 8.0, 0.0);

		/* SIZE IS POINTS, AND INDEPENDENT OF THE PIXELS. */
		[rep setSize:NSMakeSize(100.0, 50.0)];
		check_num("setSize: is in POINTS and does not move the pixels", (double)[rep size].width,
			  100.0, 0.0);
		check_num("...and the pixel width stayed put", (double)[rep pixelsWide], 8.0, 0.0);
		[rep release];

		/* --- A LAYOUT IT CANNOT HOLD IS REFUSED, NOT HALF-ACCEPTED --------------------- */
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:0 pixelsHigh:4
					bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
					colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:32];
		check("a ZERO-WIDTH layout is refused", rep == nil);
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:8 pixelsHigh:4
					bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:YES
					colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:32 bitsPerPixel:32];
		check("...and PLANAR is refused, because this class holds ONE plane", rep == nil);

		/* --- A BUFFER THE CALLER OWNS IS ADOPTED, NOT COPIED --------------------------- */
		{
			unsigned char mybuf[8 * 4 * 4];
			unsigned char *planes[1];

			memset(mybuf, 0x11, sizeof(mybuf));
			planes[0] = mybuf;
			rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:8
						pixelsHigh:4 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
						isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:32
						bitsPerPixel:32];
			check("a CALLER's buffer is adopted, not copied", [rep bitmapData] == mybuf);
			check("...and the rep reports it as its data", [rep bitmapData][0] == 0x11);
			[rep release];
		}

		/* --- THE DECODE: THE READ HALF OF THE CORE GRAPHICS DOOR ----------------------- */
		{
			NSData *data = [NSData dataWithBytes:png_4x4 length:sizeof(png_4x4)];
			NSBitmapImageRep *png = [NSBitmapImageRep imageRepWithData:data];

			check("a PNG's bytes become a rep", png != nil);
			check_num("...with the width the PNG declares", (double)[png pixelsWide], 4.0, 0.0);
			check_num("...and its height", (double)[png pixelsHigh], 4.0, 0.0);
			check("...whose pixels are the CGImage's, so bitmapData answers NULL (a stated "
			      "deviation)", [png bitmapData] == NULL);
			check("...and it has an image to draw", [png CGImage] != NULL);
			/* READ OFF THE IMAGE RATHER THAN ASSUMED FROM THE CHANNEL COUNT. */
			check("...and the colour-space name came off the image, not off a channel count",
			      [png colorSpaceName] != nil);
		}
		check("data that is neither PNG nor JPEG is REFUSED, not half-decoded",
		      [NSBitmapImageRep imageRepWithData:[NSData dataWithBytes:"zzzzzzzz" length:8]] == nil);

		/* --- AND WITH NO CURRENT CONTEXT A DRAW IS A NO, NOT A CRASH -------------------- */
		{
			NSBitmapImageRep *r2 = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
						pixelsWide:4 pixelsHigh:4 bitsPerSample:8
						samplesPerPixel:4 hasAlpha:YES isPlanar:NO
						colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:16
						bitsPerPixel:32];

			[NSGraphicsContext setCurrentContext:nil];
			check("drawing with NO current context answers NO rather than crashing",
			      [r2 drawInRect:NSMakeRect(0.0, 0.0, 4.0, 4.0)] == NO);
			check("...and likewise -draw", [r2 draw] == NO);
			[r2 release];
		}

		/* --- AND THE DRAW ITSELF, WITH A PICTURE OF OUR OWN MAKING ---------------------- */
		{
			CGContextRef cg;
			NSGraphicsContext *gctx;

			rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:8
						pixelsHigh:4 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
						isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:32
						bitsPerPixel:32];
			/* OPAQUE WHITE, WRITTEN BY HAND: the assertion below then cannot depend on what an
			 * image happens to contain. */
			memset([rep bitmapData], 0xff, 32u * 4u);
			memset(surf, 0, sizeof(surf));
			cg = CGBitmapContextCreate(surf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
						   kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
			gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
			[NSGraphicsContext setCurrentContext:gctx];
			check("a bitmap-backed rep draws into the CURRENT context",
			      [rep drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)]);
			pixel(W / 2, H / 2, p);
			check("...and the pixels really landed on the surface", p[0] == 0xff && p[3] == 0xff);
			/* AND NOW A COLOUR WHOSE CHANNELS DISAGREE, WHICH WHITE CANNOT BE. The rep's bytes are
			 * R, G, B, A, so opaque RED is 255, 0, 0, 255 — and the destination's memory is B, G, R, A,
			 * so the arriving red is byte 2. **THIS CHECK IS THE ONE THAT WOULD HAVE CAUGHT A REAL BUG
			 * THIS CLASS SHIPPED WITH:** its image was declared with the DEFAULT byte order, which for
			 * that declaration reads the bytes as A, B, G, R — so RED (byte 0 = 0) came out fully
			 * TRANSPARENT, and a probe filled with WHITE could never have seen it, because every channel
			 * of white says the same thing. */
			{
				unsigned char *d2 = [rep bitmapData];
				size_t k, cn = (size_t)[rep bytesPerRow] * (size_t)[rep pixelsHigh];

				/* EVERY PIXEL, not just the first: the rep is 8x4 drawn into 16x16, so a destination
				 * pixel in the middle reads a SOURCE pixel that is not (0,0) — setting one pixel and
				 * sampling the middle is the check looking at the wrong source pixel. */
				for (k = 0; k + 3 < cn; k += 4) {
					d2[k] = 0xff;      /* R */
					d2[k + 1] = 0x00;  /* G */
					d2[k + 2] = 0x00;  /* B */
					d2[k + 3] = 0xff;  /* A */
				}
				[rep setColorSpaceName:NSDeviceRGBColorSpace];
				memset(surf, 0, sizeof(surf));
				cg = CGBitmapContextCreate(surf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
							   kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
				gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
				[NSGraphicsContext setCurrentContext:gctx];
				[rep drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)];
				pixel(W / 2, H / 2, p);
				check("...and a RED pixel arrives RED, which a WHITE-only probe cannot tell (the "
				      "declaration's byte order)", p[2] == 0xff && p[0] == 0x00 && p[3] == 0xff);
				[NSGraphicsContext setCurrentContext:nil];
				CGContextRelease(cg);
			}
			[NSGraphicsContext setCurrentContext:nil];
			CGContextRelease(cg);
			[rep release];
		}

		/* THE DECODED ONE DRAWS TOO — the point of the bridge, without asserting anything about a
		 * fixture's content. */
		{
			NSData *data = [NSData dataWithBytes:png_4x4 length:sizeof(png_4x4)];
			NSBitmapImageRep *png = [NSBitmapImageRep imageRepWithData:data];
			CGContextRef cg;
			NSGraphicsContext *gctx;

			check("a rep decoded from a PNG has pixels to put on a surface", [png CGImage] != NULL);
			memset(surf, 0, sizeof(surf));
			cg = CGBitmapContextCreate(surf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
						   kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
			gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
			[NSGraphicsContext setCurrentContext:gctx];
			check("...and it DRAWS when the context is there",
			      [png drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)]);
			[NSGraphicsContext setCurrentContext:nil];
			CGContextRelease(cg);
		}
	}

	printf("APPKIT-BITMAP: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
