/*
 * CGColor — the value type, and the two DEVICE spaces it can read.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#include <CoreGraphics/CGColor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE MOST COMPONENTS A COLOUR CAN HOLD. RGB needs three and grayscale one; CMYK needs four.
 * The array is fixed rather than allocated because a colour is a small value that is created
 * and thrown away by the thousands while drawing, and an allocation per colour would be a
 * price paid on every one of them for a struct that fits in a cache line. The bound is kept
 * honest by `cg_color_space_components` refusing any model it does not have a count for. */
#define CG_COLOR_MAX_COMPONENTS 8

struct CGColor {
	int refcount;
	CGColorSpaceRef space;                    /* RETAINED, so the colour owns it */
	int ncomp;                                /* the SPACE's components, alpha NOT counted */
	CGFloat comp[CG_COLOR_MAX_COMPONENTS];    /* the components, ALPHA LAST */
};

/* THE ONE PLACE THAT DECIDES WHICH SPACES THIS LIBRARY CAN READ, so that `CGColorCreate` and
 * the context's colour setters cannot disagree about it. -1 means "not a space this tree can
 * interpret", and every caller refuses on it. */
static int cg_color_space_components(CGColorSpaceRef space)
{
	if (space == NULL) {
		return -1;
	}
	switch (CGColorSpaceGetModel(space)) {
	case kCGColorSpaceModelMonochrome:
		return 1;
	case kCGColorSpaceModelRGB:
		return 3;
	case kCGColorSpaceModelCMYK:
		return 4;
	default:
		/* A SPACE WHOSE NUMBERS THIS TREE CANNOT INTERPRET IS NOT GUESSED AT. An ICC
		 * profile's components are its own; reading them as a device model would draw
		 * something, and something wrong. */
		return -1;
	}
}

CGColorRef CGColorCreate(CGColorSpaceRef space, const CGFloat components[])
{
	CGColorRef color;
	int ncomp;
	int i;

	ncomp = cg_color_space_components(space);
	if (ncomp < 0 || components == NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorCreate needs a color space this tree can read "
				"(grayscale, RGB or CMYK) and a non-NULL component array\n");
		return NULL;
	}
	color = calloc(1, sizeof(struct CGColor));
	if (color == NULL) {
		return NULL;
	}
	color->refcount = 1;
	color->space = CGColorSpaceRetain(space);
	color->ncomp = ncomp;
	/* ALPHA LAST means ncomp + 1 values, so the loop is `<=` and not `<`. */
	for (i = 0; i <= ncomp; i++) {
		color->comp[i] = components[i];
	}
	return color;
}

CGColorRef CGColorCreateGenericGray(CGFloat gray, CGFloat alpha)
{
	CGColorSpaceRef space;
	CGColorRef color;
	CGFloat comp[2];

	space = CGColorSpaceCreateDeviceGray();
	if (space == NULL) {
		return NULL;
	}
	comp[0] = gray;
	comp[1] = alpha;
	color = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return color;
}

CGColorRef CGColorCreateGenericRGB(CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha)
{
	CGColorSpaceRef space;
	CGColorRef color;
	CGFloat comp[4];

	space = CGColorSpaceCreateDeviceRGB();
	if (space == NULL) {
		return NULL;
	}
	comp[0] = red;
	comp[1] = green;
	comp[2] = blue;
	comp[3] = alpha;
	color = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return color;
}

CGColorRef CGColorCreateCopy(CGColorRef color)
{
	if (color == NULL) {
		return NULL;
	}
	/* A COPY KEEPS THE SAME SPACE OBJECT, not a duplicate of it: a colour space here is an
	 * immutable description of how components are to be read, so two colours sharing one is
	 * what it is for. */
	return CGColorCreate(color->space, color->comp);
}

CGColorRef CGColorCreateCopyWithAlpha(CGColorRef color, CGFloat alpha)
{
	CGColorRef copy;

	copy = CGColorCreateCopy(color);
	if (copy == NULL) {
		return NULL;
	}
	copy->comp[copy->ncomp] = alpha;
	return copy;
}

CGColorRef CGColorRetain(CGColorRef color)
{
	if (color != NULL) {
		color->refcount++;
	}
	return color;
}

void CGColorRelease(CGColorRef color)
{
	if (color == NULL) {
		return;
	}
	if (--color->refcount > 0) {
		return;
	}
	/* THE SPACE IS RELEASED BY THE COLOUR THAT RETAINED IT, which is why creating a colour
	 * from a caller's space is enough to outlive that caller's reference to it. */
	CGColorSpaceRelease(color->space);
	free(color);
}

CGColorSpaceRef CGColorGetColorSpace(CGColorRef color)
{
	return color == NULL ? NULL : color->space;
}

const CGFloat *CGColorGetComponents(CGColorRef color)
{
	return color == NULL ? NULL : color->comp;
}

size_t CGColorGetNumberOfComponents(CGColorRef color)
{
	return color == NULL ? 0 : (size_t)(color->ncomp + 1);
}

CGFloat CGColorGetAlpha(CGColorRef color)
{
	return color == NULL ? 0.0 : color->comp[color->ncomp];
}

bool CGColorEqualToColor(CGColorRef color1, CGColorRef color2)
{
	int i;

	if (color1 == NULL || color2 == NULL) {
		return false;
	}
	/* THE SPACE'S MODEL AND NOT THE SPACE OBJECT. Two colours made from two separate device-RGB
	 * spaces are the same colour, because a device space IS a statement that the numbers are
	 * the ones to blend - there is nothing else to them. When a colour carries an ICC profile
	 * the profile will be part of the comparison and this line will have to say so. */
	if (CGColorSpaceGetModel(color1->space) != CGColorSpaceGetModel(color2->space) ||
	    color1->ncomp != color2->ncomp) {
		return false;
	}
	for (i = 0; i <= color1->ncomp; i++) {
		if (color1->comp[i] != color2->comp[i]) {
			return false;
		}
	}
	return true;
}
