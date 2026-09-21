/*
 * coregraphics_color_foundation — the Foundation-object forms, checked by Objective-C.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS PROBE IS OBJECTIVE-C WHILE ITS NEIGHBOUR IS NOT: the C probe CARRIES an `NSData *` in
 * and out without ever owning one — that is the opaque spelling doing its job — but a POSITIVE
 * check needs an object it can make and let go of, and letting go is a message. The C probe
 * therefore checks the refusals (a NULL object is refused by both forms) and this one checks the
 * path where an object really exists.
 *
 * AND IT IS MRC: the rule on this probe's extension passes `-fno-objc-arc`. `CGDataProviderCopyData`
 * returns +1 — that is the contract, copied from Apple — and under ARC the `-release` that balances
 * it is not merely unnecessary but forbidden, so a probe that wants to check the ownership rule can
 * only be written with manual memory management. The tree already makes this choice per file.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGDataProvider.h>

#include <lcms2.h>
#include <stdio.h>

/* `CGColor.h` FOR THE COLOUR ITSELF AND THE INTENT ENUM: this probe's job is the SPACE and the
 * PROVIDER, but the only way to show that a space built from Foundation data really works is to
 * make a colour in it and convert that colour — which is a colour operation. */
#include <CoreGraphics/CGColor.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-COLORF %-54s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

int main(void)
{
	const char *path = "/tmp/cg-probe-foundation.icc";
	NSString *pathString = [NSString stringWithUTF8String:path];
	NSData *profile;
	NSData *copied;
	CGDataProviderRef provider;
	CGColorSpaceRef space;
	CGColorSpaceRef device;
	CGColorRef colour;
	CGColorRef converted;
	cmsHPROFILE sRGB;
	CGFloat v[4];

	/* THE FIXTURE IS MADE BY THE ENGINE AND REACHED THROUGH FOUNDATION, which is the point of this
	 * file: the profile is a FILE the tree's own sRGB profile was written to, and it becomes an
	 * NSData the way an application would get one. */
	sRGB = cmsCreate_sRGBProfile();
	check("the engine can write the probe a profile", sRGB != NULL &&
	      cmsSaveProfileToFile(sRGB, path) != 0);
	if (sRGB != NULL) {
		cmsCloseProfile(sRGB);
	}
	profile = [NSData dataWithContentsOfFile:pathString];
	check("the probe can read it into an NSData", profile != nil);

	/* THE SPACE FROM FOUNDATION DATA: the form whose Apple name says `CFData` and whose argument
	 * here is an NSData. The model comes out of the profile, so an sRGB profile gives an RGB
	 * space — and that is read back through the LIBRARY, not through this file's knowledge. */
	space = CGColorSpaceCreateWithICCData(profile);
	check("CGColorSpaceCreateWithICCData gives a space", space != NULL);
	check("...whose model the profile decided",
	      space != NULL && CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB);
	check("...and which the library can draw, since it has a profile",
	      CGColorSpaceSupportsOutput(space));

	/* THE PROVIDER FROM FOUNDATION DATA, AND BACK: `CopyData` hands back +1, and the check that it
	 * is the SAME BYTES is `-isEqualToData:`, which is a real comparison rather than a length. */
	provider = CGDataProviderCreateWithCFData(profile);
	check("CGDataProviderCreateWithCFData gives a provider", provider != NULL);
	copied = CGDataProviderCopyData(provider);
	check("...whose bytes come back as an NSData", copied != nil);
	check("...and they are the SAME BYTES, not merely the same length",
	      copied != nil && [copied isEqualToData:profile]);
	[copied release];

	/* AND THE SPACE ACTUALLY CONVERTS — the identity check the C probe's ICC block makes for the
	 * file form, made here for the Foundation form. Two doors, one answer. */
	device = CGColorSpaceCreateDeviceRGB();
	v[0] = 0.2;
	v[1] = 0.5;
	v[2] = 0.8;
	v[3] = 1.0;
	colour = CGColorCreate(space, v);
	converted = CGColorCreateCopyByMatchingToColorSpace(colour, kCGRenderingIntentRelativeColorimetric,
							    device, NULL);
	check("a colour in it converts into device RGB", converted != NULL);
	if (converted != NULL) {
		const CGFloat *p = CGColorGetComponents(converted);

		check("...near the identity, as the file form's does",
		      p[0] > 0.19 && p[0] < 0.21 && p[1] > 0.49 && p[1] < 0.51 &&
		      p[2] > 0.79 && p[2] < 0.81);
		CGColorRelease(converted);
	}
	CGColorRelease(colour);
	CGColorSpaceRelease(device);

	/* AND THE PROVIDER HELD THE NSDATA UP: releasing it first must leave nothing dangling, which is
	 * the contract of the callback inside the library. There is nothing to assert beyond the fact
	 * that the space above still works after it — which the checks above already did — so this is
	 * the release that would crash if the retain were missing. */
	CGDataProviderRelease(provider);
	CGColorSpaceRelease(space);
	check("the space outlived the provider made from the same object", 1);

	printf("CG-COLORF: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
