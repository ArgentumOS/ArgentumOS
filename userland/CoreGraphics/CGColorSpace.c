/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGColorSpace.c — the two device colour spaces, and nothing else (see the header).
 *
 * A SINGLETON, NOT A FRESH OBJECT, and that is not an optimization: the device spaces
 * carry no state, so two objects would be two answers to "is this the device RGB
 * space?" — a question `CGColorSpaceGetModel` cannot answer between two instances and
 * a caller has every right to compare. `CGColorSpaceRetain`/`Release` move a
 * refcount; `Release` on a device space with no other holder leaves it alive, which
 * is Apple's documented behaviour for these (they are process-wide).
 */
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGColorSpace_internal.h>
#include <CoreGraphics/CGDataProvider_internal.h>

#include <lcms2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CGColorSpace {
	int refcount;
	CGColorSpaceModel model;
	size_t components;
	/* THE ENGINE'S HANDLE, AND IT IS ALSO THE KIND. A device RGB space and a calibrated or
	 * ICC RGB space share `kCGColorSpaceModelRGB`; what separates them is whether the numbers
	 * can be blended as they stand (a device space, no profile) or have to be interpreted
	 * through a profile first. C4.1's guard dispatched on the MODEL alone, which was safe only
	 * because device RGB was the only RGB space that existed — the gap recorded against it.
	 * ONE FIELD FIXES IT: a profile means "convert", no profile means "the numbers are the
	 * numbers". It doubles as the cache, because parsing a profile is expensive and a space is
	 * exactly the right lifetime to hold one. */
	cmsHPROFILE profile;
	/* THE INDEXED FAMILY: the colour table as BYTES (owned, a copy of the caller's) and the base space its
	 * values are read in (retained). `color_table_count` IS ENTRIES, not bytes — Apple's `lastIndex + 1`
	 * — and the byte length is that times the base's component count. The base is also what a PATTERN
	 * space answers with, which is why `CGColorSpaceGetBaseColorSpace` reads this field for both. */
	unsigned char *color_table;
	size_t color_table_count;
	CGColorSpaceRef base;
};

static struct CGColorSpace cg_device_rgb = { 0, kCGColorSpaceModelRGB, 3, NULL, NULL, 0, NULL };
static struct CGColorSpace cg_device_gray = { 0, kCGColorSpaceModelMonochrome, 1, NULL, NULL, 0, NULL };
/* FOUR COMPONENTS, ALPHA NOT COUNTED, like the other two: the count belongs to the SPACE, and
 * a colour adds its alpha on top of it. NO PROFILE, because there is no device-CMYK profile to
 * give it — which is what keeps a CMYK colour un-drawable while a Lab one becomes drawable
 * below. */
static struct CGColorSpace cg_device_cmyk = { 0, kCGColorSpaceModelCMYK, 4, NULL, NULL, 0, NULL };

CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void)
{
	cg_device_rgb.refcount++;
	return &cg_device_rgb;
}

CGColorSpaceRef CGColorSpaceCreateDeviceGray(void)
{
	cg_device_gray.refcount++;
	return &cg_device_gray;
}

CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void)
{
	cg_device_cmyk.refcount++;
	return &cg_device_cmyk;
}

/* APPLE'S Lab WHITE POINT IS AN XYZ TRIPLE AND lcms2's IS AN xyY TRIPLE, so this is the one
 * piece of arithmetic between the two APIs. A NULL white point means D50 — which is both
 * Apple's documented default and exactly what `cmsCreateLab4Profile(NULL)` builds, so the
 * arithmetic is skipped rather than approximated. */
static int cg_xyz_to_xyy(const CGFloat *xyz, cmsCIExyY *out)
{
	double sum = (double)xyz[0] + (double)xyz[1] + (double)xyz[2];

	if (sum <= 0.0) {
		return 0;
	}
	out->x = (double)xyz[0] / sum;
	out->y = (double)xyz[1] / sum;
	out->Y = (double)xyz[1];
	return 1;
}

CGColorSpaceRef CGColorSpaceCreateLab(const CGFloat *whitePoint, const CGFloat *blackPoint,
				      const CGFloat *range)
{
	CGColorSpaceRef space;
	cmsCIExyY white;

	/* THE BLACK POINT AND THE RANGE HAVE NO COUNTERPART IN AN ICC Lab PROFILE. Lab4 is defined
	 * by its white point alone, and its a/b range is fixed at ±128. Keeping the parameters in
	 * the signature without inventing an effect for them is the honest choice: Apple's own
	 * documentation says the black point is ignored for a Lab space and the range is honored
	 * only by the colours drawn in it. */
	(void)blackPoint;
	(void)range;

	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		return NULL;
	}
	space->refcount = 1;
	space->model = kCGColorSpaceModelLab;
	space->components = 3;
	if (whitePoint == NULL) {
		space->profile = cmsCreateLab4Profile(NULL);   /* D50 */
	} else if (cg_xyz_to_xyy(whitePoint, &white)) {
		space->profile = cmsCreateLab4Profile(&white);
	}
	if (space->profile == NULL) {
		free(space);
		return NULL;
	}
	return space;
}

/* ---------------------------------------------------------------------------------------
 * THE CALIBRATED SPACES, WHICH IS WHERE APPLE'S PARAMETERS AND THE ENGINE'S DIFFER.
 *
 * Apple describes a calibrated RGB space with a WHITE POINT, THREE GAMMAS AND A MATRIX that
 * takes RGB to XYZ; the engine wants a white point, three PRIMARIES in xy, and the same three
 * gammas. THE MATRIX AND THE PRIMARIES ARE ONE FACT IN TWO SPELLINGS — a column of an
 * RGB-to-XYZ matrix IS that primary's XYZ — so the step between them is arithmetic and not an
 * approximation, which is why the probe can demand that this space, built from sRGB's own
 * matrix, convert to device RGB as the identity.
 * ------------------------------------------------------------------------------------- */

/* THIS LIBRARY'S DEVICE RGB IS sRGB — its conversion path says so, and its probe pins it — so
 * "the default RGB primaries" can only mean these. A NULL matrix means exactly this space
 * described the long way round, which is a coherent thing for a caller to ask for; defaulting
 * to anything else would make the same words mean two different things in two places. */
static const CGFloat cg_default_rgb_matrix[9] = {
	0.4124564, 0.3575761, 0.1804375,
	0.2126729, 0.7151522, 0.0721750,
	0.0193339, 0.1191920, 0.9503041
};

/* THE THREE PRIMARIES, RECOVERED FROM AN RGB-TO-XYZ MATRIX: `X = m[0]*R + m[1]*G + m[2]*B`, so
 * channel c's contribution to X is matrix[c], to Y is matrix[3 + c] and to Z is matrix[6 + c].
 * A column whose XYZ sums to nothing is a CHANNEL WITH NO PRIMARY, which no colour space has,
 * so it is refused rather than approximated. The engine's primaries are normalised to Y = 1. */
static int cg_primaries_from_matrix(const CGFloat *matrix, cmsCIExyYTRIPLE *out)
{
	cmsCIExyY *dst[3];
	CGFloat xyz[3];
	int c;

	dst[0] = &out->Red;
	dst[1] = &out->Green;
	dst[2] = &out->Blue;
	for (c = 0; c < 3; c++) {
		xyz[0] = matrix[c];
		xyz[1] = matrix[3 + c];
		xyz[2] = matrix[6 + c];
		if (!cg_xyz_to_xyy(xyz, dst[c])) {
			return 0;
		}
		dst[c]->Y = 1.0;
	}
	return 1;
}

CGColorSpaceRef CGColorSpaceCreateCalibratedGray(const CGFloat *whitePoint,
						 const CGFloat *blackPoint, CGFloat gamma)
{
	CGColorSpaceRef space;
	cmsCIExyY white;
	cmsToneCurve *curve;

	/* THE BLACK POINT HAS NO EFFECT, and that is Apple's description of it as much as this
	 * library's limitation: Apple documents it as mattering only when the ABSOLUTE COLORIMETRIC
	 * intent is used, and an ICC matrix/TRC profile has no field for one. */
	(void)blackPoint;
	if (whitePoint == NULL || !cg_xyz_to_xyy(whitePoint, &white) || gamma <= 0.0) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateCalibratedGray needs a white point and "
				"a positive gamma\n");
		return NULL;
	}
	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		return NULL;
	}
	curve = cmsBuildGamma(NULL, (double)gamma);
	if (curve == NULL) {
		free(space);
		return NULL;
	}
	space->refcount = 1;
	space->model = kCGColorSpaceModelMonochrome;
	space->components = 1;
	space->profile = cmsCreateGrayProfile(&white, curve);
	cmsFreeToneCurve(curve);
	if (space->profile == NULL) {
		free(space);
		return NULL;
	}
	return space;
}

CGColorSpaceRef CGColorSpaceCreateCalibratedRGB(const CGFloat *whitePoint,
						const CGFloat *blackPoint,
						const CGFloat gamma[3], const CGFloat matrix[9])
{
	static const CGFloat d65[3] = { 0.95047, 1.0, 1.08883 };
	CGColorSpaceRef space;
	cmsCIExyY white;
	cmsCIExyYTRIPLE primaries;
	cmsToneCurve *curves[3];
	const CGFloat *m = (matrix != NULL) ? matrix : cg_default_rgb_matrix;
	const CGFloat *wp = (whitePoint != NULL) ? whitePoint : d65;
	int i;

	(void)blackPoint;
	/* D65 FOR A NULL WHITE POINT, because the default matrix above is the sRGB one and a matrix
	 * without its white point is half a space. */
	if (!cg_xyz_to_xyy(wp, &white) || !cg_primaries_from_matrix(m, &primaries)) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateCalibratedRGB needs a white point and a "
				"matrix whose columns are the primaries\n");
		return NULL;
	}
	if (gamma == NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateCalibratedRGB needs three gammas\n");
		return NULL;
	}
	for (i = 0; i < 3; i++) {
		curves[i] = NULL;
		if (gamma[i] <= 0.0) {
			fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateCalibratedRGB needs a positive "
					"gamma for every channel\n");
			for (i = 0; i < 3; i++) {
				if (curves[i] != NULL) {
					cmsFreeToneCurve(curves[i]);
				}
			}
			return NULL;
		}
		curves[i] = cmsBuildGamma(NULL, (double)gamma[i]);
		if (curves[i] == NULL) {
			for (i = 0; i < 3; i++) {
				if (curves[i] != NULL) {
					cmsFreeToneCurve(curves[i]);
				}
			}
			return NULL;
		}
	}
	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		for (i = 0; i < 3; i++) {
			cmsFreeToneCurve(curves[i]);
		}
		return NULL;
	}
	space->refcount = 1;
	space->model = kCGColorSpaceModelRGB;
	space->components = 3;
	space->profile = cmsCreateRGBProfile(&white, &primaries, curves);
	for (i = 0; i < 3; i++) {
		/* THE CURVES ARE THE PROFILE'S NOW — the engine copies what it needs — so they are
		 * freed here rather than kept, which is the same rule the gray case follows. */
		cmsFreeToneCurve(curves[i]);
	}
	if (space->profile == NULL) {
		free(space);
		return NULL;
	}
	return space;
}

/* THE PROFILE'S OWN COLOUR SPACE, MAPPED TO THIS LIBRARY'S MODEL — and where there is no model
 * the answer is a REFUSAL, not a guess. The engine's signatures are the ICC standard's
 * four-byte codes (`cmsSigRgbData` and friends), which is why this is a switch over them
 * rather than a comparison of names. */
static int cg_model_from_profile(cmsHPROFILE p, CGColorSpaceModel *model, size_t *components)
{
	switch (cmsGetColorSpace(p)) {
	case cmsSigGrayData:
		*model = kCGColorSpaceModelMonochrome;
		*components = 1;
		return 1;
	case cmsSigRgbData:
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return 1;
	case cmsSigCmykData:
		*model = kCGColorSpaceModelCMYK;
		*components = 4;
		return 1;
	case cmsSigLabData:
		*model = kCGColorSpaceModelLab;
		*components = 3;
		return 1;
	case cmsSigXYZData:
		/* !! AN XYZ PROFILE IS REFUSED NOW (2026-10-05), WHERE IT USED TO BE READ AS ITS OWN MODEL.
		 * `kCGColorSpaceModelXYZ` is macOS 10.8 API — the 10.6 headers have no such case — and the
		 * model is what a colour's NUMBERS mean, so there is nothing honest to answer for X, Y and
		 * Z: calling them a device model would read them as light values. The refusal has the same
		 * shape every other unknown-model path in this file already has. */
		return 0;
	default:
		return 0;
	}
}

CGColorSpaceRef CGColorSpaceCreateICCBased(size_t nComponents, const CGFloat *range,
					   CGDataProviderRef profile, CGColorSpaceRef alternate)
{
	CGColorSpaceRef space;
	CGColorSpaceModel model;
	const void *bytes;
	size_t size;
	size_t components;
	cmsHPROFILE p;

	/* THE RANGE IS ACCEPTED AND HAS NO EFFECT, which is worth stating rather than hiding: it
	 * describes the SAMPLE range of a profile's numbers, and this library's components are
	 * already 0..1 doubles, so there is nothing here for it to change. */
	(void)range;
	if (profile == NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateICCBased needs a profile\n");
		return NULL;
	}
	if (alternate != NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateICCBased has no use for an alternate "
				"space yet, and dropping one would change what was asked for\n");
		return NULL;
	}
	bytes = cg_dataprovider_bytes(profile, &size);
	if (bytes == NULL || size == 0) {
		fprintf(stderr, "CG-REFUSE: this data provider holds no profile bytes\n");
		return NULL;
	}
	p = cmsOpenProfileFromMem(bytes, (cmsUInt32Number)size);
	if (p == NULL) {
		fprintf(stderr, "CG-REFUSE: these bytes are not an ICC profile the engine can read\n");
		return NULL;
	}
	if (!cg_model_from_profile(p, &model, &components)) {
		fprintf(stderr, "CG-REFUSE: this profile's color space has no model in this library "
				"(RGB, grayscale, CMYK and Lab are the four)\n");
		cmsCloseProfile(p);
		return NULL;
	}
	if (nComponents != 0 && nComponents != components) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateICCBased was given %d components and "
				"the profile has %lu\n", (int)nComponents, (unsigned long)components);
		cmsCloseProfile(p);
		return NULL;
	}
	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		cmsCloseProfile(p);
		return NULL;
	}
	space->refcount = 1;
	space->model = model;
	space->components = components;
	/* THE SPACE KEEPS THE PARSED PROFILE AND NOT THE CALLER'S BYTES, so the provider can be
	 * released the moment this returns — which is what the probe checks. */
	space->profile = p;
	return space;
}

void *cg_colorspace_profile_handle(CGColorSpaceRef space)
{
	return space == NULL ? NULL : (void *)space->profile;
}

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space)
{
	if (space != NULL) {
		space->refcount++;
	}
	return space;
}

static int cg_space_is_static(CGColorSpaceRef space);

void CGColorSpaceRelease(CGColorSpaceRef space)
{
	CGColorSpaceRef base;
	if (space == NULL) {
		return;
	}
	if (space->refcount > 0) {
		space->refcount--;
	}
	if (space->refcount > 0) {
		return;
	}
	/* FREED UNLESS IT IS ONE OF THE PROCESS-WIDE SINGLETONS, and THE RULE USED TO BE "FREED IF IT HAS A
	 * PROFILE": that was right for every space this library had, because the singletons have no profile and
	 * every heap space had one. AN INDEXED SPACE IS THE FIRST HEAP SPACE WITH NO PROFILE, and the old rule
	 * would have leaked it — the same shape as the C4.1 guard the struct's own comment records, which was
	 * safe only because device RGB was the only RGB space that existed. THE SINGLETONS ARE NAMED, and
	 * `cg_space_is_static` is defined at the END of this file so that it can see all of them. */
	if (!cg_space_is_static(space)) {
		if (space->profile != NULL) {
			cmsCloseProfile(space->profile);
		}
		free(space->color_table);
		base = space->base;
		space->base = NULL;
		free(space);
		CGColorSpaceRelease(base);
	}
	/* NOT FREED, EVEN AT ZERO: these are the process-wide device spaces, and
	 * `CGColorSpaceCreateDeviceRGB()` may be called again at any time. A caller that
	 * balanced its retain/release has done what the contract asks; the object simply
	 * is not destroyed, which is what "no parameters, one answer" costs. */
}

void *cg_colorspace_engine_profile(CGColorSpaceRef space)
{
	if (space == NULL) {
		return NULL;
	}
	return space->profile;
}

CGColorSpaceRef cg_colorspace_from_profile(void *profile, CGColorSpaceModel model,
					   size_t components)
{
	CGColorSpaceRef space;

	if (profile == NULL) {
		return NULL;
	}
	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		return NULL;
	}
	space->refcount = 1;
	space->model = model;
	space->components = components;
	space->profile = (cmsHPROFILE)profile;
	/* NOTHING IS CLOSED HERE, AND THAT IS THE SEAM'S CONTRACT: on success this space owns the
	 * profile and its release closes it, and on failure the CALLER still owns it. Both sides of
	 * the seam state the same rule, because getting it wrong is a leak (a profile closed twice)
	 * or a crash (a profile closed never). */
	return space;
}

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef space)
{
	if (space == NULL) {
		return kCGColorSpaceModelUnknown;
	}
	return space->model;
}

size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef space)
{
	if (space == NULL) {
		return 0;
	}
	return space->components;
}

/* !! THE PREDICATES STOOD HERE AND WERE REMOVED (2026-10-05), WITH THE FOUR HELPERS THEY NEEDED:
 * `CGColorSpaceSupportsOutput`, `CGColorSpaceIsWideGamutRGB` and `CGColorSpaceUsesExtendedRange`
 * (10.12), `CGColorSpaceIsHDR` (10.15), `CGColorSpaceIsHLGBased` and `CGColorSpaceIsPQBased`
 * (12.0), plus `cg_edge_sign`, `cg_inside_or_on`, `cg_srgb_reference` and `cg_space_primaries`,
 * which existed only to answer `IsWideGamutRGB`'s question about primaries.
 *
 * WHAT IS LOST WITH THEM, STATED SO IT IS NOT DISCOVERED LATER: `SupportsOutput`'s answer — a
 * space can be drawn when it has a profile — was the same fact the context's colour setters
 * refuse from, and that refusal lives on in `cg_paint_device_rgb`; the gamut comparison is gone
 * with no replacement, and nothing in the drawing path ever asked it. */

/* THE PATTERN SPACE IS A SINGLETON LIKE THE DEVICE SPACES, and for the same reason they are: it has
 * no profile, so `CGColorSpaceRelease` does not destroy it, and a caller who asks twice gets the same
 * object back — which is what lets a colour's space be compared by pointer as well as by model. See
 * CGColorSpace.h for why it has one component and why the uncoloured form is refused. */
static struct CGColorSpace cg_pattern_space = { 0, kCGColorSpaceModelPattern, 1, NULL, NULL, 0, NULL };

/* ------------------------------------------------------------------------- */
/* the indexed family                                                          */
/* ------------------------------------------------------------------------- */

CGColorSpaceRef CGColorSpaceCreateIndexed(CGColorSpaceRef baseSpace, size_t lastIndex,
					  const uint8_t *colorTable)
{
	CGColorSpaceRef space;
	size_t bytes;

	if (baseSpace == NULL || colorTable == NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateIndexed needs a base color space and a color "
				"table\n");
		return NULL;
	}
	if (lastIndex > 255) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateIndexed takes a maximum index of at most 255, "
				"and was given %lu\n", (unsigned long)lastIndex);
		return NULL;
	}
	if (CGColorSpaceGetModel(baseSpace) == kCGColorSpaceModelIndexed) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateIndexed needs a base space whose values can be "
				"READ, and an indexed space's values are indices into another table\n");
		return NULL;
	}
	/* THE TABLE'S LENGTH COMES FROM THE BASE'S COMPONENT COUNT, which is why a base this library cannot
	 * count is refused above rather than copied blindly. */
	bytes = (lastIndex + 1) * CGColorSpaceGetNumberOfComponents(baseSpace);
	if (bytes == 0) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateIndexed needs a base color space with at least "
				"one component\n");
		return NULL;
	}
	space = calloc(1, sizeof(struct CGColorSpace));
	if (space == NULL) {
		return NULL;
	}
	space->color_table = malloc(bytes);
	if (space->color_table == NULL) {
		free(space);
		return NULL;
	}
	memcpy(space->color_table, colorTable, bytes);
	space->color_table_count = lastIndex + 1;
	space->base = CGColorSpaceRetain(baseSpace);
	space->refcount = 1;
	space->model = kCGColorSpaceModelIndexed;
	/* ONE COMPONENT, BECAUSE ITS VALUE IS AN INDEX: the table's values are the base space's, and they are
	 * read THROUGH this space rather than being this space's components. */
	space->components = 1;
	return space;
}

CGColorSpaceRef CGColorSpaceGetBaseColorSpace(CGColorSpaceRef space)
{
	/* APPLE: "the base color space of `space` if `space` is a pattern or indexed color space; otherwise
	 * NULL". A pattern space's base is one; a device space's is nobody's. */
	if (space == NULL
	    || (space->model != kCGColorSpaceModelIndexed && space->model != kCGColorSpaceModelPattern)) {
		return NULL;
	}
	return space->base;
}

size_t CGColorSpaceGetColorTableCount(CGColorSpaceRef space)
{
	/* ENTRIES, NOT BYTES — Apple's `lastIndex + 1` — and 0 for anything that is not indexed. */
	if (space == NULL || space->model != kCGColorSpaceModelIndexed) {
		return 0;
	}
	return space->color_table_count;
}

void CGColorSpaceGetColorTable(CGColorSpaceRef space, uint8_t *table)
{
	/* "COPY THE ENTRIES ... IF `space` IS AN INDEXED COLOR SPACE; OTHERWISE, DO NOTHING" — so a caller who
	 * asks a device space for a table gets their buffer back untouched rather than a partial write. */
	size_t bytes;

	if (space == NULL || table == NULL || space->model != kCGColorSpaceModelIndexed) {
		return;
	}
	bytes = space->color_table_count * CGColorSpaceGetNumberOfComponents(space->base);
	memcpy(table, space->color_table, bytes);
}

CGColorSpaceRef CGColorSpaceCreatePattern(CGColorSpaceRef baseSpace)
{
	if (baseSpace != NULL) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreatePattern does not implement an UNCOLOURED "
				"pattern space — its cell would be drawn in a colour from the base "
				"space, and this library draws coloured patterns only\n");
		return NULL;
	}
	cg_pattern_space.refcount++;
	return &cg_pattern_space;
}

/* ------------------------------------------------------------------------- */
/* which spaces are the process-wide singletons                                */
/* ------------------------------------------------------------------------- */

/* THE FOUR SPACES THAT ARE NEVER DESTROYED, named here so that `CGColorSpaceRelease` can tell "a space this
 * library handed out again and again" from "a space a caller asked for by parameters". A SPACE MADE BY
 * PARAMETERS OWNS SOMETHING THE ENGINE PARSED OR A TABLE THIS LIBRARY COPIED, and both have to be let go;
 * a singleton owns nothing that is not already the process's. THE LIST IS SPELT OUT rather than inferred
 * from "has no profile", because the inference is what an indexed space broke. */
static int cg_space_is_static(CGColorSpaceRef space)
{
	return space == &cg_device_rgb || space == &cg_device_gray || space == &cg_device_cmyk
	       || space == &cg_pattern_space;
}

