/*
 * appkit_image — a container of representations, and the size precedence that makes it one.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE INTERESTING CLAIMS ARE TWO, AND NEITHER IS ABOUT PIXELS:
 *   * `-size` has TWO SOURCES — an explicit one, else the first representation's — and the precedence
 *     is the whole reason `NSImage` is not just a rep. So it is checked both ways, including that
 *     `-setSize:` STOPS following the reps.
 *   * `-isValid` asks whether there is anything to DRAW, so an empty canvas with a size is INVALID.
 *
 * AND THE DRAW CHECK DOES NOT TRUST A FIXTURE: the PNG here proves the DECODE and the container, but
 * the pixels drawn are a rep this probe fills with opaque white by hand, so an assertion about the
 * surface cannot depend on what an image happens to contain.
 *
 * IT IS MRC AND WRAPS ITSELF IN A POOL.
 */
#import <AppKit/NSImage.h>
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
	printf("APPKIT-IMAGE %-66s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	int ok = (got >= want - tol && got <= want + tol);

	printf("APPKIT-IMAGE %-66s %s", name, ok ? "ok" : "FAIL");
	if (!ok) {
		printf(" (got %g, want %g)", got, want);
		failures++;
	}
	printf("\n");
}

static void pixel(int x, int y, unsigned char *out)
{
	memcpy(out, surf + ((size_t)(y * W + x) * 4u), 4);
}

/* A WHITE-FILLED rep of a given size, so a surface assertion has a known picture behind it. */
static NSBitmapImageRep *white_rep(NSInteger w, NSInteger h)
{
	NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
				pixelsWide:w pixelsHigh:h bitsPerSample:8 samplesPerPixel:4
				hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
				bytesPerRow:w * 4 bitsPerPixel:32];

	memset([rep bitmapData], 0xff, (size_t)(w * 4 * h));
	return rep;
}

static CGContextRef make_context(void)
{
	memset(surf, 0, sizeof(surf));
	return CGBitmapContextCreate(surf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				     kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
}

/* THE 4x4 8-BIT RGBA PNG the CoreGraphics decode probe already uses. */
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
		NSImage *im;

		/* --- AN EMPTY CANVAS IS VALID AS A CANVAS AND INVALID AS AN IMAGE ---------------- */
		im = [NSImage image];
		check("+image is an empty canvas", im != nil);
		check_num("...with a zero width", (double)[im size].width, 0.0, 0.0);
		check("...and it is NOT valid, because there is nothing to draw", ![im isValid]);
		check_num("...and it holds no representations", (double)[[im representations] count], 0.0, 0.0);
		check("...and drawing it is a NO, not a raise", ![im drawInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0)]);

		/* A SIZE WITH NO REPS IS STILL INVALID — the distinction worth asking about. */
		im = [[NSImage alloc] initWithSize:NSMakeSize(100.0, 50.0)];
		check_num("an image made with an explicit size reports it", (double)[im size].width, 100.0, 0.0);
		check("...and is STILL invalid with no representations", ![im isValid]);
		[im release];

		/* --- A DECODED IMAGE TAKES ITS SIZE FROM ITS REPRESENTATION --------------------- */
		{
			NSData *data = [NSData dataWithBytes:png_4x4 length:sizeof(png_4x4)];

			im = [[NSImage alloc] initWithData:data];
			check("+[NSImage initWithData:] on a PNG gives an image", im != nil);
			check_num("...holding exactly one representation", (double)[[im representations] count],
				  1.0, 0.0);
			check("...which makes it VALID at last", [im isValid]);
			check_num("...and its width FOLLOWS that representation", (double)[im size].width, 4.0, 0.0);
			check_num("...and its height likewise", (double)[im size].height, 4.0, 0.0);
			/* AND AN EXPLICIT SIZE STOPS THE FOLLOWING, which is the precedence. */
			[im setSize:NSMakeSize(100.0, 50.0)];
			check_num("...but setSize: STOPS that follow", (double)[im size].width, 100.0, 0.0);
			check_num("...while the rep keeps its own pixels",
				  (double)[[[im representations] objectAtIndex:0] pixelsWide], 4.0, 0.0);
			[im release];
		}
		check("data that is not an image gives NIL, not an empty image",
		      [[NSImage alloc] initWithData:[NSData dataWithBytes:"zzzzzzzz" length:8]] == nil);
		check("...and so does a file that does not exist",
		      [[NSImage alloc] initWithContentsOfFile:@"no/such/file.png"] == nil);

		/* --- THE REPS ARE OURS TO ADD AND REMOVE, AND THE SNAPSHOT IS A SNAPSHOT -------- */
		{
			NSBitmapImageRep *a = white_rep(4, 4);
			NSBitmapImageRep *b = white_rep(16, 16);
			NSArray *snapshot;

			im = [[NSImage alloc] initWithSize:NSMakeSize(16.0, 16.0)];
			[im addRepresentation:a];
			check_num("adding a representation puts it in the list",
				  (double)[[im representations] count], 1.0, 0.0);
			snapshot = [im representations];
			[im addRepresentation:b];
			check_num("...and a later add does not change an EARLIER snapshot",
				  (double)[snapshot count], 1.0, 0.0);
			check_num("...though the image now has two", (double)[[im representations] count], 2.0, 0.0);

			/* THE CHOICE IS THE CLOSEST IN POINTS, and with a 4 and a 16 asking for 16 must give the 16. */
			check("the best representation for a 16-point rect is the 16-point one",
			      [im bestRepresentationForRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)
						    context:nil hints:nil] == b);
			check("...and one for a 4-point rect is the 4-point one",
			      [im bestRepresentationForRect:NSMakeRect(0.0, 0.0, 4.0, 4.0)
						    context:nil hints:nil] == a);

			[im removeRepresentation:a];
			check_num("removing one leaves the other", (double)[[im representations] count], 1.0, 0.0);
			[im removeRepresentation:b];
			check_num("...and removing both leaves none", (double)[[im representations] count],
				  0.0, 0.0);
			check("...so an emptied image is invalid again", ![im isValid]);
			[im release];
			[a release];
			[b release];
		}

		/* --- AND THE DRAW, WITH A PICTURE OF OUR OWN MAKING ---------------------------- */
		{
			NSBitmapImageRep *white = white_rep(8, 4);
			CGContextRef cg;
			NSGraphicsContext *gctx;

			im = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)W, (CGFloat)H)];
			[im addRepresentation:white];
			cg = make_context();
			gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
			[NSGraphicsContext setCurrentContext:gctx];
			check("an image draws its representation into the CURRENT context",
			      [im drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)]);
			pixel(W / 2, H / 2, p);
			check("...and the pixels really landed", p[0] == 0xff && p[3] == 0xff);
			[NSGraphicsContext setCurrentContext:nil];

			/* WITH NO CURRENT CONTEXT THE SAME CALL IS A NO RATHER THAN A CRASH. */
			check("...and with no current context it is a NO", ![im drawInRect:NSMakeRect(0.0, 0.0, 4.0, 4.0)]);
			check("...as is -drawAtPoint:", ![im drawAtPoint:NSMakePoint(0.0, 0.0)]);

			/* AND AN IMAGE WITH NO REPS DRAWS NOTHING EVEN WITH A CONTEXT THERE. */
			[NSGraphicsContext setCurrentContext:gctx];
			{
				NSImage *empty = [[NSImage alloc] initWithSize:NSMakeSize(8.0, 8.0)];

				check("...and an image with no reps draws nothing even with a context",
				      ![empty drawInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0)]);
				[empty release];
			}
			[NSGraphicsContext setCurrentContext:nil];
			CGContextRelease(cg);
			[im release];
			[white release];
		}

		/* --- AND AN IMAGE FROM A CGIMAGE, WHICH IS THE OTHER DOOR IN -------------------- */
		{
			CGImageRef cgi;
			NSBitmapImageRep *r = white_rep(8, 8);

			cgi = [r CGImage];
			im = [[NSImage alloc] initWithCGImage:cgi size:NSMakeSize(20.0, 20.0)];
			check("an image can be built from a CGImage", im != nil);
			check_num("...with the size the caller gave, not the pixels", (double)[im size].width,
				  20.0, 0.0);
			check_num("...and the rep carries the pixels", (double)[[im representations] count],
				  1.0, 0.0);
			[im release];
			[r release];
		}
	}

	printf("APPKIT-IMAGE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
