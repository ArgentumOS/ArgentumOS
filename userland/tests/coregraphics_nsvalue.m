/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * coregraphics_nsvalue — the `CG`-spelled NSValue geometry doors, IN THE TIER THAT OWNS THEM (§63.53).
 *
 * THEY MOVED HERE FROM FOUNDATION'S `NSValue`, and this probe moved WITH them: two of the five checks were
 * Foundation's (`foundation_nsvalue`), and a Foundation probe **cannot link CoreGraphics** — the same wall
 * §63.52 hit with AppKit. So the positive half lives here, where the library is linked, and the Foundation
 * probe keeps the NEGATIVE half (that these selectors are NOT on its `NSValue`), which is the half its own tier
 * can hold and the half that catches a regression.
 *
 * ⚠ AND THIS PROBE IS HOST-ONLY, WHICH IS STATED RATHER THAN DISCOVERED: this tree has no guest case for the
 * CoreGraphics probes (there is no `tests/cases/coregraphics_*.py`), so what is measured here is the LIBRARY's
 * behaviour on the host, not the guest. The guest's half of this unit is the negative check.
 *
 * THE VALUES ARE BUILT WITH BRACE INITIALIZERS rather than `CGPointMake` &c, which is a measured rule from the
 * old home: the constructors are symbols in the library this probe links, so calling them is fine HERE — but a
 * brace-initializer builds the same struct field for field, and it is the same code in both places.
 */

#import <Foundation/Foundation.h>
#import <CoreGraphics/NSValueCGGeometry.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, int ok)
{
	if (!ok) {
		failures++;
	}
	printf("CG-NSVALUE %-56s %s\n", name, ok ? "ok" : "FAIL");
}

int main(void)
{
	CGPoint point = { 1.5, -2.5 };
	CGSize size = { 6.0, 7.5 };
	CGRect rect = { { 1.0, 2.0 }, { 3.0, 4.0 } };
	CGAffineTransform transform = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
	NSValue *boxedPoint = [NSValue valueWithCGPoint:point];
	NSValue *boxedSize = [NSValue valueWithCGSize:size];
	NSValue *boxedRect = [NSValue valueWithCGRect:rect];
	NSValue *boxedTransform = [NSValue valueWithCGAffineTransform:transform];
	CGPoint backPoint = [boxedPoint CGPointValue];
	CGSize backSize = [boxedSize CGSizeValue];
	CGRect backRect = [boxedRect CGRectValue];
	CGAffineTransform backTransform = [boxedTransform CGAffineTransformValue];

	/* EACH IS CHECKED ON ITS OWN so a failure names the struct, and each reader's answer is compared with the
	 * ENCODING the box carries — the two together are the contract, since a box is bytes plus a type. */
	check("cg-value-with-point", backPoint.x == 1.5 && backPoint.y == -2.5 &&
				     strcmp([boxedPoint objCType], @encode(CGPoint)) == 0);
	check("cg-value-with-size", backSize.width == 6.0 && backSize.height == 7.5 &&
				    strcmp([boxedSize objCType], @encode(CGSize)) == 0);
	check("cg-value-with-rect", backRect.origin.x == 1.0 && backRect.origin.y == 2.0 &&
				    backRect.size.width == 3.0 && backRect.size.height == 4.0 &&
				    strcmp([boxedRect objCType], @encode(CGRect)) == 0);
	check("cg-value-with-affine-transform", backTransform.a == 1.0 && backTransform.b == 2.0 &&
						backTransform.c == 3.0 && backTransform.d == 4.0 &&
						backTransform.tx == 5.0 && backTransform.ty == 6.0 &&
						strcmp([boxedTransform objCType],
						       @encode(CGAffineTransform)) == 0);

	/* AND THE TIER BOUNDARY IS A FACT THE PROBE CAN STATE FROM ITS OWN SIDE: `NSPoint` and `CGPoint` are ONE
	 * type here (NSGeometry.h aliases them), so a CG-spelled box and an NS-spelled one carry the SAME encoding
	 * and compare equal — which is why the two spellings could be told apart only by READING THE SDK HEADERS
	 * and not by observing the values. */
	check("the-two-spellings-are-one-type", [boxedPoint isEqualToValue:
						 [NSValue valueWithPoint:NSMakePoint(1.5, -2.5)]]);

	printf("CG-NSVALUE: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
