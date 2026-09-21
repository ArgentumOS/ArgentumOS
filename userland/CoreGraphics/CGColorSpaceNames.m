/*
 * CGColorSpaceNames — the system-defined colour spaces, by name.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FIRST OBJECTIVE-C IN THIS LIBRARY, AND THE REASON IS THE SIGNATURE. Apple names its system
 * spaces with `CFStringRef` constants and reaches them through `CGColorSpaceCreateWithName`, so
 * duplicating that API means names — and this tree spells them `NSString *`, because the
 * retracted CoreFoundation plan settled that CoreGraphics here declares its signatures with
 * FOUNDATION types rather than inventing a CF layer.
 *
 * THE REST OF THE LIBRARY DOES NOT KNOW THIS FILE EXISTS. `CGColorSpace.h` declares the parameters
 * and the constants as `NSString *` with an opaque spelling under a C compiler, and this is the
 * only translation unit that IMPORTS Foundation and can therefore compare and message them. THE
 * SPLIT WAS MEASURED BEFORE IT WAS WRITTEN: a C translation unit passing an `NSString *` to an
 * Objective-C one, and an Objective-C one returning a class pointer to C, both compile, link and
 * run — that is what "the same type" means across the two languages.
 *
 * NO ARC HERE, WHICH IS THE LIBRARY'S POLICY AND NOT AN OVERSIGHT (user, 2026-09-19): ARC is for
 * everything that USES Foundation, and using it in the library itself is unimportant so long as
 * it works. Nothing below owns anything in any case — the constants are string literals and the
 * profiles are handed to the C half, which owns them from there.
 *
 * AND THE SIX NAMES ARE THE ONES THIS LIBRARY CAN BACK WITH AN EXACT PROFILE. Each is defined by
 * a white point, three primaries and a transfer function — every one of which the engine can be
 * given here — so none of them is a substitution for a definition this library cannot express:
 *
 *   kCGColorSpaceSRGB                  sRGB's own profile, from the engine
 *   kCGColorSpaceLinearSRGB            sRGB's primaries, D65, gamma 1: no transfer function at all
 *   kCGColorSpaceAdobeRGB1998          Adobe's primaries, D65, gamma 563/256 — the exact value the
 *                                      Adobe RGB (1998) specification names, not 2.2 rounded
 *   kCGColorSpaceROMMRGB               ProPhoto's primaries, D50, gamma 1.8, which is its definition
 *   kCGColorSpaceGenericLab            Lab under D50, the ICC default Apple's generic Lab uses
 *   kCGColorSpaceGenericGrayGamma2_2   D65 with gamma 2.2
 *
 * WHAT IS NOT HERE, AND EVERY ONE OF THEM FOR THE SAME KIND OF REASON: `kCGColorSpaceDisplayP3` is
 * sRGB's PIECEWISE CURVE on P3's primaries, and this library's space constructors take one power
 * per channel, AND THE PIECEWISE CURVE HAS SINCE ARRIVED — so Display P3 and its linear form are
 * here too: `cg_srgb_curve` builds the sRGB transfer function as the engine's parametric type 4,
 * with the parameters read out of lcms2's own source rather than remembered. That closes the item
 * this paragraph used to leave open. STILL ABSENT, each for its own reason: the `Extended…` family
 * and `kCGColorSpaceACESCGLinear` are outside 0..1 or built on HDR curves;
 * `kCGColorSpaceGenericCMYK` has no profile that can be invented (what ink values mean depends on
 * the press); `kCGColorSpaceGenericXYZ` needs an XYZ model this library does not read. Every one of
 * them stays `open` in the ledger, which is where an unimplemented name belongs.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGColorSpace_internal.h>

#include <lcms2.h>
#include <stdio.h>

NSString *const kCGColorSpaceSRGB = @"kCGColorSpaceSRGB";
NSString *const kCGColorSpaceLinearSRGB = @"kCGColorSpaceLinearSRGB";
NSString *const kCGColorSpaceAdobeRGB1998 = @"kCGColorSpaceAdobeRGB1998";
NSString *const kCGColorSpaceROMMRGB = @"kCGColorSpaceROMMRGB";
NSString *const kCGColorSpaceGenericLab = @"kCGColorSpaceGenericLab";
NSString *const kCGColorSpaceGenericGrayGamma2_2 = @"kCGColorSpaceGenericGrayGamma2_2";
NSString *const kCGColorSpaceDisplayP3 = @"kCGColorSpaceDisplayP3";
NSString *const kCGColorSpaceLinearDisplayP3 = @"kCGColorSpaceLinearDisplayP3";

/* An xy pair with Y = 1, which is the spelling the engine's primaries use. */
static cmsCIExyY cg_xy(double x, double y)
{
	cmsCIExyY p;

	p.x = x;
	p.y = y;
	p.Y = 1.0;
	return p;
}

/* THE WHITE POINTS THE DEFINITIONS NAME, and they are not interchangeable: sRGB, Adobe RGB and
 * Display P3 are D65 while ProPhoto and the ICC's Lab are D50. Getting that wrong is a colour
 * cast, not a rounding error. */
#define CG_D65_X 0.3127
#define CG_D65_Y 0.3290
#define CG_D50_X 0.3457
#define CG_D50_Y 0.3585

/* AN RGB SPACE FROM THREE PRIMARIES AND ONE GAMMA FOR ALL THREE CHANNELS. Every space below that
 * is not sRGB's own profile or a Lab space has this shape, so it is written once. */
static cmsHPROFILE cg_rgb_profile(double wx, double wy, double rx, double ry, double gx, double gy,
				  double bx, double by, double gamma)
{
	cmsCIExyYTRIPLE prim;
	cmsToneCurve *curve[3];
	cmsCIExyY wp = cg_xy(wx, wy);
	cmsHPROFILE p;
	int i;

	prim.Red = cg_xy(rx, ry);
	prim.Green = cg_xy(gx, gy);
	prim.Blue = cg_xy(bx, by);
	for (i = 0; i < 3; i++) {
		curve[i] = cmsBuildGamma(NULL, gamma);
		if (curve[i] == NULL) {
			for (i = 0; i < 3; i++) {
				if (curve[i] != NULL) {
					cmsFreeToneCurve(curve[i]);
				}
			}
			return NULL;
		}
	}
	p = cmsCreateRGBProfile(&wp, &prim, curve);
	for (i = 0; i < 3; i++) {
		cmsFreeToneCurve(curve[i]);
	}
	return p;
}

/* THE sRGB TRANSFER FUNCTION AS THE ENGINE'S PARAMETRIC TYPE 4, AND THE PARAMETERS CAME FROM
 * lcms2's OWN SOURCE RATHER THAN FROM MEMORY: `cmsgamma.c` evaluates type 4 as
 *     Y = (aR + b)^g   for R >= d,      Y = cR   otherwise
 * which is the sRGB curve with {g, a, b, c, d} = {2.4, 1/1.055, 0.055/1.055, 1/12.92, 0.04045}.
 * THE LINEAR TOE IS THE WHOLE POINT OF IT: without the toe the same numbers describe a different
 * space, and the difference is not subtle at the bottom — 0.02 decodes to 0.0015 with the toe and
 * to 0.0068 without it. That is the discriminator the probe uses to prove THIS curve is the one
 * that got in, rather than a power that happens to look close. */
static cmsToneCurve *cg_srgb_curve(void)
{
	cmsFloat64Number params[5];

	params[0] = 2.4;
	params[1] = 1.0 / 1.055;
	params[2] = 0.055 / 1.055;
	params[3] = 1.0 / 12.92;
	params[4] = 0.04045;
	return cmsBuildParametricToneCurve(NULL, 4, params);
}

/* THE SAME RGB PROFILE WITH THE sRGB CURVE INSTEAD OF A POWER. The structure repeats
 * `cg_rgb_profile` because C has no closure to hand it — the gamma form builds three identical
 * curves from one number, and this asks for the piecewise curve three times — and WHAT DIFFERS IS
 * THE WHOLE REASON IT EXISTS: a space defined with a piecewise transfer function cannot be written
 * as a power at all, and Display P3 is such a space. */
static cmsHPROFILE cg_rgb_profile_srgb_curve(double wx, double wy, double rx, double ry, double gx,
					     double gy, double bx, double by)
{
	cmsCIExyYTRIPLE prim;
	cmsToneCurve *curve[3];
	cmsCIExyY wp = cg_xy(wx, wy);
	cmsHPROFILE p;
	int i;

	prim.Red = cg_xy(rx, ry);
	prim.Green = cg_xy(gx, gy);
	prim.Blue = cg_xy(bx, by);
	for (i = 0; i < 3; i++) {
		curve[i] = cg_srgb_curve();
		if (curve[i] == NULL) {
			for (i = 0; i < 3; i++) {
				if (curve[i] != NULL) {
					cmsFreeToneCurve(curve[i]);
				}
			}
			return NULL;
		}
	}
	p = cmsCreateRGBProfile(&wp, &prim, curve);
	for (i = 0; i < 3; i++) {
		cmsFreeToneCurve(curve[i]);
	}
	return p;
}

static cmsHPROFILE cg_profile_for_name(NSString *name, CGColorSpaceModel *model, size_t *components)
{
	cmsToneCurve *g;
	cmsCIExyY wp;
	cmsHPROFILE p;
	if ([name isEqual:kCGColorSpaceSRGB]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return cmsCreate_sRGBProfile();
	}
	if ([name isEqual:kCGColorSpaceLinearSRGB]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		/* GAMMA 1 IS "NO TRANSFER FUNCTION", which is what a linear space means. */
		return cg_rgb_profile(CG_D65_X, CG_D65_Y, 0.6400, 0.3300, 0.3000, 0.6000, 0.1500, 0.0600,
				      1.0);
	}
	if ([name isEqual:kCGColorSpaceDisplayP3]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		/* DISPLAY P3 IS DCI-P3'S PRIMARIES WITH THE sRGB TRANSFER FUNCTION — and D65, not the
		 * cinema white point, which is why a NEUTRAL grey in it converts to the same grey. */
		return cg_rgb_profile_srgb_curve(CG_D65_X, CG_D65_Y, 0.6800, 0.3200, 0.2650, 0.6900,
						 0.1500, 0.0600);
	}
	if ([name isEqual:kCGColorSpaceLinearDisplayP3]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return cg_rgb_profile(CG_D65_X, CG_D65_Y, 0.6800, 0.3200, 0.2650, 0.6900, 0.1500, 0.0600,
				      1.0);
	}
	if ([name isEqual:kCGColorSpaceAdobeRGB1998]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return cg_rgb_profile(CG_D65_X, CG_D65_Y, 0.6400, 0.3300, 0.2100, 0.7100, 0.1500, 0.0600,
				      563.0 / 256.0);
	}
	if ([name isEqual:kCGColorSpaceROMMRGB]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return cg_rgb_profile(CG_D50_X, CG_D50_Y, 0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001,
				      1.8);
	}
	if ([name isEqual:kCGColorSpaceGenericLab]) {
		*model = kCGColorSpaceModelLab;
		*components = 3;
		return cmsCreateLab4Profile(NULL);   /* NULL is D50, the ICC's default */
	}
	if ([name isEqual:kCGColorSpaceGenericGrayGamma2_2]) {
		*model = kCGColorSpaceModelMonochrome;
		*components = 1;
		wp = cg_xy(CG_D65_X, CG_D65_Y);
		g = cmsBuildGamma(NULL, 2.2);
		if (g == NULL) {
			return NULL;
		}
		p = cmsCreateGrayProfile(&wp, g);
		cmsFreeToneCurve(g);
		return p;
	}
	return NULL;
}

/* ---------------------------------------------------------------------------------------
 * THE CACHE, WHICH IS A DEVIATION REMOVED RATHER THAN ONE RECORDED.
 *
 * Apple returns the same object for the same name; this file used to build a fresh one per call,
 * which CGColorSpace.h stated as observable with `==`. IT BECAME NECESSARY RATHER THAN NICE WHEN
 * `CGColorSpaceCopyName` ARRIVED: a space that can say which name it was made with has to know it,
 * and the honest way to know is for there to be ONE OBJECT PER NAME rather than a side table keyed
 * by anything else.
 *
 * AN ENTRY HOLDS A REFERENCE OF ITS OWN, so a caller's release cannot drop it to zero and leave the
 * cache pointing at freed memory — the same reason the C half's device singletons count their
 * references and are never freed.
 * ------------------------------------------------------------------------------------- */
#define CG_NAMED_MAX 8
static struct {
	NSString *name;              /* the CONSTANT, which is never released */
	CGColorSpaceRef space;       /* the cache's own reference */
} cg_named[CG_NAMED_MAX];
static int cg_named_count;

/* THE NAME A SPACE WAS MADE WITH, OR NIL — and nil is the honest answer for every other space
 * here. A DEVICE space has no name to give: Apple's index has no `kCGColorSpaceDevice…` row at all
 * (measured), and Apple's own documentation for this function allows exactly that case — "or NULL
 * if the colour space has no name". A space built from a profile's bytes or from parameters was not
 * asked for by name either, and inventing one would invent a name nobody could pass back to
 * `CGColorSpaceCreateWithName`. */
NSString *CGColorSpaceCopyName(CGColorSpaceRef space)
{
	int i;

	if (space == NULL) {
		return nil;
	}
	for (i = 0; i < cg_named_count; i++) {
		if (cg_named[i].space == space) {
			/* `-copy` ON AN IMMUTABLE STRING IS THE SAME OBJECT RETAINED, which is what a
			 * `Copy`-named function owes its caller: something they own. */
			return [cg_named[i].name copy];
		}
	}
	return nil;
}

CGColorSpaceRef CGColorSpaceCreateWithName(NSString *name)
{
	CGColorSpaceModel model = kCGColorSpaceModelUnknown;
	size_t components = 0;
	CGColorSpaceRef space;
	cmsHPROFILE p;
	int i;

	if (name == nil) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateWithName needs a name\n");
		return NULL;
	}
	/* ONE OBJECT PER NAME: a hit returns the cached space, RETAINED, so that the caller's release
	 * is balanced exactly as it would be for a fresh one — the cache's own reference is separate. */
	for (i = 0; i < cg_named_count; i++) {
		if ([cg_named[i].name isEqual:name]) {
			return CGColorSpaceRetain(cg_named[i].space);
		}
	}
	p = cg_profile_for_name(name, &model, &components);
	if (p == NULL) {
		/* A NAME THIS LIBRARY DOES NOT ANSWER IS REFUSED, never quietly turned into a default:
		 * drawing in the wrong space is a wrong answer, and a silent one. */
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateWithName has no space by that name in "
				"this library yet\n");
		return NULL;
	}
	/* THE SPACE IS BUILT BY THE C HALF, which is where the struct lives; this file only knows
	 * how to turn a name into a profile. */
	space = cg_colorspace_from_profile(p, model, components);
	if (space == NULL) {
		cmsCloseProfile(p);
		return NULL;
	}
	if (cg_named_count < CG_NAMED_MAX) {
		cg_named[cg_named_count].name = name;
		cg_named[cg_named_count].space = CGColorSpaceRetain(space);
		cg_named_count++;
	}
	return space;
}
