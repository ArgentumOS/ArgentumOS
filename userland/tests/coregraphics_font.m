/*
 * coregraphics_font — the font object: what Apple's font API answers, and what it refuses.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FONT IS ONE THIS TREE SHIPS, `userland/fonts/DejaVuSans.ttf`, not the host's: a check whose
 * expected numbers come from the host's font set would pass or fail with the machine, and every
 * number below is a fact about THAT file. That is also why the numbers are asserted exactly where
 * they are known (units per em = 2048) and structurally where they are not (the cap height is
 * inside the em and above the x-height) — an assertion is only worth as much as what it pins.
 *
 * WHAT THIS PROBE CAN SEE THAT A COMPILE CANNOT: that the engine is WIRED (a font is created from
 * bytes at all), that the metrics come from the face rather than from zeros, that the name table
 * is read (PostScript and full name), that glyph names go BOTH ways, that the two array forms are
 * ALL-OR-NOTHING as the header promises, and that both creators REFUSE what they cannot answer.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-FONT %-66s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want)
{
	int ok = got == want;

	printf("CG-FONT %-66s %s (got %g, want %g)\n", name, ok ? "ok" : "FAIL", got, want);
	if (!ok) {
		failures++;
	}
}

/* THE SHIPPED FONT, FROM THE REPOSITORY ROOT — the host probes run there. */
static const char *FN_FONT_PATH = "userland/fonts/DejaVuSans.ttf";

int main(void)
{
	CGDataProviderRef provider = CGDataProviderCreateWithFilename(FN_FONT_PATH);
	CGFontRef font;
	CGGlyph glyphs[2];
	int advances[2];
	CGRect boxes[2];
	CGRect box;

	if (provider == NULL) {
		printf("CG-FONT: cannot read %s — the probe needs the shipped font\n", FN_FONT_PATH);
		return 1;
	}
	font = CGFontCreateWithDataProvider(provider);
	check("a font is created from the shipped file's bytes", font != NULL);
	if (font == NULL) {
		printf("CG-FONT: FAILURES\n");
		return failures + 1;
	}

	/* --- the face's own numbers, in font units ------------------------------------- */
	check_num("units per em is this font's own 2048", (double)CGFontGetUnitsPerEm(font), 2048.0);
	check("the glyph count is a real font's, not 0", CGFontGetNumberOfGlyphs(font) > 1000);
	check("ascent is positive", CGFontGetAscent(font) > 0);
	check("descent is negative (below the baseline)", CGFontGetDescent(font) < 0);
	check("leading is positive", CGFontGetLeading(font) > 0);
	check("cap height is inside the em", CGFontGetCapHeight(font) > 0
	      && CGFontGetCapHeight(font) < CGFontGetUnitsPerEm(font));
	check("x-height is lower than the cap height", CGFontGetXHeight(font) > 0
	      && CGFontGetXHeight(font) < CGFontGetCapHeight(font));
	check_num("this face is upright, so its italic angle is 0",
		  (double)CGFontGetItalicAngle(font), 0.0);
	/* ZERO IS THE DOCUMENTED ANSWER FOR A FACE WITH NO STEM V, which an SFNT face is: the check
	 * pins Apple's rule rather than the engine's silence. */
	check_num("stem V is 0, which is Apple's answer for a face that has none",
		  (double)CGFontGetStemV(font), 0.0);
	box = CGFontGetFontBBox(font);
	check("the global bbox straddles the baseline and has height", box.size.height > 0
	      && box.origin.y < 0 && box.origin.y + box.size.height > 0);

	/* --- glyphs by NAME, both directions ------------------------------------------- */
	{
		CGGlyph a = CGFontGetGlyphWithGlyphName(font, @"A");
		NSString *back;

		check("the glyph for the name A is found, and is not .notdef", a != 0
		      && a != (CGGlyph)kCGFontIndexInvalid);
		back = CGFontCopyGlyphNameForGlyph(font, a);
		check("the name round-trips: A -> glyph -> A",
		      back != nil && [back isEqualToString:@"A"]);
		check_num("an unknown glyph name answers kCGFontIndexInvalid",
			  (double)CGFontGetGlyphWithGlyphName(font, @"NotAGlyphName"),
			  (double)kCGFontIndexInvalid);
		check("a glyph index with no name answers NULL",
		      CGFontCopyGlyphNameForGlyph(font, (CGGlyph)kCGFontIndexInvalid) == NULL);
	}

	/* --- the two array forms, and the all-or-nothing promise ------------------------ */
	glyphs[0] = CGFontGetGlyphWithGlyphName(font, @"A");
	glyphs[1] = CGFontGetGlyphWithGlyphName(font, @"V");
	check("two named glyphs were found to measure", glyphs[0] != glyphs[1]
	      && glyphs[0] != (CGGlyph)kCGFontIndexInvalid
	      && glyphs[1] != (CGGlyph)kCGFontIndexInvalid);
	check("both advances are measured", CGFontGetGlyphAdvances(font, glyphs, 2, advances));
	check("...and each is positive and inside the em",
	      advances[0] > 0 && advances[0] < CGFontGetUnitsPerEm(font)
	      && advances[1] > 0 && advances[1] < CGFontGetUnitsPerEm(font));
	check("both boxes are measured", CGFontGetGlyphBBoxes(font, glyphs, 2, boxes));
	check("...and each is a real box, sitting on the baseline",
	      boxes[0].size.width > 0 && boxes[0].size.height > 0
	      && boxes[0].origin.y + boxes[0].size.height > 0 && boxes[0].origin.y <= 0
	      && boxes[1].size.width > 0 && boxes[1].size.height > 0);
	/* THE INVALID INDEX IS THE ALL-OR-NOTHING CASE: one unmeasurable glyph in the array makes
	 * the whole request false, which is what the header says `false` means. */
	glyphs[1] = (CGGlyph)kCGFontIndexInvalid;
	check("one unmeasurable glyph in the array refuses the whole request",
	      !CGFontGetGlyphAdvances(font, glyphs, 2, advances)
	      && !CGFontGetGlyphBBoxes(font, glyphs, 2, boxes));

	/* --- names from the name table -------------------------------------------------- */
	{
		NSString *ps = CGFontCopyPostScriptName(font);
		NSString *full = CGFontCopyFullName(font);

		check("the PostScript name is this file's own", ps != nil
		      && [ps isEqualToString:@"DejaVuSans"]);
		check("the full name comes from the name table, not from the file name",
		      full != nil && [full rangeOfString:@"DejaVu Sans"].location != NSNotFound);
	}

	/* --- ownership, and what the creator refuses ------------------------------------ */
	check("retain answers the same font", CGFontRetain(font) == font);
	CGFontRelease(font);
	check("...so one release leaves it usable", CGFontGetUnitsPerEm(font) == 2048);
	check("a NULL provider is refused", CGFontCreateWithDataProvider(NULL) == NULL);
	{
		const char *not_a_font = "this is not a font, whatever the engine tries";
		CGDataProviderRef junk = CGDataProviderCreateWithData(NULL, not_a_font,
								     strlen(not_a_font), NULL);

		check("bytes that are not a font are refused rather than guessed at",
		      CGFontCreateWithDataProvider(junk) == NULL);
		if (junk != NULL) {
			CGDataProviderRelease(junk);
		}
	}
	check("an empty provider is refused", CGFontCreateWithDataProvider(
		      CGDataProviderCreateWithData(NULL, "", 1, NULL)) == NULL);

	printf("CG-FONT: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	CGFontRelease(font);
	CGDataProviderRelease(provider);
	return failures;
}
