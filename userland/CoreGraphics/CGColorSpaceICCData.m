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
#include <CoreGraphics/CGColorSpace_internal.h>
#include <CoreGraphics/CGDataProvider.h>

/* THE ENGINE, BECAUSE THE WRITE DIRECTION IS THE ENGINE'S: `cmsSaveProfileToMem` turns the parsed
 * profile a space keeps back into bytes, and `cg_colorspace_engine_profile` — the internal seam —
 * is how this file reaches it. */
#include <lcms2.h>

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

/* ---------------------------------------------------------------------------------------
 * AND THE OTHER DIRECTION: THE PROFILE'S BYTES BACK OUT AS FOUNDATION DATA.
 *
 * WHAT COMES OUT IS A RE-SERIALISATION, NOT THE BYTES THAT WENT IN — the only honest version of
 * this function, and worth stating where a caller will read it. A space keeps the PARSED profile,
 * because that is what makes drawing through it fast and what lets the provider be released the
 * moment the space exists, so these bytes are the ones the engine WRITES. They describe the same
 * space; they are not byte-identical to the input, and a caller comparing bytes would notice. The
 * probe checks the property that matters — a space rebuilt from these bytes converts identically.
 *
 * A SPACE WITH NO STORED PROFILE ANSWERS NIL. That is Apple's own contract for a space without ICC
 * data, and it is the case for every DEVICE space here: Apple's device spaces carry profiles, this
 * library's do not, and the conversion path synthesises the one it needs when it needs it rather
 * than keeping it around.
 * ------------------------------------------------------------------------------------- */
NSData *CGColorSpaceCopyICCData(CGColorSpaceRef space)
{
	cmsUInt32Number size = 0;
	cmsHPROFILE p;
	NSMutableData *data;

	if (space == NULL) {
		return nil;
	}
	p = (cmsHPROFILE)cg_colorspace_engine_profile(space);
	if (p == NULL) {
		return nil;
	}
	/* THE ENGINE WRITES INTO A LENGTH AND CALLS AGAIN: the first call with NULL answers the size,
	 * the second fills the buffer. `NSMutableData` rather than malloc because it needs no include
	 * this file does not already have — and because `-initWithLength:` is +1, which is exactly what
	 * a `Copy`-named function owes its caller. */
	if (!cmsSaveProfileToMem(p, NULL, &size) || size == 0) {
		return nil;
	}
	data = [[NSMutableData alloc] initWithLength:(NSUInteger)size];
	if (data == nil) {
		return nil;
	}
	if (!cmsSaveProfileToMem(p, [data mutableBytes], &size)) {
		[data release];
		return nil;
	}
	/* THE SIZE CAN SHRINK, and the object says so rather than carrying a tail of uninitialised
	 * bytes that a parser would read as part of the profile. */
	[data setLength:(NSUInteger)size];
	return data;
}
