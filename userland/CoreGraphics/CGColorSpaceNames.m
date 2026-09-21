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
 * per channel, so a P3 built here would be a different space — it arrives with the piecewise
 * curve. The `Extended…` family and `kCGColorSpaceACESCGLinear` are outside 0..1 or built on HDR
 * curves; `kCGColorSpaceGenericCMYK` has no profile that can be invented (what ink values mean
 * depends on the press); `kCGColorSpaceGenericXYZ` needs an XYZ model this library does not read.
 * Every one of them stays `open` in the ledger, which is where an unimplemented name belongs.
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

CGColorSpaceRef CGColorSpaceCreateWithName(NSString *name)
{
	CGColorSpaceModel model = kCGColorSpaceModelUnknown;
	size_t components = 0;
	CGColorSpaceRef space;
	cmsHPROFILE p;

	if (name == nil) {
		fprintf(stderr, "CG-REFUSE: CGColorSpaceCreateWithName needs a name\n");
		return NULL;
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
	}
	return space;
}
