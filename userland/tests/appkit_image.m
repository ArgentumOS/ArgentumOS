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
/* §63.52: the resource doors MOVED from Foundation into an AppKit category, so this probe imports the tier
 * that now owns them — and the two Foundation classes its fixture is built with. */
#import <AppKit/NSBundleAdditions.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSURL.h>

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

	{
		/* THE SCOPE IS OURS: `im` at the top of the pool is out of reach here, which the compiler
		 * said five times before this line existed. */
		NSImage *im;

			/* --- DRAWN INTO AN OFFSCREEN CANVAS: +imageWithSize:flipped:drawingHandler: -------------- */
			/* THE OFFSCREEN CANVAS IS BUILT THROUGH +imageWithSize:flipped:drawingHandler:, AND THE REASON
			 * IS NO LONGER A POLICY. It was: `-lockFocus`/`-unlockFocus` used to be STRUCK as deprecated
			 * rows, and the standing policy was "no deprecated APIs". THAT POLICY WAS RETIRED on
			 * 2026-09-26 (plan section 62.24) - the two names are OWED now, and the ledger labels them
			 * `deprecated` rather than excluding them. What still decides this form is preference, not
			 * rule: the handler form is the modern spelling, and this check proves the canvas path either
			 * way. THE SWIZZLE PROOF IS RED IN, RED OUT again — a backwards channel swizzle reads red as
			 * BLUE and every other check here would still pass. */
			{
				CGContextRef cg;
				NSGraphicsContext *gctx;

				/* RED, DRAWN BY A HANDLER THAT ALSO ANSWERS NO — the answer is ignored, as Apple's is. */
				im = [NSImage imageWithSize:NSMakeSize(8.0, 8.0) flipped:NO
						 drawingHandler:^BOOL(NSRect dst) {
					CGContextRef c = [[NSGraphicsContext currentContext] CGContext];

					CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
					CGContextFillRect(c, dst);
					return NO;
				}];
				check("+imageWithSize:flipped:drawingHandler: makes an image", im != nil);
				check("...which is VALID, because the canvas became a rep", [im isValid]);
				check_num("...at the size it was given", (double)[im size].width, 8.0, 0.0);
				check_num("...holding one representation", (double)[[im representations] count], 1.0, 0.0);

				cg = make_context();
				gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
				[NSGraphicsContext setCurrentContext:gctx];
				check("...and it draws", [im drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)]);
				pixel(W / 2, H / 2, p);
				check("...with the RED still RED (a backwards swizzle would read it as BLUE)",
				      p[2] == 0xff && p[1] == 0x00 && p[0] == 0x00);
				[im release];
				[NSGraphicsContext setCurrentContext:nil];
				CGContextRelease(cg);
			}

			/* --- AND `flipped:` MUST MEAN SOMETHING SPECIFIC, NOT MERELY DIFFER --------------------------- */
			/* ***THIS CHECK IS WHY A REAL BUG SURVIVED A WHOLE SLICE.*** It used to assert only that the two
			 * directions put the same handler in OPPOSITE halves — WHICH A SWAPPED PAIR OF MEANINGS SATISFIES
			 * JUST AS WELL. The handler now paints THE FIRST TWO UNITS OF ITS OWN Y, and each direction is
			 * asserted against the rows it MUST land in: `flipped:YES` is AppKit's y-DOWN system, so its own
			 * y = 0 is the TOP row of the destination, and `flipped:NO` puts it at the BOTTOM. */
			{
				int first[2];
				int i;
			
				for (i = 0; i < 2; i++) {
					CGContextRef cg;
					NSGraphicsContext *gctx;
					int y;
			
					im = [NSImage imageWithSize:NSMakeSize(8.0, 8.0) flipped:(i == 1)
							 drawingHandler:^BOOL(NSRect dst) {
						CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
			
						/* THE FIRST TWO UNITS OF THE HANDLER'S OWN Y, IN BLUE. */
						CGContextSetRGBFillColor(c, 0.0, 0.0, 1.0, 1.0);
						CGContextFillRect(c, CGRectMake(0.0, 0.0, 8.0, 2.0));
						(void)dst;
						return YES;
					}];
					cg = make_context();
					gctx = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
					[NSGraphicsContext setCurrentContext:gctx];
					[im drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)W, (CGFloat)H)];
					first[i] = -1;
					for (y = 0; y < H; y++) {
						pixel(W / 2, y, p);
						if (p[0] == 0xff && first[i] < 0) {
							first[i] = y;
						}
					}
					[NSGraphicsContext setCurrentContext:nil];
					CGContextRelease(cg);
					[im release];
				}
				/* THE CANVAS IS 8 TALL AND THE DESTINATION 16, so the fill's two units become four rows. */
				check_num("...and `flipped:NO` puts its own y = 0 at the BOTTOM (row 12 of 16)",
					  (double)first[0], 12.0, 0.0);
				check_num("...while `flipped:YES` puts it at the TOP (row 0)", (double)first[1], 0.0, 0.0);
			}

			/* --- AND THE TWO REFUSALS ----------------------------------------------------------- */
			check("a NULL drawing handler is REFUSED rather than crashing",
			      [NSImage imageWithSize:NSMakeSize(8.0, 8.0) flipped:NO drawingHandler:NULL] == nil);
			check("...and a size with no pixels is refused too",
			      [NSImage imageWithSize:NSMakeSize(0.0, 0.0) flipped:NO
				      drawingHandler:^BOOL(NSRect dst) { (void)dst; return YES; }] == nil);
	}

	/* --- §63.52: THE RESOURCE DOORS THAT MOVED HERE FROM FOUNDATION --------------------------------- */
	{
		/* A MINIMAL BUNDLE, BUILT BY THIS CHECK: `ResFixture.app/Contents/` with an Info.plist and three
		 * resource files. **THE FIXTURE IS BUILT HERE RATHER THAN REACHED FOR** because the doors are this
		 * tier's now, and a check that depends on the Foundation probe's fixture would put the dependency
		 * back. What is asserted is the behaviour Foundation used to assert — an optional extension, a
		 * literal name winning, a miss answering nil — reached through the CATEGORY this tier declares. */
		NSString *root = @"/tmp/appkit-6352-fixture";
		NSString *app = [root stringByAppendingPathComponent:@"ResFixture.app"];
		NSString *contents = [app stringByAppendingPathComponent:@"Contents"];
		NSString *resources = [contents stringByAppendingPathComponent:@"Resources"];
		NSFileManager *fm = [NSFileManager defaultManager];
		NSData *one = [@"x" dataUsingEncoding:NSUTF8StringEncoding];
		NSBundle *bundle;

		[fm removeItemAtPath:root error:NULL];
		[fm createDirectoryAtPath:resources withIntermediateDirectories:YES attributes:nil error:NULL];
		[fm createFileAtPath:[contents stringByAppendingPathComponent:@"Info.plist"]
			    contents:[@"{ CFBundleIdentifier = \"org.argentum.probe.resfixture\"; }"
				      dataUsingEncoding:NSUTF8StringEncoding] attributes:nil];
		[fm createFileAtPath:[resources stringByAppendingPathComponent:@"pic.png"] contents:one attributes:nil];
		[fm createFileAtPath:[resources stringByAppendingPathComponent:@"logo.png"] contents:one attributes:nil];
		[fm createFileAtPath:[resources stringByAppendingPathComponent:@"beep.wav"] contents:one attributes:nil];

		bundle = [NSBundle bundleWithPath:app];
		{
			NSString *byStem = [bundle pathForImageResource:@"pic"];
			NSString *byFullName = [bundle pathForImageResource:@"logo.png"];
			NSString *sound = [bundle pathForSoundResource:@"beep"];
			NSURL *imageURL = [bundle URLForImageResource:@"pic"];

			check("bundle-finds-image-and-sound-resources-by-name",
			      byStem != nil && [byStem hasSuffix:@"Resources/pic.png"] &&
			      byFullName != nil && [byFullName hasSuffix:@"Resources/logo.png"] &&
			      sound != nil && [sound hasSuffix:@"Resources/beep.wav"] &&
			      [bundle pathForImageResource:@"no-such-image"] == nil &&
			      [[imageURL path] isEqualToString:byStem]);
		}
	}

	printf("APPKIT-IMAGE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
