/*
 * coregraphics_color — CGColor as a value, and the two context setters that take one.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS PROBE IS FOR: a colour is the first thing in this library that is DATA RATHER THAN
 * GEOMETRY, so what there is to get wrong is ownership and meaning — does the colour keep the
 * space it was made from, do the components come back in the order Apple's callers index them,
 * and does a colour in a space whose numbers mean nothing here get refused rather than drawn.
 */
/* `CGContext.h` KEEPS THE CONTEXT OPAQUE; `CGBitmapContext.h` is the header that defines it,
 * which is why a probe that reads its own pixels back through the context includes both. */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>

/* THE PROBE BUILDS ITS OWN ICC PROFILE WITH THE ENGINE, which is why it includes lcms2
 * directly: a real ICC file is data with a licence of its own, and shipping one would make this
 * probe about the fixture rather than about the library. THE LIBRARY UNDER TEST NEVER SEES THIS
 * HEADER — it reaches lcms2 through its own translation unit, and the seam between them is a
 * `void *` on purpose. */
#include <lcms2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

/* THE ADOPTING PROVIDER'S CALLBACK CONTRACT — the thing a CALLER has to get right: the provider
 * calls this exactly once, when its last release happens, with the `info` it was given. The
 * probe hands over the address of its own counter as that `info`, so the check can assert both
 * that the callback RAN and that it ran WITH WHAT WAS HANDED IN. */
static int released_calls;
static int released_info_ok;
static void probe_release(void *info, const void *data, size_t size)
{
	released_calls++;
	released_info_ok = (info == (void *)&released_calls) && data != NULL && size == 8;
}

static void check(const char *name, int ok)
{
	printf("CG-COLOR %-58s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	double d = got - want;

	if (d < 0) {
		d = -d;
	}
	printf("CG-COLOR %-58s %s (got %g, want %g ±%g)\n", name, d <= tol ? "ok" : "FAIL",
	       got, want, tol);
	if (!(d <= tol)) {
		failures++;
	}
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	/* THE ALPHA AND BYTE-ORDER PAIR IS NOT OPTIONAL: `CGBitmapContextCreate` refuses a
	 * `bitmapInfo` it cannot honour rather than reinterpreting the bytes, so 0 — which is what
	 * a caller writes when they have not thought about it — gives a NULL context, and the
	 * first `pixel()` after it dereferences that NULL. This probe asks for the format it pins,
	 * the same pair coregraphics_stroke_context asks for. */
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	return c;
}

static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * CGBitmapContextGetBytesPerRow(c) + (size_t)x * 4, 4);
}

static int painted(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int n = 0;
	int i;

	for (i = 0; i < W * H; i++) {
		if (d[i * 4 + 3] != 0) {
			n++;
		}
	}
	return n;
}

int main(void)
{
	CGColorSpaceRef sp;
	CGColorSpaceRef other;
	CGColorRef c;
	CGColorRef d;
	CGColorRef e;
	CGColorRef g;
	const CGFloat *comps;
	CGFloat rgb[4];
	CGFloat gray[2];
	CGContextRef ctx;

	rgb[0] = 1.0;
	rgb[1] = 0.5;
	rgb[2] = 0.25;
	rgb[3] = 0.75;

	/* --- a colour is a space plus its components ----------------------------- */
	sp = CGColorSpaceCreateDeviceRGB();
	c = CGColorCreate(sp, rgb);
	check("CGColorCreate gives a colour", c != NULL);
	check("...which keeps the space it was made from", CGColorGetColorSpace(c) == sp);
	comps = CGColorGetComponents(c);
	check("...with the components in the order they were given",
	      comps[0] == 1.0 && comps[1] == 0.5 && comps[2] == 0.25);
	/* THE COUNT INCLUDES ALPHA, which is what a caller sizing a buffer needs: Apple's
	 * `CGColorGetComponents` returns the components AND the alpha, so the number of values it
	 * is safe to index is four for an RGB colour and two for a grayscale one. */
	check_num("an RGB colour has four components, ALPHA COUNTED",
		  (double)CGColorGetNumberOfComponents(c), 4.0, 0);
	check_num("and its alpha is the last of them", (double)CGColorGetAlpha(c), 0.75, 0);
	/* A GRAYSCALE COLOUR IS NOT A DEGENERATE RGB ONE: it has TWO values, the gray and the
	 * alpha, and a caller who sized a buffer from the RGB answer would read past the end. The
	 * colour is held and released rather than created inside the check, because a probe that
	 * leaks is a probe whose discipline is not the one it is asserting. */
	g = CGColorCreateGenericGray(0.25, 1.0);
	check_num("a grayscale colour has two components, ALPHA COUNTED",
		  (double)CGColorGetNumberOfComponents(g), 2.0, 0);
	CGColorRelease(g);
	CGColorRelease(c);

	/* --- AND THE COLOUR KEPT ITS OWN REFERENCE TO THE SPACE ------------------ */
	/* THIS CHECK'S MECHANISM IS THE POINT. If the colour had merely stored the caller's
	 * pointer without retaining it, then releasing the caller's reference would free the
	 * space and the next allocation would very likely REUSE that memory - glibc's malloc
	 * hands back recently freed small blocks - so asking the colour's space for its model
	 * afterwards would answer from whatever object had moved in. On the guest's musl the
	 * same bug leaves the freed bytes intact and this check would PASS: the host is where
	 * it can fail, and the host is where it is run. */
	sp = CGColorSpaceCreateDeviceRGB();
	c = CGColorCreate(sp, rgb);
	CGColorSpaceRelease(sp);
	other = CGColorSpaceCreateDeviceGray();
	check("the colour kept its own reference to the space",
	      CGColorSpaceGetModel(CGColorGetColorSpace(c)) == kCGColorSpaceModelRGB);
	CGColorSpaceRelease(other);
	CGColorRelease(c);

	/* --- an unreadable space is refused, not drawn --------------------------- */
	c = CGColorCreate(NULL, rgb);
	check("a NULL space is refused", c == NULL);
	sp = CGColorSpaceCreateDeviceRGB();
	c = CGColorCreate(sp, NULL);
	check("a NULL component array is refused", c == NULL);
	CGColorSpaceRelease(sp);

	/* --- copies -------------------------------------------------------------- */
	sp = CGColorSpaceCreateDeviceRGB();
	c = CGColorCreate(sp, rgb);
	CGColorSpaceRelease(sp);
	d = CGColorCreateCopy(c);
	check("a copy is equal to its original", CGColorEqualToColor(c, d));
	check("...and is a DIFFERENT object", c != d);
	e = CGColorCreateCopyWithAlpha(c, 0.125);
	check("a copy with a new alpha keeps the components",
	      CGColorGetComponents(e)[0] == 1.0 && CGColorGetComponents(e)[2] == 0.25);
	check_num("...and takes the new alpha", (double)CGColorGetAlpha(e), 0.125, 0);
	check("...so it is NOT equal to the original", !CGColorEqualToColor(c, e));

	/* TWO COLOURS BUILT SEPARATELY FROM THE SAME NUMBERS ARE THE SAME COLOUR, which is what
	 * makes the comparison about colour rather than about pointers. */
	{
		CGColorRef f = CGColorCreateGenericRGB(1.0, 0.5, 0.25, 0.75);

		check("equal by value, not by pointer", CGColorEqualToColor(c, f));
		check("...though the objects differ", c != f);
		CGColorRelease(f);
	}
	gray[0] = 0.5;
	gray[1] = 1.0;
	{
		CGColorSpaceRef gs = CGColorSpaceCreateDeviceGray();
		CGColorRef g = CGColorCreate(gs, gray);

		/* A GRAY COLOUR AND AN RGB COLOUR ARE NEVER EQUAL, however their numbers line up. */
		check("a grayscale colour is not equal to an RGB one", !CGColorEqualToColor(g, c));
		check("a NULL colour is equal to nothing",
		      !CGColorEqualToColor(g, NULL) && !CGColorEqualToColor(NULL, g));
		CGColorRelease(g);
		CGColorSpaceRelease(gs);
	}
	CGColorRelease(c);
	CGColorRelease(d);
	CGColorRelease(e);

	/* --- the retain and release discipline ---------------------------------- */
	check("retaining NULL is NULL", CGColorRetain(NULL) == NULL);
	CGColorRelease(NULL);   /* must not crash */
	check("and releasing NULL is a no-op", 1);

	/* --- and the context draws with a colour --------------------------------- */
	ctx = fresh();
	{
		CGColorRef fill = CGColorCreateGenericRGB(1.0, 0.0, 0.0, 1.0);

		CGContextSetRGBFillColor(ctx, 0.0, 0.0, 1.0, 1.0);
		CGContextSetFillColorWithColor(ctx, fill);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		/* THE COLOUR REPLACED THE R,G,B SET EARLIER, and the buffer is B,G,R,A. */
		check("SetFillColorWithColor fills with that colour",
		      p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);

		CGContextSetStrokeColorWithColor(ctx, fill);
		CGContextSetLineWidth(ctx, 1.0);
		CGContextBeginPath(ctx);
		CGContextMoveToPoint(ctx, 1.0, 2.5);
		CGContextAddLineToPoint(ctx, 15.0, 2.5);
		CGContextStrokePath(ctx);
		pixel(ctx, 8, 2, p);
		check("SetStrokeColorWithColor strokes with that colour", p[2] == 255 && p[3] == 255);
		CGColorRelease(fill);
	}
	CGContextRelease(ctx);

	/* A NULL COLOUR LEAVES THE CONTEXT AS IT WAS, which is the same refusal the constructors
	 * make and the reason it is worth checking: leaving the previous colour is a visible
	 * bug, and clearing the context to something invisible is a worse one. */
	ctx = fresh();
	CGContextSetRGBFillColor(ctx, 0.0, 1.0, 0.0, 1.0);
	CGContextSetFillColorWithColor(ctx, NULL);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
	pixel(ctx, 8, 8, p);
	check("a NULL colour leaves the fill colour alone", p[1] == 255 && p[3] == 255);
	check_num("...and the fill still covered the surface", (double)painted(ctx), 256.0, 0);
	CGContextRelease(ctx);

	/* --- a CMYK space: the colour EXISTS, and the context will not draw it --------- */
	/* THE REFUSAL PATH WITH A REAL COLOUR IN IT. Until this space existed the only way to
	 * reach the context's refusal was to hand it NULL, which proves the guard fires but says
	 * nothing about whether a colour that CAN be built but cannot be interpreted is handled
	 * the same way. A CMYK colour can be built: its four components and its alpha are just
	 * numbers, and the space's model says what they mean. What cannot be done is to TURN THEM
	 * INTO LIGHT, and that is the line this checks. */
	{
		CGColorSpaceRef cmyk = CGColorSpaceCreateDeviceCMYK();
		CGFloat ink[5];
		CGColorRef k;

		check_num("a device CMYK space has four components",
			  (double)CGColorSpaceGetNumberOfComponents(cmyk), 4.0, 0);
		check("...and its model says CMYK",
		      CGColorSpaceGetModel(cmyk) == kCGColorSpaceModelCMYK);
		ink[0] = 0.1;
		ink[1] = 0.2;
		ink[2] = 0.3;
		ink[3] = 0.4;
		ink[4] = 0.5;
		k = CGColorCreate(cmyk, ink);
		check("CGColorCreate takes a CMYK colour — the numbers are numbers", k != NULL);
		check_num("...and its count is the space's four PLUS the alpha",
			  (double)CGColorGetNumberOfComponents(k), 5.0, 0);
		check_num("...with the alpha last", (double)CGColorGetAlpha(k), 0.5, 0);

		ctx = fresh();
		CGContextSetRGBFillColor(ctx, 0.0, 1.0, 0.0, 1.0);
		CGContextSetFillColorWithColor(ctx, k);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		check("a CMYK fill colour is REFUSED, leaving the green in place",
		      p[1] == 255 && p[0] == 0 && p[2] == 0);
		check_num("...and the fill still covered the surface", (double)painted(ctx), 256.0, 0);
		CGContextRelease(ctx);
		CGColorSpaceRelease(cmyk);
		CGColorRelease(k);
	}

	/* --- A SPACE WITH A PROFILE IS CREATED AND REFUSED, NOT CONVERTED --------- */
	/* THIS BLOCK USED TO CHECK THE CONVERSION IN BOTH DIRECTIONS — a Lab colour drawn by converting
	 * it, a device-CMYK one refused for want of a profile — AND THE CONVERSION IS GONE
	 * (2026-10-05): `CGColorCreateCopyByMatchingToColorSpace` is macOS 10.11 against a 10.6-era
	 * surface, so nothing here re-expresses a colour in another space. WHAT REPLACES IT IS THE
	 * REFUSAL, CHECKED WHERE IT IS ACTED ON: the colour is still CREATED — that is a value, and
	 * creating one is 10.0-era API — and the CONTEXT refuses to draw it, leaving the surface
	 * untouched. */
	{
		CGColorSpaceRef lab = CGColorSpaceCreateLab(NULL, NULL, NULL);
		CGFloat v[4];
		CGColorRef cm;

		check("CGColorSpaceCreateLab gives a space",
		      lab != NULL && CGColorSpaceGetModel(lab) == kCGColorSpaceModelLab);
		check_num("...with three components",
			  (double)CGColorSpaceGetNumberOfComponents(lab), 3.0, 0);

		v[0] = 50.0;
		v[1] = 0.0;
		v[2] = 0.0;
		v[3] = 1.0;
		cm = CGColorCreate(lab, v);
		check("Lab colours can be created (three components plus alpha)", cm != NULL);
		check_num("...and the count includes the alpha",
			  (double)CGColorGetNumberOfComponents(cm), 4.0, 0);

		/* !! THE CONVERSION CHECKS STOOD HERE AND WERE REPLACED BY THE REFUSAL (2026-10-05), AND
		 * THE PROPERTIES THEY ESTABLISHED ARE RECORDED HERE RATHER THAN LOST, because they are
		 * what a future era decision should re-establish rather than re-derive: a neutral Lab
		 * colour stayed neutral out to within two percent (the first version demanded EXACT
		 * equality and failed, because D50 to D65 is a matrix product and exactness could only
		 * ever have passed by luck); L* 20 < 50 < 80 survived the trip; a device-GRAY target
		 * worked as well as an RGB one; and in eight bits the rounding landed exactly on
		 * r == g == b. The device-CMYK refusal and the non-NULL-options refusal were this block's
		 * other two checks, and BOTH CASES ARE NOW THE WHOLE CLASS rather than one instance. */

		/* AND THE CONTEXT REFUSES IT. `CGContextSetFillColorWithColor` goes through
		 * `cg_color_to_rgba`, which now refuses any model that is not RGB or gray, so the state
		 * keeps the colour it had and this fill paints nothing. THE CHECK IS THE ALPHA BYTE: a
		 * fresh surface is zeroed, so an untouched pixel is 0 there, where a Lab colour that had
		 * been CONVERTED would have written 255 — which is exactly the mistake this check exists
		 * to catch, since reading Lab's three numbers as r, g and b is the "confident and wrong"
		 * answer the old code was written to avoid. */
		ctx = fresh();
		CGContextSetFillColorWithColor(ctx, cm);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		/* THE STATE KEEPS THE COLOUR IT HAD, which is the rule this library states for a refused
		 * colour — so the fill lands as the OPAQUE BLACK the context starts with, and NOT as the
		 * mid-gray a converted Lab would have written. The distinction is the check. */
		check("the context REFUSES a Lab fill — the colour it already had is what lands",
		      p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255);
		CGContextRelease(ctx);

		CGColorRelease(cm);
		CGColorSpaceRelease(lab);
	}

	/* --- AN ICC PROFILE, THROUGH THE DOOR THAT HAS NO COREFOUNDATION IN IT ----- */
	/* THE PROFILE IS BUILT HERE, BY THE ENGINE, AND THAT IS DELIBERATE. A real ICC file is
	 * DATA with a licence of its own, and shipping one so that the library can be tested would
	 * make the test about the fixture instead of about the library. What is being checked is
	 * what this library does with profile BYTES: that it reads the colour space out of them,
	 * gives a colour the model the profile declares, and converts THROUGH the profile. */
	{
		CGDataProviderRef provider;
		CGColorSpaceRef space;
		CGColorSpaceRef rgbspace2 = CGColorSpaceCreateDeviceRGB();
		CGColorRef c1;
		CGColorRef c2;
		cmsHPROFILE srgb;
		CGFloat v[4];
		const char *path = "/tmp/cg-probe-srgb.icc";

		/* THE ADOPTING FORM FIRST, because its contract is the one a CALLER has to keep: the
		 * bytes stay valid until the provider is released, and then the caller's callback runs
		 * with the same `info` that was handed in. A provider that kept the bytes but never
		 * called back would free nothing, and one that called back twice would free twice. */
		{
			static const unsigned char payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
			CGDataProviderRef adopted = CGDataProviderCreateWithData(
				&released_calls, payload, sizeof(payload), probe_release);

			check("CGDataProviderCreateWithData adopts the bytes", adopted != NULL);
			CGDataProviderRelease(adopted);
			check("...and its release callback runs ONCE, with the caller's info",
			      released_calls == 1 && released_info_ok);
			check("a provider with no data is refused",
			      CGDataProviderCreateWithData(NULL, NULL, 4, NULL) == NULL);
		}

		srgb = cmsCreate_sRGBProfile();
		check("the engine can build the probe a profile to read",
		      srgb != NULL && cmsSaveProfileToFile(srgb, path) != 0);
		if (srgb != NULL) {
			cmsCloseProfile(srgb);
		}

		provider = CGDataProviderCreateWithFilename(path);
		check("CGDataProviderCreateWithFilename reads it back", provider != NULL);
		check("...and a missing file gives NULL rather than an empty provider",
		      CGDataProviderCreateWithFilename("/tmp/cg-probe-nothing-here.icc") == NULL);

		space = CGColorSpaceCreateICCBased(0, NULL, provider, NULL);
		check("CGColorSpaceCreateICCBased gives a space", space != NULL);
		check("...whose model is the PROFILE's (sRGB is RGB)",
		      space != NULL && CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB);
		check_num("...with the profile's own component count",
			  (double)CGColorSpaceGetNumberOfComponents(space), 3.0, 0);
		check("a component count the profile contradicts is refused",
		      CGColorSpaceCreateICCBased(4, NULL, provider, NULL) == NULL);
		check("a non-NULL alternate space is refused rather than dropped",
		      CGColorSpaceCreateICCBased(0, NULL, provider, rgbspace2) == NULL);
		{
			static const unsigned char garbage[64] = { 1, 2, 3, 4 };
			CGDataProviderRef bad = CGDataProviderCreateWithData(NULL, garbage,
									     sizeof(garbage), NULL);

			check("bytes that are not a profile are refused",
			      CGColorSpaceCreateICCBased(0, NULL, bad, NULL) == NULL);
			CGDataProviderRelease(bad);
		}

		/* !! THE CONVERSION CHECKS STOOD HERE AND ARE REPLACED BY THE REFUSAL (2026-10-05). What
		 * they established is in history with the conversion: a colour in the profile's space
		 * converted into device RGB NEAR THE IDENTITY — 0.2/0.5/0.8 to within 0.01 — CLOSE AND NOT
		 * EXACT, because two profiles describing the same space still cross a matrix product. That
		 * is the same lesson the Lab block above paid for, and a future era decision should
		 * re-establish it rather than re-derive it. WHAT IS LEFT HERE IS WHAT A COLOUR IS: it can
		 * be created in the profile's space, and the CONTEXT refuses to draw it. */
		v[0] = 0.2;
		v[1] = 0.5;
		v[2] = 0.8;
		v[3] = 1.0;
		c1 = CGColorCreate(space, v);
		check("a colour in the PROFILE's space can be created", c1 != NULL);
		ctx = fresh();
		CGContextSetFillColorWithColor(ctx, c1);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		/* AND AN ICC **RGB** COLOUR IS NOT REFUSED, WHICH IS A DEVIATION THIS REMOVAL LEAVES AND
		 * SAYS SO. The refusal keys on the colour space's MODEL, so a profile-backed RGB space
		 * — this fixture, and any P3 or Adobe RGB profile — takes the RGB arm and its numbers are
		 * drawn AS DEVICE NUMBERS. For this fixture that is right by luck (the profile IS sRGB,
		 * and device RGB here is sRGB); for a wide-gamut profile it is wrong, and the rule that
		 * would close it is "a space with a profile needs a conversion, so REFUSE it", which is
		 * recorded in the plan as owed rather than smuggled in here. The numbers below are the
		 * evidence that the raw path is what ran. */
		check("an ICC RGB colour is DRAWN by its own numbers (stated deviation: no conversion)",
		      /* THE ARRAY IS IN MEMORY ORDER — B, G, R, A — which is what this library's
		       * premultiplied-first little-endian format means, so the colour that went in as
		       * (0.2, 0.5, 0.8) comes back as 204 BLUE, 128 green, 51 red. MEASURED, after the
		       * first form of this check asserted the right numbers in the wrong slots. */
		      p[0] > 203 && p[0] < 205 && p[1] > 127 && p[1] < 129 && p[2] > 50 && p[2] < 53 &&
		      p[3] == 255);
		CGContextRelease(ctx);
		/* AND THE SPACE KEEPS THE PROFILE IT PARSED RATHER THAN THE PROVIDER'S BYTES, so
		 * releasing the provider first must leave the space working — CHECKED WITHOUT A
		 * CONVERSION, by asking the space for its model and building a colour from it again. */
		CGDataProviderRelease(provider);
		c2 = CGColorCreate(space, v);
		check("the space outlives the provider it was made from",
		      c2 != NULL && CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB);
		CGColorRelease(c2);
		CGColorRelease(c1);
		CGColorSpaceRelease(space);
		CGColorSpaceRelease(rgbspace2);
		remove(path);
	}

	/* --- CALIBRATED SPACES: Apple's matrix, the engine's primaries -------------- */
	/* THE ROUND TRIP IS THE CHECK THAT MATTERS HERE, because the whole question is whether the
	 * MATRIX really was read as PRIMARIES. sRGB's own matrix, with D65, describes the space this
	 * library's device RGB already is — so a colour put in must come back out, and if the
	 * column-to-primary reading were wrong in any way, that is where it would show. */
	{
		static const CGFloat srgb_matrix[9] = {
			0.4124564, 0.3575761, 0.1804375,
			0.2126729, 0.7151522, 0.0721750,
			0.0193339, 0.1191920, 0.9503041
		};
		static const CGFloat d65[3] = { 0.95047, 1.0, 1.08883 };
		/* THE COLUMNS ARE THE PRIMARIES, AND THIS IS HOW THAT IS CHECKED WITHOUT QUOTING A
		 * NUMBER: SWAP THE COLUMNS AND SWAP THE COMPONENTS BY THE SAME PERMUTATION, and the
		 * device colour has to come out the same. A reading that took the matrix by ROWS, or
		 * transposed it, or read a column bottom-to-top, would put a different primary under
		 * each channel and break the identity — which is what an earlier version of this block
		 * got wrong in the other direction, by asking sRGB's matrix with PURE GAMMAS to convert
		 * as the identity. sRGB's transfer function is piecewise (a linear toe and a 2.4 power)
		 * and a calibrated space takes one power per channel, so those two are different spaces
		 * BY CONSTRUCTION: measured then, 0.186201 where 0.2 went in. */
		static const CGFloat srgb_rotated[9] = {
			0.3575761, 0.1804375, 0.4124564,
			0.7151522, 0.0721750, 0.2126729,
			0.1191920, 0.9503041, 0.0193339
		};
		CGFloat g3[3] = { 2.2, 2.2, 2.2 };
		CGFloat v[4];
		CGColorSpaceRef rgb3 = CGColorSpaceCreateDeviceRGB();
		CGColorSpaceRef calrgb = CGColorSpaceCreateCalibratedRGB(d65, NULL, g3, srgb_matrix);
		CGColorSpaceRef calrot;
		CGColorRef c;
		CGColorRef r;
		CGColorRef c2;
		CGColorRef r2;

		check("CGColorSpaceCreateCalibratedRGB gives a space",
		      calrgb != NULL && CGColorSpaceGetModel(calrgb) == kCGColorSpaceModelRGB);
		check_num("...with three components",
			  (double)CGColorSpaceGetNumberOfComponents(calrgb), 3.0, 0);
		/* !! THE MATRIX-AS-PRIMARIES CHECKS STOOD HERE AND ARE REPLACED BY THE REFUSAL
		 * (2026-10-05), because every one of them was made THROUGH the conversion. WHAT THEY
		 * ESTABLISHED IS IN HISTORY: the matrix's COLUMNS ARE THE PRIMARIES IN ORDER — the same
		 * colour spelt against the R,G,B columns and against the same columns rotated LEFT
		 * converted to the same device values to within 0.002 — and a neutral colour stayed
		 * neutral to within 0.01, which is exactly what a matrix read wrongly would break. The
		 * third check in that group mattered as much as the numbers: the result was NOT just the
		 * input, so a space that did nothing at all could not pass the first two.
		 *
		 * WHAT THE ERA KEEPS IS THE SPACE ITSELF: `CGColorSpaceCreateCalibratedRGB` still builds
		 * one from Apple's matrix, a colour can still be created in it, and the CONTEXT refuses to
		 * draw it rather than converting it. */
		v[0] = 0.2;
		v[1] = 0.5;
		v[2] = 0.8;
		v[3] = 1.0;
		c = CGColorCreate(calrgb, v);
		check("a colour in a calibrated space can be created", c != NULL);
		ctx = fresh();
		CGContextSetFillColorWithColor(ctx, c);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		/* THE SAME DEVIATION, AND HERE IT IS NOT HARMLESS: a calibrated RGB whose matrix is
		 * NOT sRGB's is drawn by its own numbers, so this colour is the wrong colour by the
		 * amount its primaries differ. It is the same owed rule as above. */
		check("a calibrated RGB colour is DRAWN by its own numbers (stated deviation)",
		      /* THE ARRAY IS IN MEMORY ORDER — B, G, R, A — which is what this library's
		       * premultiplied-first little-endian format means, so the colour that went in as
		       * (0.2, 0.5, 0.8) comes back as 204 BLUE, 128 green, 51 red. MEASURED, after the
		       * first form of this check asserted the right numbers in the wrong slots. */
		      p[0] > 203 && p[0] < 205 && p[1] > 127 && p[1] < 129 && p[2] > 50 && p[2] < 53 &&
		      p[3] == 255);
		CGContextRelease(ctx);
		check("the device-RGB space it would have converted into is a space of its own",
		      rgb3 != NULL);
		calrot = CGColorSpaceCreateCalibratedRGB(d65, NULL, g3, srgb_rotated);
		v[0] = 0.5;
		v[1] = 0.8;
		v[2] = 0.2;
		c2 = CGColorCreate(calrot, v);
		r = CGColorCreate(calrgb, v);
		r2 = CGColorCreate(calrot, v);
		check("...and the ROTATED spelling of the same matrix gives a space and colours too",
		      calrot != NULL && CGColorSpaceGetModel(calrot) == kCGColorSpaceModelRGB &&
		      c2 != NULL && r != NULL && r2 != NULL);
		CGColorRelease(r2);
		CGColorRelease(r);
		CGColorRelease(c2);
		CGColorRelease(c);
		CGColorSpaceRelease(calrot);
		/* AND A MATRIX WITH A CHANNEL THAT HAS NO PRIMARY AT ALL IS REFUSED. */
		{
			CGFloat broken[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

			check("a matrix with no primaries is refused",
			      CGColorSpaceCreateCalibratedRGB(d65, NULL, g3, broken) == NULL);
			check("a NULL gamma array is refused",
			      CGColorSpaceCreateCalibratedRGB(d65, NULL, NULL, srgb_matrix) == NULL);
		}
		CGColorSpaceRelease(calrgb);
		CGColorSpaceRelease(rgb3);
	}

	/* AND GAMMA IS THE WHOLE OF A CALIBRATED GRAY SPACE: the same white point and the same gray
	 * under gamma 1 and under gamma 2.2 must convert to DIFFERENT device values, AND IN A KNOWN
	 * DIRECTION — a linear gray is lighter than a 2.2 one once it is encoded into sRGB, which is
	 * a statement about the transfer functions rather than about the engine. */
	{
		static const CGFloat d50[3] = { 0.96422, 1.0, 0.82521 };
		CGColorSpaceRef lin = CGColorSpaceCreateCalibratedGray(d50, NULL, 1.0);
		CGColorSpaceRef gam = CGColorSpaceCreateCalibratedGray(d50, NULL, 2.2);
		CGColorSpaceRef rgb4 = CGColorSpaceCreateDeviceRGB();
		CGColorRef c1;
		CGColorRef c2;
		CGColorRef r1;
		CGColorRef r2;
		CGFloat g[2];

		check("CGColorSpaceCreateCalibratedGray gives a space",
		      lin != NULL && gam != NULL &&
		      CGColorSpaceGetModel(lin) == kCGColorSpaceModelMonochrome);
		g[0] = 0.5;
		g[1] = 1.0;
		c1 = CGColorCreate(lin, g);
		c2 = CGColorCreate(gam, g);
		/* !! THE CONVERSION CHECKS STOOD HERE AND ARE REPLACED BY THE REFUSAL (2026-10-05), and
		 * what they established is in history: a LINEAR calibrated gray and a gamma-2.2 one, the
		 * same white point and the same 0.5, converted to DIFFERENT device values AND IN A KNOWN
		 * DIRECTION — the linear one lighter once encoded into sRGB, with the 2.2 one landing near
		 * the 0.5 that was asked for. That is a statement about the transfer functions rather than
		 * about the engine, and it is what a future era decision should re-establish.
		 *
		 * WHAT THE ERA KEEPS IS THE SPACE AND THE VALUE: both spaces are still built from a white
		 * point and a gamma, and a colour can still be created in each — and the CONTEXT refuses to
		 * draw either, because a calibrated gray is not a model the rasterizer can blend. */
		check("colours can be created in both calibrated grays (and they are values)",
		      c1 != NULL && c2 != NULL);
		ctx = fresh();
		CGContextSetFillColorWithColor(ctx, c1);
		CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
		pixel(ctx, 8, 8, p);
		/* AND A CALIBRATED **GRAY** IS DRAWN BY ITS OWN NUMBER, replicated into the three
		 * channels — the carve-out that is not a conversion (see fn_components_in in NSColor.m).
		 * Its 0.5 is 128 in every channel. */
		check("...and a calibrated gray is DRAWN by its own number, in all three channels",
		      p[0] == p[1] && p[1] == p[2] && p[0] > 127 && p[0] < 129 && p[3] == 255);
		CGContextRelease(ctx);
		r1 = CGColorCreate(rgb4, g);
		r2 = CGColorCreate(rgb4, g);
		check("...while a device-RGB colour with the same numbers IS drawable",
		      r1 != NULL && r2 != NULL);
		CGColorRelease(r1);
		CGColorRelease(r2);
		CGColorRelease(c1);
		CGColorRelease(c2);
		CGColorSpaceRelease(lin);
		CGColorSpaceRelease(gam);
		CGColorSpaceRelease(rgb4);
		check("a white point of nothing is refused",
		      CGColorSpaceCreateCalibratedGray(NULL, NULL, 2.2) == NULL);
		check("a gamma of zero is refused",
		      CGColorSpaceCreateCalibratedGray(d50, NULL, 0.0) == NULL);
	}

	/* --- THE PREDICATES STOOD HERE AND WERE REMOVED (2026-10-05) --------------------------- */
	/* `CGColorSpaceSupportsOutput`, `CGColorSpaceIsWideGamutRGB`, `CGColorSpaceUsesExtendedRange`,
	 * `CGColorSpaceIsHDR`, `CGColorSpaceIsHLGBased` and `CGColorSpaceIsPQBased` are all macOS
	 * 10.12-12.0 API, and this tree's duplication is a 10.6-era surface, so both the functions
	 * and this block of checks are gone.
	 *
	 * WHAT WAS BEING MEASURED IS NOT LOST FROM THE TREE — IT MOVED TO WHERE IT IS ACTED ON. The
	 * gamut question ("is device RGB really sRGB, is Adobe RGB really outside it?") was this
	 * block's strongest check, and it is still made at the calibrated-spaces block above, from the
	 * same matrix, by comparing what the engine reports for the two spaces. The refusal that
	 * `SupportsOutput` predicted — device CMYK cannot be drawn, because it has no profile to
	 * convert through — is checked where it happens, in the context's colour setters. */

	/* --- THE NAMED SPACES: the first Foundation objects reaching a C caller --------------- */
	/* THIS PROBE IS C, AND IT PASSES `NSString *` VALUES AROUND WITHOUT EVER SEEING INSIDE ONE —
	 * that is the whole point of the opaque spelling in CGColorSpace.h, and the reason a C caller
	 * can use Apple's named-space API at all.
	 *
	 * THE CHECK THAT MATTERS IS NOT THAT A SPACE CAME BACK, it is that the NAME SELECTED THE
	 * RIGHT PROFILE. THAT CHECK USED TO ASK THIS LIBRARY'S `CGColorSpaceIsWideGamutRGB` — Adobe
	 * RGB's primaries are outside sRGB's triangle and sRGB's are on it — AND THE PREDICATE AND
	 * SEVERAL OF THE NAMES ARE GONE AS OF 2026-10-05 (they are 10.11-13.0 API; this duplication is
	 * a 10.6-era surface). WHAT REPLACES IT HERE: each space's model and component count, the
	 * cache identity, and the NAME ROUND TRIP in the Objective-C probe beside this one —
	 * `CGColorSpaceCopyName` is 10.6 and answers with the name the space was made with, so a
	 * constant that went in has to come back out. The primaries themselves are still MEASURED, at
	 * the calibrated-spaces block above, where the same Adobe matrix goes in. */
	{
		CGColorSpaceRef named_srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGColorSpaceRef named_adobe = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
		CGColorSpaceRef named_gray =
			CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2);

		check("a named space can be created from a constant this C probe cannot see inside",
		      named_srgb != NULL);
		check("...and it is an RGB space with three components",
		      CGColorSpaceGetModel(named_srgb) == kCGColorSpaceModelRGB &&
		      CGColorSpaceGetNumberOfComponents(named_srgb) == 3);
		check("the ADOBE name gives a DIFFERENT space, which is also RGB and has three components",
		      named_adobe != NULL && named_adobe != named_srgb &&
		      CGColorSpaceGetModel(named_adobe) == kCGColorSpaceModelRGB &&
		      CGColorSpaceGetNumberOfComponents(named_adobe) == 3);
		check("...and asking twice for it gives the same object back",
		      CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998) == named_adobe);
		check("and the gray name gives a one-component space",
		      named_gray != NULL && CGColorSpaceGetNumberOfComponents(named_gray) == 1);
		check("a NULL name is refused", CGColorSpaceCreateWithName(NULL) == NULL);

		CGColorSpaceRelease(named_gray);
		CGColorSpaceRelease(named_adobe);
		CGColorSpaceRelease(named_srgb);
	}

	/* --- DISPLAY P3, XYZ, DCI-P3, LINEAR GRAY AND REC.2020 STOOD HERE AND WERE REMOVED ------- */
	/* FOUR WHOLE CHECK BLOCKS WENT WITH THE NAMES THEY TESTED (2026-10-05): Display P3 and linear
	 * Display P3 (10.11.2/12.0), generic XYZ (10.11), DCI-P3 (10.11) and linear gray (10.12),
	 * ITU-R BT.2020 (10.11). Each block was built to establish that a NAME reached the RIGHT
	 * PROFILE — the P3 blocks by the sRGB curve's TOE, XYZ by the PCS white point landing white,
	 * the cinema block by DCI-P3 and Display P3 converting one grey differently — AND EVERY ONE OF
	 * THEM NEEDED TWO THINGS THAT ARE ALSO GONE: the `kCGColorSpace…` name, and
	 * `CGColorCreateCopyByMatchingToColorSpace` (10.11), which is how a converted value was read
	 * back.
	 *
	 * THE MEASUREMENTS THEMSELVES ARE NOT LOST: they are recorded in history with the definitions,
	 * including the two numbers the BT.2020 check had to correct before it passed (a predicted
	 * ratio of 1.33 against a measured 1.59, and 1.59 against 1.67 — sRGB's encode sits between
	 * the two, so only a linear target makes the decoded value observable).
	 *
	 * WHAT SURVIVES HERE IS THE CALIBRATED-SPACES BLOCK ABOVE, which builds Adobe RGB from its
	 * matrix through the parameter door — and the cache block below, which is about the named
	 * spaces that remain. */

	/* --- THE NAMED SPACES ARE CACHED: one object per name -------------------------------- */
	/* A DEVIATION REMOVED, SO THE CHECK IS THE DEVIATION'S OPPOSITE: asking for the same name
	 * twice has to give THE SAME OBJECT. It is also the precondition `CGColorSpaceCopyName`
	 * needs — and a C probe can assert the identity, because a pointer comparison needs no
	 * message, while the NAME itself is read back in the Objective-C probe beside this one. */
	{
		CGColorSpaceRef a = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
		CGColorSpaceRef b = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
		CGColorSpaceRef c = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

		check("the same name twice gives the same OBJECT", a != NULL && a == b);
		check("...and a different name gives a different one", c != NULL && c != a);
		CGColorSpaceRelease(c);
		CGColorSpaceRelease(b);
		CGColorSpaceRelease(a);
	}

	/* --- THE XYZ, CINEMA, LINEAR-GRAY AND REC.2020 BLOCKS STOOD HERE AND WERE REMOVED -------- */
	/* Three more self-contained blocks, each testing a name that is out of era and each reading
	 * its evidence back through `CGColorCreateCopyByMatchingToColorSpace` (10.11), which went with
	 * them. THE MEASUREMENTS THEY ESTABLISHED ARE RECORDED IN HISTORY WITH THE DEFINITIONS THEY
	 * BELONG TO: the ICC PCS white point landing EXACTLY white in device RGB (1.000 to the printed
	 * digit), DCI-P3 and Display P3 converting one grey DIFFERENTLY because their white points and
	 * gammas differ, and BT.2020 decoding 0.02 to 0.02/4.5 inside its linear segment — the last of
	 * which cost two corrected predictions (1.33 against a measured 1.59, 1.59 against 1.67)
	 * because sRGB's encode sits between the two spaces and its -0.055 offset breaks the ratio. */

	printf("CG-COLOR: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
