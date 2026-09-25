/*
 * appkit_view — a rectangle in a tree, and the coordinate systems that tree implies.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NO DRAWING IS TESTED HERE, ON PURPOSE: this slice is the geometry and the tree, and every check below
 * is arithmetic. That is what makes it provable without a display server, and it is why the drawing slice
 * can be judged on its own.
 *
 * THE CHECKS THAT MATTER MOST ARE THE ONES WHERE A WRONG ANSWER LOOKS FINE:
 *   * A FLIPPED SUPERVIEW with an unflipped child — a point at the child's own origin lands 10 units DOWN
 *     from the parent's top, not at its origin. A conversion that ignored `flipped` would answer (0,0)
 *     and every unflipped check in this file would still pass.
 *   * THE BORROWED SUPERVISOR LINK AFTER THE PARENT DIES: a child holds a POINTER to its parent, so
 *     releasing a parent has to clear it. Nothing else in the tree would notice, and the symptom of
 *     getting it wrong is a crash long afterwards.
 *   * WHICH SUBVIEW A POINT HITS when two overlap: the array is back-to-front, so the LAST one wins.
 */
#import <AppKit/NSView.h>
#import <AppKit/NSBitmapImageRep.h>
#import <AppKit/NSGraphicsContext.h>

#import <CoreGraphics/CGContext.h>

#include <stdio.h>
#include <string.h>

/* A VIEW THAT PAINTS, SO THAT "WAS `-drawRect:` CALLED, AND WITH WHAT?" IS ANSWERABLE. It records the
 * count and the rect it was handed, and fills a rectangle GIVEN IN ITS OWN COORDINATES — which is what
 * makes the flip observable: the same own-coordinates rectangle lands at the top of its frame or at the
 * bottom depending on the direction its system runs in. */
@interface PaintView : NSView
{
@public
	int draws;
	NSRect lastRect;
	NSRect fill;
	double red;
	double green;
	double blue;
}
@end

@implementation PaintView

- (void)drawRect:(NSRect)dirtyRect
{
	CGContextRef c = [[NSGraphicsContext currentContext] CGContext];

	draws++;
	lastRect = dirtyRect;
	if (c != NULL) {
		CGContextSetRGBFillColor(c, (CGFloat)red, (CGFloat)green, (CGFloat)blue, 1.0);
		CGContextFillRect(c, fill);
	}
}

@end

/* A REP WITH WRITABLE BYTES, since `-cacheDisplayInRect:` refuses a CGIMAGE-BACKED one. */
static NSBitmapImageRep *canvas_rep(NSInteger w, NSInteger h)
{
	return [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
				bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
				colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
}

/* The bytes of one pixel of a rep, in its own order (R, G, B, A). */
static void rep_pixel(NSBitmapImageRep *r, NSInteger x, NSInteger y, unsigned char *out)
{
	const unsigned char *d = [r bitmapData];

	memcpy(out, d + ((size_t)y * (size_t)[r bytesPerRow] + (size_t)x * 4u), 4);
}

static int failures;

static void check(const char *name, int ok)
{
	printf("APPKIT-VIEW %-70s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	int ok = (got >= want - tol && got <= want + tol);

	printf("APPKIT-VIEW %-70s %s", name, ok ? "ok" : "FAIL");
	if (!ok) {
		printf(" (got %g, want %g)", got, want);
		failures++;
	}
	printf("\n");
}

static NSView *view(double x, double y, double w, double h)
{
	return [[NSView alloc] initWithFrame:NSMakeRect(x, y, w, h)];
}

int main(void)
{
	@autoreleasepool {
		/* --- GEOMETRY ------------------------------------------------------------------- */
		{
			NSView *v = view(10.0, 20.0, 30.0, 40.0);

			check_num("a new view's frame is what it was given", [v frame].origin.x, 10.0, 0.0);
			check_num("...and its height", [v frame].size.height, 40.0, 0.0);
			check_num("...its BOUNDS start at the origin", [v bounds].origin.x, 0.0, 0.0);
			check_num("...with the frame's size", [v bounds].size.width, 30.0, 0.0);
			check("...it is NOT flipped", ![v isFlipped]);
			check("...it is not hidden", ![v isHidden]);
			check("...and a view nothing has drawn NEEDS display", [v needsDisplay]);
			[v setFlipped:YES];
			check("setFlipped: takes", [v isFlipped]);
			check_num("...and does not move the frame", [v frame].origin.y, 20.0, 0.0);

			/* THE SIZE IS SHARED BETWEEN FRAME AND BOUNDS; THE ORIGINS ARE NOT. */
			[v setBounds:NSMakeRect(5.0, 6.0, 7.0, 8.0)];
			check_num("setBounds: moves the bounds origin", [v bounds].origin.x, 5.0, 0.0);
			check_num("...and takes the frame's SIZE", [v frame].size.width, 7.0, 0.0);
			check_num("...while the frame's ORIGIN stays put", [v frame].origin.y, 20.0, 0.0);
			[v setFrame:NSMakeRect(1.0, 2.0, 3.0, 4.0)];
			check_num("setFrame: takes the bounds' SIZE", [v bounds].size.height, 4.0, 0.0);
			check_num("...and leaves the bounds ORIGIN alone", [v bounds].origin.x, 5.0, 0.0);
			check("...and marks the view as needing display", [v needsDisplay]);
			[v setNeedsDisplay:NO];
			check("...which the setter can clear", ![v needsDisplay]);
			[v release];
		}

		/* --- THE TREE ------------------------------------------------------------------- */
		{
			NSView *root = view(0.0, 0.0, 100.0, 100.0);
			NSView *a = view(0.0, 0.0, 10.0, 10.0);
			NSView *b = view(0.0, 0.0, 10.0, 10.0);
			NSView *kid = view(0.0, 0.0, 5.0, 5.0);
			NSView *other = view(0.0, 0.0, 50.0, 50.0);

			[root addSubview:a];
			check("a subview knows its superview", [a superview] == root);
			check_num("...and the parent lists it", (double)[[root subviews] count], 1.0, 0.0);
			[root addSubview:b];
			check_num("...a second one follows", (double)[[root subviews] count], 2.0, 0.0);
			check("...and the BACK-TO-FRONT order is the order added",
			      [[root subviews] objectAtIndex:0] == a && [[root subviews] objectAtIndex:1] == b);

			/* A VIEW HAS ONE PARENT, SO ADDING IT ELSEWHERE MOVES IT. */
			[other addSubview:a];
			check_num("adding a view that has a parent MOVES it — the old parent loses it",
				  (double)[[root subviews] count], 1.0, 0.0);
			check("...and the new one gains it", [a superview] == other);
			check_num("...with one subview", (double)[[other subviews] count], 1.0, 0.0);

			/* AND THE ANCESTOR QUERY. */
			[root addSubview:kid];
			[b addSubview:kid];
			check("a grandchild is a descendant of the root", [kid isDescendantOf:root]);
			check("...and of its own parent", [kid isDescendantOf:b]);
			check("...but a sibling is not", ![a isDescendantOf:b]);
			check("...and a view is NOT its own descendant", ![b isDescendantOf:b]);

			/* ADDING A VIEW TO ITSELF IS IGNORED RATHER THAN MAKING A CYCLE. */
			[root addSubview:root];
			check("adding a view to itself is ignored", [root superview] == nil);

			[b removeFromSuperview];
			check("removing a view clears its borrowed superview", [b superview] == nil);
			/* THE COUNT IS ZERO AND NOT ONE: `kid` was MOVED to `b` before this, so removing `b` leaves
			 * the root with nothing — the expectation, not the answer, is what needed the correcting. */
			check_num("...and takes it out of the parent's list, which then holds nothing",
				  (double)[[root subviews] count], 0.0, 0.0);
			[kid removeFromSuperview];
			[root removeFromSuperview];
			check("...and removing a view with NO parent is a no-op", YES);

			[a release];
			[b release];
			[kid release];
			[other release];
			[root release];
		}

		/* --- THE BORROWED LINK WHEN THE PARENT DIES ---------------------------------------- */
		{
			NSView *parent = view(0.0, 0.0, 10.0, 10.0);
			NSView *child = view(0.0, 0.0, 5.0, 5.0);

			[parent addSubview:child];
			check("a child is in its parent before the parent goes", [child superview] == parent);
			/* THE PARENT IS RELEASED WITH THE CHILD STILL ALIVE, which is the case that leaves a
			 * dangling borrowed pointer if the parent's -dealloc does not clear it. */
			[parent release];
			check("...and its borrowed superview is CLEARED when the parent is deallocated",
			      [child superview] == nil);
			[child release];
		}

		/* --- CONVERSION, INCLUDING THE FLIPPED CASE ---------------------------------------- */
		{
			NSView *root = view(0.0, 0.0, 100.0, 100.0);
			NSView *child = view(10.0, 20.0, 30.0, 40.0);
			NSPoint p;
			NSRect r;

			[root addSubview:child];
			p = [child convertPoint:NSMakePoint(5.0, 7.0) toView:root];
			check_num("an UNFLIPPED child converts by adding its frame origin", p.x, 15.0, 0.0);
			check_num("...in y too", p.y, 27.0, 0.0);
			p = [root convertPoint:NSMakePoint(15.0, 27.0) toView:child];
			check_num("...and the round trip comes back", p.x, 5.0, 0.0);
			check_num("...in y as well", p.y, 7.0, 0.0);
			/* A NULL VIEW IS THE ROOT, SO `fromView:nil` IS THE INVERSE OF `toView:nil` — it does not
			 * repeat it. The check is the ROUND TRIP, which holds whatever the stand-in means. */
			p = [child convertPoint:NSMakePoint(5.0, 7.0) fromView:nil];
			check_num("a NULL view means the ROOT: fromView:nil INVERTS toView:nil", p.y, -13.0, 0.0);
			p = [child convertPoint:[child convertPoint:NSMakePoint(5.0, 7.0) toView:nil] fromView:nil];
			check_num("...so the two round-trip", p.y, 7.0, 0.0);
			/* THE UPPER-RIGHT CORNER OF A 10x10 RECT AT THE CHILD'S ORIGIN. */
			r = [child convertRect:NSMakeRect(0.0, 0.0, 10.0, 10.0) toView:root];
			check_num("a rect converts as a rect", r.origin.x, 10.0, 0.0);
			check_num("...keeping its size", r.size.width, 10.0, 0.0);

			/* ***THE FLIPPED SUPERVIEW***: the same child under a parent whose y grows DOWN. The child's
			 * own origin is its BOTTOM-left, which is the parent's top-left plus the child's HEIGHT —
			 * so a conversion that ignored `flipped` would answer (10, 20) instead of (10, 60). */
			[root setFlipped:YES];
			p = [child convertPoint:NSMakePoint(0.0, 0.0) toView:root];
			check_num("...and under a FLIPPED superview the child's origin lands at the bottom of its "
				  "frame", p.y, 60.0, 0.0);
			check_num("...with x unchanged", p.x, 10.0, 0.0);
			p = [root convertPoint:NSMakePoint(10.0, 60.0) toView:child];
			check_num("...and that round trip comes back to the child's origin", p.y, 0.0, 0.0);
			/* A POINT AT THE PARENT'S ORIGIN IS THE CHILD'S TOP EDGE, which is y = its height. */
			p = [root convertPoint:NSMakePoint(10.0, 20.0) toView:child];
			check_num("...the parent's origin is the child's TOP edge", p.y, 40.0, 0.0);

			[child release];
			[root release];
		}

		/* --- VISIBLE RECT AND HIT TESTING -------------------------------------------------- */
		{
			NSView *root = view(0.0, 0.0, 100.0, 100.0);
			NSView *big = view(50.0, 50.0, 100.0, 100.0);
			NSView *low = view(0.0, 0.0, 20.0, 20.0);
			NSView *high = view(0.0, 0.0, 20.0, 20.0);
			NSView *hid = view(0.0, 0.0, 20.0, 20.0);
			NSRect vis;

			[root addSubview:big];
			vis = [big visibleRect];
			check_num("a view sticking out of its parent is visible for the part INSIDE it",
				  vis.size.width, 50.0, 0.0);
			check_num("...in both directions", vis.size.height, 50.0, 0.0);
			vis = [root visibleRect];
			check_num("...and a view inside its parent is fully visible", vis.size.width, 100.0, 0.0);
			/* IT LEAVES THE TREE BEFORE IT IS RELEASED: A RELEASE DOES NOT REMOVE A SUBVIEW, so leaving
			 * `big` in place would shadow the hit tests below — which is what the first version did. */
			[big removeFromSuperview];
			[big release];

			[root addSubview:low];
			[root addSubview:high];
			check("a point over two subviews hits the TOPMOST — the one added last",
			      [root hitTest:NSMakePoint(5.0, 5.0)] == high);
			[high setHidden:YES];
			check("...and a HIDDEN view is not hit at all", [root hitTest:NSMakePoint(5.0, 5.0)] == low);
			[high release];
			[low release];
			[root addSubview:hid];
			[hid setHidden:YES];
			check("a point over no subview hits the PARENT", [root hitTest:NSMakePoint(90.0, 90.0)] == root);
			check("...and a point outside everything hits nothing",
			      [root hitTest:NSMakePoint(200.0, 200.0)] == nil);
			[hid release];
			[root release];
		}
	}

		/* --- THE OFFSCREEN RENDER: `-drawRect:` CALLED, WITH A REAL CONTEXT ------------------- */
		{
			PaintView *v = [[PaintView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 8.0, 8.0)];
			NSBitmapImageRep *rep = canvas_rep(8, 8);
			unsigned char px[4];

			v->fill = NSMakeRect(0.0, 0.0, 8.0, 8.0);
			v->red = 1.0;
			v->green = 0.0;
			v->blue = 0.0;
			[v cacheDisplayInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0) toBitmapImageRep:rep];
			check_num("caching the display CALLS -drawRect: once", (double)v->draws, 1.0, 0.0);
			check_num("...and hands it the rect in the view's own coordinates", v->lastRect.size.width,
				  8.0, 0.0);
			rep_pixel(rep, 4, 4, px);
			check("...with a context it can really draw into: the pixels are RED",
			      px[0] == 0xff && px[1] == 0x00 && px[2] == 0x00 && px[3] == 0xff);
			[v release];
			[rep release];
		}

		/* --- AND THE SUBVIEWS ARE DRAWN, AT THEIR FRAMES --------------------------------------- */
		{
			PaintView *root = [[PaintView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 8.0, 8.0)];
			PaintView *child = [[PaintView alloc] initWithFrame:NSMakeRect(4.0, 0.0, 4.0, 4.0)];
			NSBitmapImageRep *rep = canvas_rep(8, 8);
			unsigned char px[4];

			root->fill = NSMakeRect(0.0, 0.0, 8.0, 8.0);
			root->red = 1.0;
			root->green = 0.0;
			root->blue = 0.0;
			/* THE ROOT IS FLIPPED, so its y runs DOWN and a frame's origin.y is its TOP edge — which means
			 * the rep's rows ARE the view's y, with no inversion to reason about. */
			[root setFlipped:YES];
			child->fill = NSMakeRect(0.0, 0.0, 4.0, 4.0);
			child->red = 0.0;
			child->green = 0.0;
			child->blue = 1.0;
			[child setFlipped:YES];
			[root addSubview:child];

			[root cacheDisplayInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0) toBitmapImageRep:rep];
			check_num("a subview is drawn too", (double)child->draws, 1.0, 0.0);
			rep_pixel(rep, 5, 1, px);
			check("...and it lands at its FRAME: the right half is BLUE", px[2] == 0xff && px[0] == 0x00);
			rep_pixel(rep, 1, 1, px);
			check("...while the left half is the PARENT's red", px[0] == 0xff && px[2] == 0x00);
			[child release];
			[root release];
			[rep release];
		}

		/* --- ***THE FLIP***: THE SAME OWN-COORDINATES RECTANGLE, IN TWO DIRECTIONS ------------- */
		/* A child that paints only THE UPPER PART OF ITS OWN SYSTEM shows which end that is, AND THE
		 * ASSERTIONS NAME THE ROWS rather than merely requiring the two to differ. ***THAT DISTINCTION IS
		 * THE WHOLE LESSON OF THIS SESSION:*** a check that can tell two states APART but cannot say what
		 * either one MEANS cannot see a swapped pair, which is how C8.12's `flipped:` survived a slice
		 * meaning the opposite of what it said. */
		{
			BOOL at_top[2];      /* is the child's own upper part at the frame's TOP? */
			BOOL at_bottom[2];
			int i;

			for (i = 0; i < 2; i++) {
				PaintView *root = [[PaintView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 8.0, 8.0)];
				PaintView *child = [[PaintView alloc] initWithFrame:NSMakeRect(4.0, 0.0, 4.0, 4.0)];
				NSBitmapImageRep *rep = canvas_rep(8, 8);
				unsigned char px[4];

				root->fill = NSMakeRect(0.0, 0.0, 8.0, 8.0);
				root->red = 1.0;
				[root setFlipped:YES];
				/* THE UPPER PART OF THE CHILD'S OWN SYSTEM, four tall. */
				child->fill = NSMakeRect(0.0, 2.0, 4.0, 2.0);
				child->blue = 1.0;
				child->red = 0.0;
				child->green = 0.0;
				[child setFlipped:(i == 1)];
				[root addSubview:child];
				[root cacheDisplayInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0) toBitmapImageRep:rep];
				rep_pixel(rep, 5, 1, px);
				at_top[i] = (px[2] == 0xff);
				rep_pixel(rep, 5, 3, px);
				at_bottom[i] = (px[2] == 0xff);
				[child release];
				[root release];
				[rep release];
			}
			/* THE CHILD'S DIRECTION MATCHES ITS PARENT'S (i == 1, both flipped): its own y = 2 is BELOW
			 * its own y = 0, so the paint is at the BOTTOM of its frame — the frame's rows 2..3. */
			check("a child whose direction MATCHES its parent's paints its own upper part at the "
			      "frame's BOTTOM", at_bottom[1] && !at_top[1]);
			/* AND ONE WHOSE DIRECTION DIFFERS IS MIRRORED ENTIRELY: the same own part lands at the TOP. */
			check("...and one whose direction DIFFERS paints it at the frame's TOP",
			      at_top[0] && !at_bottom[0]);
		}

		/* --- THE CLIP IS THE VIEW'S OWN BOUNDS ------------------------------------------------ */
		{
			PaintView *root = [[PaintView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 8.0, 8.0)];
			PaintView *child = [[PaintView alloc] initWithFrame:NSMakeRect(4.0, 0.0, 2.0, 2.0)];
			NSBitmapImageRep *rep = canvas_rep(8, 8);
			unsigned char px[4];

			root->fill = NSMakeRect(0.0, 0.0, 8.0, 8.0);
			root->red = 1.0;
			root->green = 0.0;
			root->blue = 0.0;
			[root setFlipped:YES];
			/* THE CHILD PAINTS FAR BEYOND ITSELF, which its clip must stop. */
			child->fill = NSMakeRect(0.0, 0.0, 8.0, 8.0);
			child->red = 0.0;
			child->green = 0.0;
			child->blue = 1.0;
			[child setFlipped:YES];
			[root addSubview:child];
			[root cacheDisplayInRect:NSMakeRect(0.0, 0.0, 8.0, 8.0) toBitmapImageRep:rep];
			rep_pixel(rep, 5, 1, px);
			check("a subview painting beyond itself is CLIPPED to its bounds", px[2] == 0xff);
			rep_pixel(rep, 7, 5, px);
			check("...so a pixel outside it keeps the PARENT's colour", px[0] == 0xff && px[2] == 0x00);
			[child release];
			[root release];
			[rep release];
		}

		/* --- AND THE DESTINATION OUTSIDE THE VIEW IS LEFT ALONE, PLUS THE TWO REFUSALS --------- */
		{
			PaintView *v = [[PaintView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 4.0, 4.0)];
			NSBitmapImageRep *rep = canvas_rep(16, 16);
			unsigned char px[4];
			int before;

			v->fill = NSMakeRect(0.0, 0.0, 4.0, 4.0);
			v->blue = 1.0;
			[v setFlipped:YES];
			[v cacheDisplayInRect:NSMakeRect(0.0, 0.0, 4.0, 4.0) toBitmapImageRep:rep];
			rep_pixel(rep, 12, 12, px);
			check("the part of the destination outside the view is left UNTOUCHED",
			      px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 0);
			before = v->draws;
			[v cacheDisplayInRect:NSMakeRect(0.0, 0.0, 4.0, 4.0) toBitmapImageRep:nil];
			check("...and a NULL rep is refused rather than crashing", v->draws == before);
			[v cacheDisplayInRect:NSMakeRect(0.0, 0.0, 0.0, 4.0) toBitmapImageRep:rep];
			check("...as is an empty rect", v->draws == before);
			[v release];
			[rep release];
		}

	printf("APPKIT-VIEW: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
