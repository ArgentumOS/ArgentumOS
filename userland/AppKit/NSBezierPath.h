/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBezierPath.h — a path, and the first thing in this tree that DRAWS (C8.5).
 *
 * THE PATH IS A CGPath AND THE LINE STATE IS THE PATH'S OWN, WHICH IS THE ONE STRUCTURAL DIFFERENCE
 * FROM `CGContext`. Core Graphics keeps the line width, the caps, the joins, the dash and the winding
 * rule in the CONTEXT's graphics state, and a stroke is a context operation; AppKit keeps them ON THE
 * PATH, and `-stroke` is a message to the path. So this object owns a mutable `CGPath` PLUS the six
 * pieces of line state, and drawing means: hand the path to the current context, set the context's
 * line state from OURS, and ask the context to draw it. Nothing else is stored and nothing is cached,
 * which is why `-CGPath` can be handed back whole (Apple has the same pair, `+bezierPathWithCGPath:`
 * and `-CGPath`, on macOS 10.13+ and the ledger has both as live rows).
 *
 * THAT ASYMMETRY HAS A CONSEQUENCE A CALLER CAN SEE, so it is stated rather than discovered: a stroke
 * OVERWRITES the current context's line width, caps and joins with the path's. Apple does the same and
 * this library does not save and restore around it, because a save/restore the caller did not ask for
 * would hide their own state changes from them.
 *
 * THE SURFACE IS PINNED, NOT RECALLED, and the selectors came from Apple's navigator tree — the
 * ledger reduces a method to its FIRST selector component, so a header written from the ledger alone
 * would have guessed every tail. Three enums (`NSLineCapStyle`, `NSLineJoinStyle`, `NSWindingRule`)
 * and their seven cases are here, because Apple's index files them under this class.
 *
 * WHAT IS NOT HERE, EACH WITH ITS REASON:
 *
 *   THE FLATNESS FAMILY, AND IT WOULD BE A SILENT NO-OP: `-flatness`, `-setFlatness:`,
 *   `+defaultFlatness` and `+setDefaultFlatness:`. This library's flattener picks its own tolerance
 *   from the CTM (`cg_traps_for_path` divides 0.1 by the CTM's scale, so a zoomed curve is not
 *   visibly faceted) and the CONTEXT's fill is what flattens, not the path. A path flatness would
 *   therefore be stored and never read. Making it real means threading a tolerance through
 *   `CGContextDrawPath`, which is CoreGraphics work and not this class's.
 *
 *   `-bezierPathByReversingPath`: `CGPath` here has no reversing operation, and synthesising one is a
 *   real piece of geometry rather than a forward. The FLATTENING copy IS here, because
 *   `CGPathCreateCopyByFlattening` exists.
 *
 *   AND THE PATH CLIP, WHICH IS HERE NOW BECAUSE COREGRAPHICS GREW ONE (C8.6): `-addClip`,
 *   `-setClip` and `+clipRect:` all forward to `CGContextClip`/`CGContextEOClip`/`CGContextClipToRect`,
 *   which did not exist when this class was first written. THEY INHERIT THAT CLIP'S BOUNDARY, and it is
 *   worth restating where a path author will meet it: a RECTILINEAR path clips EXACTLY, and a slanted
 *   or curved one clips too, through CoreGraphics' COVERAGE MASK half — which did not exist when this
 *   class was written and does now, so all three of these take any path shape. What remains refused at
 *   the CoreGraphics level is a clip under a ROTATED CTM, and the reason is stated there.
 *
 *   AND ONE SENTENCE OF THIS PARAGRAPH WAS WRONG AND STAYS CORRECTED: it said `-setClip` could not be
 *   built from reset-then-intersect because that "would be wrong under a transform". It is not wrong —
 *   the clip CoreGraphics stores is built in DEVICE space from the already-transformed path, so
 *   resetting and re-intersecting replaces the clip exactly, which is what `-setClip` means. The first
 *   version reasoned about the transform in USER space and drew the wrong conclusion.
 *
 *   AND DRAWING WITH NO CURRENT `NSGraphicsContext` DRAWS NOTHING, silently. That is Apple's
 *   behaviour by omission (there is no context to draw into) and it is stated here so a caller is not
 *   left wondering. The alternative — raising — would make an ordinary mistake fatal, and this tree's
 *   CoreGraphics entry points take the same position on a NULL context.
 *
 *   AND TWO OF THESE ARE NO LONGER TRUE, WHICH IS WHY THEY ARE STRUCK THROUGH HERE RATHER THAN DELETED:
 *   `-containsPoint:` was said to need a point-in-path test CoreGraphics does not have, and
 *   `-elementAtIndex:` to need its own slice — both are implemented now. The point-in-path test is a
 *   crossing count over the FLATTENED path (which this class can already produce), so it needs nothing
 *   from CoreGraphics that is not here; and the element model is declared above. The glyph constructors
 *   (`appendBezierPathWithCGGlyph…`, `+drawPackedGlyphs:atPoint:`) need `CGFont`, which this tree
 *   does not have.
 *
 * A DEVIATION, STATED: Apple's arc constructors take DEGREES with 0° along the positive x axis and
 * increasing counterclockwise; `CGPathAddArc` takes RADIANS. The conversion is here, in one place, and
 * the probe checks an arc's endpoint rather than trusting the arithmetic.
 */
#ifndef APPKIT_NSBEZIERPATH_H
#define APPKIT_NSBEZIERPATH_H

#import <CoreGraphics/CGAffineTransform.h>
#import <CoreGraphics/CGPath.h>
#import <Foundation/NSAffineTransform.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * THE THREE ENUMS APPLE FILES UNDER THIS CLASS. Every case maps one-to-one onto Core Graphics —
 * `NSLineCapStyleButt` IS `kCGLineCapButt` and so on — which is what makes the forwards below
 * one-liners. THE VALUES ARE OURS (Apple publishes case names and no numbers, the rule this tree
 * follows everywhere) and the mapping is an explicit switch rather than a cast, so an ordering that
 * differed could not silently swap two cases.
 */
typedef enum {
	NSLineCapStyleButt = 0,
	NSLineCapStyleRound,
	NSLineCapStyleSquare
} NSLineCapStyle;

typedef enum {
	NSLineJoinStyleMiter = 0,
	NSLineJoinStyleRound,
	NSLineJoinStyleBevel
} NSLineJoinStyle;

typedef enum {
	NSWindingRuleNonZero = 0,
	NSWindingRuleEvenOdd
} NSWindingRule;

/* THE ELEMENT MODEL: WHAT A PATH IS MADE OF, one step at a time.
 *
 * **AND `NSBezierPathElementCurveTo` IS DELIBERATELY ABSENT BECAUSE IT IS A `struck` ROW** — Apple
 * deprecates it at the vintage this tree pins, and it has no legitimate producer: a curve element is
 * either QUADRATIC or CUBIC, and Apple's own paths never come back as the ambiguous one. So this is a
 * model without an unreachable value rather than a model with a hole, and `-elementAtIndex:` can
 * therefore be total. THE NUMBERS ARE OURS (the policy: Apple publishes case names, never values). */
typedef enum {
	NSBezierPathElementMoveTo = 0,
	NSBezierPathElementLineTo = 1,
	NSBezierPathElementQuadraticCurveTo = 2,
	NSBezierPathElementCubicCurveTo = 3,
	NSBezierPathElementClosePath = 4
} NSBezierPathElement;

@interface NSBezierPath : NSObject <NSCopying>
{
	CGMutablePathRef _path;     /* RETAINED, and the geometry */
	NSWindingRule _windingRule;
	NSLineCapStyle _capStyle;
	NSLineJoinStyle _joinStyle;
	CGFloat _lineWidth;
	CGFloat _miterLimit;
	/* A BOUNDED DASH ARRAY IN THE OBJECT, for the reason the graphics state's is bounded: a dash
	 * pattern is a handful of numbers, and a caller's array must not become a pointer this object
	 * keeps after the call returns. */
	CGFloat _dash[16];
	NSUInteger _dashCount;
	CGFloat _dashPhase;
}

/* THE DOOR AND THE BRIDGE. `+bezierPath` is the empty one Apple's callers use; `+bezierPathWithCGPath:`
 * wraps an existing path (retaining it, so the caller may let go), and `-CGPath` hands it back whole
 * rather than as a copy — there is one representation and no second model to be lossy about. */
+ (NSBezierPath *)bezierPath;
+ (NSBezierPath *)bezierPathWithCGPath:(CGPathRef)cgPath;
@property (nullable, readonly) CGPathRef CGPath;

/* THE SHAPE CONSTRUCTORS, which are the common cases as one message each. */
+ (NSBezierPath *)bezierPathWithRect:(NSRect)rect;
+ (NSBezierPath *)bezierPathWithOvalInRect:(NSRect)rect;
+ (NSBezierPath *)bezierPathWithRoundedRect:(NSRect)rect xRadius:(CGFloat)xRadius
				    yRadius:(CGFloat)yRadius;

/* BUILDING IT. The relative forms are relative to the CURRENT POINT, which is why `-currentPoint` is
 * the query that makes them usable. */
- (void)moveToPoint:(NSPoint)point;
- (void)lineToPoint:(NSPoint)point;
- (void)curveToPoint:(NSPoint)point controlPoint1:(NSPoint)controlPoint1
	controlPoint2:(NSPoint)controlPoint2;
- (void)curveToPoint:(NSPoint)point controlPoint:(NSPoint)controlPoint;
- (void)closePath;
- (void)relativeMoveToPoint:(NSPoint)point;
- (void)relativeLineToPoint:(NSPoint)point;
- (void)relativeCurveToPoint:(NSPoint)point controlPoint1:(NSPoint)controlPoint1
	     controlPoint2:(NSPoint)controlPoint2;
- (void)relativeCurveToPoint:(NSPoint)point controlPoint:(NSPoint)controlPoint;
- (void)removeAllPoints;

/* APPENDING, WHICH IS HOW A PATH IS COMPOSED rather than built point by point. The arc's angles are
 * DEGREES — see the header note — and the four-argument form sweeps counterclockwise, which is
 * Apple's default. */
- (void)appendBezierPath:(NSBezierPath *)path;
- (void)appendBezierPathWithPoints:(const NSPoint *)points count:(NSInteger)count;
- (void)appendBezierPathWithRect:(NSRect)rect;
- (void)appendBezierPathWithOvalInRect:(NSRect)rect;
- (void)appendBezierPathWithRoundedRect:(NSRect)rect xRadius:(CGFloat)xRadius yRadius:(CGFloat)yRadius;
- (void)appendBezierPathWithArcFromPoint:(NSPoint)point1 toPoint:(NSPoint)point2 radius:(CGFloat)radius;
- (void)appendBezierPathWithArcWithCenter:(NSPoint)center radius:(CGFloat)radius
			       startAngle:(CGFloat)startAngle endAngle:(CGFloat)endAngle;
- (void)appendBezierPathWithArcWithCenter:(NSPoint)center radius:(CGFloat)radius
			       startAngle:(CGFloat)startAngle endAngle:(CGFloat)endAngle
				clockwise:(BOOL)clockwise;

/* WHAT IT IS. `bounds` is the bounding box including the control points, which is Apple's reading —
 * `controlPointBounds` is the same box under the name Apple gives the path's own. */
@property (readonly) NSRect bounds;
@property (readonly) NSRect controlPointBounds;
@property (readonly) NSPoint currentPoint;
@property (readonly, getter=isEmpty) BOOL empty;
@property (readonly) NSInteger elementCount;

/* THE LINE STATE, WHICH IS THE PATH'S OWN — see the header note. A NEW PATH TAKES THE CLASS'S
 * CURRENT DEFAULTS, which is Apple's contract and the reason the defaults exist as settable state
 * rather than constants. */
@property NSWindingRule windingRule;
@property NSLineCapStyle lineCapStyle;
@property NSLineJoinStyle lineJoinStyle;
@property CGFloat lineWidth;
@property CGFloat miterLimit;
- (void)setLineDash:(nullable const CGFloat *)pattern count:(NSInteger)count phase:(CGFloat)phase;
- (void)getLineDash:(nullable CGFloat *)pattern count:(nullable NSInteger *)count
	      phase:(nullable CGFloat *)phase;

/* AND THE DEFAULTS THEMSELVES, consulted when a path is CREATED. */
+ (NSWindingRule)defaultWindingRule;
+ (void)setDefaultWindingRule:(NSWindingRule)windingRule;
+ (NSLineCapStyle)defaultLineCapStyle;
+ (void)setDefaultLineCapStyle:(NSLineCapStyle)lineCapStyle;
+ (NSLineJoinStyle)defaultLineJoinStyle;
+ (void)setDefaultLineJoinStyle:(NSLineJoinStyle)lineJoinStyle;
+ (CGFloat)defaultLineWidth;
+ (void)setDefaultLineWidth:(CGFloat)lineWidth;
+ (CGFloat)defaultMiterLimit;
+ (void)setDefaultMiterLimit:(CGFloat)miterLimit;

/* DRAWING, INTO THE CURRENT GRAPHICS CONTEXT. `-fill` uses the path's winding rule; `-stroke` sets
 * the context's line state from ours first (see the header note). */
- (void)fill;
- (void)stroke;

/* THE ELEMENT MODEL, READ BACK. `-elementAtIndex:` answers what the step AT that index is, and
 * `-elementAtIndex:associatedPoints:` also writes the points that go with it — 1 for a move or a line,
 * 2 for a quadratic, 3 for a cubic, 0 for a close — into a caller's array, or skips them if the array is
 * NULL. AN INDEX PAST THE END IS REFUSED BY NAME rather than raising, which is this tree's rule for an
 * ordinary mistake; Apple raises. */
- (NSBezierPathElement)elementAtIndex:(NSInteger)index;
- (NSBezierPathElement)elementAtIndex:(NSInteger)index associatedPoints:(nullable NSPointArray)points;

/* AND WHETHER A POINT IS INSIDE THE PATH, by the path's own WINDING RULE — the same question the fill
 * asks, answered in AppKit because CoreGraphics here tests coverage rather than membership. The test is
 * run on the FLATTENED path, which is what makes it a polygon crossing count rather than an area. */
- (BOOL)containsPoint:(NSPoint)point;
/* THE CLIP FAMILY: `-addClip` INTERSECTS with this path, `-setClip` REPLACES the clip with it, and
 * `+clipRect:` intersects with a rectangle. The receiver's path is untouched by either — CoreGraphics'
 * clip consumes the copy the CONTEXT was given, not this object's. */
- (void)addClip;
- (void)setClip;
+ (void)clipRect:(NSRect)rect;

/* THE COPY THAT KEEPS ITS CURVES, flattened — the one copy operation Core Graphics has. */
@property (readonly, strong) NSBezierPath *bezierPathByFlatteningPath;

/* AND THE TRANSFORM, which transforms the POINTS rather than recording a matrix, exactly as
 * `CGPathCreateMutableCopyByTransformingPath` does — there is no matrix stored in a path. */
- (void)transformUsingAffineTransform:(NSAffineTransform *)transform;

/* THE FOUR CLASS-LEVEL DRAWING HELPERS, each of which is a path built and drawn in one call. */
+ (void)fillRect:(NSRect)rect;
+ (void)strokeRect:(NSRect)rect;
+ (void)strokeLineFromPoint:(NSPoint)point1 toPoint:(NSPoint)point2;

@end

NS_ASSUME_NONNULL_END

#endif /* APPKIT_NSBEZIERPATH_H */
