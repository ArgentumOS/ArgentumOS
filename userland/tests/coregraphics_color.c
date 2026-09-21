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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

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

	printf("CG-COLOR: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
