/*
 * coregraphics_text — the text state, and the one drawing door.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS PROBE MEASURES THAT A COMPILE CANNOT: that a glyph's INK lands where the text matrix, the
 * CTM and the pen say; that the two matrices MULTIPLY rather than one being read once; that the modes
 * this slice does not draw are REFUSED rather than silently filled; and that the font survives the
 * graphics-state stack — the ownership the context now has, checked by putting NULL in the current
 * state and restoring it out of the stack.
 *
 * THE SURFACE IS READ THROUGH `CGBitmapContextGetData`, WHICH IS A LESSON THIS PROBE COST: the array
 * handed to `CGBitmapContextCreate` is the one the context paints into today (the probe asserts the
 * two pointers are equal), but a probe that reads its OWN array is asserting an implementation
 * detail of the context, and the first version of this file measured an empty surface for reasons
 * that had nothing to do with the drawing.
 *
 * AND ONE CHECK IS A REGRESSION GUARD FOR A MEASURED GUEST DEFECT: two sizes on ONE font (16 then 32).
 * The toolkit's face-per-size arrangement hit the guest's open limit and failed with FreeType error 2
 * on a byte-perfect font; this library sizes one face per use, and the check that the 32pt glyph is
 * TALLER than the 16pt one is what says so from the outside.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 160
#define H 80

static unsigned char *surface;	/* the caller's array; the context's data is checked against it */
static const unsigned char *paint;
static int failures;

static void check(const char *name, int ok)
{
	printf("CG-TEXT %-68s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* BGRA IN MEMORY (kCGImageAlphaPremultipliedFirst | 32Little), so byte 2 is red. */
static void count_ink(int *n, int *x0, int *y0, int *x1, int *y1)
{
	int x, y;

	*n = 0;
	*x0 = W;
	*y0 = H;
	*x1 = -1;
	*y1 = -1;
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			const unsigned char *p = paint + ((size_t)y * W + x) * 4;

			if (p[2] < 128) {
				(*n)++;
				if (x < *x0) *x0 = x;
				if (y < *y0) *y0 = y;
				if (x > *x1) *x1 = x;
				if (y > *y1) *y1 = y;
			}
		}
	}
}

/* THE INSTRUMENT FOR SUBPIXEL SHIFTS: the ink's COVERAGE-WEIGHTED CENTROID. An integer pixel count is
 * too coarse to see a half-pixel move, and the weighting is EXACT here rather than approximate: the
 * surface is white, the ink is black, and a composite of black over white is linear in the red channel,
 * so (255 - R)/255 IS the coverage. */
static void ink_centroid(double *cx, double *cy, int *weight)
{
	double sx = 0.0, sy = 0.0, w = 0.0;
	int x, y;

	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			const unsigned char *p = paint + ((size_t)y * W + x) * 4;
			double cov = (255.0 - (double)p[2]) / 255.0;

			if (cov > 0.0) {
				sx += cov * (double)x;
				sy += cov * (double)y;
				w += cov;
			}
		}
	}
	*cx = w > 0.0 ? sx / w : 0.0;
	*cy = w > 0.0 ? sy / w : 0.0;
	*weight = (int)(w + 0.5);
}

static void repaint(CGContextRef ctx)
{
	CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
}

int main(void)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGDataProviderRef provider = CGDataProviderCreateWithFilename("userland/fonts/DejaVuSans.ttf");
	CGContextRef ctx;
	CGFontRef font;
	CGGlyph a = 0, space_glyph = 0;
	CGPoint pen;
	CGAffineTransform t;
	int n, x0, y0, x1, y1;

	surface = calloc((size_t)W * H, 4);
	if (surface == NULL || provider == NULL) {
		printf("CG-TEXT: cannot allocate the surface or read the font\n");
		return 1;
	}
	ctx = CGBitmapContextCreate(surface, W, H, 8, W * 4, space,
				    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	check("a bitmap context is created", ctx != NULL);
	if (ctx == NULL) {
		printf("CG-TEXT: FAILURES\n");
		return 1;
	}
	paint = (const unsigned char *)CGBitmapContextGetData(ctx);
	check("the context paints into the array it was handed (so the probe reads the real surface)",
	      paint == surface);
	if (paint == NULL) {
		printf("CG-TEXT: FAILURES\n");
		return failures + 1;
	}
	font = CGFontCreateWithDataProvider(provider);
	check("the font is created", font != NULL);

	/* --- the defaults, and the round trips ------------------------------------------- */
	t = CGContextGetTextMatrix(ctx);
	check("the text matrix defaults to the IDENTITY, not to a singular zero matrix",
	      t.a == 1.0 && t.b == 0.0 && t.c == 0.0 && t.d == 1.0 && t.tx == 0.0 && t.ty == 0.0);
	pen = CGContextGetTextPosition(ctx);
	check("the text position defaults to the origin", pen.x == 0.0 && pen.y == 0.0);
	CGContextSetTextPosition(ctx, 12.0, 34.0);
	pen = CGContextGetTextPosition(ctx);
	check("the text position round-trips", pen.x == 12.0 && pen.y == 34.0);
	CGContextSetTextMatrix(ctx, CGAffineTransformMakeTranslation(5.0, 6.0));
	t = CGContextGetTextMatrix(ctx);
	check("the text matrix round-trips", t.tx == 5.0 && t.ty == 6.0);
	CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
	CGContextSetTextPosition(ctx, 0.0, 0.0);
	CGContextSetCharacterSpacing(ctx, 2.0);

	/* --- what is refused, before anything is drawn ----------------------------------- */
	repaint(ctx);
	CGContextSetFontSize(ctx, 32.0);
	pen = CGPointMake(10.0, 10.0);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);	/* NO FONT SET */
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("with no font set, drawing REFUSES and paints nothing", n == 0);

	CGContextSetFont(ctx, font);
	a = CGFontGetGlyphWithGlyphName(font, @"A");
	space_glyph = CGFontGetGlyphWithGlyphName(font, @"space");
	check("the glyphs named A and space were found", a != 0 && space_glyph != 0);

	CGContextSetTextDrawingMode(ctx, kCGTextStroke);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("kCGTextStroke is REFUSED rather than filled", n == 0);
	CGContextSetTextDrawingMode(ctx, kCGTextInvisible);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("kCGTextInvisible is refused (the advance doors it needs are owed)", n == 0);
	CGContextSetTextDrawingMode(ctx, kCGTextFill);

	/* --- the ink, and where it lands ------------------------------------------------- */
	repaint(ctx);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("a glyph draws INK on the surface", n > 50);
	/* THE PEN IS AT USER (10,10), WHICH IS DEVICE (10,70) — the surface's y runs DOWN — so the
	 * glyph's ink occupies the rows just ABOVE 70 and the columns just right of 10. Both
	 * expectations below are device coordinates, and the first version of this check was written
	 * for the unflipped reading and measured nothing. */
	check("...and the ink is where the pen is, inside the em, not mirrored",
	      n > 0 && x0 >= 8 && x1 <= 10 + 32 && y0 >= 70 - 32 && y1 <= 72);
	check("...and nothing was painted far from the pen", n > 0 && x1 < 60 && y1 < H);

	/* THE ORIENTATION, WITH A GLYPH THAT CANNOT HIDE IT: 'L' has a foot along the baseline, and a
	 * MIRRORED 'L' has it at the top instead. The pen is in user space (y up) at 20, so the device
	 * baseline is at 60 (the surface's y runs down). */
	{
		CGGlyph l = CGFontGetGlyphWithGlyphName(font, @"L");
		CGPoint at = CGPointMake(10.0, 20.0);
		int x, y;

		check("the glyph named L was found", l != 0);
		repaint(ctx);
		CGContextShowGlyphsAtPositions(ctx, &l, &at, 1);
		{
			int top_cols = 0, foot_cols = 0;
			int wide_top[W], wide_foot[W];

			/* TWO BANDS OF THE SAME GLYPH: just under its top (the stem alone — narrow) and just
			 * above the baseline (the foot — wide). A MIRRORED `L` SWAPS THEM, and that is what this
			 * measures. The earlier version counted ink NEAR the baseline, which a mirrored `L`'s
			 * stem satisfies too — so it passed on an upside-down picture, which the PNG caught. */
			memset(wide_top, 0, sizeof(wide_top));
			memset(wide_foot, 0, sizeof(wide_foot));
			for (y = 38; y <= 42; y++) {
				for (x = 8; x < 60; x++) {
					if (paint[((size_t)y * W + x) * 4 + 2] < 128) {
						wide_top[x] = 1;
					}
				}
			}
			for (y = 54; y <= 59; y++) {
				for (x = 8; x < 60; x++) {
					if (paint[((size_t)y * W + x) * 4 + 2] < 128) {
						wide_foot[x] = 1;
					}
				}
			}
			for (x = 0; x < W; x++) {
				top_cols += wide_top[x];
				foot_cols += wide_foot[x];
			}
			printf("CG-TEXT %-68s (stem %d column(s), foot %d)\n", "...measured widths",
			       top_cols, foot_cols);
			check("an L is upright: its FOOT (wide, at the bottom) and its STEM (narrow, at the top) "
			      "are on the right ends of the glyph", foot_cols > 10 && top_cols < 8
			      && foot_cols > top_cols * 2);
		}
	}

	/* THE SPACE HAS NO INK AND IS NOT A FAILURE: every line of text contains one. */
	repaint(ctx);
	CGContextShowGlyphsAtPositions(ctx, &space_glyph, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("a space draws nothing and is not refused", n == 0);

	/* --- the two matrices, each measured by the ink moving --------------------------- */
	{
		int base_ink, base_x0;

		repaint(ctx);
		CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
		count_ink(&base_ink, &x0, &y0, &x1, &y1);
		base_x0 = x0;

		repaint(ctx);
		CGContextSetTextMatrix(ctx, CGAffineTransformMakeTranslation(60.0, 0.0));
		CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
		count_ink(&n, &x0, &y0, &x1, &y1);
		/* A TRANSLATION IN THE TEXT MATRIX MOVES THE GLYPH. Under EITHER reading of "where is the
		 * position" (text space or user space) a pure translation shifts the ink, so this check
		 * cannot tell the two apart — the one after it can. */
		check("a TRANSLATION in the text matrix moves the glyph",
		      n == base_ink && x0 - base_x0 >= 58 && x0 - base_x0 <= 62);

		/* AND ITS SCALE MUST NOT MOVE IT — the case that separates Apple's published reading ("the
		 * positions are specified in user space", which is what this library now implements, having
		 * first shipped the text-space one) from the reading it was corrected FROM. A scaled text
		 * matrix makes the glyph bigger and leaves the pen where it was: same left edge, more ink. */
		CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
		repaint(ctx);
		CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.5, 1.5));
		CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
		count_ink(&n, &x0, &y0, &x1, &y1);
		check("a SCALED text matrix scales the glyph and does NOT move the pen",
		      n > base_ink && x0 >= base_x0 - 1 && x0 <= base_x0 + 1);
		CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);

		/* AND THE CTM: the same glyph at 16pt, once plain and once through a 2x CTM, with the pen
		 * chosen so that BOTH land on the surface WITH THEIR INK: the ink rises from the pen, and
		 * the surface's rows run down, so a pen too near the top loses it — at user (5,50) the
		 * device origin is (5,30) and at 2x it is (10,60), both with room above. */
		{
			CGPoint near = CGPointMake(5.0, 50.0);
			int plain_h;

			repaint(ctx);
			CGContextSetFontSize(ctx, 16.0);
			CGContextShowGlyphsAtPositions(ctx, &a, &near, 1);
			count_ink(&n, &x0, &y0, &x1, &y1);
			plain_h = y1 - y0;
			repaint(ctx);
			CGContextScaleCTM(ctx, 2.0, 2.0);
			CGContextShowGlyphsAtPositions(ctx, &a, &near, 1);
			count_ink(&n, &x0, &y0, &x1, &y1);
			check("the CTM applies to text as well: at 2x the glyph is markedly taller",
			      plain_h > 4 && y1 - y0 > plain_h + 4);
			CGContextScaleCTM(ctx, 0.5, 0.5);
			CGContextSetFontSize(ctx, 32.0);
		}
	}

	/* --- one face, TWO SIZES: the measured guest defect's regression guard ----------- */
	{
		int h16, h32;

		repaint(ctx);
		CGContextSetFontSize(ctx, 16.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
		count_ink(&n, &x0, &y0, &x1, &y1);
		h16 = y1 - y0;
		repaint(ctx);
		CGContextSetFontSize(ctx, 32.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
		count_ink(&n, &x0, &y0, &x1, &y1);
		h32 = y1 - y0;
		check("two sizes on ONE font both draw, and the larger one is taller",
		      h16 > 4 && h32 > h16 + 4);
	}

	/* --- THE ADVANCE DOORS: the pen arithmetic, measured exactly --------------------- */
	/* `ShowGlyphs` moves the text position by the FONT'S OWN advance (font units scaled by the font
	 * size, which is in user space) plus the character spacing; `ShowGlyphsAtPoint` sets the position
	 * first; `ShowGlyphsWithAdvances` uses the caller's advances. THE CHECKS BELOW READ THE PEN BACK
	 * — the text position is the state these doors exist to move — and each is compared with the
	 * arithmetic the caller can do itself, so a door that drew but did not advance fails. */
	{
		CGGlyph pair[2];
		CGSize advances[2];
		CGPoint after;
		int advance = 0;
		double step;

		pair[0] = a;
		pair[1] = a;
		check("the font reports an advance for the test glyph",
		      CGFontGetGlyphAdvances(font, &a, 1, &advance) && advance > 0);
		step = (double)advance * 32.0 / (double)CGFontGetUnitsPerEm(font);

		repaint(ctx);
		CGContextSetCharacterSpacing(ctx, 0.0);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowGlyphs(ctx, pair, 2);
		after = CGContextGetTextPosition(ctx);
		count_ink(&n, &x0, &y0, &x1, &y1);
		check("ShowGlyphs draws the glyphs AND advances the pen by the font's own advance",
		      n > 50 && fabs(after.x - (10.0 + 2.0 * step)) < 0.01 && after.y == 10.0);

		repaint(ctx);
		CGContextSetCharacterSpacing(ctx, 12.0);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowGlyphs(ctx, pair, 2);
		after = CGContextGetTextPosition(ctx);
		check("the character spacing is ADDED to each advance, which is what the setter promises",
		      fabs(after.x - (10.0 + 2.0 * (step + 12.0))) < 0.01);

		repaint(ctx);
		CGContextShowGlyphsAtPoint(ctx, 20.0, 30.0, pair, 2);
		after = CGContextGetTextPosition(ctx);
		check("ShowGlyphsAtPoint sets the position and then advances from there",
		      fabs(after.x - (20.0 + 2.0 * (step + 12.0))) < 0.01 && after.y == 30.0);

		repaint(ctx);
		advances[0] = CGSizeMake(40.0, 0.0);
		advances[1] = CGSizeMake(40.0, 0.0);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowGlyphsWithAdvances(ctx, pair, advances, 2);
		after = CGContextGetTextPosition(ctx);
		check("ShowGlyphsWithAdvances uses the CALLER'S advances (user space) rather than the font's",
		      fabs(after.x - (10.0 + 2.0 * (40.0 + 12.0))) < 0.01);

		/* AND THE REFUSAL IS SHARED BY EVERY TEXT DOOR, since they all draw through one path. */
		repaint(ctx);
		CGContextSetFont(ctx, NULL);
		CGContextShowGlyphs(ctx, pair, 2);
		count_ink(&n, &x0, &y0, &x1, &y1);
		check("ShowGlyphs refuses with no font, like the door it shares its drawing with", n == 0);
		CGContextSetFont(ctx, font);
		CGContextSetCharacterSpacing(ctx, 0.0);
		CGContextSetFontSize(ctx, 32.0);
	}

	/* --- THE STRING DOORS: a byte path that must agree with the glyph path ----------- */
	/* THE DECISIVE CHECK IS AN AGREEMENT: `ShowText` of "Hi" must draw EXACTLY the ink that the two
	 * glyphs draw at the same pen, because the byte is read as a CHARACTER CODE through the font's own
	 * map (the reading CGContext.h states). If the mapping were the byte-as-glyph-index one, DejaVu's
	 * glyph order is not ASCII-aligned and this could not come out equal. */
	{
		CGGlyph pair[2];
		CGPoint at_hi[2], after;
		int adv_h = 0, adv_i = 0, units = CGFontGetUnitsPerEm(font);
		int ink_glyphs, ink_bytes, ink_short, ink_unmapped;
		int gx0, gy0, gx1, gy1, bx0, by0, bx1, by1, sx0, sy0, sx1, sy1, ux0, uy0, ux1, uy1;
		double step_h, step_i, before_x;

		pair[0] = CGFontGetGlyphWithGlyphName(font, @"H");
		pair[1] = CGFontGetGlyphWithGlyphName(font, @"i");
		check("the glyphs named H and i were found", pair[0] != 0 && pair[1] != 0
		      && pair[0] != (CGGlyph)kCGFontIndexInvalid
		      && pair[1] != (CGGlyph)kCGFontIndexInvalid);
		CGFontGetGlyphAdvances(font, &pair[0], 1, &adv_h);
		CGFontGetGlyphAdvances(font, &pair[1], 1, &adv_i);
		step_h = (double)adv_h * 32.0 / (double)units;
		step_i = (double)adv_i * 32.0 / (double)units;
		at_hi[0] = CGPointMake(10.0, 10.0);
		at_hi[1] = CGPointMake((CGFloat)(10.0 + step_h), 10.0);

		repaint(ctx);
		CGContextSetCharacterSpacing(ctx, 0.0);
		CGContextShowGlyphsAtPositions(ctx, pair, at_hi, 2);
		count_ink(&ink_glyphs, &gx0, &gy0, &gx1, &gy1);

		repaint(ctx);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowText(ctx, "Hi", 2);
		count_ink(&ink_bytes, &bx0, &by0, &bx1, &by1);
		after = CGContextGetTextPosition(ctx);
		check("ShowText draws exactly what the two glyphs draw: the byte is read as a CHARACTER",
		      ink_glyphs > 50 && ink_bytes == ink_glyphs && bx0 == gx0 && bx1 == gx1
		      && by0 == gy0 && by1 == gy1);
		check("...and it advances the pen by the font's own advances, like the glyph door",
		      fabs(after.x - (10.0 + step_h + step_i)) < 0.02 && after.y == 10.0);

		/* `length` IS THE COUNT AND NOT A TERMINATOR: the same two bytes drawn from a buffer with a NUL
		 * inside it must give the same picture as "Hi" alone. */
		repaint(ctx);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowText(ctx, "Hi\0XY", 2);
		count_ink(&ink_short, &sx0, &sy0, &sx1, &sy1);
		check("length wins over a NUL inside the buffer", ink_short == ink_bytes && sx0 == bx0
		      && sx1 == bx1);

		repaint(ctx);
		CGContextShowTextAtPoint(ctx, 30.0, 12.0, "Hi", 2);
		count_ink(&n, &x0, &y0, &x1, &y1);
		after = CGContextGetTextPosition(ctx);
		check("ShowTextAtPoint sets the position first, then advances from there",
		      n == ink_bytes && fabs(after.x - (30.0 + step_h + step_i)) < 0.02 && after.y == 12.0);

		/* AND A BYTE THIS FONT HAS NO GLYPH FOR IS SKIPPED: no ink, no advance, no refusal. */
		repaint(ctx);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		before_x = 10.0;
		CGContextShowText(ctx, "\0", 1);
		count_ink(&ink_unmapped, &ux0, &uy0, &ux1, &uy1);
		after = CGContextGetTextPosition(ctx);
		check("a byte with no glyph in this font is skipped rather than refused",
		      ink_unmapped == 0 && after.x == before_x);
	}

	/* --- THE REGISTRY, AND THE ENCODING IT IS THE ONLY WAY TO REACH ------------------ */
	{
		CGFontRef by_ps, by_full, by_bogus;
		CGGlyph adieresis;
		CGPoint at = CGPointMake(10.0, 10.0);
		int ink_macroman, ink_specific, ink_named;
		int mx0, my0, mx1, my1, nx0, ny0, nx1, ny1;

		/* THE DIRECTORIES ARE THE REGISTRY'S CHOICE AND THE OVERRIDE IS THE PROBE'S DOOR IN (see
		 * CGFont.h): this points the scan at the tree's own fonts. */
		setenv("FN_FONT_PATH", "userland/fonts", 1);
		by_ps = CGFontCreateWithFontName(@"DejaVuSans");
		by_full = CGFontCreateWithFontName(@"DejaVu Sans");
		by_bogus = CGFontCreateWithFontName(@"NoSuchFontAnywhere");
		check("CGFontCreateWithFontName finds the face by its PostScript name", by_ps != NULL);
		check("...and by its full name, which lives in the font's own name table", by_full != NULL);
		check("...and both spellings name the SAME face",
		      by_ps != NULL && by_full != NULL
		      && CGFontGetUnitsPerEm(by_ps) == CGFontGetUnitsPerEm(by_full)
		      && CGFontGetNumberOfGlyphs(by_ps) == CGFontGetNumberOfGlyphs(by_full));
		check("a name that matches nothing answers NULL rather than a substitute", by_bogus == NULL);
		if (by_ps != NULL) {
			CGFontRelease(by_ps);
		}
		if (by_full != NULL) {
			CGFontRelease(by_full);
		}

		/* THE SAME BYTE, TWO ENCODINGS, TWO ANSWERS: 0x80 is A-diaeresis in Mac OS Roman and a bare
		 * control code in the font-specific reading — which is the whole reason `SelectFont` carries the
		 * encoding, and the only door that can reach the Mac Roman half of the enum. */
		repaint(ctx);
		CGContextSelectFont(ctx, "DejaVuSans", 32.0, kCGEncodingMacRoman);
		CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowText(ctx, "\x80", 1);
		count_ink(&ink_macroman, &mx0, &my0, &mx1, &my1);

		repaint(ctx);
		CGContextSelectFont(ctx, "DejaVuSans", 32.0, kCGEncodingFontSpecific);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowText(ctx, "\x80", 1);
		count_ink(&ink_specific, &n, &y0, &x1, &y1);
		check("under Mac Roman the byte 0x80 reaches a glyph", ink_macroman > 20);
		check("...and under the font-specific reading the same byte has no glyph and is skipped",
		      ink_specific == 0);

		/* AND IT IS THE RIGHT GLYPH: Mac OS Roman 0x80 is A-DIAERESIS, so the ink must equal what the
		 * glyph NAMED Adieresis draws at the same pen. That is the table's standard-ness, measured. */
		repaint(ctx);
		adieresis = CGFontGetGlyphWithGlyphName(font, @"Adieresis");
		check("the glyph named Adieresis was found", adieresis != 0
		      && adieresis != (CGGlyph)kCGFontIndexInvalid);
		CGContextSetFont(ctx, font);
		CGContextSetFontSize(ctx, 32.0);
		CGContextShowGlyphsAtPositions(ctx, &adieresis, &at, 1);
		count_ink(&ink_named, &nx0, &ny0, &nx1, &ny1);
		check("Mac Roman 0x80 draws EXACTLY the glyph Adieresis, so the table is the standard one",
		      ink_macroman == ink_named && mx0 == nx0 && mx1 == nx1 && my0 == ny0 && my1 == ny1);

		/* A NAME THAT CANNOT BE RESOLVED REFUSES AND KEEPS THE FONT THE CONTEXT HAD. */
		repaint(ctx);
		CGContextSelectFont(ctx, "NoSuchFontAnywhere", 32.0, kCGEncodingFontSpecific);
		CGContextSetTextPosition(ctx, 10.0, 10.0);
		CGContextShowText(ctx, "H", 1);
		count_ink(&n, &x0, &y0, &x1, &y1);
		check("a name that cannot be resolved refuses and leaves the working font in place", n > 20);
		CGContextSetFont(ctx, font);
	}

	/* --- SUBPIXEL PEN POSITIONS, AND THE FOUR DOORS THAT GOVERN THEM ----------------- */
	/* THE FRACTION IS MEASURED BY A COVERAGE-WEIGHTED CENTROID, in both axes. THE Y CHECK IS SIGNED ON
	 * PURPOSE: a subpixel offset travels through the engine's frame, which the mirror correction turned
	 * around, and a sign error there is a glyph that moves the wrong way — invisible in a whole-pixel
	 * picture and caught only here. The ROUNDING checks use 10.4 rather than 10.5 so that "rounded" does
	 * not depend on which way a half rounds. */
	{
		double cx0, cy0, cx1, cy1, cxa, cya, cxb, cyb, cx2, cy2;
		int w0, w1, wa, wb, w2;
		CGPoint at;

		repaint(ctx);
		at = CGPointMake(10.0, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx0, &cy0, &w0);

		repaint(ctx);
		at = CGPointMake(10.5, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx1, &cy1, &w1);
		check("a HALF-PIXEL pen in x moves the ink half a pixel",
		      w0 > 50 && fabs((cx1 - cx0) - 0.5) < 0.2);
		check("...and an x-only shift leaves the vertical centroid alone", fabs(cy1 - cy0) < 0.2);

		repaint(ctx);
		at = CGPointMake(10.0, 10.5);	/* +0.5 in USER y, which is -0.5 in device rows */
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cxa, &cya, &wa);
		check("a HALF-PIXEL pen in y moves the ink half a pixel UP the surface (the signed check)",
		      wa > 50 && (cya - cy0) < -0.2 && (cya - cy0) > -0.8);

		/* THE CONTEXT'S GATE: with `allows' off a fractional pen is rounded to a whole one, so 10.4 and
		 * 10.0 must give the SAME pixels — which is what this library did before it could do better. */
		CGContextSetAllowsFontSubpixelPositioning(ctx, 0);
		repaint(ctx);
		at = CGPointMake(10.0, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cxb, &cyb, &wb);
		repaint(ctx);
		at = CGPointMake(10.4, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx2, &cy2, &w2);
		check("with `allows' off, a fractional pen draws the SAME pixels as a whole one",
		      w2 == wb && fabs(cx2 - cxb) < 0.01);

		/* THE STATE'S GATE, and the header's AND: `allows' back on, the state's flag off, same answer. */
		CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
		CGContextSetShouldSubpixelPositionFonts(ctx, 0);
		repaint(ctx);
		at = CGPointMake(10.4, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx2, &cy2, &w2);
		check("with the STATE's flag off the pen is rounded as well (the header's AND)",
		      fabs(cx2 - cxb) < 0.01);

		/* AND THE ASYMMETRY THE HEADER STATES: the state flag is saved and restored, the context's is
		 * not. The state one comes back (which RESUMES the fractional pen), and the context's does not
		 * (so turning it off inside a save/restore frame cannot be undone by restoring). */
		CGContextSetShouldSubpixelPositionFonts(ctx, 1);
		CGContextSaveGState(ctx);
		CGContextSetShouldSubpixelPositionFonts(ctx, 0);
		CGContextRestoreGState(ctx);
		repaint(ctx);
		at = CGPointMake(10.4, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx2, &cy2, &w2);
		check("RESTORE brings the state flag back, so the fractional pen returns",
		      w2 > 50 && fabs(cx2 - cx0) > 0.2);

		CGContextSetAllowsFontSubpixelPositioning(ctx, 0);
		CGContextSaveGState(ctx);
		CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
		CGContextRestoreGState(ctx);
		repaint(ctx);
		at = CGPointMake(10.4, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx2, &cy2, &w2);
		/* THE OBSERVATION IS THE OPPOSITE OF THE STATE FLAG'S, and that is the asymmetry: the context's
		 * flag was set INSIDE the frame and SURVIVED the restore, so the fractional pen is honoured
		 * afterwards — it is not in the graphics state to be brought back. */
		check("...and does NOT bring the CONTEXT's flag back: the one set inside the frame survives it",
		      w2 > 50 && fabs(cx2 - cxb) > 0.2);

		printf("CG-TEXT %-68s (cx 10.0=%g 10.5=%g 10.4rounded=%g)\n", "...the measured centroids",
		       cx0, cx1, cxb);

		/* THE FOUR REFUSING SETTERS ARE CALLED, so their messages land in the gate's log; nothing they do
		 * may change the picture. */
		CGContextSetAllowsFontSmoothing(ctx, 1);
		CGContextSetShouldSmoothFonts(ctx, 0);
		CGContextSetAllowsFontSubpixelQuantization(ctx, 1);
		CGContextSetShouldSubpixelQuantizeFonts(ctx, 0);

		CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
		CGContextSetShouldSubpixelPositionFonts(ctx, 1);
		repaint(ctx);
		at = CGPointMake(10.0, 10.0);
		CGContextShowGlyphsAtPositions(ctx, &a, &at, 1);
		ink_centroid(&cx2, &cy2, &w2);
		check("the refusing setters changed nothing about the picture",
		      w2 == w0 && fabs(cx2 - cx0) < 0.01);
	}

	/* --- the state stack carries the font, which is the ownership this added --------- */
	repaint(ctx);
	CGContextSaveGState(ctx);
	CGContextSetFont(ctx, NULL);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("a NULL font in the current state refuses", n == 0);
	CGContextRestoreGState(ctx);
	CGContextShowGlyphsAtPositions(ctx, &a, &pen, 1);
	count_ink(&n, &x0, &y0, &x1, &y1);
	check("...and restoring the state brings the font back, not a dangling one", n > 50);

	printf("CG-TEXT: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	CGFontRelease(font);
	CGDataProviderRelease(provider);
	return failures;
}
