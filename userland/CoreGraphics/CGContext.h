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
 * WHAT IS DELIBERATELY ABSENT FROM C2, and why it is absent rather than stubbed:
 * stroking (`CGContextStrokePath`, `SetLineWidth`, `SetLineCap`, `SetLineJoin`,
 * `SetLineDash`, the path-offsetting machinery) — a stroke is a path operation that
 * produces a fill, and half of it would draw the wrong shape convincingly; the
 * context's own path clip (`CGContextClip`, `CGContextEOClip`); text; images;
 * gradients; and the text/pattern/colour state setters. They arrive in C3–C6 with the
 * machinery that makes them mean something.
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

#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGPath.h>

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
 * `kCGBlendModePlusDarker` IS ABSENT, deliberately: pixman has no operator that is
 * the same function, so the choice would be between something close and something
 * right. `PIXMAN_OP_SATURATE` and `PIXMAN_OP_ADD` are the two near misses and neither
 * is it. That row stays `open` in the ledger, which is where an unimplemented name
 * belongs.
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
void CGContextResetClip(CGContextRef context);
CGRect CGContextGetClipBoundingBox(CGContextRef context);

/* Fill colour, alpha and compositing. The component setters are the C2's colour
 * surface; `…WithColor` needs `CGColor`, which is C4, and `SetFillColorSpace` is C4's
 * too. Components are 0…1 and the colour is NOT premultiplied — the multiplication
 * happens when the pixel is built. */
void CGContextSetGrayFillColor(CGContextRef context, CGFloat gray, CGFloat alpha);
void CGContextSetRGBFillColor(CGContextRef context, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);
void CGContextSetAlpha(CGContextRef context, CGFloat alpha);
void CGContextSetBlendMode(CGContextRef context, CGBlendMode mode);

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

/* `StrokePath` CONSUMES the current path, like the fills do. `StrokeRect`,
 * `StrokeRectWithWidth` and `StrokeLineSegments` build their own path and leave the
 * current one alone, and `StrokeRectWithWidth` uses the width it was given WITHOUT
 * changing the context's line width. */
void CGContextStrokePath(CGContextRef context);
void CGContextStrokeRect(CGContextRef context, CGRect rect);
void CGContextStrokeRectWithWidth(CGContextRef context, CGRect rect, CGFloat width);
/* `points` is an array of `count` COORDINATES — x0, y0, x1, y1, … — so count/2 segments:
 * (x0,y0)-(x1,y1), (x2,y2)-(x3,y3), … An odd count leaves one coordinate with no partner,
 * which is not a segment and is ignored rather than guessed at. */
void CGContextStrokeLineSegments(CGContextRef context, const CGPoint *points, size_t count);

/* `DrawPath` is the general entry. THE TWO COMBINED MODES FILL FIRST AND STROKE SECOND,
 * with the fill colour and then the stroke colour, FROM THE SAME PATH — which is why the
 * path is consumed at the end rather than by the first of the two. */
void CGContextDrawPath(CGContextRef context, CGPathDrawingMode mode);
/* Replaces the current path with ITS OWN STROKE OUTLINE, in the context's user space and
 * with the current line state, so that a later fill of the path draws the stroke. That is
 * Apple's contract for it and the reason it exists: it is the stroke as geometry, before
 * any colour is involved. */
void CGContextReplacePathWithStrokedPath(CGContextRef context);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGCONTEXT_H */
