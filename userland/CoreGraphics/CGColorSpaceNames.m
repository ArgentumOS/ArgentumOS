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
 * AND THE THREE NAMES ARE THE ONES THIS LIBRARY CAN BACK WITH AN EXACT PROFILE *AND* THAT THE
 * 10.6 ERA ALLOWS. Each is defined by a white point, three primaries and a transfer function —
 * every one of which the engine can be given here — so none of them is a substitution for a
 * definition this library cannot express:
 *
 *   kCGColorSpaceSRGB                  sRGB's own profile, from the engine
 *   kCGColorSpaceAdobeRGB1998          Adobe's primaries, D65, gamma 563/256 — the exact value the
 *                                      Adobe RGB (1998) specification names, not 2.2 rounded
 *   kCGColorSpaceGenericGrayGamma2_2   D65 with gamma 2.2
 *
 * NINE NAMES WERE HERE AND WERE REMOVED (2026-10-05) BECAUSE THEY ARE OUT OF ERA, not because
 * their definitions stopped working: linear sRGB (10.12), Display P3 (10.11.2) and its linear
 * form (12.0), DCI-P3 (10.11), ProPhoto/ROMM (10.11), generic XYZ (10.11), ITU-R BT.2020 (10.11),
 * linear gray (10.12) and generic Lab (10.13). With them went the whole piecewise-transfer
 * machinery they were the only users of — the parametric type-4 curve, `cg_rgb_profile_pc` and
 * `cg_rgb_profile_srgb_curve` — so this file is now one gamma-built profile per channel plus
 * sRGB's own. **THE DEFINITIONS ARE IN HISTORY AND THAT IS THE RECORD**: the white points,
 * primaries and curve parameters each one needs were measured when it was built, and a later era
 * decision that puts one back in scope has them written down there rather than to re-derive.
 *
 * WHAT REMAINS ABSENT FOR REASONS THAT ARE NOT THE ERA, and stays `open` in the ledger where an
 * unimplemented name belongs: the `Extended…` family and `kCGColorSpaceACESCGLinear` are outside
 * 0..1 or built on HDR curves, and `kCGColorSpaceGenericCMYK` has no profile that can be
 * invented (what ink values mean depends on the press) — BUT THE NAMES OF ALL FOUR SHIP, AND A NAME IS
 * NOT THE THING IT NAMES: `kCGColorSpaceGenericRGB`, `kCGColorSpaceGenericGray` and
 * `kCGColorSpaceGenericRGBLinear` are IN era, their constants are declared, and what is still owed is the
 * PROFILE each one should reach, which is why the dispatch below returns NULL for them. THAT THE LEDGER
 * CANNOT SAY BOTH THINGS AT ONCE IS A FACT ABOUT THE LEDGER: a `var` row records that a NAME is declared
 * (the sweep's `--check` verifies exactly that and nothing more), so "simply not implemented yet" and
 * "shipped" are both true of one row, and this file is where the second half lives.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGColorSpace_internal.h>

#include <lcms2.h>
#include <stdio.h>

/* THE FOUR GENERIC SPACES, AND THE VALUE OF EACH IS ITS OWN NAME. Apple publishes the NAMES, not the
 * string a caller gets by printing one, and a caller who wrote this value to a file and read it back
 * is reading a name this library chose — which is what a name is for. What must hold is that
 * CGColorSpaceCreateWithName accepts the value it was handed, which is why the name IS the value. */
NSString *const kCGColorSpaceGenericGray = @"kCGColorSpaceGenericGray";
NSString *const kCGColorSpaceGenericRGB = @"kCGColorSpaceGenericRGB";
NSString *const kCGColorSpaceGenericCMYK = @"kCGColorSpaceGenericCMYK";
NSString *const kCGColorSpaceGenericRGBLinear = @"kCGColorSpaceGenericRGBLinear";

NSString *const kCGColorSpaceSRGB = @"kCGColorSpaceSRGB";
NSString *const kCGColorSpaceAdobeRGB1998 = @"kCGColorSpaceAdobeRGB1998";
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

/* !! THE PIECEWISE-TRANSFER MACHINERY STOOD HERE AND WAS REMOVED (2026-10-05). It was the
 * engine's parametric type 4 — `cg_param_curve`, the sRGB parameter array read out of lcms2's own
 * source and the BT.2020 array inverted from the specification — plus the two profile builders
 * that asked it for a curve per channel (`cg_rgb_profile_pc` and `cg_rgb_profile_srgb_curve`).
 * ITS ONLY TWO CONSUMERS WERE OUT-OF-ERA SPACES, AND BOTH WENT IN THE SAME PASS — Display P3 (the
 * sRGB curve) and ITU-R BT.2020 (the inverted OETF). The three names that remain are one power per
 * channel or sRGB's own profile, so `cg_rgb_profile` above is all they need.
 *
 * THE PARAMETERS ARE IN HISTORY rather than here, for the reason a removal is recorded at all: the
 * numbers were measured, not recalled, and a later era decision that puts P3 or BT.2020 back in
 * scope should find the definition rather than re-derive it. THE PROBE'S DISCRIMINATOR GOES WITH
 * THEM: the sRGB curve's TOE — 0.02 decoding to 0.00444444 inside it against 0.0068 outside — was
 * how a piecewise space was told apart from a power curve, and no remaining name needs it. */

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
	/* !! FIVE BRANCHES STOOD HERE AND WERE REMOVED (2026-10-05): linear sRGB (10.12), Display P3
	 * (10.11.2), linear Display P3 (12.0), DCI-P3 (10.11) and linear gray (10.12) are all out of
	 * era, so `CGColorSpaceCreateWithName` no longer answers to those names. THE DEFINITIONS ARE
	 * IN HISTORY — DCI-P3's own white point and gamma 2.6, Display P3's P3 primaries with the sRGB
	 * curve, the linear forms' gamma 1 — each measured when it was built rather than recalled. */
	if ([name isEqual:kCGColorSpaceAdobeRGB1998]) {
		*model = kCGColorSpaceModelRGB;
		*components = 3;
		return cg_rgb_profile(CG_D65_X, CG_D65_Y, 0.6400, 0.3300, 0.2100, 0.7100, 0.1500, 0.0600,
				      563.0 / 256.0);
	}
	/* !! FOUR MORE BRANCHES STOOD HERE AND WERE REMOVED (2026-10-05): ProPhoto/ROMM (10.11),
	 * generic Lab (10.13), generic XYZ (10.11) and ITU-R BT.2020 (10.11). LAB IS STILL REACHABLE
	 * WITHOUT A NAME — through `CGColorSpaceCreateLab` and through an ICC profile — so what went
	 * is a name rather than a capability, and XYZ is reachable the same way through a profile. */
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
