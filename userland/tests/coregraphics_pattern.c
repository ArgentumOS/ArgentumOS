/*
 * coregraphics_pattern — a cell the caller draws, and the lattice it is repeated on.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CELL IS BUILT TO BE UNREADABLE-AT-A-GLANCE-IN-THE-WRONG-PLACE. It is 4x4, entirely BLUE, with a
 * 2x2 RED square at PATTERN-SPACE (0,0) — so the cell is asymmetric in BOTH axes and neither the tile
 * lookup's wrap nor the row flip can be got wrong without a check noticing. Every expected pixel below
 * is worked out from that one fact: a device point maps to a user point, the identity matrix makes
 * the pattern point the same, the step wraps it into 0…4, and the cell's own rows run the OTHER WAY
 * from pattern space's y, which is why the red square is at the BOTTOM of the picture and not the top.
 *
 * THE TILING IS CHECKED BY COUNTING COLUMNS, NOT BY LOOKING: along a row through the cell's lower
 * half the colours must run red, red, blue, blue and then REPEAT — so the checks are at x = 0, 1, 2, 3
 * and again at 4 and 8, which fail if the step wraps at the wrong place or not at all.
 *
 * AND THE PHASE IS PINNED BY ITS DIRECTION, since there is no Apple here to diff against: a phase of
 * (2, 0) has to move the tile two user units in +x, which turns the pixel that WAS red into blue and
 * the one two columns along into red. The convention is stated in CGPattern.c beside the arithmetic
 * that implements it; this probe is what stops it from drifting silently.
 */
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPattern.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

typedef struct {
	int drawn;
	int released;
} pattern_info;

static void check(const char *name, int ok)
{
	printf("CG-PATTERN %-62s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)(y * W + x) * 4u, 4);
}

static void check_rgb(const char *name, CGContextRef c, int x, int y, int r, int g, int b)
{
	pixel(c, x, y, p);
	if (p[2] < r - 2 || p[2] > r + 2 || p[1] < g - 2 || p[1] > g + 2 || p[0] < b - 2
	    || p[0] > b + 2) {
		printf("CG-PATTERN %-62s FAIL (got R%d G%d B%d, want R%d G%d B%d)\n", name, p[2], p[1],
		       p[0], r, g, b);
		failures++;
		return;
	}
	printf("CG-PATTERN %-62s ok (R%d G%d B%d)\n", name, p[2], p[1], p[0]);
}

static void check_untouched(const char *name, CGContextRef c, int x, int y)
{
	pixel(c, x, y, p);
	check(name, p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255);
}

static CGContextRef fresh(void)
{
	static unsigned char buf[W * H * 4];
	CGContextRef c;

	memset(buf, 0, sizeof(buf));
	c = CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGContextSetRGBFillColor(c, 0.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	return c;
}

/* BLUE EVERYWHERE, RED IN THE 2x2 SQUARE AT PATTERN-SPACE (0,0) — the cell's BOTTOM-LEFT. */
static void cell_draw(void *info, CGContextRef ctx)
{
	((pattern_info *)info)->drawn++;
	CGContextSetRGBFillColor(ctx, 0.0, 0.0, 1.0, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 4.0, 4.0));
	CGContextSetRGBFillColor(ctx, 1.0, 0.0, 0.0, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, 2.0, 2.0));
}

static void info_release(void *info)
{
	((pattern_info *)info)->released++;
}

static CGPatternRef make_pattern(pattern_info *info, CGFloat xStep, CGFloat yStep,
				 CGPatternTiling tiling, bool isColored)
{
	CGPatternCallbacks cb;

	cb.version = 0;
	cb.drawPattern = cell_draw;
	cb.releaseInfo = info_release;
	return CGPatternCreate(info, CGRectMake(0.0, 0.0, 4.0, 4.0), CGAffineTransformIdentity, xStep,
			       yStep, tiling, isColored, &cb);
}

int main(void)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGColorSpaceRef pattern_space;
	CGPatternRef pattern;
	CGColorRef colour;
	CGContextRef c;
	static pattern_info info;
	static pattern_info gap_info;

	/* --- what a pattern refuses ------------------------------------------------------- */
	{
		CGPatternCallbacks cb = { 0, cell_draw, NULL };
		CGPatternCallbacks v1 = { 1, cell_draw, NULL };
		CGPatternCallbacks nodraw = { 0, NULL, NULL };

		check("a NULL callbacks pointer is refused",
		      CGPatternCreate(NULL, CGRectMake(0, 0, 4, 4), CGAffineTransformIdentity, 4.0, 4.0,
				      kCGPatternTilingNoDistortion, true, NULL) == NULL);
		check("version 1 is refused", CGPatternCreate(NULL, CGRectMake(0, 0, 4, 4),
							      CGAffineTransformIdentity, 4.0, 4.0,
							      kCGPatternTilingNoDistortion, true,
							      &v1) == NULL);
		check("callbacks with no drawPattern are refused",
		      CGPatternCreate(NULL, CGRectMake(0, 0, 4, 4), CGAffineTransformIdentity, 4.0, 4.0,
				      kCGPatternTilingNoDistortion, true, &nodraw) == NULL);
		check("a STENCIL pattern is refused rather than drawn as a coloured one",
		      make_pattern(&info, 4.0, 4.0, kCGPatternTilingNoDistortion, false) == NULL);
		check("a constant-spacing tiling is refused rather than drawn as no-distortion",
		      make_pattern(&info, 4.0, 4.0, kCGPatternTilingConstantSpacing, true) == NULL);
		check("a step smaller than the cell is refused",
		      make_pattern(&info, 2.0, 4.0, kCGPatternTilingNoDistortion, true) == NULL);
		check("a cell with no area is refused",
		      CGPatternCreate(NULL, CGRectMake(0, 0, 0, 4), CGAffineTransformIdentity, 4.0, 4.0,
				      kCGPatternTilingNoDistortion, true, &cb) == NULL);
		(void)cb;
	}

	/* --- the pattern colour space, and its refusals ----------------------------------- */
	pattern_space = CGColorSpaceCreatePattern(NULL);
	check("CGColorSpaceCreatePattern(NULL) gives the coloured pattern space", pattern_space != NULL);
	check("...which has one component, the alpha",
	      pattern_space != NULL && CGColorSpaceGetNumberOfComponents(pattern_space) == 1);
	check("...and is a pattern space", pattern_space != NULL
						   && CGColorSpaceGetModel(pattern_space)
							      == kCGColorSpaceModelPattern);
	check("an UNCOLOURED pattern space is refused by name",
	      CGColorSpaceCreatePattern(space) == NULL);
	check("CGColorCreate refuses a pattern space: a pattern colour needs a pattern",
	      CGColorCreate(pattern_space, (const CGFloat[]){ 1.0 }) == NULL);

	/* --- the fill, the tiling, and the cell's orientation ---------------------------- */
	memset(&info, 0, sizeof(info));
	pattern = make_pattern(&info, 4.0, 4.0, kCGPatternTilingNoDistortion, true);
	check("a coloured no-distortion pattern is built", pattern != NULL);

	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	/* THE CELL'S LOWER-LEFT IS AT THE PICTURE'S LOWER-LEFT: device row 15 is user y 0.5, which is the
	 * cell's bottom row, which is where the red square is. */
	check_rgb("the cell's lower-left lands at the bottom-left, and it is the RED quadrant", c, 0, 15,
		  255, 0, 0);
	check_rgb("...two columns along, the cell is still red", c, 1, 15, 255, 0, 0);
	check_rgb("...and at column 2 the cell turns BLUE, which is the red square's edge", c, 2, 15, 0,
		  0, 255);
	check_rgb("...the cell's upper-right is blue", c, 3, 12, 0, 0, 255);
	check("...and the cell was drawn exactly once for the whole fill", info.drawn == 1);
	/* THE TILE REPEATS EVERY 4 COLUMNS AND EVERY 4 ROWS. */
	check_rgb("the tile repeats at column 4", c, 4, 15, 255, 0, 0);
	check_rgb("...again at 8", c, 8, 15, 255, 0, 0);
	check_rgb("...and the tile repeats every 4 ROWS too", c, 0, 11, 255, 0, 0);
	CGContextRelease(c);

	/* --- a step larger than the cell leaves a GAP ----------------------------------- */
	memset(&gap_info, 0, sizeof(gap_info));
	pattern = make_pattern(&gap_info, 8.0, 8.0, kCGPatternTilingNoDistortion, true);
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("a step of 8 with a 4-wide cell paints the cell", c, 0, 15, 255, 0, 0);
	check_untouched("...and leaves the gap between tiles exactly as it was", c, 6, 15);
	check_untouched("...in y as well as x", c, 0, 9);
	CGContextRelease(c);
	CGPatternRelease(pattern);

	/* --- the phase moves the tile, and its DIRECTION is pinned ---------------------- */
	memset(&info, 0, sizeof(info));
	pattern = make_pattern(&info, 4.0, 4.0, kCGPatternTilingNoDistortion, true);
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextSetPatternPhase(c, CGSizeMake(2.0, 0.0));
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("a phase of (2,0) moves the tile two user units in +x: the first column is now BLUE", c,
		  0, 15, 0, 0, 255);
	check_rgb("...and two columns along is now the RED quadrant", c, 2, 15, 255, 0, 0);
	CGContextRelease(c);

	/* --- the pattern's alpha --------------------------------------------------------- */
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 0.5 });
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("the components array's alpha multiplies the pattern: red at half alpha is 128", c, 0,
		  15, 128, 0, 0);
	CGContextRelease(c);

	/* --- the pattern colour, the OTHER door into the same state --------------------- */
	colour = CGColorCreateWithPattern(pattern_space, pattern, (const CGFloat[]){ 1.0 });
	check("CGColorCreateWithPattern makes a colour carrying the pattern", colour != NULL);
	check("...which has ONE component counting the alpha",
	      colour != NULL && CGColorGetNumberOfComponents(colour) == 1);
	check("...and CGColorGetPattern hands it back", CGColorGetPattern(colour) == pattern);
	check("a pattern colour made in a NON-pattern space is refused",
	      CGColorCreateWithPattern(space, pattern, (const CGFloat[]){ 1.0 }) == NULL);
	check("...and a NULL pattern is refused",
	      CGColorCreateWithPattern(pattern_space, NULL, (const CGFloat[]){ 1.0 }) == NULL);
	if (colour != NULL) {
		c = fresh();
		CGContextSetFillColorWithColor(c, colour);
		CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
		check_rgb("...and setting it as the fill colour paints the pattern", c, 0, 15, 255, 0, 0);
		CGContextRelease(c);
		CGColorRelease(colour);
	}

	/* --- the STROKE has its own pattern, and it is not the fill's ------------------- */
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextMoveToPoint(c, 0.5, 0.0);
	CGContextAddLineToPoint(c, 0.5, (CGFloat)H);
	CGContextStrokePath(c);
	check_untouched("a stroke with no stroke pattern set draws NOTHING — the fill pattern is the "
			"fill's", c, 0, 8);
	CGContextSetStrokePattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextMoveToPoint(c, 0.5, 0.0);
	CGContextAddLineToPoint(c, 0.5, (CGFloat)H);
	CGContextStrokePath(c);
	/* THE LINE COVERS USER x 0…1, so device column 0, and the pattern is at cell x 0.5 there. THE ROW
	 * MATTERS AND MY FIRST EXPECTATION FORGOT IT: the cell's red square is only its BOTTOM TWO rows,
	 * so cell y below 2 — and device row 14 is user y 1.5, which is inside it, while row 8 is user y
	 * 7.5, whose cell y of 3.5 is the blue part of the next tile. */
	check_rgb("with the stroke pattern set the stroke paints the pattern", c, 0, 14, 255, 0, 0);
	check_untouched("...and only where the stroke covers", c, 1, 8);
	CGContextRelease(c);

	/* --- the pattern is graphics STATE, so save and restore carry it ----------------- */
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGContextSaveGState(c);
	CGContextSetRGBFillColor(c, 0.0, 1.0, 0.0, 1.0);   /* a colour CLEARS the pattern */
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("a colour set after a pattern replaces it", c, 0, 15, 0, 255, 0);
	CGContextRestoreGState(c);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("...and restoring the graphics state brings the PATTERN back", c, 0, 15, 255, 0, 0);
	CGContextRelease(c);

	/* --- the ownership rule: the context's state keeps the pattern alive ------------- */
	memset(&info, 0, sizeof(info));
	pattern = make_pattern(&info, 4.0, 4.0, kCGPatternTilingNoDistortion, true);
	c = fresh();
	CGContextSetFillPattern(c, pattern, (const CGFloat[]){ 1.0 });
	CGPatternRelease(pattern);
	check("releasing the caller's reference does NOT release the pattern — the context holds it",
	      info.released == 0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("...and the context still paints it", c, 0, 15, 255, 0, 0);
	CGContextRelease(c);
	check("releasing the context fires the caller's releaseInfo exactly once", info.released == 1);

	/* --- a NULL pattern is refused, and the previous paint survives ----------------- */
	memset(&info, 0, sizeof(info));
	pattern = make_pattern(&info, 4.0, 4.0, kCGPatternTilingNoDistortion, true);
	c = fresh();
	CGContextSetRGBFillColor(c, 0.0, 1.0, 0.0, 1.0);
	CGContextSetFillPattern(c, NULL, (const CGFloat[]){ 1.0 });
	CGContextFillRect(c, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	check_rgb("a NULL pattern is refused and the fill colour still applies", c, 7, 7, 0, 255, 0);
	CGContextRelease(c);
	CGPatternRelease(pattern);

	printf("CG-PATTERN: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
