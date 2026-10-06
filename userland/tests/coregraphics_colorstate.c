/*
 * coregraphics_colorstate — the component colour doors, and the space that gives their numbers meaning.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE POINT OF THESE DOORS IS THAT `components` MEANS NOTHING ON ITS OWN: Apple's header says the array holds
 * "N color components + 1 alpha component" where N comes from THE CURRENT COLOR SPACE, so a check that only
 * ever asks for RGB cannot tell a working door from one that ignores the space. Here the same two numbers,
 * {0.5, 1.0}, are set under two different spaces and the PIXELS have to differ.
 *
 * AND APPLE'S SPACE-SETTING DOOR HAS A DOCUMENTED SIDE EFFECT — "set the fill color to a default value
 * appropriate for the color space" — so a colour that was red before the space changed must be black after it,
 * without anyone calling a colour door in between. That is the second thing these checks measure.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-COLORSTATE %-60s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 8
#define H 8
#define STRIDE (W * 4)

/* This library's one bitmap chart is BGRA in memory: blue, green, red, alpha. */
static unsigned char at(const unsigned char *buf, int x, int y, int c)
{
	return buf[(size_t)y * STRIDE + (size_t)x * 4 + (size_t)c];
}

static const unsigned char *row0(const unsigned char *buf)
{
	return buf;
}

int main(void)
{
	unsigned char canvas[STRIDE * H];
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
	CGColorSpaceRef cmyk = CGColorSpaceCreateDeviceCMYK();
	CGContextRef c = CGBitmapContextCreate(canvas, W, H, 8, STRIDE, rgb,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	const CGFloat red[4] = { 1.0, 0.0, 0.0, 1.0 };
	const CGFloat half[2] = { 0.5, 1.0 };
	const CGFloat black_ink[2] = { 0.0, 1.0 };

	check("a context is made", c != NULL);
	if (c == NULL) {
		return 1;
	}

	/* --- the default space is RGB, and the four components are r, g, b, a --------------------- */
	{
		CGContextSetFillColor(c, red);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("with no space set, four components are RGB and paint red",
		      at(row0(canvas), 2, 2, 2) == 0xFF && at(row0(canvas), 2, 2, 1) == 0x00
		      && at(row0(canvas), 2, 2, 0) == 0x00 && at(row0(canvas), 2, 2, 3) == 0xFF);
	}

	/* --- APPLE'S SIDE EFFECT: changing the space changes the colour ---------------------------- */
	{
		CGContextSetFillColorSpace(c, gray);
		memset(canvas, 0, sizeof canvas);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("setting a colour space sets the colour to the space's DEFAULT, which is black",
		      at(row0(canvas), 2, 2, 2) == 0x00 && at(row0(canvas), 2, 2, 3) == 0xFF);
	}

	/* --- and the space is what makes the components mean something ---------------------------- */
	{
		CGContextSetFillColor(c, half);		/* one colour component + alpha, in GRAY */
		memset(canvas, 0, sizeof canvas);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		{
			unsigned char r = at(row0(canvas), 2, 2, 2);

			printf("CG-COLORSTATE %-60s gray0.5 r=%u g=%u b=%u\n", "...readout", r,
			       at(row0(canvas), 2, 2, 1), at(row0(canvas), 2, 2, 0));
			check("...so the SAME shape of array {0.5, 1.0} is mid-gray here, and feeds all three channels",
			      r > 0x70 && r < 0x90 && at(row0(canvas), 2, 2, 1) == r
			      && at(row0(canvas), 2, 2, 0) == r);
		}
	}

	/* --- the stroke half, which has its own space and its own colour -------------------------- */
	{
		CGContextSetStrokeColorSpace(c, gray);
		CGContextSetStrokeColor(c, black_ink);
		/* A BAND WIDER THAN THE RECTANGLE WOULD PAINT ITS INTERIOR TOO — a 4-wide stroke on a 4-wide
		 * rectangle leaves nothing unpainted — so the rectangle is big enough to have an inside, and
		 * the two sample points are chosen to survive this library's bitmap rows: the CANVAS CORNER is
		 * within the band whichever way the rows run, and the middle is the middle either way. */
		CGContextSetLineWidth(c, 2.0);
		memset(canvas, 0, sizeof canvas);
		CGContextStrokeRect(c, CGRectMake(1, 1, 6, 6));
		check("a gray stroke colour paints an opaque black band",
		      at(row0(canvas), 0, 0, 2) == 0x00 && at(row0(canvas), 0, 0, 3) == 0xFF);
		check("...and the middle of the rectangle is left alone", at(row0(canvas), 4, 4, 3) == 0);
	}

	/* --- and a space this library cannot read is refused rather than stored ------------------- */
	{
		CGContextSetFillColor(c, half);		/* make the current colour known: mid-gray */
		CGContextSetFillColorSpace(c, cmyk);
		memset(canvas, 0, sizeof canvas);
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		check("a CMYK space is REFUSED BY NAME and the colour in use is left alone",
		      at(row0(canvas), 2, 2, 2) > 0x70 && at(row0(canvas), 2, 2, 2) < 0x90);
	}

	/* --- and nothing crashes on nothing ------------------------------------------------------- */
	{
		CGContextSetFillColor(NULL, red);
		CGContextSetFillColor(c, NULL);
		CGContextSetStrokeColor(NULL, red);
		CGContextSetStrokeColor(c, NULL);
		CGContextSetFillColorSpace(c, NULL);
		CGContextSetStrokeColorSpace(c, NULL);
		CGContextSetFillColorSpace(NULL, gray);
		check("...where a NULL context, a NULL array or a NULL space is a no-op rather than a crash", 1);
	}

	CGContextRelease(c);
	CGColorSpaceRelease(cmyk);
	CGColorSpaceRelease(gray);
	CGColorSpaceRelease(rgb);
	printf("CG-COLORSTATE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
