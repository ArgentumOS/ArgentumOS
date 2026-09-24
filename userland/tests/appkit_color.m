/*
 * appkit_color — NSColor, the first drawing class, and the two things it chose to do differently.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE INTERESTING CHECKS ARE THE TWO DEVIATIONS FROM APPLE, because they are the parts a later reader
 * will want to argue with. This library CONVERTS where Apple's `-redComponent` raises — so a white
 * colour is asked for its red component and a colour in the gamma-2.2 gray PROFILE is asked for its
 * white one, which is a trip through lcms2 and not a copy. AND IT STILL RAISES where the engine
 * cannot answer, which is exercised with a device-CMYK colour: the one colour this library can HOLD
 * and cannot CONVERT (C4's refusal). Both halves are asserted, so neither a library that always
 * raises nor one that never does can pass.
 *
 * AND THE BRIDGE'S OWNERSHIP IS CHECKED BY RELEASING THE CALLER'S REFERENCE: `+colorWithCGColor:`
 * retains what it is given, so a colour must survive its creator letting go — the difference between
 * a bridge and a borrow, and invisible to a check that holds both references throughout.
 *
 * IT IS MRC LIKE EVERY OBJECTIVE-C PROBE HERE (the rule passes -fno-objc-arc).
 */
#import <AppKit/NSColor.h>

#import <CoreGraphics/CGColor.h>
#import <CoreGraphics/CGColorSpace.h>
#import <Foundation/NSException.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("APPKIT-COLOR %-66s %s\n", name, ok ? "ok" : "FAIL");
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
	printf("APPKIT-COLOR %-66s %s (got %g, want %g ±%g)\n", name, d <= tol ? "ok" : "FAIL", got,
	       want, tol);
	if (!(d <= tol)) {
		failures++;
	}
}

/* WAS THE GETTER A RAISE? The only way to check that this library still refuses what it cannot
 * convert, and it needs a real `@try` because the raise is an Objective-C exception. */
static int raised(void)
{
	NSColor *cmyk;
	CGColorSpaceRef cmyk_space;
	CGFloat comp[5] = { 0.0, 0.0, 0.0, 0.0, 1.0 };
	int got = 0;

	cmyk_space = CGColorSpaceCreateDeviceCMYK();
	cmyk = [NSColor colorWithCGColor:CGColorCreate(cmyk_space, comp)];
	CGColorSpaceRelease(cmyk_space);
	/* A CMYK COLOUR IS HELD HAPPILY — it has five components and CoreGraphics made it. */
	check("a device-CMYK colour is held (5 components counting alpha)",
	      [cmyk numberOfComponents] == 5);
	@try {
		(void)[cmyk redComponent];
	} @catch (NSException *e) {
		(void)e;
		got = 1;
	}
	return got;
}

int main(void)
{
	NSColor *c;
	NSColor *d;
	CGFloat comp[6];
	CGFloat r = -1.0, g = -1.0, b = -1.0, a = -1.0;
	CGColorRef cg;

	/* --- the bridge, in both directions ------------------------------------------- */
	{
		CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
		CGFloat v[4] = { 0.25, 0.5, 0.75, 1.0 };

		cg = CGColorCreate(space, v);
		CGColorSpaceRelease(space);
	}
	c = [NSColor colorWithCGColor:cg];
	check("+colorWithCGColor: wraps the colour it was given", c != nil);
	check("...and -CGColor hands back THAT colour, not a copy", [c CGColor] == cg);
	/* THE RETAIN: the creator lets go and the colour keeps working. A borrow would be freed here. */
	CGColorRelease(cg);
	check_num("...and the colour survives its creator's release (it was retained)",
		  (double)[c redComponent], 0.25, 1e-9);
	check("+colorWithCGColor:NULL answers nil rather than wrapping nothing",
	      [NSColor colorWithCGColor:NULL] == nil);

	/* --- components round-trip, and ALPHA IS COUNTED -------------------------------- */
	{
		NSColor *q = [NSColor colorWithDeviceRed:0.25 green:0.5 blue:0.75 alpha:0.5];

		check_num("a device-RGB colour reports its red", (double)[q redComponent], 0.25, 1e-9);
		check_num("...its green", (double)[q greenComponent], 0.5, 1e-9);
		check_num("...its blue", (double)[q blueComponent], 0.75, 1e-9);
		check_num("...and its alpha", (double)[q alphaComponent], 0.5, 1e-9);
		check("...and numberOfComponents counts ALPHA, so an RGB colour has four",
		      [q numberOfComponents] == 4);
		[q getRed:&r green:&g blue:&b alpha:&a];
		check("getRed:green:blue:alpha: fills all four pointers",
		      r == 0.25 && g == 0.5 && b == 0.75 && a == 0.5);
		/* THE POINTER FORM TOLERATES NIL, which is what its nullability says. */
		r = -1.0;
		[q getRed:&r green:NULL blue:NULL alpha:NULL];
		check_num("...and tolerates NULL for the ones the caller does not want", (double)r, 0.25,
			  1e-9);
	}

	/* --- DEVIATION ONE: THE GETTERS CONVERT THROUGH THE ENGINE ---------------------- */
	{
		/* A GRAY COLOUR HAS A RED COMPONENT HERE. Apple's documentation raises for this. */
		NSColor *w = [NSColor colorWithDeviceWhite:0.5 alpha:1.0];

		check("a gray colour has TWO components (gray and alpha)",
		      [w numberOfComponents] == 2);
		/* THE TOLERANCE IS A COLOUR-TRANSFORM TOLERANCE AND NOT AN EXACT ONE, which is the first
		 * thing this probe taught: 0.5 gray comes back as 0.5039, because the engine is converting
		 * between gamma-2.2 gray against D50 and sRGB. An exact expectation here is the mistake this
		 * file made on its first run — four times. */
		check_num("...and asking it for RED converts through the engine rather than raising "
			  "(a gamma difference, so not the same number)",
			  (double)[w redComponent], 0.5039, 0.01);
		check_num("...and its white component is what it was made with",
			  (double)[w whiteComponent], 0.5, 1e-9);

		/* AND THE OTHER DIRECTION, WHICH IS A REAL TRANSFORM AND NOT A COPY: a mid-gray RGB
		 * colour's white component is its LUMINANCE, which for equal RGB is the value itself. */
		check_num("an RGB colour has a white component (its luminance)",
			  (double)[[NSColor colorWithDeviceRed:0.5 green:0.5 blue:0.5 alpha:1.0]
				  whiteComponent],
			  0.5, 1e-2);
		/* AND SATURATED RED'S LUMINANCE IS NOWHERE NEAR 1, which is what makes the check above a
		 * measurement rather than an identity. */
		check("...and pure red's is NOT 1, so the conversion is doing something",
		      [[NSColor redColor] whiteComponent] < 0.6);
	}

	/* --- DEVIATION TWO: THE RAISE THAT REMAINS -------------------------------------- */
	check("a colour the engine CANNOT convert still raises rather than answering wrongly",
	      raised());

	/* --- the named spaces really are the named spaces ------------------------------- */
	{
		NSColor *device = [NSColor colorWithDeviceRed:1.0 green:0.0 blue:0.0 alpha:1.0];
		NSColor *srgb = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
		NSColor *p3 = [NSColor colorWithDisplayP3Red:1.0 green:0.0 blue:0.0 alpha:1.0];
		NSColor *gamma22 = [NSColor colorWithGenericGamma22White:0.5 alpha:1.0];

		check("SRGB IS THE sRGB PROFILE, not the device space: the spaces differ",
		      CGColorGetColorSpace([srgb CGColor]) != CGColorGetColorSpace([device CGColor]));
		check("...and Display P3 is a third one",
		      CGColorGetColorSpace([p3 CGColor]) != CGColorGetColorSpace([srgb CGColor]));
		check("...and the generic white is the gamma-2.2 gray profile, not device gray",
		      CGColorGetColorSpace([gamma22 CGColor])
			      != CGColorGetColorSpace([[NSColor colorWithDeviceWhite:0.5 alpha:1.0]
							      CGColor]));
		check_num("...while all four report the components they were made with",
			  (double)[p3 redComponent] + (double)[gamma22 whiteComponent], 0.5 + 1.0, 1e-6);
	}

	/* --- the documented consequence of the RGB-space deviation ---------------------- */
	{
		NSColor *device = [NSColor colorWithDeviceRed:0.5 green:0.5 blue:0.5 alpha:1.0];
		NSColor *calibrated = [NSColor colorWithCalibratedRed:0.5 green:0.5 blue:0.5 alpha:1.0];
		NSColor *generic = [NSColor colorWithRed:0.5 green:0.5 blue:0.5 alpha:1.0];

		check("the three RGB constructors resolve to ONE space, so equal numbers are EQUAL colours",
		      [device isEqual:calibrated] && [device isEqual:generic]);
		check("...and different numbers are not",
		      ![device isEqual:[NSColor colorWithDeviceRed:0.5 green:0.5 blue:0.6 alpha:1.0]]);
		check("...and a colour is not equal to something that is not a colour",
		      ![device isEqual:@"not a colour"]);
		check("...and equal colours hash alike", [device hash] == [calibrated hash]);
	}

	/* --- alpha, copying, and the components form ------------------------------------ */
	{
		NSColor *q = [NSColor colorWithDeviceRed:0.1 green:0.2 blue:0.3 alpha:1.0];
		NSColor *faded = [q colorWithAlphaComponent:0.25];

		check_num("-colorWithAlphaComponent: changes the alpha", (double)[faded alphaComponent],
			  0.25, 1e-9);
		check_num("...and NOT the red", (double)[faded redComponent], 0.1, 1e-9);
		check("-copy returns the same immutable object", [q copy] == q);
		check("...retained, so releasing the original leaves the copy alive", 1);

		memset(comp, 0, sizeof(comp));
		[q getComponents:comp];
		check("getComponents: gives the colour's OWN components with alpha last",
		      comp[0] == 0.1 && comp[1] == 0.2 && comp[2] == 0.3 && comp[3] == 1.0);
		[faded release];
	}

	/* --- the fifteen class colours, and the singletons ------------------------------ */
	{
		NSColor *red = [NSColor redColor];

		check("a class colour is a colour", red != nil);
		check_num("...and redColor is red", (double)[red redComponent], 1.0, 1e-9);
		check_num("...with green at zero", (double)[red greenComponent], 0.0, 1e-9);
		check("...and it is the SAME OBJECT every time, like the device colour spaces",
		      red == [NSColor redColor]);
		/* THE THREE DOCUMENTED GREYS ARE DEVICE-RGB COLOURS, so their RGB components are EXACT and
		 * their WHITE components are the same values only through the engine's gamma. Both are
		 * asserted, each with the tolerance its own claim deserves. */
		check_num("grayColor is 0.5 in RGB", (double)[[NSColor grayColor] redComponent], 0.5, 1e-9);
		check_num("...and its white component is that 0.5 converted, not copied",
			  (double)[[NSColor grayColor] whiteComponent], 0.4962, 0.01);
		check_num("darkGrayColor is 1/3 in RGB", (double)[[NSColor darkGrayColor] redComponent],
			  1.0 / 3.0, 1e-9);
		check_num("lightGrayColor is 2/3 in RGB", (double)[[NSColor lightGrayColor] redComponent],
			  2.0 / 3.0, 1e-9);
		check_num("...whose white components likewise cross the gamma",
			  (double)[[NSColor lightGrayColor] whiteComponent], 0.6608, 0.01);
		check_num("clearColor is black at alpha zero", (double)[[NSColor clearColor] alphaComponent],
			  0.0, 1e-9);
		check("whiteColor is white",
		      [[NSColor whiteColor] redComponent] == 1.0
			      && [[NSColor whiteColor] blueComponent] == 1.0);
		check("blackColor is black", [[NSColor blackColor] redComponent] == 0.0);
		/* THE OTHERS EXIST AND ARE DISTINCT, which is all that can be said about values Apple does
		 * not publish (`orangeColor`, `purpleColor`, `brownColor` are this library's numbers). */
		check("every one of the fifteen answers a colour",
		      [NSColor cyanColor] != nil && [NSColor yellowColor] != nil
			      && [NSColor magentaColor] != nil && [NSColor orangeColor] != nil
			      && [NSColor purpleColor] != nil && [NSColor brownColor] != nil
			      && [NSColor greenColor] != nil && [NSColor blueColor] != nil);
		c = [NSColor orangeColor];
		d = [NSColor purpleColor];
		check("...and two of them differ from each other", ![c isEqual:d]);
	}

	printf("APPKIT-COLOR: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
