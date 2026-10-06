/*
 * coregraphics_icc — the ICC profile both ways, and what a profile means to a space.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ROUND TRIP IS THE CLAIM: a space's profile comes out as bytes, and those bytes make a space again whose
 * model and component count are the same AND WHICH CAN STILL BE FILLED WITH. A door that produced bytes nothing
 * accepted would pass a length check and fail these.
 *
 * AND APPLE'S OWN ANSWER FOR A SPACE WITH NO PROFILE IS PART OF THE CONTRACT — "or NULL if the color space
 * doesn't have an ICC profile" — which in this library means every DEVICE space, because a device space is the
 * one whose numbers are the numbers.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>

#include <stdio.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-ICC %-66s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 4
#define H 4
#define STRIDE (W * 4)

int main(void)
{
	CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGColorSpaceRef device = CGColorSpaceCreateDeviceRGB();
	NSData *bytes;
	CGColorSpaceRef back;

	check("a profile-backed space is available to copy from", srgb != NULL);
	bytes = CGColorSpaceCopyICCProfile(srgb);
	check("its profile comes out as bytes", bytes != nil && [bytes length] > 0);
	check("...and a DEVICE space has none to give, which is Apple's NULL",
	      CGColorSpaceCopyICCProfile(device) == nil && CGColorSpaceCopyICCProfile(NULL) == nil);

	back = CGColorSpaceCreateWithICCProfile(bytes);
	check("those bytes make a color space again", back != NULL);
	check("...with the same model and component count",
	      back != NULL && CGColorSpaceGetModel(back) == kCGColorSpaceModelRGB
	      && CGColorSpaceGetNumberOfComponents(back) == 3);
	check("...and its own profile copies out too, so the round trip is stable",
	      back != NULL && CGColorSpaceCopyICCProfile(back) != nil);

	/* AND THE SPACE IS USABLE, not merely describable: a colour in it fills a context. */
	{
		unsigned char canvas[STRIDE * H];
		CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, device,
						       kCGImageAlphaPremultipliedFirst
						       | kCGBitmapByteOrder32Little);
		const CGFloat red[4] = { 1.0, 0.0, 0.0, 1.0 };
		CGColorRef color = CGColorCreate(back, red);

		check("a colour can be made in the round-tripped space", color != NULL);
		if (color != NULL && c != NULL) {
			CGContextSetFillColorWithColor(c, color);
			CGContextFillRect(c, CGRectMake(0, 0, W, H));
			printf("CG-ICC %-66s b=%u g=%u r=%u a=%u\n", "...readout", canvas[0], canvas[1],
			       canvas[2], canvas[3]);
			check("...and it paints red-ish, which means the profile was really parsed",
			      canvas[2] > 0xE0 && canvas[1] < 0x20 && canvas[0] < 0x20 && canvas[3] == 0xFF);
		}
		if (color != NULL) {
			CGColorRelease(color);
		}
		CGContextRelease(c);
	}

	check("and bytes that are not a profile are refused rather than kept",
	      CGColorSpaceCreateWithICCProfile([NSData dataWithBytes:"not a profile" length:4]) == NULL);
	check("...as is no data at all", CGColorSpaceCreateWithICCProfile(nil) == NULL);

	CGColorSpaceRelease(back);
	if (bytes != nil) {
		[bytes release];
	}
	CGColorSpaceRelease(device);
	CGColorSpaceRelease(srgb);
	printf("CG-ICC: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
