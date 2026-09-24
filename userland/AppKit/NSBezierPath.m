/*
 * NSBezierPath — a mutable CGPath, the line state that belongs to the path, and the draw verbs.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * TWO THINGS ARE EASY TO GET SUBTLY WRONG HERE, and each is one helper with a probe check on the
 * RESULT rather than on the arithmetic:
 *
 *   THE ANGLE UNITS. Apple's arc constructors take DEGREES with 0° along the positive x axis and
 *   increasing counterclockwise; `CGPathAddArc` takes RADIANS. `fn_radians` is the whole conversion,
 *   and the probe measures where a quarter-turn arc ENDS.
 *
 *   AND THE ELEMENT REPLAY, WHICH IS THIS TREE'S INSTEAD OF APPLE'S. CoreGraphics here has no
 *   `CGPathAddPath` and no `CGPathCreateCopyByTransformingPath` — I reached for both when this file was
 *   first written, because they are Apple's Core Graphics and not this library's — so APPENDING and
 *   TRANSFORMING are the same operation with one flag: walk the source path with `CGPathApply` and
 *   replay its elements onto a destination, mapping each point when a transform is in force.
 *   TRANSFORMING THE POINTS IS EXACT FOR AN AFFINE MAP (the map of a Bezier's control points is the map
 *   of the Bezier), which is why the replay needs no subdivision.
 *
 *   AND `-stroke` WRITES THE CONTEXT'S LINE STATE from the path's. That is Apple's arrangement — the
 *   line state belongs to the PATH — and it means a caller who set a width on the context and then
 *   stroked a path with its own width gets the path's. Stated in NSBezierPath.h, not discovered.
 */
#import <AppKit/NSBezierPath.h>
#import <AppKit/NSGraphicsContext.h>

#import <CoreGraphics/CGContext.h>
#import <CoreGraphics/CGPath.h>
#import <Foundation/NSAffineTransform.h>

#include <stdio.h>

/* THE DASH ARRAY IS BOUNDED IN THE OBJECT, for the reason the graphics state's is: a pattern is a
 * handful of numbers, and a longer one is REFUSED rather than truncated — dashes the caller did not
 * ask for are worse than none. */
#define APPKIT_DASH_MAX 16

/* THE CLASS DEFAULTS, which a new path COPIES at creation (Apple's contract, and what makes
 * `+setDefaultLineWidth:` affect the paths made AFTER it rather than the ones already alive). The
 * values are Apple's documented ones; `flatness` is absent because it is deferred — NSBezierPath.h
 * says why. */
static CGFloat fn_default_line_width = 1.0;
static CGFloat fn_default_miter_limit = 10.0;
static NSLineCapStyle fn_default_cap = NSLineCapStyleButt;
static NSLineJoinStyle fn_default_join = NSLineJoinStyleMiter;
static NSWindingRule fn_default_winding = NSWindingRuleNonZero;

/* THE THREE ENUM MAPPINGS, AS SWITCHES AND NOT CASTS: the values are ours, so a cast would hide an
 * ordering that differed between the two headers. */
static CGLineCap fn_cap(NSLineCapStyle s)
{
	switch (s) {
	case NSLineCapStyleRound: return kCGLineCapRound;
	case NSLineCapStyleSquare: return kCGLineCapSquare;
	case NSLineCapStyleButt:
	default: return kCGLineCapButt;
	}
}

static CGLineJoin fn_join(NSLineJoinStyle s)
{
	switch (s) {
	case NSLineJoinStyleRound: return kCGLineJoinRound;
	case NSLineJoinStyleBevel: return kCGLineJoinBevel;
	case NSLineJoinStyleMiter:
	default: return kCGLineJoinMiter;
	}
}

/* DEGREES TO RADIANS, in one place — see the file's header note. */
static CGFloat fn_radians(CGFloat degrees)
{
	return degrees * 3.14159265358979323846 / 180.0;
}

/* --- the element walk, which serves BOTH appending and transforming -------------- */

typedef struct {
	CGMutablePathRef dst;
	CGAffineTransform t;
	int transform;      /* 0 for an append, 1 when `t` is in force */
} fn_replay;

static CGPoint fn_mapped(const fn_replay *r, const CGPoint *p)
{
	return r->transform ? CGPointApplyAffineTransform(*p, r->t) : *p;
}

/* ALL FIVE ELEMENT TYPES ARRIVE, and the point counts are Core Graphics': one for a move, one for a
 * line, two for a quadratic, three for a cubic, none for a close. A switch with no default is
 * deliberate — a sixth element type would be a compile error here rather than a silently dropped
 * piece of the path. */
static void fn_replay_element(void *info, const CGPathElement *e)
{
	fn_replay *r = info;
	CGPoint a;
	CGPoint b;
	CGPoint c;

	switch (e->type) {
	case kCGPathElementMoveToPoint:
		a = fn_mapped(r, &e->points[0]);
		CGPathMoveToPoint(r->dst, NULL, a.x, a.y);
		break;
	case kCGPathElementAddLineToPoint:
		a = fn_mapped(r, &e->points[0]);
		CGPathAddLineToPoint(r->dst, NULL, a.x, a.y);
		break;
	case kCGPathElementAddQuadCurveToPoint:
		a = fn_mapped(r, &e->points[0]);
		b = fn_mapped(r, &e->points[1]);
		CGPathAddQuadCurveToPoint(r->dst, NULL, a.x, a.y, b.x, b.y);
		break;
	case kCGPathElementAddCurveToPoint:
		a = fn_mapped(r, &e->points[0]);
		b = fn_mapped(r, &e->points[1]);
		c = fn_mapped(r, &e->points[2]);
		CGPathAddCurveToPoint(r->dst, NULL, a.x, a.y, b.x, b.y, c.x, c.y);
		break;
	case kCGPathElementCloseSubpath:
		CGPathCloseSubpath(r->dst);
		break;
	}
}

/* A FRESH MUTABLE COPY OF A PATH, BY REPLAY — because `CGPathCreateMutableCopy` DOES NOT EXIST here.
 * `CGPath.h` has `CGPathCreateMutable`, the three `CreateWith*` forms, `CGPathCreateCopyBy{Dashing,
 * Flattening,Stroking}` and `CGPathRetain`, and NO mutable copy — the third time in this file that
 * Apple's Core Graphics and this library's were mistaken for each other. */
static CGMutablePathRef fn_copy_path(CGPathRef src)
{
	fn_replay r;
	CGMutablePathRef dst = CGPathCreateMutable();

	if (dst == NULL) {
		return NULL;
	}
	r.dst = dst;
	r.transform = 0;
	if (src != NULL) {
		CGPathApply(src, &r, fn_replay_element);
	}
	return dst;
}

/* AN ELEMENT COUNTER, for the same reason: Core Graphics walks a path by CALLBACK rather than
 * answering a count. */
static void fn_count_element(void *info, const CGPathElement *element)
{
	(void)element;
	(*(NSInteger *)info)++;
}

@implementation NSBezierPath

+ (NSBezierPath *)bezierPath
{
	return [[[self alloc] init] autorelease];
}

+ (NSBezierPath *)bezierPathWithCGPath:(CGPathRef)cgPath
{
	NSBezierPath *p;

	if (cgPath == NULL) {
		return nil;
	}
	p = [[self alloc] init];
	if (p == nil) {
		return nil;
	}
	/* THE CALLER'S PATH IS COPIED INTO A MUTABLE ONE, because a path this class was handed may be
	 * immutable and this class builds on its own. Retaining it would make `-lineToPoint:` a crash. */
	CGPathRelease((CGPathRef)p->_path);
	p->_path = fn_copy_path(cgPath);
	return [p autorelease];
}

- (nullable CGPathRef)CGPath
{
	return _path;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_path = CGPathCreateMutable();
	if (_path == NULL) {
		[self release];
		return nil;
	}
	_lineWidth = fn_default_line_width;
	_miterLimit = fn_default_miter_limit;
	_capStyle = fn_default_cap;
	_joinStyle = fn_default_join;
	_windingRule = fn_default_winding;
	return self;
}

- (void)dealloc
{
	CGPathRelease((CGPathRef)_path);
	[super dealloc];
}

- (id)copy
{
	NSBezierPath *c = [[[self class] alloc] init];
	NSUInteger i;

	if (c == nil) {
		return nil;
	}
	CGPathRelease((CGPathRef)c->_path);
	c->_path = fn_copy_path((CGPathRef)_path);
	c->_lineWidth = _lineWidth;
	c->_miterLimit = _miterLimit;
	c->_capStyle = _capStyle;
	c->_joinStyle = _joinStyle;
	c->_windingRule = _windingRule;
	c->_dashCount = _dashCount;
	c->_dashPhase = _dashPhase;
	for (i = 0; i < _dashCount && i < APPKIT_DASH_MAX; i++) {
		c->_dash[i] = _dash[i];
	}
	return c;
}

/* --- the shape constructors --------------------------------------------------- */

+ (NSBezierPath *)bezierPathWithRect:(NSRect)rect
{
	NSBezierPath *p = [self bezierPath];

	[p appendBezierPathWithRect:rect];
	return p;
}

+ (NSBezierPath *)bezierPathWithOvalInRect:(NSRect)rect
{
	NSBezierPath *p = [self bezierPath];

	[p appendBezierPathWithOvalInRect:rect];
	return p;
}

+ (NSBezierPath *)bezierPathWithRoundedRect:(NSRect)rect xRadius:(CGFloat)xRadius
				    yRadius:(CGFloat)yRadius
{
	NSBezierPath *p = [self bezierPath];

	[p appendBezierPathWithRoundedRect:rect xRadius:xRadius yRadius:yRadius];
	return p;
}

/* --- building it -------------------------------------------------------------- */

- (void)moveToPoint:(NSPoint)point
{
	CGPathMoveToPoint(_path, NULL, point.x, point.y);
}

- (void)lineToPoint:(NSPoint)point
{
	CGPathAddLineToPoint(_path, NULL, point.x, point.y);
}

- (void)curveToPoint:(NSPoint)point controlPoint1:(NSPoint)controlPoint1
	controlPoint2:(NSPoint)controlPoint2
{
	CGPathAddCurveToPoint(_path, NULL, controlPoint1.x, controlPoint1.y, controlPoint2.x,
			      controlPoint2.y, point.x, point.y);
}

/* THE ONE-CONTROL-POINT FORM IS QUADRATIC, which is why it is not a cubic with a repeated control
 * point — the difference is visible in the curve. */
- (void)curveToPoint:(NSPoint)point controlPoint:(NSPoint)controlPoint
{
	CGPathAddQuadCurveToPoint(_path, NULL, controlPoint.x, controlPoint.y, point.x, point.y);
}

- (void)closePath
{
	CGPathCloseSubpath(_path);
}

- (void)relativeMoveToPoint:(NSPoint)point
{
	NSPoint here = [self currentPoint];

	[self moveToPoint:NSMakePoint(here.x + point.x, here.y + point.y)];
}

- (void)relativeLineToPoint:(NSPoint)point
{
	NSPoint here = [self currentPoint];

	[self lineToPoint:NSMakePoint(here.x + point.x, here.y + point.y)];
}

- (void)relativeCurveToPoint:(NSPoint)point controlPoint1:(NSPoint)controlPoint1
	     controlPoint2:(NSPoint)controlPoint2
{
	NSPoint here = [self currentPoint];

	[self curveToPoint:NSMakePoint(here.x + point.x, here.y + point.y)
	      controlPoint1:NSMakePoint(here.x + controlPoint1.x, here.y + controlPoint1.y)
	      controlPoint2:NSMakePoint(here.x + controlPoint2.x, here.y + controlPoint2.y)];
}

- (void)relativeCurveToPoint:(NSPoint)point controlPoint:(NSPoint)controlPoint
{
	NSPoint here = [self currentPoint];

	[self curveToPoint:NSMakePoint(here.x + point.x, here.y + point.y)
	      controlPoint:NSMakePoint(here.x + controlPoint.x, here.y + controlPoint.y)];
}

- (void)removeAllPoints
{
	CGPathRelease((CGPathRef)_path);
	_path = CGPathCreateMutable();
}

/* --- appending, which is the element replay ------------------------------------ */

- (void)appendBezierPath:(NSBezierPath *)path
{
	fn_replay r;

	if (path == nil || path->_path == NULL) {
		return;
	}
	r.dst = _path;
	r.transform = 0;
	CGPathApply((CGPathRef)path->_path, &r, fn_replay_element);
}

- (void)appendBezierPathWithPoints:(const NSPoint *)points count:(NSInteger)count
{
	NSInteger i;

	if (points == NULL || count <= 0) {
		return;
	}
	CGPathMoveToPoint(_path, NULL, points[0].x, points[0].y);
	for (i = 1; i < count; i++) {
		CGPathAddLineToPoint(_path, NULL, points[i].x, points[i].y);
	}
}

- (void)appendBezierPathWithRect:(NSRect)rect
{
	CGPathAddRect(_path, NULL, rect);
}

- (void)appendBezierPathWithOvalInRect:(NSRect)rect
{
	CGPathAddEllipseInRect(_path, NULL, rect);
}

- (void)appendBezierPathWithRoundedRect:(NSRect)rect xRadius:(CGFloat)xRadius yRadius:(CGFloat)yRadius
{
	CGPathAddRoundedRect(_path, NULL, rect, xRadius, yRadius);
}

- (void)appendBezierPathWithArcFromPoint:(NSPoint)point1 toPoint:(NSPoint)point2 radius:(CGFloat)radius
{
	CGPathAddArcToPoint(_path, NULL, point1.x, point1.y, point2.x, point2.y, radius);
}

- (void)appendBezierPathWithArcWithCenter:(NSPoint)center radius:(CGFloat)radius
			       startAngle:(CGFloat)startAngle endAngle:(CGFloat)endAngle
{
	/* THE FOUR-ARGUMENT FORM SWEEPS COUNTERCLOCKWISE, Apple's default. */
	CGPathAddArc(_path, NULL, center.x, center.y, radius, fn_radians(startAngle),
		     fn_radians(endAngle), false);
}

- (void)appendBezierPathWithArcWithCenter:(NSPoint)center radius:(CGFloat)radius
			       startAngle:(CGFloat)startAngle endAngle:(CGFloat)endAngle
				clockwise:(BOOL)clockwise
{
	CGPathAddArc(_path, NULL, center.x, center.y, radius, fn_radians(startAngle),
		     fn_radians(endAngle), clockwise ? true : false);
}

/* --- what it is -------------------------------------------------------------- */

- (NSRect)bounds
{
	/* THE PATH'S OWN BOX; `-controlPointBounds` is the control-point-INCLUSIVE one. That is the
	 * distinction CGPath.h draws between its two functions, attached to the names the way each name
	 * reads. */
	return CGPathGetPathBoundingBox((CGPathRef)_path);
}

- (NSRect)controlPointBounds
{
	return CGPathGetBoundingBox((CGPathRef)_path);
}

- (NSPoint)currentPoint
{
	return CGPathGetCurrentPoint((CGPathRef)_path);
}

- (BOOL)isEmpty
{
	return CGPathIsEmpty((CGPathRef)_path) ? YES : NO;
}

- (NSInteger)elementCount
{
	NSInteger n = 0;

	CGPathApply((CGPathRef)_path, &n, fn_count_element);
	return n;
}

/* --- the line state ---------------------------------------------------------- */

- (NSWindingRule)windingRule
{
	return _windingRule;
}

- (void)setWindingRule:(NSWindingRule)windingRule
{
	_windingRule = windingRule;
}

- (NSLineCapStyle)lineCapStyle
{
	return _capStyle;
}

- (void)setLineCapStyle:(NSLineCapStyle)lineCapStyle
{
	_capStyle = lineCapStyle;
}

- (NSLineJoinStyle)lineJoinStyle
{
	return _joinStyle;
}

- (void)setLineJoinStyle:(NSLineJoinStyle)lineJoinStyle
{
	_joinStyle = lineJoinStyle;
}

- (CGFloat)lineWidth
{
	return _lineWidth;
}

- (void)setLineWidth:(CGFloat)lineWidth
{
	_lineWidth = lineWidth;
}

- (CGFloat)miterLimit
{
	return _miterLimit;
}

- (void)setMiterLimit:(CGFloat)miterLimit
{
	_miterLimit = miterLimit;
}

- (void)setLineDash:(nullable const CGFloat *)pattern count:(NSInteger)count phase:(CGFloat)phase
{
	NSInteger i;

	if (pattern == NULL || count <= 0) {
		_dashCount = 0;
		_dashPhase = 0.0;
		return;
	}
	if (count > APPKIT_DASH_MAX) {
		fprintf(stderr, "APPKIT-REFUSE: a dash pattern of %d entries does not fit this class's "
				"%d, and truncating it would draw dashes the caller did not ask for\n",
			(int)count, APPKIT_DASH_MAX);
		return;
	}
	for (i = 0; i < count; i++) {
		_dash[i] = pattern[i];
	}
	_dashCount = (NSUInteger)count;
	_dashPhase = phase;
}

- (void)getLineDash:(nullable CGFloat *)pattern count:(nullable NSInteger *)count
	      phase:(nullable CGFloat *)phase
{
	NSUInteger i;

	if (pattern != NULL) {
		for (i = 0; i < _dashCount; i++) {
			pattern[i] = _dash[i];
		}
	}
	if (count != NULL) {
		*count = (NSInteger)_dashCount;
	}
	if (phase != NULL) {
		*phase = _dashPhase;
	}
}

/* --- the defaults ------------------------------------------------------------ */

+ (NSWindingRule)defaultWindingRule
{
	return fn_default_winding;
}

+ (void)setDefaultWindingRule:(NSWindingRule)windingRule
{
	fn_default_winding = windingRule;
}

+ (NSLineCapStyle)defaultLineCapStyle
{
	return fn_default_cap;
}

+ (void)setDefaultLineCapStyle:(NSLineCapStyle)lineCapStyle
{
	fn_default_cap = lineCapStyle;
}

+ (NSLineJoinStyle)defaultLineJoinStyle
{
	return fn_default_join;
}

+ (void)setDefaultLineJoinStyle:(NSLineJoinStyle)lineJoinStyle
{
	fn_default_join = lineJoinStyle;
}

+ (CGFloat)defaultLineWidth
{
	return fn_default_line_width;
}

+ (void)setDefaultLineWidth:(CGFloat)lineWidth
{
	fn_default_line_width = lineWidth;
}

+ (CGFloat)defaultMiterLimit
{
	return fn_default_miter_limit;
}

+ (void)setDefaultMiterLimit:(CGFloat)miterLimit
{
	fn_default_miter_limit = miterLimit;
}

/* --- drawing ----------------------------------------------------------------- */

/* THE CONTEXT A PATH DRAWS INTO IS THE CURRENT ONE — there is no argument for it, which is the
 * AppKit's model and the reason this class needs NSGraphicsContext at all. A nil context means there
 * is nothing to draw into, and NSBezierPath.h says so. */
static CGContextRef fn_current(void)
{
	return [[NSGraphicsContext currentContext] CGContext];
}

- (void)fn_apply_line_state:(CGContextRef)ctx
{
	CGContextSetLineWidth(ctx, _lineWidth);
	CGContextSetLineCap(ctx, fn_cap(_capStyle));
	CGContextSetLineJoin(ctx, fn_join(_joinStyle));
	CGContextSetMiterLimit(ctx, _miterLimit);
	CGContextSetLineDash(ctx, _dashPhase, _dashCount > 0 ? _dash : NULL, _dashCount);
}

- (void)fill
{
	CGContextRef ctx = fn_current();

	if (ctx == NULL) {
		return;
	}
	CGContextAddPath(ctx, (CGPathRef)_path);
	CGContextDrawPath(ctx, _windingRule == NSWindingRuleEvenOdd ? kCGPathEOFill : kCGPathFill);
}

- (void)stroke
{
	CGContextRef ctx = fn_current();

	if (ctx == NULL) {
		return;
	}
	[self fn_apply_line_state:ctx];
	CGContextAddPath(ctx, (CGPathRef)_path);
	CGContextDrawPath(ctx, kCGPathStroke);
}

- (NSBezierPath *)bezierPathByFlatteningPath
{
	NSBezierPath *p;
	CGPathRef flat;

	/* THE TOLERANCE IS THIS LIBRARY'S OWN AND NOT A SETTABLE FLATNESS: the flatness family is deferred
	 * (NSBezierPath.h says why), and 0.1 is the device-space tolerance the context's own flattener
	 * uses. A caller with a different tolerance has no way to say so YET, which is the honest state of
	 * it. */
	flat = CGPathCreateCopyByFlattening((CGPathRef)_path, 0.1);
	if (flat == NULL) {
		return nil;
	}
	p = [[self class] bezierPathWithCGPath:flat];
	CGPathRelease(flat);
	if (p != nil) {
		/* THE LINE STATE COMES ACROSS TOO, because a flattened path is meant to draw the same way. */
		p->_lineWidth = _lineWidth;
		p->_miterLimit = _miterLimit;
		p->_capStyle = _capStyle;
		p->_joinStyle = _joinStyle;
		p->_windingRule = _windingRule;
		p->_dashCount = _dashCount;
		p->_dashPhase = _dashPhase;
	}
	return p;
}

- (void)transformUsingAffineTransform:(NSAffineTransform *)transform
{
	fn_replay r;
	CGMutablePathRef dst;
	NSAffineTransformStruct s;

	if (transform == nil) {
		return;
	}
	s = [transform transformStruct];
	r.t = CGAffineTransformMake(s.m11, s.m12, s.m21, s.m22, s.tX, s.tY);
	r.transform = 1;
	dst = CGPathCreateMutable();
	if (dst == NULL) {
		return;
	}
	r.dst = dst;
	CGPathApply((CGPathRef)_path, &r, fn_replay_element);
	CGPathRelease((CGPathRef)_path);
	_path = dst;
}

/* --- the class helpers ------------------------------------------------------- */

+ (void)fillRect:(NSRect)rect
{
	[[self bezierPathWithRect:rect] fill];
}

+ (void)strokeRect:(NSRect)rect
{
	[[self bezierPathWithRect:rect] stroke];
}

+ (void)strokeLineFromPoint:(NSPoint)point1 toPoint:(NSPoint)point2
{
	NSBezierPath *p = [self bezierPath];

	[p moveToPoint:point1];
	[p lineToPoint:point2];
	[p stroke];
}

@end
