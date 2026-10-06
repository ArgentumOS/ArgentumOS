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

	/* THE SPACE FROM FOUNDATION DATA, THROUGH THE IN-ERA DOOR. This used to be
	 * `CGColorSpaceCreateWithICCData` — the NSData spelling, macOS 10.12, REMOVED on 2026-10-05 —
	 * so it now goes through the PROVIDER door, which is the same profile bytes reaching the same
	 * parser: `CGDataProviderCreateWithCFData` (10.4) and `CGColorSpaceCreateICCBased` (10.0).
	 * THE MODEL STILL COMES OUT OF THE PROFILE, read back through the LIBRARY rather than through
	 * this file's knowledge, which is what the check below is for. */
	{
		CGDataProviderRef p = CGDataProviderCreateWithCFData(profile);
		check("CGDataProviderCreateWithCFData gives a provider", p != NULL);
		space = CGColorSpaceCreateICCBased(3, NULL, p, NULL);
		CGDataProviderRelease(p);
	}
	check("the provider door turns those bytes into a space", space != NULL);
	check("...whose model the profile decided",
	      space != NULL && CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB);

	/* THE PROVIDER FROM FOUNDATION DATA, AND BACK: `CopyData` hands back +1, and the check that it
	 * is the SAME BYTES is `-isEqualToData:`, which is a real comparison rather than a length. */
	provider = CGDataProviderCreateWithCFData(profile);
	check("CGDataProviderCreateWithCFData gives a provider", provider != NULL);
	copied = CGDataProviderCopyData(provider);
	check("...whose bytes come back as an NSData", copied != nil);
	check("...and they are the SAME BYTES, not merely the same length",
	      copied != nil && [copied isEqualToData:profile]);
	[copied release];

	/* !! THE CONVERSION CHECK STOOD HERE AND IS REPLACED BY THE REFUSAL (2026-10-05), and what it
	 * established is in history: the FOUNDATION form of the profile — the provider built from an
	 * NSData rather than from a file — reached the SAME identity the C probe's file form did,
	 * 0.2/0.5/0.8 back to within a hundredth. TWO DOORS, ONE ANSWER is what it proved, and the
	 * answer is that both doors lead to the same parser. WHAT IS LEFT NOW IS THE VALUE: the colour
	 * is created in the profile's space, and nothing here can re-express it, because
	 * `CGColorCreateCopyByMatchingToColorSpace` is macOS 10.11 against a 10.6-era surface. */
	device = CGColorSpaceCreateDeviceRGB();
	v[0] = 0.2;
	v[1] = 0.5;
	v[2] = 0.8;
	v[3] = 1.0;
	colour = CGColorCreate(space, v);
	check("a colour in the profile's space can be created through the provider door",
	      colour != NULL);
	check("...and its model is still the profile's",
	      CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB);
	CGColorRelease(colour);
	CGColorSpaceRelease(device);

	/* AND THE PROVIDER HELD THE NSDATA UP: releasing it first must leave nothing dangling, which is
	 * the contract of the callback inside the library. There is nothing to assert beyond the fact
	 * that the space above still works after it — which the checks above already did — so this is
	 * the release that would crash if the retain were missing. */
	CGDataProviderRelease(provider);
	CGColorSpaceRelease(space);
	check("the space outlived the provider made from the same object", 1);

	/* --- A NAMED SPACE CAN SAY ITS NAME, WHICH IS THE LOOP CLOSING ------------------------- */
	/* A NAME THAT COMES OUT OF `CGColorSpaceCopyName` IS ONE THAT CAN GO STRAIGHT BACK IN, and
	 * that round trip is the check: it holds only because the named spaces are one object per
	 * name. A DEVICE SPACE HAS NO NAME — Apple's index has no device-space name constant at all —
	 * and nil is what its own documentation allows for that case. */
	{
		CGColorSpaceRef named = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
		CGColorSpaceRef device = CGColorSpaceCreateDeviceRGB();
		NSString *name;
		CGColorSpaceRef again;

		name = CGColorSpaceCopyName(named);
		check("a named space reports the name it was made with",
		      name != nil && [name isEqual:kCGColorSpaceAdobeRGB1998]);
		again = CGColorSpaceCreateWithName(name);
		check("...and that name goes straight back in, giving the SAME space",
		      again != NULL && again == named);
		CGColorSpaceRelease(again);
		/* THE CALLER OWNS THE COPY, and under MRC that means releasing it — which is also the
		 * check that this function's ownership is what its name promises. */
		[name release];

		check("a device space has NO name to report", CGColorSpaceCopyName(device) == nil);
		check("...and a NULL space is answered rather than crashed on",
		      CGColorSpaceCopyName(NULL) == nil);
		CGColorSpaceRelease(device);
		CGColorSpaceRelease(named);
	}

	/* --- THE ICC ROUND TRIP STOOD HERE AND WAS REMOVED (2026-10-05) ------------------------ */
	/* This block asked whether a profile's bytes come back out of a space and rebuild a space that
	 * converts the same way. BOTH HALVES OF THAT PAIR ARE GONE — `CGColorSpaceCopyICCData` and
	 * `CGColorSpaceCreateWithICCData` are macOS 10.12, out of era — and so is the conversion the
	 * answer used to be read back through (`CGColorCreateCopyByMatchingToColorSpace`, 10.11). So
	 * the block cannot be re-pointed at an in-era door: there is no way left for a caller to ask a
	 * space for its bytes. THE IN-ERA COUNTERPART IS A ROW, NOT AN IMPLEMENTATION:
	 * `CGColorSpaceCopyICCProfile` (10.5) is owed work, and when it lands, THIS block's property —
	 * a re-serialisation that describes the same space — is what it should be checked against. */

	printf("CG-COLORF: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
