/*
 * CGGradientColors — the `NSArray` form of `CGGradientCreateWithColors`, and why it is a file.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SIGNATURE NAMES A FOUNDATION CLASS, so this half has to be Objective-C: unwrapping an NSValue
 * with `-pointerValue` is a message send, and a C compiler cannot make one. The library already has
 * this shape twice — CGColorSpaceNames.m sends `-isEqualToString:` for the named spaces, and
 * CGDataProviderFoundation.m holds the `NSData` form of a provider — so this is the third and it
 * follows the same arrangement: the Objective-C file is named for the FEATURE and `.m`, the C
 * arithmetic stays in the C file, and the two meet at a C function declared in an internal header.
 *
 * WHY A NAMED SEPARATE FILE AND NOT A `.m` FOR THE WHOLE GRADIENT: the build derives one object name
 * per source file, so a `CGGradient.c` and a `CGGradient.m` in the same directory would both be
 * `coregraphics-CGGradient.o` and the second would overwrite the first. That is not a style
 * preference, it is a rule about this tree's build, and it is written down here because the next
 * person to add an Objective-C half will meet it.
 *
 * WHAT THIS FILE DOES NOT DO IS CONVERT COLOURS. It unwraps, checks, and hands an array of
 * `CGColorRef` to `cg_gradient_create_with_colors`, which is the same function the components form
 * calls — so a stop means one thing in this library, whichever door it came through.
 */
#include <CoreGraphics/CGGradient.h>
#include <CoreGraphics/CGGradient_internal.h>

#import <Foundation/NSArray.h>
#import <Foundation/NSValue.h>

#include <stdio.h>
#include <stdlib.h>

CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef space, NSArray *colors,
					 const CGFloat *locations)
{
	CGColorRef *refs;
	CGGradientRef g;
	NSUInteger count;
	NSUInteger i;

	if (colors == NULL) {
		fprintf(stderr, "CG-REFUSE: CGGradientCreateWithColors needs an array of colours\n");
		return NULL;
	}
	/* CHECKED HERE RATHER THAN IN THE SHARED BUILDER, because "fewer than two" has a different
	 * cause in this form — an array the caller built short — and because the array's count is
	 * what has to be walked to build the C array below. The builder checks it again, which costs
	 * nothing and keeps that function correct on its own. */
	count = [colors count];
	if (count < 2) {
		fprintf(stderr, "CG-REFUSE: a gradient needs at least two stops to interpolate "
				"between; the array holds %lu\n", (unsigned long)count);
		return NULL;
	}
	refs = calloc(count, sizeof(CGColorRef));
	if (refs == NULL) {
		return NULL;
	}
	for (i = 0; i < count; i++) {
		id element = [colors objectAtIndex:i];

		/* AN ELEMENT THAT IS NOT AN `NSValue` IS A REFUSAL, NOT A CAST. `-pointerValue` sent
		 * to, say, an NSString would be an unrecognised selector at best and a colour made
		 * from a string's address at worst; checking first is one `-isKindOfClass:` against a
		 * class of picture drawn from a caller's mistake. */
		if (element == nil || ![element isKindOfClass:[NSValue class]]) {
			fprintf(stderr, "CG-REFUSE: gradient stop %lu is not an NSValue — the array "
					"this function takes holds pointer-wrapped CGColorRef; see "
					"CGGradient.h\n", (unsigned long)i);
			free(refs);
			return NULL;
		}
		refs[i] = (CGColorRef)[(NSValue *)element pointerValue];
		if (refs[i] == NULL) {
			fprintf(stderr, "CG-REFUSE: gradient stop %lu wraps a NULL colour\n",
				(unsigned long)i);
			free(refs);
			return NULL;
		}
	}
	g = cg_gradient_create_with_colors(space, refs, (size_t)count, locations);
	free(refs);
	return g;
}
