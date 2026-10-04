/*
 * coregraphics_shading — a ramp whose colours are COMPUTED, and the callout contract around it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TWO THINGS A SHADING ADDS TO A GRADIENT ARE WHAT THIS PROBE IS FOR. The first is WHERE THE
 * COLOURS COME FROM: a caller's function is called with the ramp's parameter, so the probe counts the
 * calls and records the parameters it was given — a library that called it with a DISTANCE instead of
 * a fraction, or called it for pixels outside the ramp, would pass a colour check and fail these. The
 * sharpest of them is exact: with the ends NOT extended and an axis 7 units long starting half a
 * pixel in, the painted columns are device 4…11 and the function must be called 8 × 16 = 128 times —
 * NOT 256, because the 128 pixels beyond the ramp's ends have no parameter to report.
 *
 * The second is the CONTRACT AROUND THE CALLER'S POINTERS: `info` comes back through `evaluate` and
 * through `releaseInfo` exactly once, at the moment the last reference to the function goes — which
 * the probe pins by hand, retaining the function, releasing it, and checking that nothing was freed
 * while a shading still held it.
 *
 * THE GEOMETRY ITSELF IS NOT RE-CHECKED IN PIXELS HERE. The axial and radial parameters are
 * CGPaint.c's, shared with the gradient, and coregraphics_gradient.c already pins them against exact
 * endpoint-aligned columns — so this probe checks ONE endpoint and one interior value per geometry
 * and spends its checks on what is new.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGFunction.h>
#include <CoreGraphics/CGShading.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

typedef struct {
	int calls;
	CGFloat min_t;
	CGFloat max_t;
	int released;
} probe_info;

static void check(const char *name, int ok)
{
	printf("CG-SHADING %-62s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_rgb(const char *name, CGContextRef c, int x, int y, int r, int g, int b)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(p, d + (size_t)(y * W + x) * 4u, 4);
	if (p[2] < r - 2 || p[2] > r + 2 || p[1] < g - 2 || p[1] > g + 2 || p[0] < b - 2
	    || p[0] > b + 2) {
		printf("CG-SHADING %-62s FAIL (got R%d G%d B%d, want R%d G%d B%d)\n", name, p[2], p[1],
		       p[0], r, g, b);
		failures++;
		return;
	}
	printf("CG-SHADING %-62s ok (R%d G%d B%d)\n", name, p[2], p[1], p[0]);
}

static void check_untouched(const char *name, CGContextRef c, int x, int y)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(p, d + (size_t)(y * W + x) * 4u, 4);
	check(name, p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255);
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	CGContextSetRGBFillColor(c, 0.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	return c;
}

/* RED AT THE START, GREEN AT THE END, BLUE NOWHERE — the same ramp the gradient probe uses, so the
 * pixel values are comparable between the two probes and a difference means the shading, not the
 * arithmetic. The parameter is recorded so the callout's argument can be checked. */
static void ramp_evaluate(void *info, const CGFloat *in, CGFloat *out)
{
	probe_info *pr = info;
	CGFloat t = in[0];

	pr->calls++;
	if (pr->calls == 1 || t < pr->min_t) {
		pr->min_t = t;
	}
	if (pr->calls == 1 || t > pr->max_t) {
		pr->max_t = t;
	}
	out[0] = 1.0 - t;
	out[1] = t;
	out[2] = 0.0;
}

static void info_release(void *info)
{
	((probe_info *)info)->released++;
}

static CGFunctionRef ramp_function(probe_info *pr, const CGFloat *range)
{
	CGFunctionCallbacks cb;

	cb.version = 0;
	cb.evaluate = ramp_evaluate;
	cb.releaseInfo = info_release;
	return CGFunctionCreate(pr, 1, NULL, 3, range, &cb);
}

/* A FUNCTION THAT ASKS FOR MORE THAN IT MAY HAVE: it writes the parameter straight into green, so a
 * declared range of 0…0.5 has to stop it half way. */
static void over_evaluate(void *info, const CGFloat *in, CGFloat *out)
{
	(void)info;
	out[0] = 0.0;
	out[1] = in[0];
	out[2] = 0.0;
}

static CGFunctionRef over_function(const CGFloat *range)
{
	CGFunctionCallbacks cb;

	cb.version = 0;
	cb.evaluate = over_evaluate;
	cb.releaseInfo = NULL;
	return CGFunctionCreate(NULL, 1, NULL, 3, range, &cb);
}

int main(void)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGFunctionRef fn;
	CGShadingRef s;
	CGContextRef c;
	static probe_info pr;
	static probe_info clamp_info;

	/* --- what a function refuses ------------------------------------------------------ */
	{
		CGFunctionCallbacks v1 = { 1, ramp_evaluate, NULL };
		CGFunctionCallbacks noeval = { 0, NULL, NULL };

		check("a NULL callbacks pointer is refused",
		      CGFunctionCreate(NULL, 1, NULL, 3, NULL, NULL) == NULL);
		check("version 1 is refused rather than read as version 0",
		      CGFunctionCreate(NULL, 1, NULL, 3, NULL, &v1) == NULL);
		check("callbacks with no evaluate are refused",
		      CGFunctionCreate(NULL, 1, NULL, 3, NULL, &noeval) == NULL);
		{
			CGFunctionCallbacks ok = { 0, ramp_evaluate, NULL };

			check("a zero dimension is refused",
			      CGFunctionCreate(NULL, 0, NULL, 3, NULL, &ok) == NULL);
			check("...and so is a zero range dimension",
			      CGFunctionCreate(NULL, 1, NULL, 0, NULL, &ok) == NULL);
		}
	}

	/* --- the axial shading, both ends extended --------------------------------------- */
	/* THE SAME AXIS THE GRADIENT PROBE USES: 7 units long, half a pixel in, so device column 4 is
	 * EXACTLY parameter 0 and column 11 EXACTLY 1. */
	memset(&pr, 0, sizeof(pr));
	fn = ramp_function(&pr, NULL);
	s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, true, true);
	check("an axial shading is built", s != NULL);
	c = fresh();
	CGContextDrawShading(c, s);
	check_rgb("axial, both extended: the start is the function at parameter zero", c, 4, 7, 255, 0,
		  0);
	check_rgb("...the end is the function at parameter one", c, 11, 7, 0, 255, 0);
	check_rgb("...halfway is the function at the midpoint", c, 7, 7, 146, 109, 0);
	check_rgb("...past the start is clamped to parameter zero", c, 0, 7, 255, 0, 0);
	check_rgb("...past the end is clamped to parameter one", c, 15, 7, 0, 255, 0);
	check("...and the function saw only parameters in 0…1", pr.min_t >= 0.0 && pr.max_t <= 1.0);
	check("...and was called once per pixel of the whole surface", pr.calls == W * H);
	CGContextRelease(c);
	CGShadingRelease(s);

	/* --- the callout is called ONLY where the ramp reaches --------------------------- */
	memset(&pr, 0, sizeof(pr));
	fn = ramp_function(&pr, NULL);
	s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, false, false);
	c = fresh();
	CGContextDrawShading(c, s);
	check_untouched("axial, not extended: before the start is left exactly as it was", c, 0, 7);
	check_untouched("...and after the end likewise", c, 15, 7);
	check_rgb("...and the ramp itself is painted", c, 4, 7, 255, 0, 0);
	/* 8 PAINTED COLUMNS × 16 ROWS, AND NOT 256: the function is not called for a pixel that has no
	 * parameter, which is what "the callout follows the geometry" means and what a check on the
	 * pixels alone could not see. */
	check("...and the function was called for the 8 painted columns only, not the whole surface",
	      pr.calls == 8 * H);
	check("...with its smallest parameter at zero and its largest at one",
	      pr.min_t < 1e-9 && pr.max_t > 1.0 - 1e-9);
	CGContextRelease(c);
	CGShadingRelease(s);

	/* --- the radial shading, concentric ---------------------------------------------- */
	memset(&pr, 0, sizeof(pr));
	fn = ramp_function(&pr, NULL);
	s = CGShadingCreateRadial(space, CGPointMake(7.5, 8.5), 0.0, CGPointMake(7.5, 8.5), 8.0, fn,
				  true, true);
	check("a radial shading is built", s != NULL);
	c = fresh();
	CGContextDrawShading(c, s);
	check_rgb("radial, concentric: the centre is the function at parameter zero", c, 7, 7, 255, 0, 0);
	check_rgb("...eight units out is parameter one", c, 15, 7, 0, 255, 0);
	check_rgb("...seven units out is seven eighths of the way", c, 0, 7, 32, 223, 0);
	CGContextRelease(c);
	CGShadingRelease(s);

	/* --- THE RANGE CLAMPS, which is what the range parameter is FOR ------------------ */
	{
		static const CGFloat half[6] = { 0.0, 1.0, 0.0, 0.5, 0.0, 1.0 };

		fn = over_function(half);
		s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, true,
					 true);
		c = fresh();
		CGContextDrawShading(c, s);
		check_rgb("a declared range stops the function: green is held at 0.5, so 128", c, 11, 7, 0,
			  128, 0);
		CGContextRelease(c);
		CGShadingRelease(s);

		fn = over_function(NULL);
		s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, true,
					 true);
		c = fresh();
		CGContextDrawShading(c, s);
		check_rgb("...and with NO range declared the same function reaches 255", c, 11, 7, 0, 255,
			  0);
		CGContextRelease(c);
		CGShadingRelease(s);
	}

	/* --- the ownership rule: a shading keeps the function alive ---------------------- */
	memset(&pr, 0, sizeof(pr));
	fn = ramp_function(&pr, NULL);
	s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, true, true);
	CGFunctionRetain(fn);
	CGFunctionRelease(fn);
	check("releasing the caller's reference does NOT release the function — the shading holds it",
	      pr.released == 0);
	CGFunctionRelease(fn);
	check("...and the shading's reference is the one that keeps it", pr.released == 0);
	c = fresh();
	CGContextDrawShading(c, s);
	check_rgb("...and the shading still draws correctly afterwards", c, 4, 7, 255, 0, 0);
	CGContextRelease(c);
	CGShadingRelease(s);
	check("releasing the shading fires the caller's releaseInfo exactly once", pr.released == 1);

	/* --- what a shading refuses ------------------------------------------------------ */
	memset(&clamp_info, 0, sizeof(clamp_info));
	fn = ramp_function(&pr, NULL);
	{
		CGFunctionCallbacks three_out = { 0, ramp_evaluate, NULL };
		CGFunctionRef four;

		check("a NULL space is refused",
		      CGShadingCreateAxial(NULL, CGPointMake(0, 0), CGPointMake(1, 0), fn, false, false)
			      == NULL);
		check("a NULL function is refused",
		      CGShadingCreateAxial(space, CGPointMake(0, 0), CGPointMake(1, 0), NULL, false,
					   false) == NULL);
		/* A FUNCTION OF TWO PARAMETERS IS NOT A SHADING'S FUNCTION. */
		four = CGFunctionCreate(&clamp_info, 2, NULL, 3, NULL, &three_out);
		check("a function whose domain is not one-dimensional is refused",
		      CGShadingCreateAxial(space, CGPointMake(0, 0), CGPointMake(1, 0), four, false,
					   false) == NULL);
		CGFunctionRelease(four);
		/* AND SO IS ONE WHOSE OUTPUT DOES NOT FIT THE SPACE. */
		four = CGFunctionCreate(&clamp_info, 1, NULL, 2, NULL, &three_out);
		check("a function whose range does not match the space's components is refused",
		      CGShadingCreateAxial(space, CGPointMake(0, 0), CGPointMake(1, 0), four, false,
					   false) == NULL);
		CGFunctionRelease(four);
	}
	CGFunctionRelease(fn);

	/* --- a NULL shading is refused, not drawn as nothing ---------------------------- */
	c = fresh();
	CGContextDrawShading(c, NULL);
	check_untouched("a NULL shading paints nothing", c, 7, 7);
	CGContextRelease(c);

	/* --- the context's alpha reaches the paint --------------------------------------- */
	memset(&pr, 0, sizeof(pr));
	fn = ramp_function(&pr, NULL);
	s = CGShadingCreateAxial(space, CGPointMake(4.5, 8.5), CGPointMake(11.5, 8.5), fn, true, true);
	c = fresh();
	CGContextSetAlpha(c, 0.5);
	CGContextDrawShading(c, s);
	check_rgb("CGContextSetAlpha multiplies a shading too: opaque red at half alpha lands at 128", c,
		  4, 7, 128, 0, 0);
	CGContextRelease(c);
	CGShadingRelease(s);
	CGFunctionRelease(fn);

	CGColorSpaceRelease(space);
	printf("CG-SHADING: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
