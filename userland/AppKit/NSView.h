/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSView.h — A RECTANGLE IN A TREE, and the coordinate systems that tree implies (C8.14).
 *
 * THIS SLICE IS THE GEOMETRY, NOT THE DRAWING, and the split is deliberate rather than a stop halfway:
 * almost everything a view does — where it is, what is inside it, which subview a click landed on, what
 * part of it is visible — is answered by the TREE and nothing else. None of that needs a display server,
 * so all of it is provable now. The drawing (`-drawRect:` being called, and being called with a context
 * to draw into) is the next slice, because THAT is where a substrate has to be chosen: this AppKit has
 * no window server, so the honest answer is an offscreen one, and that decision deserves its own turn
 * rather than a guess smuggled in with the geometry.
 *
 * SO `-drawRect:` IS DECLARED HERE AND DOES NOTHING, which is also what Apple's plain `NSView` does: a
 * view with no subclass draws nothing, and the method exists to be OVERRIDDEN. A subclass that overrides
 * it is drawing code waiting for the slice that calls it.
 *
 * THE TWO PARTS OF A VIEW'S GEOMETRY, AND WHY BOTH EXIST: `frame` is the view's rectangle in its
 * SUPERVIEW's coordinates, and `bounds` is its own coordinate system. They are usually the same size and
 * both start at the origin, which is why the pair looks redundant until a view scrolls or scales — this
 * layer keeps them SEPARATE and states the relationship it does not model: setting one sets the other's
 * SIZE and leaves its ORIGIN alone. Apple's bounds/frame relationship is richer (a bounds origin that is
 * not zero is a scroll offset), and the richer part is not claimed here.
 *
 * `flipped` IS THE DIRECTION OF THE Y AXIS, and it is the reason coordinate conversion is not arithmetic
 * on the frame alone: an unflipped view has its origin at its BOTTOM-LEFT and y growing up, a flipped one
 * at its TOP-LEFT with y growing down, so the same point converts differently. Getting this wrong is
 * visible as a mirrored drawing rather than as an error, which is why the probe checks a FLIPPED
 * SUPERVIEW against an UNFLIPPED CHILD rather than only the easy case.
 *
 * WHAT IS NOT HERE, AND WHY: `window`, and everything that needs one (`-display`,
 * `-displayIfNeeded`, `-viewDidMoveToWindow`, `-nextKeyView`, the layer and backing-scale members
 * `-convertPointToBacking:`/`-convertPointToLayer:` and their siblings) — there is no window server, and
 * a scale factor needs a screen to ask. `-graphicsContext`, `-lockFocus`, `-unlockFocus`,
 * `-lockFocusIfCanDraw` and the `…ToBase`/`…FromBase` conversions are ABSENT BECAUSE THEY ARE `struck`
 * ROWS: deprecated at this tree's pinned vintage, and the modern replacements
 * (`-cacheDisplayInRect:toBitmapImageRep:`, `-convertPoint:fromView:`) are the ones a caller should
 * have. The responder chain, the layer tree, `NSView`'s layout constraints and its `NSMenu`/tooltip
 * surface each need a subsystem that does not exist here; each is absent rather than stubbed.
 */

#import <Foundation/NSArray.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSView : NSObject
{
	/* THE FRAME IS THE VIEW IN ITS SUPERVIEW; THE BOUNDS ARE ITS OWN SYSTEM. See the note above for the
	 * relationship this layer keeps simple. */
	NSRect _frame;
	NSRect _bounds;
	BOOL _flipped;
	BOOL _needsDisplay;
	BOOL _hidden;
	/* OWNED. The subviews are ordered BACK TO FRONT — index 0 is the bottom, the LAST is the top — which
	 * is Apple's order and the reason `-hitTest:` walks the array in REVERSE. */
	NSMutableArray *_subviews;
	/* BORROWED, as Apple's is: a view does not own its superview, and a parent owns its children. */
	NSView *_superview;
}

/* THE DESIGNATED INITIALISER. The frame is given in the SUPERVIEW's coordinates, and the bounds start as
 * the same size at the origin, which is what every view begins as. */
- (instancetype)initWithFrame:(NSRect)frameRect;

/* THE GEOMETRY. Setting either sets the other's SIZE and leaves its ORIGIN alone. */
@property NSRect frame;
@property NSRect bounds;

/* THE Y AXIS DIRECTION. `-isFlipped` is the getter, as Apple's is. */
@property (getter=isFlipped) BOOL flipped;

/* WHETHER THE VIEW HAS BEEN DRAWN SINCE SOMETHING CHANGED. A NEW VIEW NEEDS DISPLAY, because nothing has
 * drawn it yet. Nothing in this slice acts on the flag — the drawing slice does — but a caller can set
 * and read it, and the setter is Apple's `-setNeedsDisplay:`. */
@property BOOL needsDisplay;

/* AND WHETHER IT IS HIDDEN. Not a drawing decision: `-hitTest:` ignores a hidden view, which is the one
 * thing this slice can honour about it. */
@property (getter=isHidden) BOOL hidden;

/* THE TREE. `-addSubview:` RETAINS and re-parents (removing the view from a previous superview first,
 * as Apple's does); `-removeFromSuperview` clears the borrowed link. `_superview` is BORROWED, so a view
 * that is still in a parent's list must outlive nothing — the parent owns it. */
@property (readonly, copy) NSArray *subviews;
@property (nullable, readonly) NSView *superview;
- (void)addSubview:(NSView *)aView;
- (void)removeFromSuperview;

/* THE TREE QUERIES. `-isDescendantOf:` asks the ancestor chain; `-hitTest:` asks the tree the other way
 * round — WHICH VIEW IS AT THIS POINT — and takes its point in the SUPERVIEW's coordinates, as Apple's
 * does. It walks the subviews in REVERSE (topmost first), and answers self only if the point is inside
 * its bounds. */
- (BOOL)isDescendantOf:(NSView *)aView;
- (nullable NSView *)hitTest:(NSPoint)point;

/* COORDINATE CONVERSION, WHICH IS WHERE `flipped` HAS TEETH. A NULL VIEW MEANS "the window's coordinate
 * system", and with no window here the ROOT VIEW stands in for it — the one place this slice has to give
 * NULL a meaning, and it is stated rather than implied. */
- (NSPoint)convertPoint:(NSPoint)point fromView:(nullable NSView *)view;
- (NSPoint)convertPoint:(NSPoint)point toView:(nullable NSView *)view;
- (NSRect)convertRect:(NSRect)rect fromView:(nullable NSView *)view;
- (NSRect)convertRect:(NSRect)rect toView:(nullable NSView *)view;

/* THE PART OF THE VIEW NOT CLIPPED AWAY BY ITS ANCESTORS, in the view's own coordinates. */
@property (readonly) NSRect visibleRect;

/* THE OVERRIDE POINT, WHICH THIS SLICE DECLARES AND DOES NOT CALL. A plain view draws nothing. */
- (void)drawRect:(NSRect)dirtyRect;

@end

NS_ASSUME_NONNULL_END
