/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_context.c — C2's acceptance: the context, the clip, and the fill.
 *
 * IT RUNS ON THE HOST, WHICH IS THE POINT OF THE "OFFSCREEN FIRST" DECISION: a bitmap
 * context's surface is a block of bytes and pixman is deterministic, so a fill can be
 * asserted at EXACT PIXEL VALUES with no QEMU boot. The test tier keeps its runs for
 * the milestone where something must appear on a screen.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE COULD FAIL:
 *
 *   * THE BYTE ORDER. The first check reads the four bytes a red pixel turned out to
 *     be. CGBitmapContext.h promises B, G, R, A and flags that the Apple-constant ↔
 *     byte binding is a measurement rather than a transcription; this is the
 *     measurement. If it ever disagrees with Apple's headers, this line is what says so.
 *   * THE DEFAULT FLIP. User space is y-up from the lower-left, so user (0,0) must be
 *     the BOTTOM-left pixel. Both directions of the conversion are checked, because a
 *     transform and its inverse that are both wrong in the same way would pass one.
 *   * COVERAGE, EXACT AND FRACTIONAL. A rectangle with an integer extent must paint
 *     whole pixels and nothing else; one ending at 2.5 must paint a PARTIAL pixel. The
 *     partial test asserts proportion rather than a constant, because measurement says
 *     this pixman quantises trapezoid coverage in SEVENTEENTHS (0.5 reads 135 = 9/17,
 *     0.25 reads 60 = 4/17) — see the check, which records that and tests it.
 *   * THE TWO WINDING RULES, ON A PATH THAT CANNOT SELF-INTERSECT: two concentric
 *     squares, drawn in the SAME direction, are a filled square under the non-zero rule
 *     (the inner area has winding 2) and a square with a HOLE under even-odd. Drawn in
 *     OPPOSITE directions, BOTH rules give the hole. That is a real distinction between
 *     the two functions, which a single-ring test cannot make.
 *   * THE REFUSALS, ASSERTED BY THEIR EFFECT ON PIXELS — the only form of the claim
 *     that matters: a self-intersecting path draws NOTHING, and a rotated `ClipToRect`
 *     leaves the clip exactly as it was rather than clipping to a bounding box.
 *   * THE STATE STACK. Save/restore must bring back the CTM, the clip AND the fill
 *     colour — three separate fields, so three separate checks.
 *
 * Output: one line per check, then a total. The exit status is the failure count, so
 * the caller needs no log parsing.
 */
#include <CoreGraphics/CGShading.h>
#include <CoreGraphics/CGPattern.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGGradient.h>
#include <CoreGraphics/CGFunction.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGContext_internal.h>

/* THE RENAME TABLE — see NSBezierPath.m: the clip reset is internal now, and what these
 * checks are about is what the AppKit does with it. */
#define CGContextResetClip cg_context_reset_clip
#include <CoreGraphics/CGPath.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	if (ok) {
		printf("CG-PROBE %-46s ok\n", name);
	} else {
		printf("CG-PROBE %-46s FAIL\n", name);
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	if (got >= want - tol && got <= want + tol) {
		printf("CG-PROBE %-46s ok\n", name);
	} else {
		printf("CG-PROBE %-46s FAIL (got %g, want %g ±%g)\n", name, got, want, tol);
		failures++;
	}
}

#define W 4
#define H 4
static unsigned char surface[W * H * 4];

static CGContextRef fresh(void)
{
	memset(surface, 0, sizeof(surface));
	return CGBitmapContextCreate(surface, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
}

/* The bytes of one pixel, in memory order. */
static void pixel(CGContextRef c, int x, int y, unsigned char out[4])
{
	unsigned char *d = CGBitmapContextGetData(c);

	memcpy(out, d + (size_t)y * W * 4 + (size_t)x * 4, 4);
}

static int count_nonzero(CGContextRef c)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int i, n = 0;

	for (i = 0; i < W * H * 4; i++) {
		if (d[i] != 0) {
			n++;
		}
	}
	return n;
}

int main(void)
{
	CGContextRef c;
	unsigned char p[4];
	CGAffineTransform t;
	CGRect box;
	CGMutablePathRef path;

	/* --- the format, pinned by its bytes ------------------------------------- */
	c = fresh();
	check("bitmap context created", c != NULL);
	if (c == NULL) {
		return 1;
	}
	check_num("width", (double)CGBitmapContextGetWidth(c), W, 0);
	check_num("height", (double)CGBitmapContextGetHeight(c), H, 0);
	check_num("bits per component", (double)CGBitmapContextGetBitsPerComponent(c), 8, 0);
	check_num("bits per pixel", (double)CGBitmapContextGetBitsPerPixel(c), 32, 0);
	check_num("bytes per row", (double)CGBitmapContextGetBytesPerRow(c), W * 4, 0);
	check("alpha info round-trips",
	      CGBitmapContextGetAlphaInfo(c) == kCGImageAlphaPremultipliedFirst);
	check("colour space is device RGB",
	      CGColorSpaceGetModel(CGBitmapContextGetColorSpace(c)) == kCGColorSpaceModelRGB);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	pixel(c, 0, 0, p);
	check("opaque red is B,G,R,A = 0,0,255,255 in memory",
	      p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);
	{
		int all = 1;
		int x, y;

		for (y = 0; y < H; y++) {
			for (x = 0; x < W; x++) {
				pixel(c, x, y, p);
				if (!(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255)) {
					all = 0;
				}
			}
		}
		check("a full-surface fill covers every pixel", all);
	}
	CGContextRelease(c);

	/* --- the default coordinate system --------------------------------------- */
	c = fresh();
	t = CGContextGetCTM(c);
	check_num("default CTM a", t.a, 1.0, 0);
	check_num("default CTM d", t.d, -1.0, 0);
	check_num("default CTM ty", t.ty, H, 0);
	{
		CGPoint o = CGContextConvertPointToDeviceSpace(c, CGPointMake(0.0, 0.0));
		CGPoint tl = CGContextConvertPointToDeviceSpace(c, CGPointMake(0.0, H));
		CGPoint back = CGContextConvertPointToUserSpace(c, CGPointMake(0.0, 0.0));

		check("user origin is the bottom-left device point", o.x == 0.0 && o.y == H);
		check("user top-left maps to device origin", tl.x == 0.0 && tl.y == 0.0);
		/* DEVICE (0,0) IS USER (0,H): the round trip of the DEVICE ORIGIN therefore
		 * lands at the TOP-left in user space. The first version of this check expected
		 * (0,0) — it was wrong about which point it was converting, not about the
		 * transform. */
		check("the conversion round-trips", back.x == 0.0 && back.y == H);
	}
	/* AND THE SAME CLAIM IN PIXELS: a fill at user y in [0,1) must land on the BOTTOM
	 * row, which is the last one in the buffer. */
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, 1.0));
	pixel(c, 0, H - 1, p);
	check("a fill at user y 0..1 lands on the bottom row", p[3] == 255);
	pixel(c, 0, 0, p);
	check("...and not on the top row", p[3] == 0);
	CGContextRelease(c);

	/* --- coverage, exact and fractional -------------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 2.0, H));
	/* TWO NON-ZERO BYTES PER PIXEL, NOT FOUR: an opaque red pixel is B,G,R,A =
	 * 0,0,255,255 and this counts BYTES that are not zero, so 2 columns × 4 rows × 2 = 16.
	 * Stated because the first version of this check expected 32 — it was measuring the
	 * colour rather than the coverage. */
	check_num("an integer-extent fill covers exactly its own pixels",
		  (double)count_nonzero(c), 2.0 * H * 2, 0);
	CGContextRelease(c);

	{
		unsigned char half;
		unsigned char quarter;

		c = fresh();
		CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
		CGContextFillRect(c, CGRectMake(0.0, 0.0, 2.5, H));
		pixel(c, 2, H - 1, p);
		half = p[3];
		CGContextRelease(c);

		c = fresh();
		CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
		CGContextFillRect(c, CGRectMake(0.0, 0.0, 2.25, H));
		pixel(c, 2, H - 1, p);
		quarter = p[3];

		/* THE MASK TAKES SEVENTEEN VALUES, AND THAT IS MEASURED RATHER THAN ASSUMED:
		 * 0.5 coverage reads 135 and 0.25 reads 60 — both multiples of 255/17 (9 and 4),
		 * not of 255/256. The first version of this check asserted 128 ± 2 and would have
		 * been an assertion about pixman's RASTERIZER rather than about this file's
		 * geometry. What this file controls is that coverage is PARTIAL and PROPORTIONAL,
		 * and the seventeen-value grid is recorded as a property of the engine: if this
		 * check ever fails, the engine changed its quantisation and nothing here did. */
		check("a half-covered pixel is partial", half > 100 && half < 160);
		check("the mask lands on multiples of 255/17 (measured)",
		      half % 15 == 0 && quarter % 15 == 0);
		check_num("a quarter-covered pixel is about half the half-covered one",
			  (double)half - 2.0 * (double)quarter, 0.0, 15.0);
		pixel(c, 0, H - 1, p);
		check_num("a fully covered pixel is opaque", (double)p[3], 255.0, 0.0);
		CGContextRelease(c);
	}

	/* --- zero-area and empty fills ------------------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(1.0, 1.0, 0.0, 0.0));
	check_num("a zero-area fill draws nothing", (double)count_nonzero(c), 0.0, 0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 0.0, H));
	check_num("a zero-width fill draws nothing", (double)count_nonzero(c), 0.0, 0);
	CGContextRelease(c);

	/* --- a path, and the two winding rules ----------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 0.0, 0.0);
	CGContextAddLineToPoint(c, 3.0, 0.0);
	CGContextAddLineToPoint(c, 0.0, 3.0);
	CGContextClosePath(c);
	check("the path is not empty before the fill", !CGContextIsPathEmpty(c));
	box = CGContextGetPathBoundingBox(c);
	check("the path box is in user space", box.origin.x == 0.0 && box.origin.y == 0.0 &&
	      box.size.width == 3.0 && box.size.height == 3.0);
	CGContextFillPath(c);
	check("a fill consumes the path", CGContextIsPathEmpty(c));
	pixel(c, 0, H - 1, p);
	check("a pixel inside the triangle is painted", p[3] == 255);
	pixel(c, 3, 0, p);
	check("a pixel outside the triangle is not", p[3] == 0);
	CGContextRelease(c);

	/* TWO CONCENTRIC SQUARES IN THE SAME DIRECTION: non-zero sees winding 2 in the
	 * middle and fills it; even-odd counts two crossings and leaves a hole. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	CGContextAddRect(c, CGRectMake(1.0, 1.0, 2.0, 2.0));
	CGContextFillPath(c);
	pixel(c, 1, 1, p);
	check("non-zero winding fills the middle of same-direction squares", p[3] == 255);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	CGContextAddRect(c, CGRectMake(1.0, 1.0, 2.0, 2.0));
	CGContextEOFillPath(c);
	pixel(c, 1, 1, p);
	check("even-odd leaves a hole in the same squares", p[3] == 0);
	pixel(c, 0, 3, p);
	check("...and still paints the ring", p[3] == 255);
	CGContextRelease(c);

	/* THE SAME TWO SQUARES WITH THE INNER ONE REVERSED: both rules agree on a hole. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextAddRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	CGContextMoveToPoint(c, 1.0, 1.0);
	CGContextAddLineToPoint(c, 1.0, 3.0);
	CGContextAddLineToPoint(c, 3.0, 3.0);
	CGContextAddLineToPoint(c, 3.0, 1.0);
	CGContextClosePath(c);
	CGContextFillPath(c);
	pixel(c, 1, 1, p);
	check("non-zero with a reversed inner ring leaves a hole", p[3] == 0);
	CGContextRelease(c);

	/* --- FillRect does not disturb the path ---------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	check("FillRect leaves the current path alone", !CGContextIsPathEmpty(c));
	box = CGContextGetPathBoundingBox(c);
	check("...and the path is still just its move", box.origin.x == 1.0 && box.origin.y == 1.0 &&
	      box.size.width == 0.0 && box.size.height == 0.0);
	CGContextRelease(c);

	/* --- the clip ------------------------------------------------------------ */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextClipToRect(c, CGRectMake(0.0, 0.0, 2.0, 2.0));
	/* The clip is in USER space on the way in and lives in DEVICE space after, so the
	 * box comes back as the same quadrant on a bitmap context with no flip of its own. */
	box = CGContextGetClipBoundingBox(c);
	check("the clip box is the rect that was asked for",
	      box.origin.x == 0.0 && box.origin.y == 0.0 && box.size.width == 2.0 && box.size.height == 2.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	check_num("a clip limits a fill to a quarter", (double)count_nonzero(c), 2.0 * 2.0 * 4, 0);
	pixel(c, 0, H - 1, p);
	check("a clipped-in pixel is painted", p[3] == 255);
	pixel(c, 3, 0, p);
	check("a clipped-out pixel is not", p[3] == 0);
	CGContextRelease(c);

	/* THE CLIP REFUSAL: a rotated CTM makes the rect a parallelogram, so the clip is
	 * left ALONE rather than replaced by a bounding box that keeps too much. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextClipToRect(c, CGRectMake(0.0, 0.0, 2.0, 2.0));
	CGContextRotateCTM(c, 0.5);
	CGContextClipToRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	CGContextRotateCTM(c, -0.5);
	/* ASSERTED IN PIXELS, AND WITH THE CTM PUT BACK, so the only thing this check can be
	 * measuring is whether the REFUSED call moved the clip: the fill is then the same
	 * axis-aligned one the check above already passes. */
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 4.0, 4.0));
	check_num("a rotated ClipToRect leaves the clip alone",
		  (double)count_nonzero(c), 4.0 * 4, 0);
	CGContextRelease(c);

	/* A FILL THAT ENCLOSES THE SURFACE — every one of its edges outside it — MUST PAINT
	 * THE WHOLE SURFACE, and this check exists because it did not.
	 *
	 * HOW IT WAS FOUND: by accident, when this section's fill was a 64×64 rectangle
	 * rotated around a 4×4 surface. The geometry said the entire surface lay inside the
	 * rectangle, and NOTHING was painted. The cause was the clipper, not the sweep:
	 * clipping each EDGE to the surface kept no edges at all, because every edge of a
	 * polygon that contains the surface lies outside it — an outline crossing the
	 * surface was fine, an outline containing it produced no geometry. `cg_close_subpath`
	 * in CGContext.c now clips the OUTLINE (Sutherland–Hodgman), which turns this case
	 * into the surface itself.
	 *
	 * IT IS A CHECK RATHER THAN A NOTE because it is the case a background fill is, and
	 * because the version of it that was here before — asserting that NOTHING was painted
	 * — is exactly the kind of check that quietly becomes permanent. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(-16.0, -16.0, 32.0, 32.0));
	check_num("an enclosing fill paints the whole surface",
		  (double)count_nonzero(c), W * H * 4, 0);
	CGContextRelease(c);

	/* AND THE SAME CASE WITH THE OUTLINE ROTATED — which is how the bug was found, and
	 * which is a DIFFERENT PATH through the clipper rather than a re-run of the same one:
	 * a rotated quad's sides meet the surface's four sides at non-integral parameters, so
	 * each of the four Sutherland–Hodgman passes INTERPOLATES a new vertex instead of
	 * reusing a corner. The surface must still come out fully painted. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextRotateCTM(c, 0.7);
	CGContextFillRect(c, CGRectMake(-16.0, -16.0, 32.0, 32.0));
	check_num("a ROTATED enclosing fill paints the whole surface",
		  (double)count_nonzero(c), W * H * 4, 0);
	CGContextRelease(c);

	/* --- the state stack ----------------------------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
	CGContextSaveGState(c);
	CGContextSetRGBFillColor(c, 0.0, 0.0, 1.0, 1.0);
	CGContextTranslateCTM(c, 10.0, 0.0);
	CGContextClipToRect(c, CGRectMake(0.0, 0.0, 1.0, 1.0));
	CGContextRestoreGState(c);
	t = CGContextGetCTM(c);
	check("restore brings back the CTM", t.tx == 0.0 && t.ty == H);
	box = CGContextGetClipBoundingBox(c);
	check("restore brings back the clip", box.size.width == W && box.size.height == H);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	pixel(c, 0, 0, p);
	check("restore brings back the fill colour (red, not blue)", p[2] == 255 && p[0] == 0);
	CGContextRelease(c);

	/* --- alpha and blend modes ---------------------------------------------- */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 0.5);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	pixel(c, 0, 0, p);
	check_num("half-alpha red over white is ~50% red", (double)p[2], 255.0, 2.0);
	check("...and stays opaque", p[3] == 255);
	CGContextRelease(c);

	c = fresh();
	CGContextSetRGBFillColor(c, 0.0, 0.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	CGContextSetBlendMode(c, kCGBlendModeMultiply);
	CGContextSetRGBFillColor(c, 1.0, 0.0, 0.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	pixel(c, 0, 0, p);
	check("multiply of red onto blue is black", p[0] < 2 && p[1] < 2 && p[2] < 2);
	CGContextRelease(c);

	c = fresh();
	CGContextSetAlpha(c, 0.5);
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, W, H));
	pixel(c, 0, 0, p);
	check_num("SetAlpha multiplies the fill's alpha", (double)p[3], 128.0, 2.0);
	CGContextRelease(c);

	/* --- antialiasing switched off ------------------------------------------ */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextSetShouldAntialias(c, 0);
	CGContextFillRect(c, CGRectMake(0.0, 0.0, 2.5, H));
	pixel(c, 2, H - 1, p);
	/* A 1-BIT MASK CANNOT PRODUCE A PARTIAL PIXEL, so the assertion is the property
	 * rather than the value: which way pixman rounds a 50% sample is its business. */
	check("with antialiasing off, an edge pixel is all or nothing",
	      p[3] == 0 || p[3] == 255);
	CGContextRelease(c);

	/* --- a self-intersecting fill, WHICH USED TO BE REFUSED ------------------ */
	/* THE CROSSING SPLIT IS WHY THIS CHECK READS THE WAY IT DOES NOW: the sweep's bands end
	 * at every edge-edge CROSSING as well as at every vertex, so the x-order of the active
	 * edges holds inside each band and an outline that crosses itself computes like any
	 * other. While that was missing this check asserted that NOTHING was painted, because
	 * the fill refused such a path — and it failed the day the split landed, which is what
	 * it was for. A bowtie paints its lobes. */
	c = fresh();
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextBeginPath(c);
	CGContextMoveToPoint(c, 0.0, 0.0);
	CGContextAddLineToPoint(c, 4.0, 4.0);
	CGContextAddLineToPoint(c, 4.0, 0.0);
	CGContextAddLineToPoint(c, 0.0, 4.0);
	CGContextClosePath(c);
	CGContextFillPath(c);
	check("a self-intersecting fill paints its lobes", count_nonzero(c) > 0);
	CGContextRelease(c);

	/* --- the constructor's refusals ----------------------------------------- */
	c = CGBitmapContextCreate(surface, W, H, 16, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	check("16 bits per component is refused with NULL", c == NULL);
	c = CGBitmapContextCreate(surface, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				  kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
	check("an unsupported alpha/order combination is refused with NULL", c == NULL);

	/* --- the path API on its own -------------------------------------------- */
	path = CGPathCreateMutable();
	check("an empty path is empty", CGPathIsEmpty((CGPathRef)path));
	CGPathMoveToPoint(path, NULL, 1.0, 2.0);
	check("a move makes the path non-empty", !CGPathIsEmpty((CGPathRef)path));
	{
		CGPoint cur = CGPathGetCurrentPoint((CGPathRef)path);

		check("the current point is the move", cur.x == 1.0 && cur.y == 2.0);
	}
	CGPathAddLineToPoint(path, NULL, 4.0, 6.0);
	box = CGPathGetBoundingBox((CGPathRef)path);
	check("the path box spans its points", box.origin.x == 1.0 && box.origin.y == 2.0 &&
	      box.size.width == 3.0 && box.size.height == 4.0);
	check("an empty path's box is the null rectangle",
	      CGRectIsNull(CGPathGetBoundingBox(CGPathCreateMutable())));
	{
		/* A RECTANGLE ADDED THROUGH A ROTATION IS A QUADRILATERAL, NOT A BOX — the
		 * check that `CGPathAddRect` must not have used the rect's bounding box. */
		CGRect box2;

		path = CGPathCreateMutable();
		CGPathAddRect(path, &(CGAffineTransform){ 0.0, 1.0, -1.0, 0.0, 0.0, 0.0 },
			      CGRectMake(0.0, 0.0, 1.0, 1.0));
		box2 = CGPathGetBoundingBox((CGPathRef)path);
		check("a rotated rect keeps its four corners", box2.origin.x == -1.0 &&
		      box2.origin.y == 0.0 && box2.size.width == 1.0 && box2.size.height == 1.0);
	}
	CGPathRelease((CGPathRef)path);

	/* --- THE PATH CLIP: exact for a rectilinear path, refused otherwise --------------- */
	/* THE CHECKS COUNT PIXELS, because a clip is not visible any other way: the assertion is on how
	 * much of a full-surface fill SURVIVED, which one number per case settles. */
	{
		CGContextRef k = fresh();
		CGPathRef rp = CGPathCreateMutable();

		CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
		CGPathAddRect((CGMutablePathRef)rp, NULL, CGRectMake(1.0, 1.0, 2.0, 2.0));
		CGContextAddPath(k, rp);
		CGContextClip(k);
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		/* THE UNIT IS BYTES, WHICH THIS PROBE'S OWN HELPER DECIDES AND I GOT WRONG FIVE TIMES: an
		 * opaque white pixel is FOUR nonzero bytes, so a 2x2 clip is 16 and the whole 4x4 is 64. */
		check_num("a path clip confines a full-surface fill to 2x2 of a 4x4 (16 bytes = 4 pixels)",
			  (double)count_nonzero(k), 16.0, 0.0);
		/* AND THE BOX READS BACK IN USER SPACE, converted from the device region it is stored as. */
		{
			CGRect box = CGContextGetClipBoundingBox(k);

			check_num("...and GetClipBoundingBox reports it in USER space",
				  (double)box.size.width, 2.0, 1e-9);
		}
		CGContextRelease(k);
		CGPathRelease(rp);
	}

	/* THE TWO WINDING RULES, WHICH ONE PAIR OF CHECKS TELLS APART: two nested rects wind twice, so
	 * non-zero keeps the middle and even-odd leaves a HOLE. */
	{
		int nonzero;
		int evenodd;
		int i;

		for (i = 0; i < 2; i++) {
			CGContextRef k = fresh();
			CGMutablePathRef rp = CGPathCreateMutable();

			CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
			CGPathAddRect(rp, NULL, CGRectMake(0.0, 0.0, 4.0, 4.0));
			CGPathAddRect(rp, NULL, CGRectMake(1.0, 1.0, 2.0, 2.0));
			CGContextAddPath(k, (CGPathRef)rp);
			if (i == 0) {
				CGContextClip(k);
			} else {
				CGContextEOClip(k);
			}
			CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
			if (i == 0) {
				nonzero = count_nonzero(k);
			} else {
				evenodd = count_nonzero(k);
			}
			CGContextRelease(k);
			CGPathRelease((CGPathRef)rp);
		}
		check_num("a NON-ZERO path clip keeps the inner rect (both rings wind the same way; 64 "
			  "bytes = the whole surface)", (double)nonzero, 64.0, 0.0);
		check_num("...and an EVEN-ODD one leaves it as a HOLE (48 bytes = 12 pixels, the ring "
			  "without its middle)", (double)evenodd, 48.0, 0.0);
	}

	/* --- THE MASK HALF: A PATH THAT IS NOT RECTILINEAR NOW CLIPS INSTEAD OF REFUSING --------- */
	/* C8.6 REFUSED A SLANTED PATH because a clip was a REGION of rectangles. It is not any more: a
	 * non-rectilinear path becomes an 8-bit COVERAGE MASK, and the clip carries both halves. These
	 * checks are the fills' half of that; the gradient and image halves are in their own probes,
	 * because a mask that only worked for one of the three composites would be worse than none. */
	{
		CGContextRef k = fresh();
		CGMutablePathRef tri = CGPathCreateMutable();
		unsigned char px[4];
		int painted;
		int outside_top_left;

		CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
		CGPathMoveToPoint(tri, NULL, 0.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 4.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 2.0, 4.0);
		CGPathCloseSubpath(tri);
		CGContextAddPath(k, (CGPathRef)tri);
		CGContextClip(k);
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		painted = count_nonzero(k);
		/* AND A BYTE COUNT CANNOT ANSWER THIS QUESTION, WHICH COST ME A WRONG EXPECTATION: the
		 * triangle's own BOUNDING BOX IS THE WHOLE SURFACE, so with coverage it inks ALL SIXTEEN
		 * pixels — the three corners with a few percent each. `painted == 64` is therefore correct
		 * behaviour and reads exactly like "the mask did nothing". The assertions below are on
		 * COVERAGE, which is the thing a mask actually is. (A white fill is premultiplied, so every
		 * channel of a pixel equals its alpha, which is why one byte is enough.) */
		check_num("...the byte count is the whole surface even though the clip confined it, because "
			  "the triangle's bounding box IS the surface (why a count cannot test this)",
			  (double)painted, 64.0, 0.0);
		pixel(k, 1, 3, px);
		check_num("...but the triangle's interior is painted FULLY", (double)px[2], 255.0, 1.0);
		pixel(k, 0, 0, px);
		outside_top_left = (px[2] > 0 && px[2] < 255);
		check("...and its corner pixel is PARTIALLY covered, which is the proof this is a COVERAGE "
		      "clip and not an all-or-nothing region", outside_top_left);
		CGContextRelease(k);
		CGPathRelease((CGPathRef)tri);
	}

	/* --- AND THE MASK INTERSECTS THE REGION, so two clips combine ---------------------------- */
	{
		CGContextRef k = fresh();
		CGMutablePathRef tri = CGPathCreateMutable();
		int both;
		unsigned char px2[4];

		CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
		/* A RECTANGLE CLIP FIRST (the region half), THEN A TRIANGLE (the mask half). */
		CGContextClipToRect(k, CGRectMake(0.0, 0.0, 2.0, 4.0));
		CGPathMoveToPoint(tri, NULL, 0.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 4.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 2.0, 4.0);
		CGPathCloseSubpath(tri);
		CGContextAddPath(k, (CGPathRef)tri);
		CGContextClip(k);
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		both = count_nonzero(k);
		check("...and the two halves COMBINE: the triangle alone inks every pixel of a 4x4, so the "
		      "disjoint pixels here can only come from the rect", both < 64);
		/* THE PIXELS A RECT CLIP EXCLUDES ENTIRELY ARE THE READABLE PART: the triangle inks all of a
		 * 4x4 surface, so a zero here can only be the region half. */
		pixel(k, 3, 3, px2);
		check("...a pixel outside the RECT is uninked even though the triangle covers it",
		      px2[2] == 0);
		pixel(k, 1, 3, px2);
		check_num("...and one inside both is painted fully", (double)px2[2], 255.0, 1.0);
		/* AND RESET CLEARS BOTH HALVES. */
		CGContextResetClip(k);
		CGContextClearRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		check_num("...and ResetClip clears the mask as well as the region",
			  (double)count_nonzero(k), 64.0, 0.0);
		CGContextRelease(k);
		CGPathRelease((CGPathRef)tri);
	}

	/* --- AND SAVE/RESTORE CARRIES THE MASK, which is the part a lifetime bug would show ---------- */
	{
		CGContextRef k = fresh();
		CGMutablePathRef tri = CGPathCreateMutable();
		int clipped;
		int after_restore;

		CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
		CGPathMoveToPoint(tri, NULL, 0.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 4.0, 0.0);
		CGPathAddLineToPoint(tri, NULL, 2.0, 4.0);
		CGPathCloseSubpath(tri);
		CGContextAddPath(k, (CGPathRef)tri);
		CGContextClip(k);
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		clipped = count_nonzero(k);
		CGContextSaveGState(k);
		CGContextResetClip(k);
		CGContextClearRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		check_num("...and with the mask RESET, the whole surface paints", (double)count_nonzero(k),
			  64.0, 0.0);
		CGContextRestoreGState(k);
		CGContextClearRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		CGContextFillRect(k, CGRectMake(0.0, 0.0, 4.0, 4.0));
		after_restore = count_nonzero(k);
		check("...and RESTORING brings the mask back, to the same coverage it had",
		      after_restore == clipped);
		CGContextRelease(k);
		CGPathRelease((CGPathRef)tri);
	}


	/* A ROTATED CTM IS REFUSED for the reason CGContextClipToRect already refuses one. */
	{
		CGContextRef k = fresh();
		CGPathRef rp = CGPathCreateMutable();

		CGContextSetRGBFillColor(k, 1.0, 1.0, 1.0, 1.0);
		CGContextRotateCTM(k, 0.5);
		CGPathAddRect((CGMutablePathRef)rp, NULL, CGRectMake(1.0, 1.0, 2.0, 2.0));
		CGContextAddPath(k, rp);
		CGContextClip(k);
		CGContextFillRect(k, CGRectMake(-4.0, -4.0, 16.0, 16.0));
		check_num("a path clip under a ROTATED CTM is refused", (double)count_nonzero(k), 64.0, 0.0);
		CGContextRelease(k);
		CGPathRelease(rp);
	}

	/* AND A CLIP CONSUMES THE CURRENT PATH, as the fills do. */
	{
		CGPathRef rp = CGPathCreateMutable();
		CGContextRef k = fresh();

		CGPathAddRect((CGMutablePathRef)rp, NULL, CGRectMake(0.0, 0.0, 2.0, 2.0));
		CGContextAddPath(k, rp);
		CGContextClip(k);
		check("...and it CONSUMES the current path", CGContextIsPathEmpty(k));
		CGContextRelease(k);
		CGPathRelease(rp);
	}

	/* --- THE TYPE IDENTITIES: one per class, stable, and never shared ------------------ */
	/* THE PROPERTY A TYPE ID EXISTS FOR IS THE PAIR: the same class always answers the same number, and two
	 * classes never answer the same one. Nothing here can check a CONSTANT against Apple's (its ids are
	 * assigned by its runtime and published nowhere), so what is checked is the contract: non-zero, stable,
	 * and pairwise distinct. THE TWO MISSING DOORS ARE NAMED WHERE THEY ARE OWED: `CGDataConsumerGetTypeID`
	 * and `CGLayerGetTypeID` have no class in this library to identify. */
	{
		CFTypeID ids[11];
		int i, j, nonzero = 1, distinct = 1;

		ids[0] = CGColorGetTypeID();
		ids[1] = CGColorSpaceGetTypeID();
		ids[2] = CGContextGetTypeID();
		ids[3] = CGDataProviderGetTypeID();
		ids[4] = CGFontGetTypeID();
		ids[5] = CGFunctionGetTypeID();
		ids[6] = CGGradientGetTypeID();
		ids[7] = CGImageGetTypeID();
		ids[8] = CGPathGetTypeID();
		ids[9] = CGPatternGetTypeID();
		ids[10] = CGShadingGetTypeID();

		for (i = 0; i < 11; i++) {
			if (ids[i] == 0) {
				nonzero = 0;
			}
			for (j = i + 1; j < 11; j++) {
				if (ids[i] == ids[j]) {
					distinct = 0;
				}
			}
		}
		check("every class has a type id and none of them is the invalid zero", nonzero);
		check("...and no two classes share one, which is the whole point of a type id", distinct);
		check("...and asking twice gives the same answer: a constant, not an allocation",
		      ids[0] == CGColorGetTypeID() && ids[6] == CGGradientGetTypeID());
	}

	printf("CG-PROBE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
