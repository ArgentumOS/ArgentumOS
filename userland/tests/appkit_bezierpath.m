/*
 * appkit_bezierpath — the first thing in this tree that DRAWS, and the checks that it really does.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A PATH CLASS IS ONLY PROVEN BY PIXELS, so most of what follows sets the CURRENT graphics context to
 * a bitmap, builds a path, draws it, and reads the surface. A check that only asked `-bounds` would
 * pass for a class that never reached CoreGraphics at all.
 *
 * AND THREE CHECKS ARE ABOUT THINGS THAT ARE EASY TO GET SUBTLY WRONG: the winding rule (a donut
 * filled even-odd has a HOLE and filled non-zero does not, which one assertion can tell apart), the
 * arc's DEGREES-TO-RADIANS conversion (measured by WHERE a quarter-turn arc ends), and the class
 * defaults (a new path takes them, an older one keeps its own).
 *
 * IT IS MRC AND WRAPS ITSELF IN A POOL, because this class's convenience constructors autorelease the
 * way Apple's do — the tree's Foundation autoreleases too, and `@autoreleasepool` is what the landed
 * Objective-C probes use.
 */
#import <AppKit/NSBezierPath.h>
#import <AppKit/NSGraphicsContext.h>

#import <CoreGraphics/CGBitmapContext.h>
#import <CoreGraphics/CGContext.h>
#import <CoreGraphics/CGPath.h>
#import <Foundation/NSAffineTransform.h>

#include <stdio.h>
#include <string.h>

#define W 16
#define H 16

static int failures;
static unsigned char p[4];

static void check(const char *name, int ok)
{
	printf("APPKIT-PATH %-68s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void check_num(const char *name, double got, double want, double tol)
{
	double d = got - want;

	if (d < 0) {
		d = -d;
	}
	printf("APPKIT-PATH %-68s %s (got %g, want %g ±%g)\n", name, d <= tol ? "ok" : "FAIL", got,
	       want, tol);
	if (!(d <= tol)) {
		failures++;
	}
}

static CGContextRef bitmap(void)
{
	static unsigned char buf[W * H * 4];

	memset(buf, 0, sizeof(buf));
	return CGBitmapContextCreate(buf, W, H, 8, W * 4, CGColorSpaceCreateDeviceRGB(),
				     kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
}

static void pixel(CGContextRef c, int x, int y, unsigned char *out)
{
	unsigned char *d = CGBitmapContextGetData(c);
	int row = H - 1 - y;    /* the probe works in USER space: y up, origin lower-left */

	memcpy(out, d + (size_t)(row * W + x) * 4u, 4);
}

/* IS THERE INK AT THIS USER-SPACE POINT? The only question the pixel checks need. */
static int inked(CGContextRef c, int x, int y)
{
	pixel(c, x, y, p);
	return p[2] > 8 || p[1] > 8 || p[0] > 8;
}

int main(void)
{
	@autoreleasepool {
		CGContextRef cg = bitmap();
		NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];

		[NSGraphicsContext setCurrentContext:g];
		CGContextSetRGBFillColor(cg, 1.0, 1.0, 1.0, 1.0);
		/* AND THE STROKE COLOUR TOO, WHICH COST A DEBUGGING ROUND: the context's stroke colour defaults
		 * to BLACK, `inked()` tests for BRIGHT ink, and so two stroke checks read zero rows inked from a
		 * stroke that had drawn perfectly. The instrument was wrong, not the class. */
		CGContextSetRGBStrokeColor(cg, 1.0, 1.0, 1.0, 1.0);

		/* --- building one, and the bridge --------------------------------------- */
		{
			NSBezierPath *p = [NSBezierPath bezierPath];
			CGPathRef raw;
			NSBezierPath *wrapped;

			check("+bezierPath is empty", [p isEmpty]);
			check("...and has no elements", [p elementCount] == 0);
			[p moveToPoint:NSMakePoint(1.0, 2.0)];
			check_num("after a move the current point is that point", (double)[p currentPoint].y,
				  2.0, 1e-9);
			[p lineToPoint:NSMakePoint(5.0, 6.0)];
			check_num("...and after a line it is the line's end", (double)[p currentPoint].x, 5.0,
				  1e-9);
			check("...with two elements", [p elementCount] == 2);
			check("...and it is no longer empty", ![p isEmpty]);
			check_num("bounds reaches the end point", (double)[p bounds].size.width, 4.0, 1e-9);

			/* THE BRIDGE, AND THE HALF THAT MATTERS IS THAT THE WRAPPED PATH IS MUTABLE: a class
			 * that retained the caller's (possibly immutable) path would crash on the next line. */
			raw = [p CGPath];
			check("-CGPath hands back the path itself", raw != NULL);
			wrapped = [NSBezierPath bezierPathWithCGPath:raw];
			check("+bezierPathWithCGPath: wraps it", wrapped != nil);
			[wrapped lineToPoint:NSMakePoint(9.0, 9.0)];
			check("...and the wrapped path can still be BUILT ON, so it was copied mutably",
			      [wrapped elementCount] == 3);
			check("...while the original is untouched", [p elementCount] == 2);
		}

		/* --- DRAWING, which is the point of the class ---------------------------- */
		{
			NSBezierPath *r = [NSBezierPath bezierPathWithRect:NSMakeRect(2.0, 2.0, 4.0, 4.0)];

			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[r fill];
			check("a filled rect inks its inside", inked(cg, 3, 3));
			check("...and not its outside", !inked(cg, 1, 1));
			check("...and not past its right edge", !inked(cg, 7, 3));
		}

		/* --- the line state, which is the PATH's and reaches the CONTEXT ---------- */
		{
			NSBezierPath *thick = [NSBezierPath bezierPath];
			NSBezierPath *thin = [NSBezierPath bezierPath];
			int i;
			int thick_rows = 0;
			int thin_rows = 0;

			[thick moveToPoint:NSMakePoint(0.0, 8.0)];
			[thick lineToPoint:NSMakePoint(16.0, 8.0)];
			[thick setLineWidth:4.0];
			[thin moveToPoint:NSMakePoint(0.0, 8.0)];
			[thin lineToPoint:NSMakePoint(16.0, 8.0)];
			[thin setLineWidth:1.0];

			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[thick stroke];
			for (i = 0; i < H; i++) {
				if (inked(cg, 8, i)) {
					thick_rows++;
				}
			}
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[thin stroke];
			for (i = 0; i < H; i++) {
				if (inked(cg, 8, i)) {
					thin_rows++;
				}
			}
			check_num("a width-4 stroke on the integer line y=8 covers four whole rows",
				  (double)thick_rows, 4.0, 0.0);
			/* AND A WIDTH-1 STROKE ON THE SAME LINE COVERS TWO, WHICH IS RIGHT AND WAS MY THIRD WRONG
			 * EXPECTATION IN THIS PROBE: the line spans y 7.5…8.5, so it straddles the boundary between
			 * two pixel rows and antialiasing inks HALF of each. A line centred IN a row (y=8.5) would
			 * ink one. This is the "which side is ink" question the parked toolkit's gates learned to
			 * ask, and the check now states the measurement instead of assuming the line sat inside a
			 * pixel. */
			check_num("...while a width-1 stroke on the same line inks TWO rows, because it straddles "
				  "the pixel boundary rather than sitting inside a row",
				  (double)thin_rows, 2.0, 0.0);
		}

		/* --- THE WINDING RULE, which one assertion tells apart ------------------- */
		{
			NSBezierPath *donut = [NSBezierPath bezierPath];

			/* TWO NESTED RECTS: a hole is exactly what even-odd does with them and what non-zero does
			 * not, so this pair of checks is the whole property. */
			[donut appendBezierPathWithRect:NSMakeRect(2.0, 2.0, 10.0, 10.0)];
			[donut appendBezierPathWithRect:NSMakeRect(5.0, 5.0, 4.0, 4.0)];

			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[donut setWindingRule:NSWindingRuleNonZero];
			[donut fill];
			check("non-zero winding fills the INNER rect too (both rings wind the same way)",
			      inked(cg, 6, 6));

			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[donut setWindingRule:NSWindingRuleEvenOdd];
			[donut fill];
			check("...and even-odd leaves it as a HOLE", !inked(cg, 6, 6));
			check("...while the ring itself is still inked", inked(cg, 3, 3));
		}

		/* --- the arc: DEGREES IN, and the endpoint measured ----------------------- */
		{
			NSBezierPath *a = [NSBezierPath bezierPath];

			[a appendBezierPathWithArcWithCenter:NSMakePoint(4.0, 4.0) radius:2.0
						  startAngle:0.0 endAngle:90.0];
			check_num("a 0°→90° counterclockwise arc ends at (4, 6) in y-up space: the degrees "
				  "were converted, and the direction is Apple's",
				  (double)[a currentPoint].y, 6.0, 1e-6);
			check_num("...at the centre's x", (double)[a currentPoint].x, 4.0, 1e-6);

			/* AND THE DIRECTION IS MEASURED BY THE BOUNDS, NOT BY THE ENDPOINT — WHICH IS THE SECOND
			 * THING THIS PROBE GOT WRONG: a clockwise sweep from 0° to 90° goes the LONG WAY ROUND and
			 * still ENDS at 90°, so its endpoint is (4, 6) exactly like the counterclockwise one. What
			 * differs is how much of the circle is covered: a quarter turn spans one quadrant, the
			 * clockwise three-quarter turn spans three. */
			check_num("a counterclockwise quarter turn spans a 2x2 box", (double)[a bounds].size.width,
				  2.0, 1e-6);
			{
				NSBezierPath *b = [NSBezierPath bezierPath];

				[b appendBezierPathWithArcWithCenter:NSMakePoint(4.0, 4.0) radius:2.0
							  startAngle:0.0 endAngle:90.0 clockwise:YES];
				check_num("...while the CLOCKWISE sweep from the same angles goes the long way round",
					  (double)[b bounds].size.width, 4.0, 1e-6);
				check_num("...and so does its height", (double)[b bounds].size.height, 4.0, 1e-6);
			}
		}

		/* --- the class defaults are consulted when a path is MADE ---------------- */
		{
			NSBezierPath *before = [NSBezierPath bezierPath];

			check_num("the default line width is Apple's 1.0",
				  (double)[NSBezierPath defaultLineWidth], 1.0, 1e-9);
			check_num("...and a new path takes it", (double)[before lineWidth], 1.0, 1e-9);
			[NSBezierPath setDefaultLineWidth:5.0];
			{
				NSBezierPath *after = [NSBezierPath bezierPath];

				check_num("...changing the default changes the NEXT path",
					  (double)[after lineWidth], 5.0, 1e-9);
				check_num("...and leaves the one already made alone",
					  (double)[before lineWidth], 1.0, 1e-9);
			}
			[NSBezierPath setDefaultLineWidth:1.0];
		}

		/* --- appending is the element replay, and transforming is the same walk --- */
		{
			NSBezierPath *p = [NSBezierPath bezierPathWithRect:NSMakeRect(0.0, 0.0, 2.0, 2.0)];

			[p appendBezierPath:[NSBezierPath bezierPathWithRect:NSMakeRect(8.0, 8.0, 2.0, 2.0)]];
			check_num("appending a second rect widens the bounds to include both",
				  (double)[p bounds].size.width, 10.0, 1e-6);
			check("...and appends its elements", [p elementCount] == 10);

			[p transformUsingAffineTransform:[NSAffineTransform transform]];
			{
				NSAffineTransform *m = [NSAffineTransform transform];

				[m translateXBy:4.0 yBy:0.0];
				[p transformUsingAffineTransform:m];
			}
			check_num("...and a translate by 4 moves the bounds' origin",
				  (double)[p bounds].origin.x, 4.0, 1e-6);
		}

		/* --- the clip, which INTERSECTS (the only clipping the context can do) ---- */
		{
			/* `-addClip` IS NOT TESTED BECAUSE IT DOES NOT EXIST: this library's context clips with a
			 * rectangle REGION and has no path clip at all (NSBezierPath.h says so). What CAN be checked
			 * is that a path drawn inside a rectangle clip lands only there. */
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			CGContextClipToRect(cg, CGRectMake(4.0, 4.0, 4.0, 4.0));
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("an NSBezierPath fill HONOURS the context's rectangle clip", inked(cg, 5, 5));
			check("...and is confined by it", !inked(cg, 1, 1));
			CGContextResetClip(cg);
		}

		/* --- THE CLIP FAMILY, which C8.6's rectilinear path clip made possible ------------ */
		{
			NSBezierPath *a = [NSBezierPath bezierPathWithRect:NSMakeRect(0.0, 0.0, 8.0, 8.0)];
			NSBezierPath *b = [NSBezierPath bezierPathWithRect:NSMakeRect(4.0, 4.0, 8.0, 8.0)];

			/* TWICE WITH THE SAME PATH IS A NO-OP, and that is also the check that the RECEIVER's
			 * path survived: CoreGraphics' clip consumes the COPY the context was given, not this
			 * object's, so a second call clips with the same shape again. */
			CGContextResetClip(cg);
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[a addClip];
			[a addClip];
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("-addClip twice with the same path leaves the clip where it was (so the path "
			      "survived)", inked(cg, 3, 3));
			check("...and it is still a clip", !inked(cg, 9, 9));

			CGContextResetClip(cg);
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[a addClip];
			[b addClip];
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("...and two DIFFERENT clips INTERSECT: the overlap paints", inked(cg, 5, 5));
			check("...while the first clip's own area no longer does", !inked(cg, 2, 2));

			/* setClip REPLACES, which is the correction the header records. */
			CGContextResetClip(cg);
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[a addClip];
			[b setClip];
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("-setClip REPLACES the clip: the new area paints", inked(cg, 9, 9));
			check("...and the replaced one no longer does", !inked(cg, 3, 3));

			CGContextResetClip(cg);
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			[NSBezierPath clipRect:NSMakeRect(2.0, 2.0, 4.0, 4.0)];
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("+clipRect: intersects", inked(cg, 3, 3));
			check("...and confines", !inked(cg, 9, 9));

			/* AND THE REFUSAL A CALLER MAY MEET IS COREGRAPHICS', INHERITED RATHER THAN COPIED. */
			CGContextResetClip(cg);
			CGContextClearRect(cg, CGRectMake(0.0, 0.0, 16.0, 16.0));
			{
				NSBezierPath *tri = [NSBezierPath bezierPath];

				[tri moveToPoint:NSMakePoint(0.0, 0.0)];
				[tri lineToPoint:NSMakePoint(16.0, 0.0)];
				[tri lineToPoint:NSMakePoint(8.0, 16.0)];
				[tri closePath];
				[tri addClip];
			}
			[NSBezierPath fillRect:NSMakeRect(0.0, 0.0, 16.0, 16.0)];
			check("a THREE-CORNERED -addClip is refused by CoreGraphics, leaving the clip alone",
			      inked(cg, 1, 1));
			CGContextResetClip(cg);
		}

		/* --- and with NO current context a draw does nothing rather than crashing -- */
		[NSGraphicsContext setCurrentContext:nil];
		{
			NSBezierPath *p = [NSBezierPath bezierPathWithRect:NSMakeRect(1.0, 1.0, 4.0, 4.0)];

			[p fill];
			[p stroke];
			check("drawing with no current context is harmless (NSBezierPath.h says it draws "
			      "nothing)", 1);
		}

		CGContextRelease(cg);
	}

		/* --- THE ELEMENT MODEL, READ BACK ------------------------------------------------------ */
		/* A PATH IS A SEQUENCE OF STEPS, and reading them back is what lets a caller walk a path it did
		 * not build. The path below holds one of each kind this model has, in order. */
		{
			NSBezierPath *p = [NSBezierPath bezierPath];
			CGPoint pts[3];

			[p moveToPoint:NSMakePoint(1.0, 2.0)];
			[p lineToPoint:NSMakePoint(3.0, 4.0)];
			[p curveToPoint:NSMakePoint(8.0, 2.0) controlPoint1:NSMakePoint(5.0, 9.0)
				 controlPoint2:NSMakePoint(7.0, 9.0)];
			[p closePath];

			check_num("the path reports four elements", (double)[p elementCount], 4.0, 0.0);
			check("element 0 is a MoveTo", [p elementAtIndex:0] == NSBezierPathElementMoveTo);
			pts[0] = NSMakePoint(-1.0, -1.0);
			check("...and its associated points are written out",
			      [p elementAtIndex:0 associatedPoints:pts] == NSBezierPathElementMoveTo &&
			      pts[0].x == 1.0 && pts[0].y == 2.0);
			check("element 1 is a LineTo with its endpoint",
			      [p elementAtIndex:1 associatedPoints:pts] == NSBezierPathElementLineTo &&
			      pts[0].x == 3.0 && pts[0].y == 4.0);
			/* THE CUBIC IS THE TEST THAT THE POINT COUNT FOLLOWS THE ELEMENT KIND: three points. */
			check("element 2 is a CubicCurveTo with THREE points",
			      [p elementAtIndex:2 associatedPoints:pts] == NSBezierPathElementCubicCurveTo &&
			      pts[0].x == 5.0 && pts[1].x == 7.0 && pts[2].x == 8.0);
			check("element 3 is a ClosePath", [p elementAtIndex:3] == NSBezierPathElementClosePath);
			/* AND A NULL ARRAY IS ALLOWED — asking for the KIND alone must not need storage. */
			check("...and asking for the kind with a NULL array is fine",
			      [p elementAtIndex:3 associatedPoints:NULL] == NSBezierPathElementClosePath);
			/* AN INDEX PAST THE END IS REFUSED BY NAME, and the answer must be the one element that
			 * carries NO points, so a caller that ignores the refusal cannot read uninitialised
			 * memory out of its own array. */
			pts[0] = NSMakePoint(-1.0, -1.0);
			check("an index past the end is refused, and writes NO points",
			      [p elementAtIndex:99 associatedPoints:pts] == NSBezierPathElementMoveTo &&
			      pts[0].x == -1.0 && pts[0].y == -1.0);
		}

		/* --- AND MEMBERSHIP, BY THE PATH'S OWN WINDING RULE ------------------------------------ */
		{
			NSBezierPath *r = [NSBezierPath bezierPathWithRect:NSMakeRect(0.0, 0.0, 10.0, 10.0)];

			check("a point inside a rectangle is inside the path",
			      [r containsPoint:NSMakePoint(5.0, 5.0)]);
			check("...and one beyond it is not", ![r containsPoint:NSMakePoint(15.0, 5.0)]);
			check("...nor is one outside on the other side",
			      ![r containsPoint:NSMakePoint(-1.0, 5.0)]);
		}
		/* THE PAIR THAT PROVES THE RULE IS HONOURED RATHER THAN ASSUMED: the SAME two nested rectangles
		 * are a DONUT under even-odd and a SOLID under non-zero, because both rings wind the same way.
		 * One rule would answer both questions identically, so the pair cannot pass by accident. */
		{
			NSBezierPath *donut = [NSBezierPath bezierPath];

			[donut appendBezierPathWithRect:NSMakeRect(0.0, 0.0, 10.0, 10.0)];
			[donut appendBezierPathWithRect:NSMakeRect(2.0, 2.0, 6.0, 6.0)];
			[donut setWindingRule:NSWindingRuleEvenOdd];
			check("a donut's CENTRE is OUTSIDE it under the even-odd rule",
			      ![donut containsPoint:NSMakePoint(5.0, 5.0)]);
			check("...while its RING is inside", [donut containsPoint:NSMakePoint(1.0, 5.0)]);
			[donut setWindingRule:NSWindingRuleNonZero];
			check("...and under the NON-ZERO rule the same centre is INSIDE",
			      [donut containsPoint:NSMakePoint(5.0, 5.0)]);
		}
		/* AND THE TEST RUNS ON THE FLATTENED PATH, which an OVAL proves: it has no line segments at
		 * all, so a crossing test that only understood straight edges would answer NO everywhere. */
		{
			NSBezierPath *oval = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(0.0, 0.0, 10.0, 6.0)];

			check("an OVAL's centre is inside it, which a lines-only test could not say",
			      [oval containsPoint:NSMakePoint(5.0, 3.0)]);
			check("...and a corner of its bounding box is outside",
			      ![oval containsPoint:NSMakePoint(0.5, 0.5)]);
		}


	{
		/* §63.187: -transformBezierPath: RETURNS A TRANSFORMED COPY; the argument is untouched. */
		NSBezierPath *path = [NSBezierPath bezierPath];
		NSAffineTransform *m = [NSAffineTransform transform];
		NSBezierPath *moved;
		NSPoint before;
		NSPoint after;

		[path moveToPoint:NSMakePoint(1.0, 1.0)];
		[path lineToPoint:NSMakePoint(3.0, 1.0)];
		[m translateXBy:10.0 yBy:20.0];
		moved = [m transformBezierPath:path];
		before = [path currentPoint];
		after = [moved currentPoint];
		printf("APPKIT-PATH original.x=%g copy.x=%g\n", before.x, after.x);
		check("affine-transform-bezier-path-copies", moved != nil && moved != path);
		check_num("...the copy is moved by tX", after.x, 13.0, 1e-9);
		check_num("...and the original is not", before.x, 3.0, 1e-9);
	}

	printf("APPKIT-PATH: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
