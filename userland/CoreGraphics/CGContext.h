/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGContext.h — the drawing context, as C2 defines it (C2 = state, CTM, clip, fill).
 *
 * THE SHAPE OF A CONTEXT HERE: a destination SURFACE, a graphics state (CTM, clip,
 * fill colour, alpha, blend mode, antialiasing), and a stack of saved copies of that
 * state. Drawing a filled path means: take the recorded path, transform it into
 * DEVICE space through the CTM, turn it into trapezoids, and hand those to pixman.
 * Everything below the context is pixman's job, and everything above it is a caller's.
 *
 * WHAT IS DELIBERATELY ABSENT, AND WHY IT IS ABSENT RATHER THAN STUBBED: text, images,
 * text and shadows. **THE CONTEXT'S OWN PATH CLIP USED TO BE ON THIS LIST AND IS NOT ANY MORE**
 * (C8.6): `CGContextClip` and `CGContextEOClip` are implemented for a RECTILINEAR path, where a
 * device-space region of rectangles is EXACT, AND A SLANTED OR CURVED EDGE IS NO LONGER REFUSED
 * either: the MASK half this note used to call owed is IMPLEMENTED, so a clip path of any shape now
 * clips with COVERAGE. The half-present machinery the note warned about was real and the answer was
 * to build it rather than fake it.
 *
 * THE STROKE HALF OF THIS LIST IS GONE, and the colour half has moved: C3 landed the
 * stroking (`CGContextStrokePath`, `SetLineWidth`, `SetLineCap`, `SetLineJoin`,
 * `SetLineDash` and the rest of the line state), and `CGColor` landed the colour state
 * setters. What is left of the colour story is the part that needs a CONVERSION — a colour
 * in an sRGB or ICC or CMYK space — and that is refused rather than guessed at until the
 * colour engine (C4.2) can say what its numbers mean.
 *
 * THE DEFAULT COORDINATE SYSTEM IS APPLE'S FOR A BITMAP CONTEXT: user space has its
 * ORIGIN AT THE LOWER-LEFT and y increasing UPWARD, so the default CTM is
 * (1, 0, 0, -1, 0, height) — the flip that makes `CGContextGetCTM` read the way a
 * UIKit developer expects. It is asserted in the probe rather than asserted here:
 * user-space (0,0) must be the BOTTOM-left pixel and (0,height) the top-left one.
 *
 * ONE CONSEQUENCE WORTH STATING: that flip has a NEGATIVE determinant, so it mirrors
 * the geometry, which reverses the sign of every winding number the fill computes.
 * Nothing needs to care — non-zero is non-zero either way — and that is the reason the
 * rasterizer can use the sign of an edge's dy as its winding contribution without
 * knowing whether the CTM is mirrored.
 */
#ifndef CORE_GRAPHICS_CGCONTEXT_H
#define CORE_GRAPHICS_CGCONTEXT_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGGeometry.h>
/* AND CGGradient.h, FOR THE SAME REASON CGImage.h IS HERE: Apple declares the gradient DRAWS in the
 * context's header and the gradient TYPE in the gradient's, so a caller who includes CGContext.h
 * alone must have both. */
#include <CoreGraphics/CGGradient.h>
/* AND CGShading.h, ON THE SAME REASON — the shading's draw verb is declared below, so a caller who
 * includes CGContext.h alone must have the type. */
#include <CoreGraphics/CGShading.h>
/* AND CGImage.h, FOR THE IMAGE TYPE AND FOR `CGContextDrawImage`: Apple declares that function in
 * the context's header, and a caller who includes only CGContext.h has to reach it — which is
 * exactly why this include is here rather than the declaration being read somewhere else. */
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGPath.h>
/* AND CGPattern.h, FOR THE TYPE THE THREE PATTERN SETTERS BELOW NAME. */
#include <CoreGraphics/CGPattern.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGContext *CGContextRef;

/*
 * EVERY CASE NAME HERE IS APPLE'S, TAKEN FROM THE C0 LEDGER (which carries all 28 of
 * them), and the VALUES are this tree's, as everywhere Apple publishes names and no
 * numbers.
 *
 * THEY MAP ONTO PIXMAN OPERATORS ALMOST ONE FOR ONE, and that is not a coincidence to
 * be pleased about but a fact to be relied on: both follow the PDF blend-mode
 * definitions, so `kCGBlendModeMultiply` IS `PIXMAN_OP_MULTIPLY` and the probe can
 * assert the arithmetic rather than the label.
 *
 * `kCGBlendModePlusDarker` IS ABSENT AND THAT ROW STAYS `open`. THE FIRST REASON GIVEN FOR IT HERE
 * WAS TRUE BUT INCOMPLETE, and the second half is the one that decides it: pixman has no operator
 * that IS this function — `PIXMAN_OP_SATURATE` and `PIXMAN_OP_ADD` are the two near misses — BUT
 * THE FUNCTION IS REACHABLE AS A SEQUENCE. Plus-darker is `max(0, S + D - 1)`, which is
 * `~(min(1, ~D + ~S))`: invert the destination, add the inverted source with `PIXMAN_OP_ADD`, invert
 * back. THE SEQUENCE IS EXACT ONLY WHEN BOTH THE SOURCE AND THE DESTINATION ARE OPAQUE, and a fill in
 * this library is antialiased, so `alpha_s < 1` along every edge of every shape. The general case is
 * the PDF composite with the blend applied to UNPREMULTIPLIED components, which needs a per-pixel path
 * this library's substrate does not expose. SO THE CHOICE WOULD STILL BE between something close and
 * something right, and it is made the same way: the name is not declared, the row is owed, and the
 * reason is written here rather than rediscovered.
 */
typedef enum {
	kCGBlendModeNormal = 0,
	kCGBlendModeMultiply,
	kCGBlendModeScreen,
	kCGBlendModeOverlay,
	kCGBlendModeDarken,
	kCGBlendModeLighten,
	kCGBlendModeColorDodge,
	kCGBlendModeColorBurn,
	kCGBlendModeSoftLight,
	kCGBlendModeHardLight,
	kCGBlendModeDifference,
	kCGBlendModeExclusion,
	kCGBlendModeHue,
	kCGBlendModeSaturation,
	kCGBlendModeColor,
	kCGBlendModeLuminosity,
	kCGBlendModeClear,
	kCGBlendModeCopy,
	kCGBlendModeSourceIn,
	kCGBlendModeSourceOut,
	kCGBlendModeSourceAtop,
	kCGBlendModeDestinationOver,
	kCGBlendModeDestinationIn,
	kCGBlendModeDestinationOut,
	kCGBlendModeDestinationAtop,
	kCGBlendModeXOR,
	kCGBlendModePlusLighter
} CGBlendMode;

/* Lifetime. A context is a counted object; the bitmap constructor is
 * `CGBitmapContextCreate` (CGBitmapContext.h), which is implemented in CGContext.c
 * because a bitmap context IS a context with a particular surface. */
CGContextRef CGContextRetain(CGContextRef context);
void CGContextRelease(CGContextRef context);

/* `Flush` and `Synchronize` are Apple's "make sure the drawing has arrived". For a
 * bitmap context the drawing is already in the caller's bytes when these return, so
 * both are honest no-ops that VALIDATE their argument instead of pretending to have
 * work to do. They exist because a caller writing code against a windowing context
 * will call them, and a missing symbol would make that code uncompilable for no
 * reason. */
void CGContextFlush(CGContextRef context);
void CGContextSynchronize(CGContextRef context);

/* The graphics state. */
void CGContextSaveGState(CGContextRef context);
void CGContextRestoreGState(CGContextRef context);

/* The CTM. `CGContextGetCTM` returns the USER-TO-DEVICE transform including the
 * default flip, which is why it reads (1, 0, 0, -1, 0, height) on a fresh bitmap
 * context. `GetUserSpaceToDeviceSpaceTransform` is the same matrix under the name
 * Apple gives it; both exist because both are rows in the ledger and a caller may
 * use either. */
CGAffineTransform CGContextGetCTM(CGContextRef context);
CGAffineTransform CGContextGetUserSpaceToDeviceSpaceTransform(CGContextRef context);
void CGContextConcatCTM(CGContextRef context, CGAffineTransform transform);
void CGContextTranslateCTM(CGContextRef context, CGFloat tx, CGFloat ty);
void CGContextScaleCTM(CGContextRef context, CGFloat sx, CGFloat sy);
void CGContextRotateCTM(CGContextRef context, CGFloat angle);

/* The coordinate conversions, which are the CTM and its inverse made callable. They
 * are here in C2 because they are two lines each and they let a probe assert the
 * default flip from both directions. */
CGPoint CGContextConvertPointToDeviceSpace(CGContextRef context, CGPoint point);
CGPoint CGContextConvertPointToUserSpace(CGContextRef context, CGPoint point);
CGSize CGContextConvertSizeToDeviceSpace(CGContextRef context, CGSize size);
CGSize CGContextConvertSizeToUserSpace(CGContextRef context, CGSize size);
CGRect CGContextConvertRectToDeviceSpace(CGContextRef context, CGRect rect);
CGRect CGContextConvertRectToUserSpace(CGContextRef context, CGRect rect);

/* The clip. C2's clip is a REGION OF RECTANGLES in device space, which is what
 * `pixman_image_set_clip_region32` is for, so `CGContextClipToRect` intersects the
 * region with the transformed rectangle. SEE THE ONE CASE IT REFUSES, in
 * CGContext.c: under a CTM with rotation or skew the transformed rectangle is not a
 * rectangle, and clipping to its bounding box would clip away pixels the caller asked
 * to keep — a silent wrong answer, so it refuses by name instead. */
void CGContextClipToRect(CGContextRef context, CGRect rect);
/*
 * THE PATH CLIP, WHICH C2 DELIBERATELY LEFT OUT AND THIS CLOSES FOR THE CASE THAT CAN BE EXACT.
 * C2's note says the context's own clip "needs machinery that would otherwise be half-present", and
 * that is exactly right: a clip is stored here as a REGION OF DEVICE-SPACE RECTANGLES, so a path can
 * be a clip only when its edges land on that grid.
 *
 * SO A RECTILINEAR PATH IS EXACT AND ANY OTHER SHAPE BECOMES A COVERAGE MASK. The path is flattened
 * and swept into the same device-space trapezoids a FILL uses; every trapezoid of a rectilinear path is
 * a RECTANGLE and those rectangles are unioned into the clip REGION, and a path with a slanted or
 * curved edge is rasterised instead into an 8-BIT COVERAGE MASK over the surface, so the clip has two
 * halves and both are applied. THAT SECOND HALF IS APPLIED IN ALL THREE COMPOSITES — the trapezoid
 * fills (the path's coverage times the clip's, through a temporary), the clip-only paints such as
 * gradients and shadings (the mask slot is free there), and the hand-written image blit (which
 * composites pixel by pixel and consults the mask itself). A mask that reached only some of them would
 * be worse than none, which is why each one is checked in its own probe.
 *
 * A ROTATED OR SKEWED CTM IS STILL REFUSED HERE, and it is the one refusal of this pair that the mask
 * did not remove: the check is made before the path is swept, because a rotated clip would need the
 * mask half for EVERY path rather than only the curved ones. `CGContextClipToRect` refuses a rotation
 * for its own reason too (an axis-aligned region cannot hold a parallelogram), so a rotated rectangle
 * clip needs a mask as well. Both are named residual work rather than silently approximated by a
 * bounding box that would keep pixels the caller asked to exclude. `EOClip` uses the even-odd rule and `Clip` the non-zero one, which is
 * the same pair the fills use. BOTH CONSUME THE CURRENT PATH, as Apple's do.
 */
void CGContextClip(CGContextRef context);
void CGContextEOClip(CGContextRef context);
/* !! `CGContextResetClip` STOOD HERE AND IS INTERNAL NOW (2026-10-05): the 10.6 headers do not
 * carry it, so a caller of this era cannot name it. THE PRIMITIVE STAYS, as `cg_context_reset_clip`
 * in CGContext_internal.h, because the AppKit's own `-setClip:` is a REPLACE and this library's
 * clip is CoreGraphics' — see NSBezierPath.m, the one shipped consumer. An era caller replaces a
 * clip the way the era did: `CGContextSaveGState`/`RestoreGState`, or `CGContextClipToRect`. */
CGRect CGContextGetClipBoundingBox(CGContextRef context);

/* Fill colour, alpha and compositing. The component setters are the C2's colour
 * surface; `…WithColor` needs `CGColor`, which is C4, and `SetFillColorSpace` is C4's
 * too. Components are 0…1 and the colour is NOT premultiplied — the multiplication
 * happens when the pixel is built. */
void CGContextSetGrayFillColor(CGContextRef context, CGFloat gray, CGFloat alpha);
void CGContextSetRGBFillColor(CGContextRef context, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);
void CGContextSetAlpha(CGContextRef context, CGFloat alpha);
void CGContextSetBlendMode(CGContextRef context, CGBlendMode mode);

/*
 * THE INTERPOLATION QUALITY, WHICH IS HOW `CGContextDrawImage` SAMPLES WHEN IT SCALES. The names
 * are Apple's, from the C0 ledger; **THE VALUES ARE OURS, AND `None` IS 0 ON PURPOSE**: Apple
 * publishes case names and no numbers (the same rule Foundation's enums follow), and a zeroed
 * graphics state should mean the behaviour this library had before the knob existed — NEAREST —
 * rather than a side effect of a different ordering. THE DEFAULT IS THEREFORE STATED RATHER THAN
 * INHERITED: a fresh context samples nearest, and a caller asks for anything else.
 *
 * TWO OF THE FIVE ARE REFUSED BY NAME AT THE SETTER, and that is a limitation named rather than
 * hidden: this library has TWO samplers — nearest and bilinear — so `Default` and `Medium` are
 * bilinear, while `Low` and `High` ask for a third and a fourth filter that do not exist here.
 * Accepting them would give three names one behaviour, which is the silent collapse this tree
 * refuses elsewhere (`kCGBlendModePlusDarker`, `kCGPatternTilingConstantSpacing`).
 */
typedef enum {
	kCGInterpolationNone = 0,
	kCGInterpolationDefault,
	kCGInterpolationLow,
	kCGInterpolationMedium,
	kCGInterpolationHigh
} CGInterpolationQuality;

void CGContextSetInterpolationQuality(CGContextRef context, CGInterpolationQuality quality);
CGInterpolationQuality CGContextGetInterpolationQuality(CGContextRef context);

/* Antialiasing. `SetAllowsAntialiasing` is the context's capability and
 * `SetShouldAntialias` is the drawing state; a fill is antialiased only when BOTH say
 * so, which is the reading this tree gives the two names. The mechanism is real
 * rather than ignored: pixman rasterizes the trapezoids into an 8-bit mask when
 * antialiasing is on and a 1-BIT mask when it is off, which is exactly "coverage" and
 * "no coverage" with no first-party rounding in between. */
void CGContextSetAllowsAntialiasing(CGContextRef context, int allows);
void CGContextSetShouldAntialias(CGContextRef context, int antialias);

/* The path. `CGContextAddPath` appends the path's subpaths to the current path; the
 * path is in USER space and stays there until something is painted. */
void CGContextBeginPath(CGContextRef context);
void CGContextMoveToPoint(CGContextRef context, CGFloat x, CGFloat y);
void CGContextAddLineToPoint(CGContextRef context, CGFloat x, CGFloat y);
void CGContextAddRect(CGContextRef context, CGRect rect);
void CGContextAddPath(CGContextRef context, CGPathRef path);
void CGContextClosePath(CGContextRef context);
int CGContextIsPathEmpty(CGContextRef context);
/* USER SPACE, which is the space the path is in — see the header note above. */
CGRect CGContextGetPathBoundingBox(CGContextRef context);

/* THE CONTEXT'S OWN CONSTRUCTORS. EACH IS A ONE-LINE PASSTHROUGH TO THE PATH FUNCTION OF
 * THE SAME NAME — `CGContextAddArc` calls `CGPathAddArc` on the context's path — and they
 * are here because this is the spelling a caller reaches for: without them, drawing a
 * circle means allocating a path, adding it, and releasing it. There is no state and no
 * second geometry; the passthrough IS the implementation, which is why the arc probe checks
 * one of them through the pixels rather than trusting that the wiring is right.
 *
 * `CGContextAddLines` IS A POLYLINE (one subpath through every point) while
 * `CGContextAddRects` adds each rectangle as a CLOSED SUBPATH of its own; in both, `count`
 * is the number of POINTS or RECTANGLES and not of coordinates, which is Apple's reading and
 * the one that keeps a caller from reading past the end of their array. */
void CGContextAddLines(CGContextRef context, const CGPoint *points, size_t count);
void CGContextAddRects(CGContextRef context, const CGRect *rects, size_t count);
void CGContextAddQuadCurveToPoint(CGContextRef context, CGFloat cpx, CGFloat cpy, CGFloat x,
				  CGFloat y);
void CGContextAddCurveToPoint(CGContextRef context, CGFloat cp1x, CGFloat cp1y, CGFloat cp2x,
			      CGFloat cp2y, CGFloat x, CGFloat y);
void CGContextAddArc(CGContextRef context, CGFloat x, CGFloat y, CGFloat radius,
		     CGFloat startAngle, CGFloat endAngle, bool clockwise);
void CGContextAddEllipseInRect(CGContextRef context, CGRect rect);
void CGContextAddRoundedRect(CGContextRef context, CGRect rect, CGFloat cornerWidth,
			     CGFloat cornerHeight);

/* THE IMAGE'S DRAWN FORM, DECLARED WHERE A CONTEXT'S OPERATIONS LIVE — and `CGContext.h` includes
 * `CGImage.h` for the type, so a caller who includes this header alone has everything. The IMAGE
 * itself is that header's; what is here is the verb. */
void CGContextDrawImage(CGContextRef context, CGRect rect, CGImageRef image);

/*
 * AND THE GRADIENT'S DRAWN FORMS, WHICH FOLLOW THE IMAGE'S ARRANGEMENT EXACTLY: the gradient is
 * CGGradient.h's, the VERBS are here, and they are implemented in CGGradient.c because that is where
 * the ramp's arithmetic lives. This is not a new shape — `CGContextDrawImage` above is declared here
 * and defined in the image's file for the same reason.
 *
 * WHAT THESE PAINT IS THE CLIP, NOT THE PATH, which is Apple's contract and the reason they take no
 * path: a gradient is a way of filling the current clipping region, and a caller who wants it inside
 * a shape clips to that shape first. The current path is UNTOUCHED — no `BeginPath` is implied and
 * none happens — so a caller may clip, draw a gradient, and then fill the path they had.
 *
 * THE TWO POINTS ARE USER SPACE, and they are the ramp's axis: `startPoint` gets the first stop's
 * colour and `endPoint` the last. They are deliberately not called "left" and "right" — the axis may
 * point anywhere on the page. `options` is what happens BEYOND those two points, and neither flag is
 * the useful default: with neither, only the band between the points is painted and the rest of the
 * clip is left exactly as it was.
 *
 * THE RADIAL FORM BLENDS BETWEEN TWO CIRCLES, and the second may be offset from the first, which is
 * what makes a cone rather than a circle. Either radius may be zero (a point), and the two centres
 * may coincide (concentric) — the degenerate cases are answered in CGGradient.c rather than refused,
 * because Apple's own documentation draws them.
 */
void CGContextDrawLinearGradient(CGContextRef context, CGGradientRef gradient, CGPoint startPoint,
				 CGPoint endPoint, CGGradientDrawingOptions options);
void CGContextDrawRadialGradient(CGContextRef context, CGGradientRef gradient,
				 CGPoint startCenter, CGFloat startRadius, CGPoint endCenter,
				 CGFloat endRadius, CGGradientDrawingOptions options);
/* !! `CGContextDrawConicGradient` STOOD HERE AND WAS REMOVED (2026-10-05) — THE ANGULAR RAMP IS
 * macOS 14.0 and this duplication is a 10.6-era surface. What it did is not lost so much as
 * re-spelled: an angular ramp between two stops is `CGContextDrawLinearGradient` (10.5) rotated,
 * and the only thing the conic form had of its own was that it WRAPS — which is why it took no
 * drawing-options parameter and why nothing else needed changing when it went. */

/*
 * AND THE SHADING'S DRAWN FORM, WHICH IS THE SAME CONTRACT AS THE GRADIENTS ABOVE: it paints the
 * CLIP, not the path, and the current path is untouched. The difference is entirely in where the
 * colours come from — a shading carries its own geometry (it was built AXIAL or RADIAL) and calls the
 * caller's function for a colour, so there is nothing to pass but the object.
 */
void CGContextDrawShading(CGContextRef context, CGShadingRef shading);

/* The current point OF THE PATH, in user space: the pen position the last move or add left
 * behind. */
CGPoint CGContextGetPathCurrentPoint(CGContextRef context);

/* Painting. `FillPath` uses the non-zero winding rule and `EOFillPath` the even-odd
 * one; both consume the current path (Apple: "the current path is cleared").
 * `FillRect` does NOT touch the current path, which the probe asserts. */
void CGContextFillPath(CGContextRef context);
void CGContextEOFillPath(CGContextRef context);
void CGContextFillRect(CGContextRef context, CGRect rect);
/* `ClearRect` clears to TRANSPARENT BLACK regardless of the current fill colour, which
 * is the whole point of it. Whether the current blend mode applies is a question this
 * tree cannot settle without the SDK, so it does not apply it, and the probe asserts
 * the clear. */
void CGContextClearRect(CGContextRef context, CGRect rect);

/* The line state, and stroking. A STROKE IS A PATH WHOSE NON-ZERO FILL IS THE STROKE
 * (`CGPathCreateCopyByStrokingPath`, CGPath.h), so everything here is plumbing around
 * that one fact: the width, the cap, the join and the miter limit go INTO the stroked
 * path, and the outline is filled with the STROKE colour.
 *
 * THE DEFAULTS ARE APPLE'S: width 1, butt caps, miter joins, miter limit 10. */
void CGContextSetLineWidth(CGContextRef context, CGFloat width);
void CGContextSetLineCap(CGContextRef context, CGLineCap cap);
void CGContextSetLineJoin(CGContextRef context, CGLineJoin join);
void CGContextSetMiterLimit(CGContextRef context, CGFloat limit);
void CGContextSetGrayStrokeColor(CGContextRef context, CGFloat gray, CGFloat alpha);
void CGContextSetRGBStrokeColor(CGContextRef context, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);

/* THE COLOUR-TAKING FORMS, declared here because a context is what they set and this header
 * includes CGColor.h for the type. A colour this library cannot INTERPRET — anything that is
 * not a device RGB or device grayscale space — is REFUSED and the context keeps the colour it
 * had: conversion between colour spaces is the colour engine's job (C4.2), and drawing a
 * profile's raw numbers as if they were device values paints something confident and wrong.
 * A NULL colour is the same refusal, not a reset to black. */
void CGContextSetFillColorWithColor(CGContextRef context, CGColorRef color);
void CGContextSetStrokeColorWithColor(CGContextRef context, CGColorRef color);

/* `StrokePath` CONSUMES the current path, like the fills do. `StrokeRect`,
 * `StrokeRectWithWidth` and `StrokeLineSegments` build their own path and leave the
 * current one alone, and `StrokeRectWithWidth` uses the width it was given WITHOUT
 * changing the context's line width. */
void CGContextStrokePath(CGContextRef context);
void CGContextStrokeRect(CGContextRef context, CGRect rect);
void CGContextStrokeRectWithWidth(CGContextRef context, CGRect rect, CGFloat width);
/* `points` is an array of `count` POINTS — x0,y0 x1,y1 x2,y2 … — treated as PAIRS, so
 * (x0,y0)-(x1,y1), (x2,y2)-(x3,y3) and so on: count/2 segments. COUNT IS THE NUMBER OF
 * POINTS AND NOT OF COORDINATES, which is Apple's reading and there is a safety reason to
 * say so twice — a caller who read the first version of this comment and passed twice as
 * many would have sent the function past the end of their array. An odd count leaves one
 * point with no partner, which is not a segment and is dropped rather than paired with
 * something invented. THE SAME READING APPLIES TO `CGContextAddLines`, whose count is the
 * number of points on its polyline. */
void CGContextStrokeLineSegments(CGContextRef context, const CGPoint *points, size_t count);

/* `DrawPath` is the general entry. THE TWO COMBINED MODES FILL FIRST AND STROKE SECOND,
 * with the fill colour and then the stroke colour, FROM THE SAME PATH — which is why the
 * path is consumed at the end rather than by the first of the two. */
void CGContextDrawPath(CGContextRef context, CGPathDrawingMode mode);

/*
 * THE PATTERN PAINTS (C6.3). A pattern is the third kind of paint, after a colour and a gradient: it
 * is set as STATE rather than drawn, and every fill or stroke afterwards uses it until something
 * replaces it — which is why the two setters are named `SetFill…` and `SetStroke…` and not `Draw…`,
 * and why `CGContextSetFillColorWithColor` CLEARS a pattern that was set (a colour and a pattern are
 * two answers to the same question and the later one wins; see CGColor.h for the pattern colour,
 * which is the other door into this state).
 *
 * `components` is `1 + n`, where `n` is the base colour space's component count: the alpha LAST, and
 * the leading values selecting a colour in the base space. THIS LIBRARY SHIPS THE COLOURED CASE ONLY,
 * where `n` is zero and `components[0]` is the alpha — see `CGColorSpaceCreatePattern`, which refuses
 * an uncoloured space for the reason CGPattern.h gives.
 *
 * AND THE PHASE IS IN USER SPACE, applied to the pattern's origin: a phase of (px, py) puts the
 * tiling's origin at that point of the user space of whatever draws next. It is a SIZE and not a
 * POINT, Apple's spelling, because a phase is a displacement.
 */
void CGContextSetFillPattern(CGContextRef context, CGPatternRef pattern, const CGFloat *components);
void CGContextSetStrokePattern(CGContextRef context, CGPatternRef pattern, const CGFloat *components);
void CGContextSetPatternPhase(CGContextRef context, CGSize phase);

/* THE DASH PATTERN IS PART OF THE LINE STATE, so it is saved and restored, and it applies to
 * EVERY stroke the context draws — but to the PATH rather than to the pixels: the dashes are
 * pieces of the path, and each is stroked with its own caps and joins. A NULL `lengths` or a
 * `count` of zero is how a caller goes back to a SOLID line. The pattern is held in a bounded
 * array in the graphics state, so one longer than the state can hold is REFUSED rather than
 * truncated — a caller never gets dashes they did not ask for. */
void CGContextSetLineDash(CGContextRef context, CGFloat phase, const CGFloat *lengths, size_t count);
/* Replaces the current path with ITS OWN STROKE OUTLINE, in the context's user space and
 * with the current line state, so that a later fill of the path draws the stroke. That is
 * Apple's contract for it and the reason it exists: it is the stroke as geometry, before
 * any colour is involved. */
void CGContextReplacePathWithStrokedPath(CGContextRef context);

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------- */
/* Text: the state, and one drawing door                                     */
/* ------------------------------------------------------------------------- */

/* THE FONT TYPE IS NAMED BY THE DOORS BELOW, so this header includes the header that defines it. */
#include <CoreGraphics/CGFont.h>

/* APPLE'S EIGHT MODES, IN APPLE'S ORDER — the values are written out although Apple's own header
 * leaves them IMPLICIT. SIX OF THEM DRAW: Fill through the rasterised mask, and Stroke, FillStroke, Clip
 * and FillClip through the GLYPH'S OUTLINE as a path (`cg_font_glyph_outline`) — the same geometry a PDF
 * context and Core Text need — while Invisible draws nothing and still advances, which is the mode a
 * caller measures with. THE TWO THAT REMAIN REFUSE BY NAME, AND NOT FOR LACK OF MACHINERY: StrokeClip and
 * FillStrokeClip both need a decision the 10.6 header does not make — whether the clip is the glyph's
 * OUTLINE or the STROKED region around it — and this library does not invent contracts. */
typedef enum {
	kCGTextFill = 0,
	kCGTextStroke = 1,
	kCGTextFillStroke = 2,
	kCGTextInvisible = 3,
	kCGTextFillClip = 4,
	kCGTextStrokeClip = 5,
	kCGTextFillStrokeClip = 6,
	kCGTextClip = 7
} CGTextDrawingMode;

void CGContextSetFont(CGContextRef context, CGFontRef font);
void CGContextSetFontSize(CGContextRef context, CGFloat size);
void CGContextSetTextMatrix(CGContextRef context, CGAffineTransform t);
CGAffineTransform CGContextGetTextMatrix(CGContextRef context);
void CGContextSetTextPosition(CGContextRef context, CGFloat x, CGFloat y);
CGPoint CGContextGetTextPosition(CGContextRef context);
void CGContextSetCharacterSpacing(CGContextRef context, CGFloat spacing);

/* SUBPIXEL PEN POSITIONS, AND THE FOUR DOORS THAT GOVERN THEM. Apple's rule, from the 10.6 header's
 * own comment: a context places glyphs at subpixel positions IF fonts will be antialiased when drawn AND
 * `allowsFontSubpixelPositioning` AND `shouldSubpixelPositionFonts` are true. All four terms are
 * implemented — the two antialiasing settings this library already had, and the two flags — and the
 * ASYMMETRY IN WHERE THEY LIVE IS TRANSCRIBED: the header says the `Allows` parameter "is not part of
 * the graphics state", so that flag is the context's, while its `Should` twin is saved and restored with
 * everything else. WITH POSITIONING OFF THE PEN IS ROUNDED TO WHOLE DEVICE PIXELS, which is exactly what
 * this library did before it could do better.
 *
 * THE OTHER FOUR SETTERS EXIST AND REFUSE BY NAME, because each is a rendering path this library does
 * not have rather than a flag it forgot: smoothing is LCD subpixel ANTIALIASING (three samples per pixel
 * and a filter along their order), and quantisation is refused because the header says a context
 * "quantizes subpixel positions" and NEVER SAYS TO WHAT — choosing a quantum would be inventing a
 * contract rather than duplicating one. */
void CGContextSetAllowsFontSubpixelPositioning(CGContextRef context, bool allows);
void CGContextSetShouldSubpixelPositionFonts(CGContextRef context, bool should);
void CGContextSetAllowsFontSmoothing(CGContextRef context, bool allows);
void CGContextSetShouldSmoothFonts(CGContextRef context, bool should);
void CGContextSetAllowsFontSubpixelQuantization(CGContextRef context, bool allows);
void CGContextSetShouldSubpixelQuantizeFonts(CGContextRef context, bool should);
void CGContextSetTextDrawingMode(CGContextRef context, CGTextDrawingMode mode);

/* POSITIONS ARE IN USER SPACE — AND THAT IS A CORRECTION, NOT A CHOICE. This door first shipped with
 * "positions are in text space" pinned in this very comment, on the reasoning that Core Text sets the
 * text matrix and passes positions along a line. THE 10.6 HEADER SAYS OTHERWISE, in its own words and
 * twice: "Draw `glyphs` ... at the points specified by `positions` ... the positions are specified in
 * user space", and the same for `ShowGlyphsAtPoint`'s point and `ShowGlyphsWithAdvances`' advances.
 * The PUBLIC HEADER IS THE CONTRACT THIS LIBRARY DUPLICATES, so user space it is: the TEXT MATRIX
 * TRANSFORMS THE GLYPH (its outline, its size), and the CTM alone places the pen. A pure TRANSLATION
 * in the text matrix moves the ink under either reading — which is why the probe's original check could
 * not tell them apart — so the probe now distinguishes them the only way that works: a SCALED text
 * matrix must scale the glyph WITHOUT moving it. */
void CGContextShowGlyphsAtPositions(CGContextRef context, const CGGlyph glyphs[],
				    const CGPoint positions[], size_t count);

/* THE THREE ADVANCE DOORS: the pen starts at the TEXT POSITION (in user space, see above), each glyph
 * is drawn where the pen is, and the pen then moves. `ShowGlyphs` uses the FONT'S OWN advances, scaled
 * from font units by the font size; `ShowGlyphsWithAdvances` uses the caller's, which the header above
 * says are in user space; `ShowGlyphsAtPoint` sets the text position first. CHARACTER SPACING IS ADDED
 * AFTER EACH GLYPH IN ALL THREE, which is what `CGContextSetCharacterSpacing` promises — that door was
 * recorded-but-unapplied until these existed, and the probe now measures the widening. */
void CGContextShowGlyphs(CGContextRef context, const CGGlyph glyphs[], size_t count);
void CGContextShowGlyphsAtPoint(CGContextRef context, CGFloat x, CGFloat y, const CGGlyph glyphs[],
				 size_t count);
void CGContextShowGlyphsWithAdvances(CGContextRef context, const CGGlyph glyphs[],
				      const CGSize advances[], size_t count);

/* THE TWO STRING DOORS, AND THE ENCODING THEY READ A BYTE WITH. Apple's own words: "each byte of the
 * string is mapped through the encoding vector of the current font to obtain the glyph to display".
 * THE ENUM BELOW CARRIES APPLE'S TWO VALUES, TRANSCRIBED IN APPLE'S ORDER (the 10.6 header leaves
 * them implicit: 0 and 1). AND ONLY ONE OF THE TWO IS REACHABLE TODAY: the door that SELECTS an
 * encoding is `CGContextSelectFont`, which needs the font registry and is therefore owed — so these
 * two doors read a byte as a CHARACTER CODE through the font's own map (see
 * `cg_font_glyph_for_byte`), which is this library's implementation of the font-specific encoding and
 * is stated rather than assumed. THE MACROMAN TABLE WAITS FOR THE DOOR THAT CAN CHOOSE IT. */
typedef enum {
	kCGEncodingFontSpecific = 0,
	kCGEncodingMacRoman = 1
} CGTextEncoding;

/* AND THE DOOR THAT NAMES A FONT **AND** AN ENCODING, which is why the encoding is a parameter HERE and
 * nowhere else — and why the MacRoman half of the enum was unreachable until this door existed.
 * A name this library cannot resolve is REFUSED BY NAME and the context keeps the font it had:
 * setting the font to nothing would turn a typo into a silent blank line. AND THE DECLARATION
 * SITS AFTER THE ENUM because it NAMES it — the trap CGPath.h records for `CGAffineTransform`,
 * met in a header of this library's own. */
void CGContextSelectFont(CGContextRef context, const char *name, CGFloat size,
			 CGTextEncoding textEncoding);

/* `length` IS THE COUNT, NOT A TERMINATOR: these doors draw exactly the bytes they are given, which
 * is what makes them usable on a slice of a buffer. A BYTE THIS FONT HAS NO GLYPH FOR IS SKIPPED —
 * no ink, no advance, no refusal — because a missing glyph is a font fact, not a caller error. */
void CGContextShowText(CGContextRef context, const char *string, size_t length);
void CGContextShowTextAtPoint(CGContextRef context, CGFloat x, CGFloat y, const char *string,
			      size_t length);

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGContextGetTypeID(void);

#endif /* CORE_GRAPHICS_CGCONTEXT_H */
