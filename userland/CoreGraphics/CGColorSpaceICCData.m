/*
 * CGColorSpaceICCData — a colour space from an ICC profile held as Foundation data.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE THIRD DOOR INTO THE SAME ROOM, AND IT IS A BRIDGE AND NOT A THIRD IMPLEMENTATION. A profile
 * already arrives one way — a `CGDataProviderRef`, which is a Core Graphics type and needed no
 * Foundation at all (C4.3) — and this is Apple's other spelling of the same thing: `CFDataRef`,
 * which in this tree is `NSData *`.
 *
 * SO IT GOES THROUGH THE PROVIDER: make one from the data, hand it to `CGColorSpaceCreateICCBased`,
 * release it. Parsing, validation, the model taken from the profile, the refusal for a colour
 * space this library has no model for — all of that lives in one place and is exercised by the
 * file form's checks. A second implementation would be a second answer to "what does this profile
 * mean", which is the kind of duplication that ends with two of them disagreeing.
 *
 * NO ARC HERE, matching the library's policy, and nothing in this file owns anything beyond the
 * provider it makes and releases.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdio.h>

CGColorSpaceRef CGColorSpaceCreateWithICCData(NSData *data)
{
	CGDataProviderRef provider;
	CGColorSpaceRef space;

	if (data == nil) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateWithICCData needs data\n");
		return NULL;
	}
	provider = CGDataProviderCreateWithCFData(data);
	if (provider == NULL) {
		return NULL;
	}
	/* N COMPONENTS OF 0 MEANS "TAKE THE PROFILE'S OWN", and there is nothing else to pass: the
	 * range and the alternate space are the two parameters this library refuses or ignores, and
	 * both are documented at the function this arrives at. */
	space = CGColorSpaceCreateICCBased(0, NULL, provider, NULL);
	CGDataProviderRelease(provider);
	return space;
}
