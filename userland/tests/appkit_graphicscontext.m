/*
 * appkit_graphicscontext — C8's seam: does the AppKit object really put CoreGraphics in reach?
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE POINT OF A BRIDGE IS THAT THINGS CROSS IT, so the checks that matter are not "does the
 * getter return the pointer" — they are the two that DRAW. One fills a rectangle through the
 * `CGContext` the object hands back and reads the pixel, which is the only way to know the object
 * gave a usable context rather than a copy of the pointer. The other fills, saves, fills in a
 * different colour, restores, and fills again, which is the only way to know that
 * `-saveGraphicsState` reached `CGContextSaveGState` rather than merely existing.
 *
 * AND ONE CHECK EXISTS TO TELL THE TWO STACKS APART, because that is this class's one real trap.
 * `-saveGraphicsState` is the RECEIVER's state; `+saveGraphicsState` pushes the CONTEXT onto a
 * per-thread stack. The probe sets current = A, saves, sets current = B, restores, and asserts
 * that current is A again — an assertion the instance pair cannot satisfy, since A and B are
 * different contexts and no amount of C2 state could put one back in place of the other.
 *
 * IT IS MRC LIKE EVERY OBJECTIVE-C PROBE HERE (the rule passes -fno-objc-arc): the factory
 * returns +1, `+setCurrentContext:` retains, and a probe that has to release what it makes cannot
 * be written under ARC at all.
 */
#import <AppKit/NSGraphicsContext.h>

#import <CoreGraphics/CGBitmapContext.h>
#import <CoreGraphics/CGContext.h>
#import <Foundation/NSObject.h>

#include <stdio.h>
#include <string.h>

#define W 8
#define H 8

static int failures;
static unsigned char p[4];

static void check(const char *name, int ok)
{
	printf("APPKIT-GC %-64s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* A bitmap context over its own buffer, so a check can read the pixels afterwards. */
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

	memcpy(out, d + (size_t)(y * W + x) * 4u, 4);
}

int main(void)
{
	CGContextRef cgA = bitmap();
	CGContextRef cgB = bitmap();
	NSGraphicsContext *a;
	NSGraphicsContext *b;
	NSGraphicsContext *held;

	/* --- the seam: the object gives the context back ------------------------------- */
	a = [NSGraphicsContext graphicsContextWithCGContext:cgA flipped:YES];
	check("the factory makes a context object", a != nil);
	check("...and -CGContext is the context that was handed in", [a CGContext] == cgA);
	check("...and the flip is reported as it was given", [a isFlipped]);

	b = [NSGraphicsContext graphicsContextWithCGContext:cgB flipped:NO];
	check("a second context reports flipped:NO", b != nil && ![b isFlipped]);

	/* --- AND THE CONTEXT IS USABLE, which is the whole point ----------------------- */
	CGContextSetRGBFillColor([a CGContext], 1.0, 0.0, 0.0, 1.0);
	CGContextFillRect([a CGContext], CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	pixel(cgA, 4, 4, p);
	check("drawing through -CGContext reaches the surface (opaque red, R255)",
	      p[2] == 255 && p[1] == 0 && p[0] == 0 && p[3] == 255);

	/* --- the INSTANCE save/restore is the C2 graphics state ------------------------ */
	CGContextSetRGBFillColor([a CGContext], 0.0, 1.0, 0.0, 1.0);
	[a saveGraphicsState];
	CGContextSetRGBFillColor([a CGContext], 0.0, 0.0, 1.0, 1.0);
	CGContextFillRect([a CGContext], CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	pixel(cgA, 0, 0, p);
	check("...and a fill inside the save is the NEW colour (blue)", p[0] == 255 && p[2] == 0);
	[a restoreGraphicsState];
	CGContextFillRect([a CGContext], CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	pixel(cgA, 0, 0, p);
	check("-restoreGraphicsState put the fill colour back (green, not blue)",
	      p[1] == 255 && p[0] == 0);

	/* --- the destination question, answered from what this library can make -------- */
	check("-isDrawingToScreen is NO: every context this library makes is a bitmap",
	      ![a isDrawingToScreen]);
	check("+currentContextDrawingToScreen is NO before anything is current",
	      ![NSGraphicsContext currentContextDrawingToScreen]);

	/* --- the current context is a slot that replaces --------------------------------- */
	check("+currentContext is nil before anything is set", [NSGraphicsContext currentContext] == nil);
	[NSGraphicsContext setCurrentContext:a];
	check("...and after +setCurrentContext: it is the object set",
	      [NSGraphicsContext currentContext] == a);
	check("...so +currentContextDrawingToScreen follows it", ![NSGraphicsContext currentContextDrawingToScreen]);

	/* --- THE DISTINGUISHING CHECK: the CLASS pair is a stack OF CONTEXTS ------------ */
	/* current = A, save, current = B, restore -> current is A AGAIN. The instance pair cannot
	 * do this, because A and B are different contexts. */
	[NSGraphicsContext saveGraphicsState];
	[NSGraphicsContext setCurrentContext:b];
	check("the class save let the current context be replaced while it was held",
	      [NSGraphicsContext currentContext] == b);
	[NSGraphicsContext restoreGraphicsState];
	check("+restoreGraphicsState brought the FIRST context back, not merely a state (A, not B)",
	      [NSGraphicsContext currentContext] == a);

	/* --- the stack nests, and an unbalanced restore is survivable ------------------ */
	[NSGraphicsContext saveGraphicsState];
	[NSGraphicsContext saveGraphicsState];
	[NSGraphicsContext setCurrentContext:b];
	[NSGraphicsContext restoreGraphicsState];
	check("a nested pair unwinds one level at a time", [NSGraphicsContext currentContext] == a);
	[NSGraphicsContext restoreGraphicsState];
	check("...and the outer restore leaves the context that was current", [NSGraphicsContext currentContext] == a);
	[NSGraphicsContext restoreGraphicsState];   /* one too many */
	check("an unbalanced +restoreGraphicsState is survivable and changes nothing",
	      [NSGraphicsContext currentContext] == a);

	/* --- and setting a context does not free the one it was already set to ---------- */
	held = [a retain];
	[NSGraphicsContext setCurrentContext:a];
	check("setting the context that is already current does not free it", [a CGContext] == cgA);
	[held release];

	/* --- an object with NO context is tolerated rather than crashing ---------------- */
	/* `-init` AND NOT A NULL ARGUMENT TO THE FACTORY, which is a distinction the first version of
	 * this probe got wrong: the header declares `graphicsContextWithCGContext:` NON-NULL (Apple's
	 * own contract, under an assume-non-null region), so passing NULL there is a caller error the
	 * header already forbids — and the compiler said so in a `-Wnonnull` warning. `alloc`/`init` is
	 * the path that genuinely produces a context-less object, so it is the one worth covering. */
	{
		NSGraphicsContext *none = [[NSGraphicsContext alloc] init];

		check("an alloc/init object exists and has no context", none != nil && [none CGContext] == NULL);
		check("...whose flip is still reported", none != nil && ![none isFlipped]);
		[none saveGraphicsState];
		[none restoreGraphicsState];
		[none flushGraphics];
		check("...and whose state calls are harmless with no context", 1);
		[none release];
	}

	/* --- flush reaches the context (a no-op for a bitmap, and it must not crash) ---- */
	[a flushGraphics];
	check("-flushGraphics returns without disturbing the surface", 1);

	/* --- THE RENDERING OPTIONS: A SETTER IS ONLY REAL IF IT REACHES THE CONTEXT --------- */
	/* THESE ARE NOT ROUND-TRIP CHECKS. Reading back what was just written would pass for a setter
	 * that stored and did nothing, so each one below is measured on the SURFACE instead — which is
	 * the only way to tell "the context was told" from "the object remembers". */
	{
		CGContextRef cc = bitmap();
		NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithCGContext:cc flipped:YES];
		int aa_on;
		int aa_off;

		check("a fresh context reports antialiasing ON, the context's default", [g shouldAntialias]);
		check("...and SourceOver, the default blend mode",
		      [g compositingOperation] == NSCompositingOperationSourceOver);
		check("...and a pattern phase of (0,0)",
		      [g patternPhase].x == 0.0 && [g patternPhase].y == 0.0);

		/* A RECT 3.5 WIDE COVERS HALF OF DEVICE COLUMN 3: with antialiasing on that half-coverage
		 * is a grey; with it off the column is a hard edge. Both halves are asserted, so a setter
		 * that did nothing cannot pass by leaving one of them unchanged. */
		CGContextSetRGBFillColor(cc, 1.0, 1.0, 1.0, 1.0);
		[g setShouldAntialias:YES];
		CGContextClearRect(cc, CGRectMake(0.0, 0.0, 8.0, 8.0));
		CGContextFillRect(cc, CGRectMake(0.0, 0.0, 3.5, 8.0));
		pixel(cc, 3, 4, p);
		aa_on = p[2];
		[g setShouldAntialias:NO];
		CGContextClearRect(cc, CGRectMake(0.0, 0.0, 8.0, 8.0));
		CGContextFillRect(cc, CGRectMake(0.0, 0.0, 3.5, 8.0));
		pixel(cc, 3, 4, p);
		aa_off = p[2];
		check("setShouldAntialias REACHES the context: the half-covered edge pixel is a coverage "
		      "value (1..254) when ON and a hard edge (0 or 255) when OFF",
		      aa_on > 1 && aa_on < 254 && (aa_off == 0 || aa_off == 255));

		/* WHITE MULTIPLIED BY MID-GREY STAYS MID-GREY; white composited OVER it would be white. */
		CGContextClearRect(cc, CGRectMake(0.0, 0.0, 8.0, 8.0));
		CGContextSetRGBFillColor(cc, 0.5, 0.5, 0.5, 1.0);
		CGContextFillRect(cc, CGRectMake(0.0, 0.0, 8.0, 8.0));
		[g setCompositingOperation:NSCompositingOperationMultiply];
		CGContextSetRGBFillColor(cc, 1.0, 1.0, 1.0, 1.0);
		CGContextFillRect(cc, CGRectMake(0.0, 0.0, 8.0, 8.0));
		pixel(cc, 4, 4, p);
		check("setCompositingOperation REACHES the context: white multiplied into mid-grey lands "
		      "at ~128, where a source-over would land at 255",
		      p[2] >= 124 && p[2] <= 132);

		/* AND THE ONE CASE WITH NO OPERATOR IS REFUSED RATHER THAN APPROXIMATED. */
		[g setCompositingOperation:NSCompositingOperationSourceOver];
		[g setCompositingOperation:NSCompositingOperationPlusDarker];
		check("...and PlusDarker, which pixman cannot compute, is REFUSED, leaving the operation "
		      "where it was", [g compositingOperation] == NSCompositingOperationSourceOver);

		/* THE PHASE ROUND-TRIPS, AND ITS OTHER HALF IS NAMED RATHER THAN GLOSSED: what a phase DOES
		 * is only visible through a pattern fill, which is the CG pattern probe's territory (its
		 * own header says the phase's direction is pinned there). This check is therefore the
		 * property, not the effect, and it says which one it is. */
		[g setPatternPhase:NSMakePoint(3.0, 1.0)];
		check("setPatternPhase round-trips through the object (its EFFECT needs a pattern fill, "
		      "which coregraphics_pattern.c pins)",
		      [g patternPhase].x == 3.0 && [g patternPhase].y == 1.0);

		/* --- AND THE ONE OPTION THAT READS THROUGH TO THE CONTEXT -------------------------- */
		/* THE DIFFERENCE IS OBSERVABLE ONLY WHEN THE CONTEXT REFUSES SOMETHING, which is why that
		 * is the check that matters: a property that STORED what it was given would report the
		 * rejected level back, and this one reports what is actually in force. */
		check("a fresh context reports interpolation None, which is THIS library's stated default "
		      "(CoreGraphics' is None because a zeroed state means nearest)",
		      [g imageInterpolation] == NSImageInterpolationNone);

		[g setImageInterpolation:NSImageInterpolationMedium];
		check("setImageInterpolation:Medium reads back as Medium",
		      [g imageInterpolation] == NSImageInterpolationMedium);
		/* THE PROPERTY REACHED THE CONTEXT, checked at the context rather than on the surface: the
		 * SURFACE half of interpolation (nearest gives 0/255, bilinear gives 95/159 on a 2x1 strip)
		 * is coregraphics_image.c's, and duplicating it here would test CoreGraphics twice and this
		 * bridge once. */
		check("...and it REACHED the context rather than only being remembered",
		      CGContextGetInterpolationQuality(cc) == kCGInterpolationMedium);

		[g setImageInterpolation:NSImageInterpolationLow];
		check("setImageInterpolation:Low is REFUSED by CoreGraphics, and the property reports the "
		      "quality ACTUALLY IN FORCE - which a storing property could not do",
		      [g imageInterpolation] == NSImageInterpolationMedium);
		check("...and the context is untouched by the refusal",
		      CGContextGetInterpolationQuality(cc) == kCGInterpolationMedium);
		[g setImageInterpolation:NSImageInterpolationHigh];
		check("...and the same holds for High",
		      [g imageInterpolation] == NSImageInterpolationMedium);

		CGContextRelease(cc);
	}

	/* --- a context-less object starts from the DEFAULTS, not from the zeroes alloc leaves --- */
	{
		NSGraphicsContext *none = [[NSGraphicsContext alloc] init];

		check("a context-less object still reports antialiasing ON", [none shouldAntialias]);
		check("...and SourceOver rather than the Clear that enum value 0 would give",
		      [none compositingOperation] == NSCompositingOperationSourceOver);
		[none setShouldAntialias:NO];
		[none setCompositingOperation:NSCompositingOperationMultiply];
		[none setPatternPhase:NSMakePoint(1.0, 1.0)];
		check("...and all three setters are harmless with no context", 1);
		check("...and a context-less object reports interpolation None rather than a stored fallback",
		      [none imageInterpolation] == NSImageInterpolationNone);
		[none setImageInterpolation:NSImageInterpolationMedium];
		check("...and setting it is harmless too", 1);
		[none release];
	}

	[NSGraphicsContext setCurrentContext:nil];
	check("+setCurrentContext:nil clears the slot", [NSGraphicsContext currentContext] == nil);

	CGContextRelease(cgA);
	CGContextRelease(cgB);
	printf("APPKIT-GC: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
