/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_nsvalue, unit of 1 — F13.8c's acceptance for NSValue and NSNull.
 * docs/design/foundation-plan.md §10.
 *
 * THE NAME IS `nsvalue` AND NOT `value` because `foundation_value` ALREADY EXISTS: it is F2/F8's
 * probe for NSNumber, NSData and NSDate — "values" in the older sense — and it is tracked, with a
 * header and a support unit. A new probe's name has to be checked against the suite before it is
 * written, which is cheap here and would not have been cheap in the tree.
 *
 * ONE unit, like the set probe: the claim is not a cross-translation-unit boundary but what a BOX
 * does with bytes, and it imports only <Foundation/Foundation.h> (which also proves the umbrella
 * exports both headers).
 *
 * THE MEASUREMENT THAT MATTERS IS `value-copies-exactly-its-size`, and it is a CANARY rather than a
 * field comparison: the source buffer is 64 bytes with a real structure at the front and 0xAA after
 * it, the destination is 64 bytes of 0x55, and the assertion is that the structure arrives AND the
 * bytes past it are still 0x55. A box that copied too much would smear 0xAA; one that copied too
 * little would leave 0x55 inside the structure. A plain round trip cannot tell either apart,
 * because `-getValue:` copies back whatever length it was told.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>

/* A structure with PADDING, which is the point: {double,int} is 16 bytes and not 12, so a size
 * derived by adding field widths would be wrong by four. */
struct FNVMeasure {
	double amount;
	int tag;
};

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NSVALUE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NSVALUE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		struct FNVMeasure mine;
		struct FNVMeasure back;
		struct FNVMeasure different;
		NSValue *boxed;
		NSValue *other;

		memset(&mine, 0, sizeof(mine));
		mine.amount = 2.5;
		mine.tag = 41;
		boxed = [NSValue valueWithBytes:&mine objCType:@encode(struct FNVMeasure)];
		other = [NSValue valueWithBytes:&mine objCType:@encode(struct FNVMeasure)];
		memset(&back, 0, sizeof(back));
		if (boxed != nil) {
			[boxed getValue:&back];
		}
		different = mine;
		different.tag = 42;
		check("value-bytes-roundtrip",
		      boxed != nil && back.amount == 2.5 && back.tag == 41 &&
		      strcmp([boxed objCType], @encode(struct FNVMeasure)) == 0 &&
		      [boxed isEqualToValue:other] && [boxed isEqual:other] &&
		      [boxed hash] == [other hash] &&
		      ![boxed isEqualToValue:[NSValue valueWithBytes:&different
							    objCType:@encode(struct FNVMeasure)]],
		      [NSString stringWithFormat:@"amount=%g tag=%d equal=%d",
			back.amount, back.tag, (int)[boxed isEqualToValue:other]]);
	}

	{
		/* THE CANARY. Source: the structure, then 0xAA. Destination: 0x55 throughout. */
		unsigned char source[64];
		unsigned char destination[64];
		struct FNVMeasure mine;
		NSUInteger i;
		BOOL tail_untouched = YES;
		BOOL head_arrived;

		mine.amount = 1.25;
		mine.tag = 7;
		memset(source, 0xAA, sizeof(source));
		memcpy(source, &mine, sizeof(mine));
		memset(destination, 0x55, sizeof(destination));
		{
			NSValue *boxed = [NSValue valueWithBytes:source
							objCType:@encode(struct FNVMeasure)];

			[boxed getValue:destination];
		}
		head_arrived = memcmp(destination, &mine, sizeof(mine)) == 0;
		for (i = sizeof(mine); i < sizeof(destination); i++) {
			if (destination[i] != 0x55) {
				tail_untouched = NO;
				break;
			}
		}
		check("value-copies-exactly-its-size",
		      head_arrived && tail_untouched && sizeof(mine) == 16,
		      [NSString stringWithFormat:@"head=%d tail=%d sizeof=%lu firstTail=0x%02x",
			(int)head_arrived, (int)tail_untouched, (unsigned long)sizeof(mine),
			(unsigned)destination[sizeof(mine)]]);
	}

	{
		int local = 0;
		NSValue *boxed = [NSValue valueWithPointer:&local];

		check("value-pointer",
		      boxed != nil && [boxed pointerValue] == &local,
		      [NSString stringWithFormat:@"ptr=%p want=%p",
			[boxed pointerValue], (void *)&local]);
	}

	{
		NSRange range = NSMakeRange(2, 5);
		NSValue *boxed = [NSValue valueWithRange:range];
		NSRange back = NSMakeRange(0, 0);

		if (boxed != nil) {
			back = [boxed rangeValue];
		}
		check("value-range",
		      boxed != nil && back.location == 2 && back.length == 5 &&
		      [[boxed description] isEqualToString:@"NSRange: {2, 5}"],
		      [NSString stringWithFormat:@"got={%lu, %lu} text=%@",
			(unsigned long)back.location, (unsigned long)back.length,
			boxed != nil ? [boxed description] : @"?"]);
	}

	{
		/* THE FOUNDATION-GEOMETRY BOXES. Each is boxed by its own door and read back by its own reader,
		 * over the private payload primitive — and the POINT is also compared against a box built with the
		 * CG spelling, because @encode(NSPoint) and @encode(CGPoint) are the SAME string (NSGeometry.h
		 * typedefs one to the other): that identity is what lets these doors exist at all, so it is
		 * asserted rather than assumed. */
		NSPoint point = NSMakePoint(1.5, -2.25);
		NSSize size = NSMakeSize(3.0, 4.5);
		NSRect rect = NSMakeRect(1.0, 2.0, 3.0, 4.0);
		NSValue *boxedPoint = [NSValue valueWithPoint:point];
		NSValue *boxedSize = [NSValue valueWithSize:size];
		NSValue *boxedRect = [NSValue valueWithRect:rect];
		NSValue *viaCG = [NSValue valueWithBytes:&point objCType:@encode(CGPoint)];
		NSPoint pointBack = NSMakePoint(0, 0);
		NSSize sizeBack = NSMakeSize(0, 0);
		NSRect rectBack = NSMakeRect(0, 0, 0, 0);

		if (boxedPoint != nil) {
			pointBack = [boxedPoint pointValue];
		}
		if (boxedSize != nil) {
			sizeBack = [boxedSize sizeValue];
		}
		if (boxedRect != nil) {
			rectBack = [boxedRect rectValue];
		}

		check("value-point",
		      boxedPoint != nil && pointBack.x == 1.5 && pointBack.y == -2.25 &&
		      strcmp([boxedPoint objCType], @encode(NSPoint)) == 0 &&
		      strcmp([boxedPoint objCType], @encode(CGPoint)) == 0 &&
		      [boxedPoint isEqualToValue:viaCG],
		      [NSString stringWithFormat:@"got={%g, %g} enc=%s",
			pointBack.x, pointBack.y, [boxedPoint objCType]]);

		check("value-size",
		      boxedSize != nil && sizeBack.width == 3.0 && sizeBack.height == 4.5 &&
		      strcmp([boxedSize objCType], @encode(NSSize)) == 0,
		      [NSString stringWithFormat:@"got={%g, %g} enc=%s",
			sizeBack.width, sizeBack.height, [boxedSize objCType]]);

		check("value-rect",
		      boxedRect != nil && rectBack.origin.x == 1.0 && rectBack.origin.y == 2.0 &&
		      rectBack.size.width == 3.0 && rectBack.size.height == 4.0 &&
		      strcmp([boxedRect objCType], @encode(NSRect)) == 0 &&
		      [[boxedRect description] length] > 0,
		      [NSString stringWithFormat:@"got={{%g, %g}, {%g, %g}} enc=%s",
			rectBack.origin.x, rectBack.origin.y,
			rectBack.size.width, rectBack.size.height, [boxedRect objCType]]);
	}

	{
		/* ⚠ THE `CG`-SPELLED BOXES ARE NOT FOUNDATION'S ANY MORE (§63.53). They moved to the COREGRAPHICS tier
		 * (`userland/CoreGraphics/NSValueCGGeometry.{h,m}`), because the measurement that settles the two
		 * spellings of one idea is: `+valueWithCGPoint:` and `-CGPointValue` appear in **NO Foundation header
		 * of either SDK**, while `+valueWithPoint:`/`-pointValue` DO (macOS's `NSGeometry.h`) — and the
		 * `NS`-spelled pair is still asserted just above.
		 *
		 * THEIR POSITIVE CHECKS MOVED WITH THEM, to `coregraphics_nsvalue`, a CoreGraphics probe which can link
		 * the tier that owns them. WHAT THIS PROBE HOLDS IS THE OTHER HALF, and it is the half that catches a
		 * regression: `NSSelectorFromString` rather than `@selector(…)` because NO DECLARATION IS IN SCOPE
		 * HERE, which is exactly the fact being asserted. */
		/* ⚠ THIS CHECK USED TO ASSERT THE OPPOSITE — that Foundation's NSValue does NOT respond to the five
		 * CG-spelled boxes and readers — AND THAT ASSERTION CANNOT HOLD IN ANY PROCESS THAT LINKS
		 * COREGRAPHICS, which is every probe: the doors are a CATEGORY, `NSValue (NSValueCGGeometryAdditions)`
		 * in userland/CoreGraphics/NSValueCGGeometry.{h,m}, so the runtime answers YES whichever header
		 * declared them. The header-level claim ("no Foundation header of either SDK declares these") is a
		 * SOURCE fact and belongs to the SWEEP, which judges declaration and owner; what a probe can observe
		 * is the boundary as it IS — the CG-spelled tier answering, beside Foundation's own NS-spelled pair
		 * asserted above. It was invisible until the host tier could run at all, which is how it was found. */
		check("cg-spelled-geometry-doors-are-the-coregraphics-tier-s",
		      [NSValue respondsToSelector:NSSelectorFromString(@"valueWithCGPoint:")] &&
		      [NSValue respondsToSelector:NSSelectorFromString(@"valueWithCGSize:")] &&
		      [NSValue respondsToSelector:NSSelectorFromString(@"valueWithCGRect:")] &&
		      [NSValue respondsToSelector:NSSelectorFromString(@"valueWithCGVector:")] &&
		      [NSValue respondsToSelector:NSSelectorFromString(@"valueWithCGAffineTransform:")] &&
		      [NSValue instancesRespondToSelector:NSSelectorFromString(@"CGPointValue")] &&
		      [NSValue instancesRespondToSelector:NSSelectorFromString(@"CGSizeValue")] &&
		      [NSValue instancesRespondToSelector:NSSelectorFromString(@"CGRectValue")] &&
		      [NSValue instancesRespondToSelector:NSSelectorFromString(@"CGVectorValue")] &&
		      [NSValue instancesRespondToSelector:NSSelectorFromString(@"CGAffineTransformValue")],
		      [NSString stringWithFormat:@"the five CG-spelled boxes and readers come from CoreGraphics' "
						@"NSValue category, so they answer here; Foundation's NS-spelled pair is asserted "
						@"above and its headers declare none of these (the sweep holds that half)"]);
	}

	{
		/* THE EDGE-INSETS BOX over `NSEdgeInsets` — the type this tree ships, and the one Apple's
		 * documentation names for THIS selector (UIKit's is the differently-named +valueWithUIEdgeInsets:).
		 * ⚠ The encoding asserted is @encode(NSEdgeInsets) as this tree SPELLS it; the LAYOUT matches
		 * Apple's but the struct TAG does not (see NSValue.h), so an Apple-built value would report a
		 * different -objCType string than this one. */
		NSEdgeInsets insets = NSEdgeInsetsMake(1.0, 2.0, 3.0, 4.0);
		NSValue *boxedInsets = [NSValue valueWithEdgeInsets:insets];
		NSEdgeInsets insetsBack = NSEdgeInsetsMake(0, 0, 0, 0);

		if (boxedInsets != nil) {
			insetsBack = [boxedInsets edgeInsetsValue];
		}
		check("value-edge-insets",
		      boxedInsets != nil && insetsBack.top == 1.0 && insetsBack.left == 2.0 &&
		      insetsBack.bottom == 3.0 && insetsBack.right == 4.0 &&
		      strcmp([boxedInsets objCType], @encode(NSEdgeInsets)) == 0,
		      [NSString stringWithFormat:@"got={%g,%g,%g,%g} enc=%s",
			insetsBack.top, insetsBack.left, insetsBack.bottom, insetsBack.right,
			[boxedInsets objCType]]);
	}

	{
		/* THE NON-RETAINED OBJECT. The box stores the ADDRESS only (Apple documents this as equivalent to
		 * `+value:withObjCType:` over @encode(void *)), so -objCType answers "^v", -pointerValue hands back
		 * the same address, and -nonretainedObjectValue returns the SAME object — borrowed, never owned. */
		NSString *borrowed = [NSString stringWithFormat:@"borrowed-%d", 7];
		NSValue *boxedBorrowed = [NSValue valueWithNonretainedObject:borrowed];
		id back = [boxedBorrowed nonretainedObjectValue];
		BOOL same = (back == borrowed);

		check("value-nonretained-object",
		      boxedBorrowed != nil && same && [back isEqualToString:borrowed] &&
		      strcmp([boxedBorrowed objCType], @encode(void *)) == 0 &&
		      [boxedBorrowed pointerValue] == (__bridge void *)borrowed,
		      [NSString stringWithFormat:@"same=%d enc=%s text=%@",
			(int)same, [boxedBorrowed objCType], back]);
	}

	{
		/* THE INSTANCE SPELLING of the raw-bytes door, beside its class twin: the two must agree. */
		int number = 123456;
		int numberBack = 0;
		NSValue *boxedInt = [[NSValue alloc] initWithBytes:&number objCType:@encode(int)];
		NSValue *sameInt = [NSValue valueWithBytes:&number objCType:@encode(int)];

		if (boxedInt != nil) {
			[boxedInt getValue:&numberBack];
		}
		check("value-init-with-bytes",
		      boxedInt != nil && numberBack == 123456 &&
		      strcmp([boxedInt objCType], @encode(int)) == 0 &&
		      [boxedInt isEqualToValue:sameInt],
		      [NSString stringWithFormat:@"got=%d enc=%s", numberBack, [boxedInt objCType]]);
	}

	{
		/* A BOX IS COMPARED BY ITS BYTES, so a CONTAINER built on -hash/-isEqual: must collapse two
		 * equal boxes — which is exactly what NSSet does. */
		int ten = 10;
		int alsoTen = 10;
		const id boxes[2] = { [NSValue valueWithBytes:&ten objCType:@encode(int)],
				      [NSValue valueWithBytes:&alsoTen objCType:@encode(int)] };
		id one = boxes[0];
		id two = boxes[1];
		NSSet *set = [NSSet setWithObjects:boxes count:2];

		check("value-in-a-container",
		      one != two && [one isEqualToValue:two] && set != nil &&
		      [set count] == 1 && [set containsObject:two] && [one copy] == one,
		      [NSString stringWithFormat:@"count=%lu copyIsSelf=%d",
			(unsigned long)(set != nil ? [set count] : 0), (int)([one copy] == one)]);
	}

	{
		NSNull *one = [NSNull null];
		NSNull *two = [NSNull null];
		NSNull *made = [[NSNull alloc] init];

		check("null-is-one-object",
		      one == two && made == one && [one isEqual:two] && [one hash] == [two hash],
		      [NSString stringWithFormat:@"same=%d allocIsNull=%d",
			(int)(one == two), (int)(made == one)]);
	}

	{
		id placeholder = [NSNull null];
		NSArray *array = @[@"a", placeholder, @"c"];
		NSDictionary *dictionary = @{ placeholder : @"the hole" };

		check("null-holds-a-place-in-a-collection",
		      array != nil && [array count] == 3 &&
		      [[array objectAtIndex:1] isEqual:placeholder] &&
		      [[array objectAtIndex:1] isEqual:[NSNull null]] &&
		      dictionary != nil && [dictionary count] == 1 &&
		      [[dictionary objectForKey:[NSNull null]] isEqualToString:@"the hole"] &&
		      ![placeholder isEqual:@"a"] &&
		      [[placeholder description] isEqualToString:@"<null>"],
		      [NSString stringWithFormat:@"array=%lu dict=%lu text=%@",
			(unsigned long)(array != nil ? [array count] : 0),
			(unsigned long)(dictionary != nil ? [dictionary count] : 0),
			[placeholder description]]);
	}

	printf("FOUNDATION-NSVALUE RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-NSVALUE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-NSVALUE DONE\n");
	return failc ? 1 : 0;
}
