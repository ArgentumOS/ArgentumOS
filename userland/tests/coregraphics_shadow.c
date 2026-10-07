/*
 * coregraphics_shadow — the shadow, and the blur that is really a blur.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SHADOW IS MEASURED ON AN OPAQUE WHITE CANVAS SO ITS COLOUR IS READABLE: `CGContextSetShadow` is defined
 * by Apple as a call with BLACK AT ONE THIRD ALPHA, so a shadow pixel over white is about two thirds white —
 * a number that is neither the canvas nor the shape, which is what makes it a shadow rather than a copy.
 *
 * AND THE BLUR IS ASKED FOR AS A PAIR, AT ONE PIXEL: with `blur` 0 the shadow's edge is hard, so a pixel a few
 * units past the offset square is pure canvas; with `blur` 3 the SAME pixel must be darker than the canvas and
 * darker than the same pixel was without the blur. A "blur" that merely faded or moved the shadow cannot make
 * both of those true, and the reach is checked one step further out as well.
 *
 * THE HOOK IS THE PATH PAINT, SO THIS IS A FILL; images keep their own sampling loop and are not shadowed yet,
 * which is stated in CGContext.h rather than hidden.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-SHADOW %-64s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 16
#define H 8
#define STRIDE (W * 4)

/* BGRA in memory: the mean of the three colour channels is what "how dark" means here. */
static int darkness_at(const unsigned char *buf, int x)
{
	size_t i = (size_t)x * 4;

	return (int)((buf[i] + buf[i + 1] + buf[i + 2]) / 3);
}

static void white_canvas(CGContextRef c, unsigned char *canvas)
{
	memset(canvas, 0, (size_t)STRIDE * H);
	CGContextSetRGBFillColor(c, 1, 1, 1, 1);
	CGContextFillRect(c, CGRectMake(0, 0, W, H));
}

/* A FULL-HEIGHT BAND AND NOT A SQUARE, BECAUSE THE READINGS ARE TAKEN ON ROW 0: this library's bitmap rows do
 * not run the way its y axis does, so a shape that occupies four rows makes every check a check on which row
 * was read. The QUESTION here is about X — where the offset puts the shadow and how far a blur spreads it — so
 * the shape spans every row and row 0 answers for all of them. THE FIRST VERSION OF THIS PROBE USED A SQUARE AT
 * y IN [2, 4) AND READ ROW 0, where even the shape itself was invisible: every readout was 255. */
static void a_band(CGContextRef c)
{
	CGContextSetRGBFillColor(c, 0, 0, 0, 1);
	CGContextFillRect(c, CGRectMake(1, 0, 2, H));
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	int beyond_hard, beyond_blurred, far_hard, far_blurred, shadow;

	check("a context is made", c != NULL);

	/* --- a hard shadow, offset in x, in black at one third alpha ------------------------------- */
	CGContextSetShadow(c, CGSizeMake(3, 0), 0);
	white_canvas(c, canvas);
	a_band(c);
	shadow = darkness_at(canvas, 5);	/* 3 to the right of the square's 1..3 */
	beyond_hard = darkness_at(canvas, 7);	/* past the shadow's own 3-unit reach */
	far_hard = darkness_at(canvas, 8);	/* two past the hard shadow's edge, within a blur's reach */
	printf("CG-SHADOW %-64s hard: shadow=%d beyond=%d far=%d square=%d\n", "...readout", shadow,
	       beyond_hard, far_hard, darkness_at(canvas, 1));
	check("the shape itself is painted as usual, opaque", darkness_at(canvas, 1) == 0);
	check("...and a shadow of BLACK AT ONE THIRD appears where the offset puts it",
	      shadow > 140 && shadow < 200);
	check("...and reaches no further than the square's own width",
	      beyond_hard > 240 && far_hard > 240);

	/* --- the same, blurred: the reach grows and the near edge lightens ------------------------- */
	CGContextSetShadow(c, CGSizeMake(3, 0), 3);
	white_canvas(c, canvas);
	a_band(c);
	beyond_blurred = darkness_at(canvas, 7);
	far_blurred = darkness_at(canvas, 8);
	printf("CG-SHADOW %-64s blurred: beyond=%d far=%d shadow=%d\n", "...readout", beyond_blurred,
	       far_blurred, darkness_at(canvas, 5));
	check("a blur of 3 makes the SAME pixel past the hard edge darker than the canvas",
	      beyond_blurred < 250);
	check("...growing the reach: that pixel was pure canvas without the blur", beyond_blurred < beyond_hard);
	check("...and it spreads further out, one step beyond the hard shadow's whole extent",
	      far_blurred < far_hard);
	check("...while the shape itself is still untouched by its own shadow", darkness_at(canvas, 1) == 0);

	/* --- the state: a shadow is a gstate parameter, and OFF is a transparent colour ------------ */
	/* A KNOWN STATE FIRST, AND THE SAME ONE THE CHECKS ABOVE USED. Everything from here on is about
	 * what save and restore carry, and the section above left a BLURRED shadow set — so a restored
	 * reading has to be compared against a state this probe chose, not against whichever one the
	 * previous section happened to leave behind. Asserting the hard shadow's reading after the restore
	 * is then a real question: "did the blur come back?" would be, too, but only if the blur were
	 * still what was saved. */
	CGContextSetShadow(c, CGSizeMake(3, 0), 0);
	{
		CGContextSaveGState(c);
		CGContextSetShadowWithColor(c, CGSizeMake(3, 0), 0, NULL);
		white_canvas(c, canvas);
		a_band(c);
		check("a NULL shadow colour turns shadowing OFF, as Apple's header says it does",
		      darkness_at(canvas, 5) > 240 && darkness_at(canvas, 7) > 240);
		CGContextRestoreGState(c);
	}
	white_canvas(c, canvas);
	a_band(c);
	check("...and the shadow the save/restore protected is back afterwards",
	      darkness_at(canvas, 5) > 140 && darkness_at(canvas, 5) < 200);

	/* --- and what is refused ------------------------------------------------------------------- */
	/* THE SAME KNOWN STATE, because a refusal is only observable as "the state did NOT become what
	 * this call asked for" — and this call asks for two things that would each show: an offset of 1
	 * (which would move the shadow off x=5) and a NULL colour (which is OFF, and would leave x=5 as
	 * bare canvas). Both readings are outside the range asserted below, so the check fails if EITHER
	 * half was adopted. */
	CGContextSetShadowWithColor(c, CGSizeMake(1, 0), -1.0, NULL);
	white_canvas(c, canvas);
	a_band(c);
	check("a NEGATIVE blur is refused, in Apple's words (\"a non-negative number\")",
	      darkness_at(canvas, 5) > 140 && darkness_at(canvas, 5) < 200);
	CGContextSetShadow(NULL, CGSizeMake(1, 1), 1.0);
	CGContextSetShadowWithColor(NULL, CGSizeMake(1, 1), 1.0, NULL);
	check("...and a NULL context is a no-op rather than a crash", 1);

	CGContextRelease(c);
	CGColorSpaceRelease(rgb);
	printf("CG-SHADOW: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
