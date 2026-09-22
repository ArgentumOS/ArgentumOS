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
};

static struct CGColorSpace cg_device_rgb = { 0, kCGColorSpaceModelRGB, 3, NULL };
static struct CGColorSpace cg_device_gray = { 0, kCGColorSpaceModelMonochrome, 1, NULL };
/* FOUR COMPONENTS, ALPHA NOT COUNTED, like the other two: the count belongs to the SPACE, and
 * a colour adds its alpha on top of it. NO PROFILE, because there is no device-CMYK profile to
 * give it — which is what keeps a CMYK colour un-drawable while a Lab one becomes drawable
 * below. */
static struct CGColorSpace cg_device_cmyk = { 0, kCGColorSpaceModelCMYK, 4, NULL };

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
		/* AN XYZ PROFILE IS A COLOUR SPACE LIKE ANY OTHER HERE, and it is worth stating why the
		 * MODEL matters: `CGColorSpaceIsWideGamutRGB` asks about PRIMARIES, and an XYZ space has
		 * none — it answers NO for that reason, not because something failed to read it. */
		*model = kCGColorSpaceModelXYZ;
		*components = 3;
		return 1;
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

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space)
{
	if (space != NULL) {
		space->refcount++;
	}
	return space;
}

void CGColorSpaceRelease(CGColorSpaceRef space)
{
	if (space == NULL) {
		return;
	}
	if (space->refcount > 0) {
		space->refcount--;
	}
	if (space->refcount > 0) {
		return;
	}
	/* A SPACE WITH A PROFILE IS NOT A SINGLETON, so this one IS destroyed — the device spaces
	 * are the ones that are not, and the difference is the profile: `CGColorSpaceCreateDeviceRGB()`
	 * may be called again at any time and must get the same object back, while a Lab space was
	 * asked for by parameters and owns an engine handle that has to be closed. */
	if (space->profile != NULL) {
		cmsCloseProfile(space->profile);
		free(space);
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

/* ---------------------------------------------------------------------------------------
 * THE PREDICATES, AND HOW THEY ARE ANSWERED.
 *
 * TWO OF THEM ARE COMPUTED FROM FACTS THIS LIBRARY ALREADY ACTS ON — whether a space can be
 * drawn, and whether its gamut reaches beyond sRGB's — and four are facts about which spaces
 * exist here. None of them is a stand-in for an answer: the four say NO because no such space
 * can be built yet, and each says so where a reader will find it.
 * ------------------------------------------------------------------------------------- */

/* TWICE A TRIANGLE'S SIGNED AREA FOR ONE EDGE, which is all it takes to say where a point sits.
 * `sRGB's primaries are given as a flat x0,y0,x1,y1,x2,y2 and the winding is the one the
 * standard values have. */
static double cg_edge_sign(double ax, double ay, double bx, double by, double px, double py)
{
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* INSIDE THE TRIANGLE OR ON ITS EDGE, WITH A TOLERANCE AT THE EDGE — AND THE TOLERANCE IS THE
 * WHOLE POINT OF THIS FUNCTION, because "on the edge" is not something a profile can express
 * exactly. An ICC profile keeps its colourants in 16.16 FIXED POINT, so a space whose primaries
 * ARE sRGB's comes back about 1.5e-5 away from them, and the sign of a cross product that shares
 * an edge then flips by a few units of an area. Asking this question with no tolerance answered
 * WIDE GAMUT for sRGB ITSELF — measured, twice, against two different profile-backed sRGB spaces
 * — which is a wrong answer about a space this library defines.
 *
 * SO A SIGN BELOW THE ROUNDING'S OWN SCALE COUNTS AS ZERO, and the scale is small enough to be
 * harmless: Adobe RGB's green sits 0.08 outside sRGB's triangle, which is four thousand times
 * this. Below it, a point is on the edge; at or above it, the sign means what it says. */
static int cg_inside_or_on(double px, double py, const double *tri)
{
	const double epsilon = 1e-5;
	double s0 = cg_edge_sign(tri[0], tri[1], tri[2], tri[3], px, py);
	double s1 = cg_edge_sign(tri[2], tri[3], tri[4], tri[5], px, py);
	double s2 = cg_edge_sign(tri[4], tri[5], tri[0], tri[1], px, py);
	int negative = (s0 < -epsilon) + (s1 < -epsilon) + (s2 < -epsilon);

	return negative == 0 || negative == 3;
}

/* THE PRIMARIES OF THIS LIBRARY'S sRGB, READ BACK FROM A PROFILE — AND THAT IS THE ONLY HONEST
 * REFERENCE, FOR A REASON THAT WAS MEASURED RATHER THAN REASONED. An ICC profile keeps its
 * colourants in the PCS, which is D50, so `cmsCreate_sRGBProfile()`'s own primaries read back as
 * (0.64844, 0.33086) instead of sRGB's published (0.6400, 0.3300): the numbers are D50-ADAPTED.
 * Comparing those against the published D65 values asked a boundary question in two white points
 * at once, and answered WIDE GAMUT for sRGB ITSELF. Reading BOTH SIDES the same way removes the
 * question instead of shrinking it — an earlier version of this code put a tolerance here, and
 * the measurement showed the real difference is five hundred times any rounding. */
static int cg_srgb_reference(double ref[6])
{
	static const cmsTagSignature tags[3] = {
		cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag
	};
	cmsHPROFILE p = cmsCreate_sRGBProfile();
	cmsCIEXYZ *xyz;
	double sum;
	int i;

	if (p == NULL) {
		return 0;
	}
	for (i = 0; i < 3; i++) {
		xyz = (cmsCIEXYZ *)cmsReadTag(p, tags[i]);
		if (xyz == NULL) {
			cmsCloseProfile(p);
			return 0;
		}
		sum = xyz->X + xyz->Y + xyz->Z;
		if (sum <= 0.0) {
			cmsCloseProfile(p);
			return 0;
		}
		ref[i * 2] = xyz->X / sum;
		ref[i * 2 + 1] = xyz->Y / sum;
	}
	cmsCloseProfile(p);
	return 1;
}

/* A SPACE'S PRIMARIES IN xy, OR 0 IF THEY CANNOT BE READ. Device RGB goes THROUGH THE ENGINE
 * rather than reporting sRGB's published values, so that a device space and a profile-backed one
 * answer in the SAME white point — the property the comparison in `IsWideGamutRGB` depends on.
 * A profile that is NOT a matrix shaper answers 0, and that refusal is the point of the
 * function: a gamut nobody can read is a gamut nobody can compare, and answering "not wide" for
 * a profile nobody looked at would be a guess dressed as a fact. */
static int cg_space_primaries(CGColorSpaceRef space, double prim[6])
{
	static const cmsTagSignature tags[3] = {
		cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag
	};
	cmsHPROFILE p;
	cmsCIEXYZ *xyz;
	double sum;
	int i;

	if (space == NULL || CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB) {
		return 0;
	}
	p = (cmsHPROFILE)cg_colorspace_engine_profile(space);
	if (p == NULL) {
		return cg_srgb_reference(prim);
	}
	for (i = 0; i < 3; i++) {
		xyz = (cmsCIEXYZ *)cmsReadTag(p, tags[i]);
		if (xyz == NULL) {
			return 0;
		}
		sum = xyz->X + xyz->Y + xyz->Z;
		if (sum <= 0.0) {
			return 0;
		}
		prim[i * 2] = xyz->X / sum;
		prim[i * 2 + 1] = xyz->Y / sum;
	}
	return 1;
}

bool CGColorSpaceSupportsOutput(CGColorSpaceRef space)
{
	CGColorSpaceModel model;

	if (space == NULL) {
		return 0;
	}
	model = CGColorSpaceGetModel(space);
	if (model == kCGColorSpaceModelRGB || model == kCGColorSpaceModelMonochrome) {
		return 1;   /* a device space: the rasterizer blends its numbers as they stand */
	}
	/* ANYTHING ELSE HAS TO HAVE A PROFILE, because drawing it means CONVERTING it and converting
	 * it means the engine having something to convert through. Device CMYK is the case that
	 * exists today and answers NO — the same answer, from the same fact, that the context's
	 * colour setters give it. */
	return cg_colorspace_engine_profile(space) != NULL;
}

bool CGColorSpaceIsWideGamutRGB(CGColorSpaceRef space)
{
	double srgb[6];
	double prim[6];
	int i;

	/* THE REFERENCE IS READ THE SAME WAY THE SPACE IS — through a profile, and therefore in the
	 * PCS — which is what makes this a comparison in ONE white point instead of a boundary
	 * question asked in two. And if the reference cannot be read at all, the answer is NO: a
	 * comparison with no reference is not a smaller answer, it is no answer. */
	if (!cg_srgb_reference(srgb) || !cg_space_primaries(space, prim)) {
		return 0;
	}
	for (i = 0; i < 3; i++) {
		if (!cg_inside_or_on(prim[i * 2], prim[i * 2 + 1], srgb)) {
			/* ONE PRIMARY OUTSIDE sRGB'S TRIANGLE IS ENOUGH: "wide gamut" means the space
			 * includes colours sRGB does not, and a primary is such a colour. */
			return 1;
		}
	}
	return 0;
}

bool CGColorSpaceUsesExtendedRange(CGColorSpaceRef space)
{
	(void)space;
	/* NO, FOR EVERY SPACE THIS LIBRARY CAN BUILD — and that is a fact rather than a placeholder.
	 * An extended-range space is one whose components may go OUTSIDE 0..1: an HDR or
	 * scene-referred profile, built on a transfer curve that is not bounded. Every space here is
	 * bounded and 0..1, so the answer is NO for all of them; the day one can be built, this is
	 * where it is answered. */
	return 0;
}

bool CGColorSpaceIsPQBased(CGColorSpaceRef space)
{
	(void)space;
	/* THE SAME ANSWER AND THE SAME REASON: a PQ-based space is one whose transfer curve is the
	 * perceptual quantizer, which is an HDR curve and therefore outside what this library builds
	 * today. See `CGColorSpaceUsesExtendedRange` above. */
	return 0;
}

bool CGColorSpaceIsHLGBased(CGColorSpaceRef space)
{
	(void)space;
	/* THE HYBRID LOG-GAMMA CURVE, the other HDR one, and again: no space here is built on it. */
	return 0;
}

bool CGColorSpaceIsHDR(CGColorSpaceRef space)
{
	(void)space;
	/* HDR IS THE UNION OF THE TWO: a space is HDR when it is PQ- or HLG-based, and this library
	 * builds neither, so it is NO for every space here. */
	return 0;
}
