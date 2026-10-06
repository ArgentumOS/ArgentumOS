/*
 * CGColorNames — the names `CGColorGetConstantColor` is asked for.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THESE ARE NAMES AND NOT COLOURS: in the 10.6 surface (which is the surface this library targets) they are
 * `CFStringRef` constants, spelled here as `NSString *` like every other CF type, and a caller hands one to
 * `CGColorGetConstantColor` to get the colour back. Apple's own heading says so: "Names of colors for use
 * with `CGColorGetConstantColor'".
 *
 * THE VALUE IS THIS LIBRARY'S OWN, AND APPLE PUBLISHES NONE — the constant's *contents* are not part of any
 * published contract, only the fact that these three names are recognised. So each one's value is its own
 * name, which is the property that makes it useless to guess at: the door below recognises exactly what the
 * header declares, and nothing else gets a colour.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColor.h>

#include <stdio.h>

NSString *const kCGColorWhite = @"kCGColorWhite";
NSString *const kCGColorBlack = @"kCGColorBlack";
NSString *const kCGColorClear = @"kCGColorClear";

/* ------------------------------------------------------------------------- */
/* The door the names are for                                                  */
/* ------------------------------------------------------------------------- */

/* THE CACHE IS THE CONTRACT: these are constants, so the second call answers the SAME OBJECT as the first.
 * The cache holds a reference of its own, which is what makes that safe — a caller is free to release the
 * colour it was handed, and the entry has to survive it. Three slots and no eviction: there are three names.
 *
 * NOTE THE SPACE: `CGColorCreateGenericGray` builds a DEVICE gray colour while Apple's comment places these
 * in the generic gray space. See CGColor.h for why the divergence is unobservable through this library.
 *
 * AND `clear` IS GRAY ZERO AT ALPHA ZERO: nothing can see the gray, but a caller reading the components back
 * gets a number either way, and zero says "no ink" rather than "white, invisible". */
static CGColorRef fn_constant[3];

static CGColorRef fn_cached(int slot, CGFloat gray, CGFloat alpha)
{
	if (fn_constant[slot] == NULL) {
		fn_constant[slot] = CGColorCreateGenericGray(gray, alpha);
	}
	return fn_constant[slot];
}

CGColorRef CGColorGetConstantColor(NSString *name)
{
	if (name == nil) {
		return NULL;
	}
	if ([name isEqual:kCGColorWhite]) {
		return fn_cached(0, 1.0, 1.0);
	}
	if ([name isEqual:kCGColorBlack]) {
		return fn_cached(1, 0.0, 1.0);
	}
	if ([name isEqual:kCGColorClear]) {
		return fn_cached(2, 0.0, 0.0);
	}
	fprintf(stderr, "CG-REFUSE: CGColorGetConstantColor knows kCGColorWhite, kCGColorBlack and "
			"kCGColorClear, and was asked for a name it does not have\n");
	return NULL;
}
