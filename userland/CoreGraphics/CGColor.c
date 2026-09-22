/*
 * CGColor — the value type, and the two DEVICE spaces it can read.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace_internal.h>

#include <lcms2.h>
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
	case kCGColorSpaceModelLab:
		return 3;   /* L*, a*, b* */
	case kCGColorSpaceModelXYZ:
		return 3;   /* X, Y, Z -- a colour CAN be in an XYZ space, and the engine converts it */
	default:
		/* A SPACE WHOSE NUMBERS THIS TREE CANNOT INTERPRET IS NOT GUESSED AT. A space this
		 * code has no model for would have its components read as if they were a device
		 * model, which draws something, and something wrong. */
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

/* ---------------------------------------------------------------------------------------
 * THE CONVERSION. Every line below is the engine's (lcms2), and everything above it in this
 * file treats a colour as numbers in a named space; this is the one place where the numbers
 * are interpreted.
 * ------------------------------------------------------------------------------------- */

/* THE INTENTS DO NOT LINE UP, WHICH IS WHY THIS FUNCTION EXISTS. Apple documents Default,
 * AbsoluteColorimetric, RelativeColorimetric, Perceptual, Saturation. The engine numbers
 * Perceptual 0, RelativeColorimetric 1, Saturation 2, AbsoluteColorimetric 3 - so passing an
 * intent through as it stands would ask for a different one, silently, and only in the output.
 * Default is PERCEPTUAL, which is what Apple's documentation calls the usual default. */
static int cg_engine_intent(CGColorRenderingIntent intent)
{
	switch (intent) {
	case kCGRenderingIntentAbsoluteColorimetric:
		return INTENT_ABSOLUTE_COLORIMETRIC;
	case kCGRenderingIntentRelativeColorimetric:
		return INTENT_RELATIVE_COLORIMETRIC;
	case kCGRenderingIntentPerceptual:
		return INTENT_PERCEPTUAL;
	case kCGRenderingIntentSaturation:
		return INTENT_SATURATION;
	default:
		return INTENT_PERCEPTUAL;
	}
}

/* THE ENGINE'S TYPE CODE DESCRIBES THE MEMORY LAYOUT, NOT THE SPACE: three doubles for RGB,
 * one for gray, three for Lab in the ICC convention (L* 0..100, a* and b* around 0), which is
 * also what a Lab colour's components mean here. A model with no code cannot be converted, and
 * that refusal is the honest answer rather than a guess at a layout. */
static int cg_engine_format(CGColorSpaceModel model, int *ncomp)
{
	switch (model) {
	case kCGColorSpaceModelMonochrome:
		*ncomp = 1;
		return TYPE_GRAY_DBL;
	case kCGColorSpaceModelRGB:
		*ncomp = 3;
		return TYPE_RGB_DBL;
	case kCGColorSpaceModelLab:
		*ncomp = 3;
		return TYPE_Lab_DBL;
	case kCGColorSpaceModelXYZ:
		/* THE ENGINE HAS A DOUBLE FORMAT FOR XYZ, which is why this is one arm rather than a
		 * conversion of its own — and why the first run of the probe's XYZ check failed: this
		 * table had no XYZ row, so the conversion refused and the check dereferenced NULL. */
		*ncomp = 3;
		return TYPE_XYZ_DBL;
	default:
		*ncomp = 0;
		return 0;
	}
}

/* THE PROFILE TO CONVERT WITH — and for a DEVICE space there is none to ask for, because being
 * a device space is precisely not having one. Converting INTO one therefore has to NAME what
 * Apple's device spaces mean: device RGB is treated as sRGB, and device gray as gamma-2.2 gray
 * against D50, which is what Apple's own colour management does with them and the assumption
 * this library has drawn under since C2. `*own` says whether the caller has to close the
 * result, because these two are built here and do not belong to the space. */
static cmsHPROFILE cg_engine_profile_for(CGColorSpaceRef space, int *own)
{
	cmsHPROFILE p = (cmsHPROFILE)cg_colorspace_engine_profile(space);
	cmsToneCurve *gamma;

	*own = 0;
	if (p != NULL) {
		return p;
	}
	switch (CGColorSpaceGetModel(space)) {
	case kCGColorSpaceModelRGB:
		*own = 1;
		return cmsCreate_sRGBProfile();
	case kCGColorSpaceModelMonochrome:
		*own = 1;
		gamma = cmsBuildGamma(NULL, 2.2);
		if (gamma == NULL) {
			return NULL;
		}
		/* `cmsD50_xyY` IS A FUNCTION AND NOT AN OBJECT, which is the one detail of the engine's
		 * API a compiler had to point out: the identifier alone names the function, so passing
		 * it without the call puts a pointer-to-function where a white point belongs. */
		p = cmsCreateGrayProfile(cmsD50_xyY(), gamma);
		cmsFreeToneCurve(gamma);
		return p;
	default:
		/* DEVICE CMYK, TODAY. There is no profile to invent either: what a set of ink values
		 * means depends on the press, so a made-up conversion would be a made-up press. */
		return NULL;
	}
}

CGColorRef CGColorCreateCopyByMatchingToColorSpace(CGColorRef color, CGColorRenderingIntent intent,
						   CGColorSpaceRef space, void *options)
{
	cmsHPROFILE src;
	cmsHPROFILE dst;
	cmsHTRANSFORM tr;
	CGFloat in[CG_COLOR_MAX_COMPONENTS];
	CGFloat out[CG_COLOR_MAX_COMPONENTS];
	CGColorRef result;
	const CGFloat *comp;
	int src_own;
	int dst_own;
	int src_ncomp;
	int dst_ncomp;
	int src_fmt;
	int dst_fmt;
	int i;

	if (color == NULL) {
		return NULL;
	}
	if (options != NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorCreateCopyByMatchingToColorSpace has no options "
				"yet, and dropping one a caller asked for would change the output\n");
		return NULL;
	}
	src_fmt = cg_engine_format(CGColorSpaceGetModel(color->space), &src_ncomp);
	dst_fmt = cg_engine_format(CGColorSpaceGetModel(space), &dst_ncomp);
	if (src_fmt == 0 || dst_fmt == 0) {
		fprintf(stderr, "CG-REFUSE: no conversion from or to this color space's model\n");
		return NULL;
	}
	src = cg_engine_profile_for(color->space, &src_own);
	dst = cg_engine_profile_for(space, &dst_own);
	if (src == NULL || dst == NULL) {
		if (src_own && src != NULL) {
			cmsCloseProfile(src);
		}
		if (dst_own && dst != NULL) {
			cmsCloseProfile(dst);
		}
		fprintf(stderr, "CG-REFUSE: this color space has no profile to convert through — a "
				"device CMYK colour is the case that exists today\n");
		return NULL;
	}
	tr = cmsCreateTransform(src, (cmsUInt32Number)src_fmt, dst, (cmsUInt32Number)dst_fmt,
				cg_engine_intent(intent), 0);
	if (src_own) {
		cmsCloseProfile(src);
	}
	if (dst_own) {
		cmsCloseProfile(dst);
	}
	if (tr == NULL) {
		fprintf(stderr, "CG-REFUSE: the color engine could not build this conversion\n");
		return NULL;
	}
	comp = CGColorGetComponents(color);
	for (i = 0; i < src_ncomp; i++) {
		in[i] = comp[i];
	}
	cmsDoTransform(tr, in, out, 1);
	cmsDeleteTransform(tr);
	/* THE ALPHA IS NOT CONVERTED — it is not a colour, and the engine has no opinion about
	 * it — so it is carried across and placed last, where the component array keeps it. */
	out[dst_ncomp] = comp[src_ncomp];
	result = CGColorCreate(space, out);
	return result;
}
