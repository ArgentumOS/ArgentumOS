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

	/* --- A SPACE WITH A PROFILE IS CONVERTED, NOT REFUSED -------------------- */
	/* BOTH SIDES OF THE LINE THE DESIGN DRAWS, IN ONE PLACE: a Lab colour is DRAWN, by
	 * conversion; a device CMYK colour is REFUSED, because there is no profile to convert it
	 * through. Lab is what makes the first half testable at all, which is why it arrived with
	 * the engine.
	 *
	 * THE CHECKS ARE PROPERTIES, NOT MAGIC NUMBERS, AND DELIBERATELY. Lab(50, 0, 0) is a mid
	 * gray with a value nobody remembers; a literal here would be a number the engine's next
	 * version could legitimately change. That a neutral Lab colour stays neutral in RGB, and
	 * that lightness ORDERS, are statements about Lab itself — and each of them fails if the
	 * conversion is wrong in a way that matters. */
	{
		CGColorSpaceRef lab = CGColorSpaceCreateLab(NULL, NULL, NULL);
		CGColorSpaceRef rgbspace = CGColorSpaceCreateDeviceRGB();
		CGFloat v[4];
		CGColorRef cm;
		CGColorRef cd;
		CGColorRef cl;

		check("CGColorSpaceCreateLab gives a space",
		      lab != NULL && CGColorSpaceGetModel(lab) == kCGColorSpaceModelLab);
		check_num("...with three components",
			  (double)CGColorSpaceGetNumberOfComponents(lab), 3.0, 0);

		v[0] = 50.0;
		v[1] = 0.0;
		v[2] = 0.0;
		v[3] = 1.0;
		cm = CGColorCreate(lab, v);
		v[0] = 20.0;
		cd = CGColorCreate(lab, v);
		v[0] = 80.0;
		cl = CGColorCreate(lab, v);
		check("Lab colours can be created (three components plus alpha)", cm != NULL);
		check_num("...and the count includes the alpha",
			  (double)CGColorGetNumberOfComponents(cm), 4.0, 0);

		{
			CGColorRef rm = CGColorCreateCopyByMatchingToColorSpace(
				cm, kCGRenderingIntentDefault, rgbspace, NULL);
			CGColorRef rd = CGColorCreateCopyByMatchingToColorSpace(
				cd, kCGRenderingIntentDefault, rgbspace, NULL);
			CGColorRef rl = CGColorCreateCopyByMatchingToColorSpace(
				cl, kCGRenderingIntentDefault, rgbspace, NULL);

			check("a Lab colour CONVERTS into device RGB",
			      rm != NULL && rd != NULL && rl != NULL);
			if (rm != NULL && rd != NULL && rl != NULL) {
				const CGFloat *crm = CGColorGetComponents(rm);
				const CGFloat *crd = CGColorGetComponents(rd);
				const CGFloat *crl = CGColorGetComponents(rl);

				/* NEUTRAL IN, NEUTRAL OUT — AS A TOLERANCE, AND THE TOLERANCE IS THE FINDING.
				 * The first version of this check demanded EXACT equality and failed: the
				 * conversion carries Lab under D50 to sRGB under D65, and a chromatic
				 * adaptation between two white points is a matrix product, so agreement holds
				 * to within rounding rather than to the last bit. Exact equality was a check
				 * that could only ever pass by luck. Note what the DEVICE check further down
				 * does with the same conversion: in EIGHT BITS it lands exactly on r == g == b,
				 * because the rounding is absorbed on the way out. Two percent is still far
				 * tighter than a wrong conversion, which shows up as a cast of tens. */
				check_num("a neutral Lab colour stays neutral: r - g",
					  (double)(crm[0] - crm[1]), 0.0, 0.02);
				check_num("...and g - b", (double)(crm[1] - crm[2]), 0.0, 0.02);
				check("...and lands in the middle, not at an end",
				      crm[0] > 0.05 && crm[0] < 0.95);
				/* THE ORDER OF THREE LIGHTNESSES MUST SURVIVE THE CONVERSION. */
				check("L* 20 < 50 < 80 after conversion",
				      crd[0] < crm[0] && crm[0] < crl[0]);
				/* THE TARGET IS NOT HARD-WIRED: device gray converts too. */
				{
					CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
					CGColorRef g2 = CGColorCreateCopyByMatchingToColorSpace(
						cm, kCGRenderingIntentDefault, gray, NULL);

					check("...and a device GRAY target works as well", g2 != NULL);
					CGColorRelease(g2);
					CGColorSpaceRelease(gray);
				}
				/* THE REFUSAL IS STILL THERE FOR THE SPACE WITH NO PROFILE. */
				{
					CGColorSpaceRef cmyk = CGColorSpaceCreateDeviceCMYK();
					CGFloat ink[5] = { 0.1, 0.2, 0.3, 0.4, 1.0 };
					CGColorRef k = CGColorCreate(cmyk, ink);

					check("a device CMYK colour still has NO conversion",
					      CGColorCreateCopyByMatchingToColorSpace(
						      k, kCGRenderingIntentDefault, rgbspace, NULL) == NULL);
					CGColorRelease(k);
					CGColorSpaceRelease(cmyk);
				}
				/* AND AN OPTION THIS LIBRARY DOES NOT HAVE IS REFUSED, NOT IGNORED. */
				check("a non-NULL options is refused rather than dropped",
				      CGColorCreateCopyByMatchingToColorSpace(
					      cm, kCGRenderingIntentDefault, rgbspace, v) == NULL);
			}
			CGColorRelease(rm);
			CGColorRelease(rd);
			CGColorRelease(rl);
		}

		/* AND THE CONTEXT DRAWS ONE: the setter converts through the same function, so a Lab
		 * fill lands as the gray it means. This is the check that would have caught C4.1's
		 * model-based guard, which would have copied Lab's three numbers into r, g and b. */
		ctx = fresh();
		{
			CGFloat lv[4];
			CGColorRef lab_colour;

			lv[0] = 50.0;
			lv[1] = 0.0;
			lv[2] = 0.0;
			lv[3] = 1.0;
			lab_colour = CGColorCreate(lab, lv);
			CGContextSetFillColorWithColor(ctx, lab_colour);
			CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 16.0, 16.0));
			pixel(ctx, 8, 8, p);
			check("the context DRAWS a Lab colour by converting it", p[3] == 255);
			/* A ONE-UNIT TOLERANCE, WHICH IS NOT A CONTRADICTION OF THE CHECK ABOVE: on THIS
			 * engine the doubles came out a hair apart and the eight-bit write rounds them to
			 * the same byte, so demanding exactness would pass here and could break on a
			 * different rounding for a reason that is not a bug. One unit says the same thing
			 * about neutrality without betting on which way a half rounds. */
			check("...as a neutral gray, to within one unit",
			      (p[0] > p[1] ? p[0] - p[1] : p[1] - p[0]) <= 1 &&
			      (p[1] > p[2] ? p[1] - p[2] : p[2] - p[1]) <= 1);
			check("...and a middle one, not black or white", p[0] > 12 && p[0] < 243);
			CGColorRelease(lab_colour);
		}
		CGContextRelease(ctx);

		CGColorRelease(cm);
		CGColorRelease(cd);
		CGColorRelease(cl);
		CGColorSpaceRelease(lab);
		CGColorSpaceRelease(rgbspace);
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

		/* AND THE CONVERSION GOES THROUGH THE PROFILE: sRGB into DEVICE RGB, which this library
		 * treats as sRGB, must land very close to the identity — CLOSE AND NOT EXACT, because
		 * these are two different profiles describing the same space and the transform between
		 * them is a matrix product. That is the same lesson the Lab check above already paid
		 * for, and the bound is tighter here because the two spaces really are the same one. */
		v[0] = 0.2;
		v[1] = 0.5;
		v[2] = 0.8;
		v[3] = 1.0;
		c1 = CGColorCreate(space, v);
		check("a colour in the PROFILE's space can be created", c1 != NULL);
		c2 = CGColorCreateCopyByMatchingToColorSpace(c1, kCGRenderingIntentRelativeColorimetric,
							     rgbspace2, NULL);
		check("...and it converts into device RGB", c2 != NULL);
		if (c2 != NULL) {
			const CGFloat *r = CGColorGetComponents(c2);

			check_num("...near the identity, since both describe sRGB: r", (double)r[0], 0.2,
				  0.01);
			check_num("...g", (double)r[1], 0.5, 0.01);
			check_num("...b", (double)r[2], 0.8, 0.01);
			CGColorRelease(c2);
		}
		/* AND THE SPACE KEEPS THE PROFILE IT PARSED RATHER THAN THE PROVIDER'S BYTES, so
		 * releasing the provider first must leave the space working. */
		CGDataProviderRelease(provider);
		c2 = CGColorCreateCopyByMatchingToColorSpace(c1, kCGRenderingIntentDefault, rgbspace2,
							     NULL);
		check("the space outlives the provider it was made from", c2 != NULL);
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
		/* THE SAME COLOUR IN TWO SPELLINGS: (0.2, 0.5, 0.8) against the R,G,B columns, and
		 * (0.5, 0.8, 0.2) against the same columns rotated left. The components move with their
		 * primaries, so the device colour must not move at all. */
		v[0] = 0.2;
		v[1] = 0.5;
		v[2] = 0.8;
		v[3] = 1.0;
		c = CGColorCreate(calrgb, v);
		r = CGColorCreateCopyByMatchingToColorSpace(c, kCGRenderingIntentRelativeColorimetric,
							    rgb3, NULL);
		check("...a colour in it converts to device RGB", r != NULL);
		calrot = CGColorSpaceCreateCalibratedRGB(d65, NULL, g3, srgb_rotated);
		v[0] = 0.5;
		v[1] = 0.8;
		v[2] = 0.2;
		c2 = CGColorCreate(calrot, v);
		r2 = CGColorCreateCopyByMatchingToColorSpace(c2, kCGRenderingIntentRelativeColorimetric,
							     rgb3, NULL);
		check("...and the rotated spelling converts too", r2 != NULL);
		if (r != NULL && r2 != NULL) {
			const CGFloat *q = CGColorGetComponents(r);
			const CGFloat *q2 = CGColorGetComponents(r2);

			check_num("the matrix's columns ARE the primaries, in order: r", (double)q[0],
				  (double)q2[0], 0.002);
			check_num("...g", (double)q[1], (double)q2[1], 0.002);
			check_num("...b", (double)q[2], (double)q2[2], 0.002);
			/* AND THE CONVERSION IS NOT A COPY, which is what would make the three checks
			 * above pass for a space that did nothing at all. */
			check("...and the result is not just the input", q[0] != 0.2 || q[1] != 0.5);
		}
		/* A NEUTRAL COLOUR STAYS NEUTRAL: equal components point at the white point — D65 here,
		 * the same one this library's device RGB uses — so a gray must not pick up a tint. A
		 * matrix read wrongly would give it one. */
		v[0] = 0.5;
		v[1] = 0.5;
		v[2] = 0.5;
		v[3] = 1.0;
		{
			CGColorRef cn = CGColorCreate(calrgb, v);
			CGColorRef rn = CGColorCreateCopyByMatchingToColorSpace(
				cn, kCGRenderingIntentRelativeColorimetric, rgb3, NULL);

			if (rn != NULL) {
				const CGFloat *qn = CGColorGetComponents(rn);

				check_num("a neutral colour stays neutral: r - g",
					  (double)(qn[0] - qn[1]), 0.0, 0.01);
				check_num("...and g - b", (double)(qn[1] - qn[2]), 0.0, 0.01);
			} else {
				check("a neutral colour converts", 0);
			}
			CGColorRelease(rn);
			CGColorRelease(cn);
		}
		CGColorRelease(r2);
		CGColorRelease(c2);
		CGColorRelease(r);
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
		r1 = CGColorCreateCopyByMatchingToColorSpace(c1, kCGRenderingIntentRelativeColorimetric,
							     rgb4, NULL);
		r2 = CGColorCreateCopyByMatchingToColorSpace(c2, kCGRenderingIntentRelativeColorimetric,
							     rgb4, NULL);
		check("both convert", r1 != NULL && r2 != NULL);
		if (r1 != NULL && r2 != NULL) {
			double light = CGColorGetComponents(r1)[0];
			double dark = CGColorGetComponents(r2)[0];

			check("linear gray 0.5 is LIGHTER than gamma-2.2 gray 0.5 in sRGB", light > dark);
			check("...and the 2.2 one lands near the 0.5 that was asked for",
			      dark > 0.4 && dark < 0.6);
		}
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

	/* --- THE PREDICATES: answers computed from the same facts the library acts on ---------- */
	/* TWO OF THESE ARE COMPUTED AND FOUR ARE STATEMENTS ABOUT WHICH SPACES EXIST HERE. The
	 * interesting one is `IsWideGamutRGB`, because it has a real answer to get right: device RGB
	 * IS sRGB by this library's definition and must come out NOT wide, while Adobe RGB — whose
	 * green primary, (0.21, 0.71), sits well outside sRGB's triangle — must come out wide. THAT
	 * SECOND CASE IS BUILT FROM ITS PRIMARIES AS A MATRIX, so the check exercises the same
	 * matrix-to-primaries reading the calibrated block above proves, from the other direction. */
	{
		static const CGFloat adobe_rgb_matrix[9] = {
			0.5767309, 0.1855540, 0.1881852,
			0.2973769, 0.6273491, 0.0752741,
			0.0270343, 0.0706872, 0.9911085
		};
		static const CGFloat d65[3] = { 0.95047, 1.0, 1.08883 };
		CGFloat g3[3] = { 2.2, 2.2, 2.2 };
		CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
		CGColorSpaceRef cmyk = CGColorSpaceCreateDeviceCMYK();
		CGColorSpaceRef lab = CGColorSpaceCreateLab(NULL, NULL, NULL);
		CGColorSpaceRef wide_rgb = CGColorSpaceCreateCalibratedRGB(d65, NULL, g3,
									  adobe_rgb_matrix);

		check("device RGB and device gray SUPPORT output",
		      CGColorSpaceSupportsOutput(rgb) && CGColorSpaceSupportsOutput(gray));
		check("...and so does a space that has to be CONVERTED (Lab)",
		      CGColorSpaceSupportsOutput(lab));
		check("device CMYK does NOT — there is no profile to draw it through",
		      !CGColorSpaceSupportsOutput(cmyk));
		check("device RGB is NOT wide gamut: it IS sRGB, by this library's definition",
		      !CGColorSpaceIsWideGamutRGB(rgb));
		check("...but ADOBE RGB's primaries are, its green being (0.21, 0.71)",
		      CGColorSpaceIsWideGamutRGB(wide_rgb));
		check("a grayscale space is not wide-gamut RGB", !CGColorSpaceIsWideGamutRGB(gray));
		check("a NULL space is not wide-gamut RGB", !CGColorSpaceIsWideGamutRGB(NULL));
		check("nothing here uses an extended range",
		      !CGColorSpaceUsesExtendedRange(rgb) && !CGColorSpaceUsesExtendedRange(wide_rgb));
		check("nothing here is HDR, PQ-based or HLG-based",
		      !CGColorSpaceIsHDR(lab) && !CGColorSpaceIsPQBased(lab) &&
		      !CGColorSpaceIsHLGBased(wide_rgb));
		check("and an extended-range question about NULL is still answered, not crashed on",
		      !CGColorSpaceUsesExtendedRange(NULL) && !CGColorSpaceSupportsOutput(NULL));

		CGColorSpaceRelease(wide_rgb);
		CGColorSpaceRelease(lab);
		CGColorSpaceRelease(cmyk);
		CGColorSpaceRelease(gray);
		CGColorSpaceRelease(rgb);
	}

	/* --- THE NAMED SPACES: the first Foundation objects reaching a C caller --------------- */
	/* THIS PROBE IS C, AND IT PASSES `NSString *` VALUES AROUND WITHOUT EVER SEEING INSIDE ONE —
	 * that is the whole point of the opaque spelling in CGColorSpace.h, and the reason a C caller
	 * can use Apple's named-space API at all.
	 *
	 * THE CHECK THAT MATTERS IS NOT THAT A SPACE CAME BACK, it is that the NAME SELECTED THE
	 * RIGHT PROFILE: Adobe RGB's primaries are outside sRGB's triangle and sRGB's are on it, and
	 * both answers come from this library's own predicate rather than from the probe. If the
	 * comparison in the Objective-C file matched the wrong branch — or the primaries were read
	 * from the wrong space — this is where it shows. */
	{
		CGColorSpaceRef named_srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGColorSpaceRef named_adobe = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
		CGColorSpaceRef named_romm = CGColorSpaceCreateWithName(kCGColorSpaceROMMRGB);
		CGColorSpaceRef named_linear = CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
		CGColorSpaceRef named_lab = CGColorSpaceCreateWithName(kCGColorSpaceGenericLab);
		CGColorSpaceRef named_gray =
			CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2);

		check("a named space can be created from a constant this C probe cannot see inside",
		      named_srgb != NULL);
		check("...and it is an RGB space with three components",
		      CGColorSpaceGetModel(named_srgb) == kCGColorSpaceModelRGB &&
		      CGColorSpaceGetNumberOfComponents(named_srgb) == 3);
		check("...and NOT wide gamut, because that name IS sRGB",
		      !CGColorSpaceIsWideGamutRGB(named_srgb));
		check("the ADOBE name selects Adobe's primaries, which ARE wide gamut",
		      named_adobe != NULL && CGColorSpaceIsWideGamutRGB(named_adobe));
		check("the ProPhoto name selects ProPhoto's, which are wide too",
		      named_romm != NULL && CGColorSpaceIsWideGamutRGB(named_romm));
		check("...while the LINEAR sRGB name keeps sRGB's primaries and is not wide",
		      named_linear != NULL && !CGColorSpaceIsWideGamutRGB(named_linear));
		check("the Lab name gives a Lab space, which the context can therefore be asked to draw",
		      named_lab != NULL && CGColorSpaceGetModel(named_lab) == kCGColorSpaceModelLab &&
		      CGColorSpaceSupportsOutput(named_lab));
		check("and the gray name gives one component",
		      named_gray != NULL && CGColorSpaceGetNumberOfComponents(named_gray) == 1);
		check("a NULL name is refused", CGColorSpaceCreateWithName(NULL) == NULL);

		CGColorSpaceRelease(named_gray);
		CGColorSpaceRelease(named_lab);
		CGColorSpaceRelease(named_linear);
		CGColorSpaceRelease(named_romm);
		CGColorSpaceRelease(named_adobe);
		CGColorSpaceRelease(named_srgb);
	}

	printf("CG-COLOR: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
