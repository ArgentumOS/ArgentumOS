/*
 * coregraphics_gradient — the ramp, its two geometries, and the two extension options.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CHECKS ARE ARRANGED SO EVERY ONE OF THEM COULD FAIL FOR ITS OWN REASON. The interesting part of
 * a gradient is not that pixels appear — a solid fill does that — but WHERE the ramp's parameter is
 * zero and one, and WHAT HAPPENS PAST THOSE POINTS. So the ramp is always placed so that a PIXEL
 * CENTRE lands exactly on each end (the axis below is 7 units long and starts half a pixel in, which
 * is what makes device column 4 EXACTLY the first stop and column 11 EXACTLY the last). Without that
 * alignment the endpoint checks would be comparing interpolated values and would pass while the
 * endpoints were wrong.
 *
 * THE BACKGROUND IS NEVER BLACK-ON-BLACK WITH THE RAMP. It is opaque black and the ramp's stops are
 * red and blue, so "this pixel was left exactly as it was" and "this pixel was painted" are different
 * bytes — which is the whole of what the drawing options mean.
 *
 * THE CONTEXT'S Y IS FLIPPED (CGContext.h says why): user y grows upward, device row 0 is the TOP.
 * One check draws the same ramp vertically and asserts which END lands at which row, so the flip is
 * measured rather than assumed.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGGradient.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

static void check(const char *name, int ok)
{
	printf("CG-GRADIENT %-62s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	/* OPAQUE BLACK, SO "UNTOUCHED" IS A KNOWN PICTURE rather than a transparent surface a
	 * mistaken composite could also produce. */
	CGContextSetRGBFillColor(c, 0.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	return c;
}

/* B, G, R, A in memory — the chart `CGBitmapContextCreate` was given. */
static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)(y * W + x) * 4u, 4);
}

static void check_rgb(const char *name, CGContextRef c, int x, int y, double r, double g, double b)
{
	/* THE BOUNDS ARE INTEGERS AND NOT `unsigned char` CASTS, which is not tidiness: the very
	 * first run of this probe reported every gradient check as a FAILURE while printing
	 * identical "got" and "want" numbers, because `(unsigned char)(255 + 2)` WRAPS to 1 and the
	 * upper bound came out below the value being tested. A tolerance that has to be clamped to
	 * the channel's range must be clamped as an `int`. */
	int R = (int)(r + 0.5);
	int G = (int)(g + 0.5);
	int B = (int)(b + 0.5);

	pixel(c, x, y, p);
	if (p[2] < R - 2 || p[2] > R + 2 || p[1] < G - 2 || p[1] > G + 2 || p[0] < B - 2
	    || p[0] > B + 2) {
		printf("CG-GRADIENT %-62s FAIL (got R%d G%d B%d, want R%d G%d B%d)\n", name, p[2],
		       p[1], p[0], R, G, B);
		failures++;
		return;
	}
	printf("CG-GRADIENT %-62s ok (R%d G%d B%d)\n", name, p[2], p[1], p[0]);
}

static void check_untouched(const char *name, CGContextRef c, int x, int y)
{
	pixel(c, x, y, p);
	check(name, p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255);
}

/* RED TO BLUE, IN DEVICE RGB, WITH THE STOPS WHERE A CALLER PUTS THEM. */
static CGGradientRef red_blue(void)
{
	static const CGFloat stops[8] = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 };

	return CGGradientCreateWithColorComponents(CGColorSpaceCreateDeviceRGB(), stops, NULL, 2);
}

/* ------------------------------------------------------------------------- */

int main(void)
{
	CGGradientRef g;
	CGContextRef c;

	/* --- what a ramp refuses, each for its own reason -------------------------------- */
	{
		static const CGFloat one[4] = { 1.0, 0.0, 0.0, 1.0 };
		static const CGFloat two_high[8] = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 };
		static const CGFloat bad_loc[2] = { 0.0, 1.5 };
		static const CGFloat dec_loc[2] = { 0.8, 0.2 };

		check("a single stop is refused: there is no ramp to interpolate along",
		      CGGradientCreateWithColorComponents(CGColorSpaceCreateDeviceRGB(), one, NULL, 1) == NULL);
		check("a location above 1 is refused",
		      CGGradientCreateWithColorComponents(CGColorSpaceCreateDeviceRGB(), two_high, bad_loc,
							  2) == NULL);
		check("locations that decrease are refused",
		      CGGradientCreateWithColorComponents(CGColorSpaceCreateDeviceRGB(), two_high, dec_loc,
							  2) == NULL);
		check("a NULL components array is refused",
		      CGGradientCreateWithColorComponents(CGColorSpaceCreateDeviceRGB(), NULL, NULL, 2) == NULL);
		check("a NULL colour space is refused",
		      CGGradientCreateWithColorComponents(NULL, two_high, NULL, 2) == NULL);
		check("an evenly spaced two-stop ramp is built", red_blue() != NULL);
	}

	/* --- the linear ramp, and what the two options do past its ends ------------------ */
	/* THE AXIS IS 7 UNITS LONG AND STARTS HALF A PIXEL IN: user x from 4.5 to 11.5, so device
	 * column 4 (whose centre is user x 4.5) is EXACTLY the first stop and column 11 EXACTLY the
	 * last, while columns 0…3 are before the start and 12…15 after the end. */
	g = red_blue();

	c = fresh();
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_rgb("linear, both ends extended: before the start is the FIRST stop", c, 0, 7, 255, 0, 0);
	check_rgb("...the start itself is the first stop", c, 4, 7, 255, 0, 0);
	check_rgb("...the middle is the midpoint of the two stops", c, 7, 7, 146, 0, 109);
	check_rgb("...the end itself is the last stop", c, 11, 7, 0, 0, 255);
	check_rgb("...after the end is the LAST stop", c, 15, 7, 0, 0, 255);
	CGContextRelease(c);

	c = fresh();
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    (CGGradientDrawingOptions)0);
	check_untouched("linear, neither option: before the start is left exactly as it was", c, 0, 7);
	check_rgb("...and the ramp itself is still painted at the start", c, 4, 7, 255, 0, 0);
	check_rgb("...and at the end", c, 11, 7, 0, 0, 255);
	check_untouched("...and after the end is left exactly as it was", c, 15, 7);
	CGContextRelease(c);

	c = fresh();
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsBeforeStartLocation);
	check_rgb("linear, before only: before the start IS painted", c, 0, 7, 255, 0, 0);
	check_untouched("...and after the end is NOT — the two ends are independent", c, 15, 7);
	CGContextRelease(c);

	c = fresh();
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsAfterEndLocation);
	check_untouched("linear, after only: before the start is NOT painted", c, 0, 7);
	check_rgb("...and after the end IS", c, 15, 7, 0, 0, 255);
	CGContextRelease(c);

	/* --- the axis is the caller's, not the page's: the same ramp drawn VERTICALLY ----- */
	c = fresh();
	CGContextDrawLinearGradient(c, g, CGPointMake(8.5, 4.5), CGPointMake(8.5, 11.5),
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_rgb("a vertical ramp puts the first stop where user y is small (device row 11)", c, 8, 11,
		  255, 0, 0);
	check_rgb("...and the last stop where user y is large (device row 4, the TOP of the image)", c, 8,
		  4, 0, 0, 255);
	CGContextRelease(c);

	/* --- the radial ramp, concentric ------------------------------------------------- */
	c = fresh();
	CGContextDrawRadialGradient(c, g, CGPointMake(7.5, 8.5), 0.0, CGPointMake(7.5, 8.5), 8.0,
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_rgb("radial, concentric: the centre is the first stop", c, 7, 7, 255, 0, 0);
	check_rgb("...eight units out is the last stop", c, 15, 7, 0, 0, 255);
	check_rgb("...seven units out is seven eighths of the way", c, 0, 7, 32, 0, 223);
	CGContextRelease(c);

	c = fresh();
	CGContextDrawRadialGradient(c, g, CGPointMake(7.5, 8.5), 0.0, CGPointMake(7.5, 8.5), 8.0,
				    (CGGradientDrawingOptions)0);
	check_rgb("radial, neither option: the centre is painted (its parameter is zero, not past "
		  "it)", c, 7, 7, 255, 0, 0);
	check_untouched("...and a corner beyond the outer circle is NOT (its parameter is past the "
			"end)", c, 0, 0);
	CGContextRelease(c);

	/* --- the radial ramp where the quadratic loses its leading term ------------------ */
	/* THE CONE THAT IS REALLY A HALF-SPACE: the circles are offset by exactly the difference in
	 * their radii, so the parameter is LINEAR rather than the root of a quadratic. This is the
	 * branch a first draft divides by zero on. */
	c = fresh();
	CGContextDrawRadialGradient(c, g, CGPointMake(0.5, 8.5), 0.0, CGPointMake(8.5, 8.5), 8.0,
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_rgb("radial, the degenerate cone: its point is the first stop", c, 0, 7, 255, 0, 0);
	check_rgb("...and half way along it is half way through the ramp", c, 8, 7, 128, 0, 128);
	CGContextRelease(c);

	/* --- the conic ramp, and its wrap ------------------------------------------------ */
	c = fresh();
	CGContextDrawConicGradient(c, g, CGPointMake(7.5, 8.5), 0.0);
	check_rgb("conic: angle zero (to the right of the centre) is the first stop", c, 8, 7, 255, 0, 0);
	check_rgb("...a quarter turn is a quarter through the ramp", c, 7, 6, 191, 0, 64);
	check_rgb("...half a turn is half way", c, 6, 7, 128, 0, 128);
	check_rgb("...and three quarters of a turn WRAPS rather than clamping", c, 7, 8, 64, 0, 191);
	CGContextRelease(c);

	/* --- the context's alpha reaches the paint --------------------------------------- */
	c = fresh();
	CGContextSetAlpha(c, 0.5);
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_rgb("CGContextSetAlpha multiplies the ramp: opaque red at half alpha lands at 128", c, 4,
		  7, 128, 0, 0);
	CGContextRelease(c);

	/* --- the paint is bounded by the CLIP, and the current path is untouched ---------- */
	c = fresh();
	CGContextClipToRect(c, CGRectMake(4.0, 4.0, 8.0, 8.0));
	CGContextMoveToPoint(c, 0.0, 0.0);
	CGContextAddLineToPoint(c, 1.0, 1.0);
	CGContextDrawLinearGradient(c, g, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_untouched("a clipped gradient does not paint outside the clip", c, 0, 7);
	check_rgb("...and does paint inside it", c, 7, 7, 146, 0, 109);
	check("a gradient draw leaves the CURRENT PATH alone (no BeginPath is implied)",
	      !CGContextIsPathEmpty(c));
	CGContextRelease(c);

	/* --- a NULL gradient is refused, not drawn as nothing ---------------------------- */
	c = fresh();
	CGContextDrawLinearGradient(c, NULL, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5),
				    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
	check_untouched("a NULL gradient paints nothing", c, 7, 7);
	CGContextRelease(c);

	/* --- the object is counted ------------------------------------------------------- */
	g = red_blue();
	check("a gradient is a counted object", CGGradientRetain(g) == g);
	CGGradientRelease(g);
	CGGradientRelease(g);
	check("...and releasing it to zero is safe", 1);

	/* --- AND THE CLIP'S MASK HALF REACHES THE CLIP-ONLY PAINTS, NOT ONLY THE FILLS -------------- */
	/* A gradient does not go through the trapezoid path: `cg_paint_clip` composites a sampled image
	 * through the mask slot directly. A mask that worked for fills and silently not for gradients
	 * would be worse than none, so this is checked in its own probe.
	 *
	 * THE PATH IS THE USER-SPACE TRIANGLE (0,0),(16,0),(0,16), whose DEVICE form (the CTM flips y)
	 * covers the half-surface py >= px — so the pixel to assert on is a DEVICE one: (14,2) is outside
	 * it by a wide margin and (2,14) is well inside. REASONED IN DEVICE SPACE ON PURPOSE, because this
	 * probe's `pixel()` reads the raw row and a user-space guess is what has cost me twice today. */
	{
		CGGradientRef grd = red_blue();
		CGContextRef g = fresh();
		unsigned char in[4];

		CGContextBeginPath(g);
		CGContextMoveToPoint(g, 0.0, 0.0);
		CGContextAddLineToPoint(g, 16.0, 0.0);
		CGContextAddLineToPoint(g, 0.0, 16.0);
		CGContextClosePath(g);
		CGContextClip(g);
		CGContextDrawLinearGradient(g, grd, CGPointMake(0.0, 0.0), CGPointMake(16.0, 16.0), 0);
		check_untouched("a gradient under a CURVED clip does not reach a pixel outside it", g, 14, 2);
		pixel(g, 2, 14, in);
		check("...and it DOES reach one inside the same clip", !(in[0] == 0 && in[1] == 0 && in[2] == 0));
		CGGradientRelease(grd);
		CGContextRelease(g);
	}

	printf("CG-GRADIENT: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
