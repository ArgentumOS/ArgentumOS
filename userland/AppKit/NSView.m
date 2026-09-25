/*
 * NSView — a rectangle in a tree, and the coordinate systems that tree implies.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TREE AND THE COORDINATE SYSTEMS ARE THE WHOLE OF THIS FILE, and both are arithmetic rather than
 * drawing — which is why they can be finished and proved without a window server.
 *
 * THE OWNERSHIP IS ONE RULE AND ONE CONSEQUENCE: a parent OWNS its subviews and a subview BORROWS its
 * superview, so the borrowed link has to be cleared on both sides of the relationship — when a subview
 * leaves (`-removeFromSuperview`) and when the parent is deallocated. Leaving the second out is a
 * dangling pointer that only shows up as a crash much later, which is what `-dealloc` below prevents.
 */
#import <AppKit/NSView.h>
#import <AppKit/NSGraphicsContext.h>
#import <AppKit/NSImage.h>
#include <stdio.h>
#include <string.h>

/* A POINT FROM A VIEW'S OWN SYSTEM INTO ITS SUPERVIEW'S, AND BACK. The flip is the entire difference,
 * AND IT IS THE COMPARISON OF TWO FLIPS RATHER THAN ONE VIEW'S — which is the bug the probe found after
 * the first version compared only the CHILD's:
 *
 *   the frame's origin.y is measured in the SUPERVIEW's y direction, while the point is measured in the
 *   view's OWN. So the two agree — and the point needs no mirroring — exactly when the view and its
 *   superview have the SAME y direction, and are mirrored when they do not. A flipped superview with an
 *   unflipped child therefore puts the child's own origin at the BOTTOM of its frame, which is the case a
 *   one-sided test answers as if nothing had flipped at all.
 */
static BOOL fn_same_y_direction(NSView *v)
{
	NSView *s = [v superview];

	/* THE ROOT HAS NO SUPERVIEW, and a view with nothing above it is trivially "the same direction". */
	if (s == nil) {
		return YES;
	}
	return [v isFlipped] == [s isFlipped];
}

static NSPoint fn_to_super(NSPoint p, NSView *v)
{
	NSRect f = [v frame];
	BOOL same = fn_same_y_direction(v);

	return NSMakePoint(f.origin.x + p.x, f.origin.y + (same ? p.y : f.size.height - p.y));
}

static NSPoint fn_from_super(NSPoint p, NSView *v)
{
	NSRect f = [v frame];
	BOOL same = fn_same_y_direction(v);
	CGFloat y = p.y - f.origin.y;

	return NSMakePoint(p.x - f.origin.x, same ? y : f.size.height - y);
}

/* THE ROOT VIEW STANDS IN FOR "THE WINDOW'S COORDINATE SYSTEM", which is the one place this slice has to
 * give a NULL view a meaning. Root space IS the root view's own system, so a walk upwards stops BEFORE
 * applying the root's own frame — the root is not placed in anything. */
static NSView *fn_root(NSView *v)
{
	NSView *s = [v superview];

	while (s != nil) {
		v = s;
		s = [v superview];
	}
	return v;
}

static NSPoint fn_up_to_root(NSPoint p, NSView *v)
{
	while (v != nil && [v superview] != nil) {
		p = fn_to_super(p, v);
		v = [v superview];
	}
	return p;
}

static NSPoint fn_down_from_root(NSView *v, NSPoint p)
{
	NSView *s = [v superview];

	if (s == nil) {
		return p;
	}
	return fn_from_super(fn_down_from_root(s, p), v);
}

/* HAND-WRITTEN BECAUSE THE INTERSECTION IS FOUR COMPARISONS and a Foundation function for it would be
 * one more thing this file depends on. A non-overlapping pair answers the empty rect. */
static NSRect fn_intersect(NSRect a, NSRect b)
{
	CGFloat x1 = a.origin.x > b.origin.x ? a.origin.x : b.origin.x;
	CGFloat y1 = a.origin.y > b.origin.y ? a.origin.y : b.origin.y;
	CGFloat x2 = (a.origin.x + a.size.width) < (b.origin.x + b.size.width)
			     ? (a.origin.x + a.size.width) : (b.origin.x + b.size.width);
	CGFloat y2 = (a.origin.y + a.size.height) < (b.origin.y + b.size.height)
			     ? (a.origin.y + a.size.height) : (b.origin.y + b.size.height);

	if (x2 <= x1 || y2 <= y1) {
		return NSMakeRect(0.0, 0.0, 0.0, 0.0);
	}
	return NSMakeRect(x1, y1, x2 - x1, y2 - y1);
}

@implementation NSView

- (instancetype)initWithFrame:(NSRect)frameRect
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_frame = frameRect;
	/* THE BOUNDS BEGIN AS THE FRAME'S SIZE AT THE ORIGIN, which is what every view starts as. */
	_bounds = NSMakeRect(0.0, 0.0, frameRect.size.width, frameRect.size.height);
	_flipped = NO;
	_hidden = NO;
	/* A VIEW NOTHING HAS DRAWN NEEDS DISPLAY, which is the truthful initial state rather than a choice. */
	_needsDisplay = YES;
	_subviews = nil;
	_superview = nil;
	return self;
}

- (void)dealloc
{
	/* THE BORROWED LINKS ARE CLEARED BEFORE THE CHILDREN ARE RELEASED, and the order matters: a child
	 * holds a POINTER to this view, so releasing the array while those pointers still name a dying object
	 * would leave every child one message away from a use-after-free. THIS IS WHAT A WEAK LINK MEANS WHEN
	 * THE LANGUAGE DOES NOT HAVE ONE, and it is the reason this method is not just a release. */
	if (_subviews != nil) {
		NSUInteger i;

		for (i = 0; i < [_subviews count]; i++) {
			NSView *c = [_subviews objectAtIndex:i];

			c->_superview = nil;
		}
		[_subviews release];
	}
	[super dealloc];
}

- (NSRect)frame
{
	return _frame;
}

- (void)setFrame:(NSRect)frameRect
{
	_frame = frameRect;
	/* THE SIZE IS SHARED AND THE ORIGINS ARE NOT — see the header for what this deliberately does not
	 * model (a bounds origin that is a scroll offset). */
	_bounds = NSMakeRect(_bounds.origin.x, _bounds.origin.y, frameRect.size.width,
			     frameRect.size.height);
	_needsDisplay = YES;
}

- (NSRect)bounds
{
	return _bounds;
}

- (void)setBounds:(NSRect)boundsRect
{
	_bounds = boundsRect;
	_frame = NSMakeRect(_frame.origin.x, _frame.origin.y, boundsRect.size.width,
			    boundsRect.size.height);
	_needsDisplay = YES;
}

- (BOOL)isFlipped
{
	return _flipped;
}

- (void)setFlipped:(BOOL)flag
{
	_flipped = flag ? YES : NO;
}

- (BOOL)needsDisplay
{
	return _needsDisplay;
}

- (void)setNeedsDisplay:(BOOL)flag
{
	_needsDisplay = flag ? YES : NO;
}

- (BOOL)isHidden
{
	return _hidden;
}

- (void)setHidden:(BOOL)flag
{
	_hidden = flag ? YES : NO;
}

- (NSArray *)subviews
{
	if (_subviews == nil) {
		return [NSArray array];
	}
	return _subviews;
}

- (NSView *)superview
{
	return _superview;
}

- (void)addSubview:(NSView *)aView
{
	if (aView == nil || aView == self) {
		return;
	}
	/* A VIEW HAS ONE SUPERVIEW, so adding one that has a parent MOVES it — Apple's behaviour, and the
	 * reason this is not just an append. */
	if ([aView superview] != nil) {
		[aView removeFromSuperview];
	}
	if (_subviews == nil) {
		_subviews = [[NSMutableArray alloc] init];
	}
	/* THE ARRAY OWNS IT (it retains), and the borrowed link is set from here because `_superview` is
	 * PROTECTED and only a method of this class may write another instance's. */
	[_subviews addObject:aView];
	aView->_superview = self;
}

- (void)removeFromSuperview
{
	NSView *parent = _superview;

	if (parent == nil) {
		return;
	}
	/* CLEARED BEFORE THE REMOVAL, because REMOVING A VIEW MAY DEALLOCATE IT — Apple documents exactly
	 * that, and a view with no other owner dies here. Touching `self` afterwards would be the bug. */
	_superview = nil;
	[parent->_subviews removeObjectIdenticalTo:self];
}

- (BOOL)isDescendantOf:(NSView *)aView
{
	NSView *v = _superview;

	while (v != nil) {
		if (v == aView) {
			return YES;
		}
		v = [v superview];
	}
	return NO;
}

- (NSView *)hitTest:(NSPoint)point
{
	NSPoint here;
	NSUInteger i;

	if (_hidden) {
		return nil;
	}
	/* THE POINT ARRIVES IN THE SUPERVIEW'S COORDINATES — EXCEPT AT THE ROOT, where there is no superview
	 * and the point is already in this view's own space. That is the same stand-in the conversions use for
	 * a NULL view, and getting it the other way round is a root view that answers NO everywhere. */
	if (_superview != nil && !NSPointInRect(point, _frame)) {
		return nil;
	}
	here = (_superview != nil) ? fn_from_super(point, self) : point;
	if (_subviews != nil) {
		/* TOPMOST FIRST: the array is back-to-front, so a hit is the LAST one that contains the point. */
		for (i = [_subviews count]; i > 0; i--) {
			NSView *hit = [[_subviews objectAtIndex:i - 1] hitTest:here];

			if (hit != nil) {
				return hit;
			}
		}
	}
	return NSPointInRect(here, _bounds) ? self : nil;
}

- (NSPoint)convertPoint:(NSPoint)point toView:(NSView *)view
{
	NSView *target = view != nil ? view : fn_root(self);
	NSPoint p = fn_up_to_root(point, self);

	/* BOTH VIEWS ARE ASSUMED TO SHARE A ROOT. Two views in different trees have no common frame of
	 * reference, and Apple leaves that case undefined; this one converts through each root's own space,
	 * which is arithmetic rather than a raise. */
	return fn_down_from_root(target, p);
}

- (NSPoint)convertPoint:(NSPoint)point fromView:(NSView *)view
{
	NSView *src = view != nil ? view : fn_root(self);
	NSPoint p = fn_up_to_root(point, src);

	return fn_down_from_root(self, p);
}

- (NSRect)convertRect:(NSRect)rect fromView:(NSView *)view
{
	/* THE TWO CORNERS ARE CONVERTED AND THE RECT REBUILT, because a flip can put the origin at what was
	 * the far corner — which is why the result is standardised rather than assembled in order. */
	NSPoint a = [self convertPoint:rect.origin fromView:view];
	NSPoint b = [self convertPoint:NSMakePoint(rect.origin.x + rect.size.width,
						  rect.origin.y + rect.size.height) fromView:view];

	return NSMakeRect(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.x < b.x ? b.x - a.x : a.x - b.x,
			  a.y < b.y ? b.y - a.y : a.y - b.y);
}

- (NSRect)convertRect:(NSRect)rect toView:(NSView *)view
{
	NSPoint a = [self convertPoint:rect.origin toView:view];
	NSPoint b = [self convertPoint:NSMakePoint(rect.origin.x + rect.size.width,
						  rect.origin.y + rect.size.height) toView:view];

	return NSMakeRect(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.x < b.x ? b.x - a.x : a.x - b.x,
			  a.y < b.y ? b.y - a.y : a.y - b.y);
}

- (NSRect)visibleRect
{
	NSRect r = _bounds;
	NSView *v = _superview;

	/* EVERY ANCESTOR'S BOUNDS, BROUGHT INTO THIS VIEW'S COORDINATES AND INTERSECTED IN. The conversion is
	 * the same one the rest of the class uses, which is the point: a second implementation of the flip
	 * here is how the two would come to disagree. */
	while (v != nil) {
		r = fn_intersect(r, [v convertRect:[v bounds] toView:self]);
		v = [v superview];
	}
	return r;
}

/* ------------------------------------------------------------------------- */
/* the offscreen render                                                      */
/* ------------------------------------------------------------------------- */

/* ONE VIEW, IN ITS PARENT'S COORDINATE SYSTEM, WITH ITS SUBVIEWS.
 *
 * THE FLIP RULE HERE IS THE SAME ONE `fn_to_super` USES, AND FOR THE SAME REASON: the frame places the
 * view in its parent's y direction, the view's own coordinates run in ITS direction, and the two are the
 * same rectangle read from opposite ends exactly when the directions DIFFER. So a view whose direction
 * differs from its parent's is entered by MIRRORING about its frame — and the ORDER of the two calls is
 * load-bearing, measured in NSImage's canvas: SCALE FIRST, THEN TRANSLATE, because a translate applied
 * after a scale is not itself mirrored.
 *
 * A NON-ZERO BOUNDS ORIGIN IS REFUSED BY NAME, and it is the one thing this draw does not model: a
 * bounds origin that is not zero is a scroll offset, which C8.14's header already lists as not modelled,
 * and offsetting it here would be a second, silently different answer to the same question. */
static void fn_draw_view(NSView *v, CGContextRef cg, NSRect dirty)
{
	NSRect f = [v frame];
	NSRect b = [v bounds];
	NSView *parent = [v superview];
	BOOL same = (parent == nil) ? NO : ([v isFlipped] == [parent isFlipped]);
	NSArray *subs;
	NSUInteger i;

	if (b.origin.x != 0.0 || b.origin.y != 0.0) {
		fprintf(stderr, "APPKIT-REFUSE: -cacheDisplayInRect: will not draw a view whose BOUNDS ORIGIN "
				"is not zero (%g, %g) — that is a scroll offset, and this layer does not model "
				"one\n", (double)b.origin.x, (double)b.origin.y);
		return;
	}
	CGContextSaveGState(cg);
	/* ENTER THE VIEW'S RECTANGLE IN THE PARENT'S SYSTEM, then its own. */
	if (parent == nil) {
		/* THE ROOT DRAWS IN THE DESTINATION'S OWN SYSTEM, which the canvas gives as y-down — so a root
		 * that is NOT flipped has to be mirrored about its whole frame. */
		if (![v isFlipped]) {
			CGContextScaleCTM(cg, 1.0, -1.0);
			CGContextTranslateCTM(cg, 0.0, f.origin.y + f.size.height);
		}
	} else if (!same) {
		CGContextScaleCTM(cg, 1.0, -1.0);
		CGContextTranslateCTM(cg, f.origin.x, f.origin.y + f.size.height);
	} else {
		CGContextTranslateCTM(cg, f.origin.x, f.origin.y);
	}
	/* THE VIEW'S OWN CLIP, so a subclass cannot draw outside itself. */
	CGContextClipToRect(cg, b);
	[v drawRect:dirty];
	subs = [v subviews];
	for (i = 0; i < [subs count]; i++) {
		fn_draw_view([subs objectAtIndex:i], cg, dirty);
	}
	CGContextRestoreGState(cg);
}

- (void)cacheDisplayInRect:(NSRect)rect toBitmapImageRep:(NSBitmapImageRep *)bitmapImageRep
{
	NSImage *canvas;
	NSArray *reps;
	NSBitmapImageRep *src;
	const unsigned char *sp;
	unsigned char *dp;
	NSInteger rows;
	NSInteger cols;
	NSInteger row;
	size_t sstride;
	size_t dstride;

	if (bitmapImageRep == nil || rect.size.width <= 0.0 || rect.size.height <= 0.0) {
		fprintf(stderr, "APPKIT-REFUSE: -cacheDisplayInRect: needs a bitmap rep and a non-empty "
				"rect\n");
		return;
	}
	dp = [bitmapImageRep bitmapData];
	if (dp == NULL) {
		fprintf(stderr, "APPKIT-REFUSE: -cacheDisplayInRect: needs a rep with WRITABLE bytes; this "
				"one is CGIMAGE-BACKED (what +imageRepWithData: produces) and caching into it "
				"would need a second copy of the pixels\n");
		return;
	}
	/* THE CANVAS IS THE REP'S PIXEL SIZE, and `flipped:YES` because that is the destination's own
	 * orientation: y grows down from the top, which is what a bitmap's rows mean. */
	canvas = [NSImage imageWithSize:NSMakeSize((CGFloat)[bitmapImageRep pixelsWide],
						   (CGFloat)[bitmapImageRep pixelsHigh])
				flipped:YES
			 drawingHandler:^BOOL(NSRect dst) {
		CGContextRef c = [[NSGraphicsContext currentContext] CGContext];

		if (c != NULL) {
			[NSGraphicsContext saveGraphicsState];
			fn_draw_view(self, c, rect);
			[NSGraphicsContext restoreGraphicsState];
		}
		(void)dst;
		return YES;
	}];
	if (canvas == nil) {
		return;
	}
	reps = [canvas representations];
	src = ([reps count] > 0) ? [reps objectAtIndex:0] : nil;
	sp = (src != nil) ? [src bitmapData] : NULL;
	if (sp == NULL) {
		return;
	}
	/* ROW BY ROW, BECAUSE THE TWO STRIDES NEED NOT MATCH — and both buffers are R, G, B, A
	 * premultiplied, so the move is a copy rather than a conversion. */
	rows = [bitmapImageRep pixelsHigh];
	cols = [bitmapImageRep pixelsWide];
	if (rows > [src pixelsHigh]) {
		rows = [src pixelsHigh];
	}
	if (cols > [src pixelsWide]) {
		cols = [src pixelsWide];
	}
	sstride = (size_t)[src bytesPerRow];
	dstride = (size_t)[bitmapImageRep bytesPerRow];
	for (row = 0; row < rows; row++) {
		memcpy(dp + (size_t)row * dstride, sp + (size_t)row * sstride, (size_t)cols * 4u);
	}
}

- (void)drawRect:(NSRect)dirtyRect
{
	/* A PLAIN VIEW DRAWS NOTHING, which is also what Apple's does. This exists to be OVERRIDDEN, and the
	 * slice that CALLS it is the next one: it needs a context to pass, and that decision is the substrate
	 * this slice deliberately left open. */
	(void)dirtyRect;
}

@end
