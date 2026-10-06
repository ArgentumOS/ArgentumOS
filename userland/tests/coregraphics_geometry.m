/*
 * coregraphics_geometry — the dictionary representations, and the third comparison.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ROUND TRIP IS THE SUPPORTED CONTRACT AND THE KEY NAMES ARE THE THING THAT MAKES IT USEFUL BEYOND THIS
 * LIBRARY, so both are checked: every `Create` form's dictionary is read back with the `Make` form, AND the
 * dictionary's own keys are inspected — a representation this library wrote has to be readable by a caller who
 * knows the spelling, and a caller's dictionary has to be readable here.
 *
 * THE STRUCT IS WHAT IS STORED, NOT A NORMALISED SHAPE: a rectangle with negative width and height comes back
 * with them negative, because `CGRectStandardize` is a different function and a caller who stored that rectangle
 * gets that rectangle back.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGGeometry.h>

#include <stdio.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-GEOMETRY %-62s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static double num(NSDictionary *d, NSString *key)
{
	id v = [d objectForKey:key];

	return (v != nil && [v isKindOfClass:[NSNumber class]]) ? [v doubleValue] : -99999.0;
}

int main(void)
{
	/* --- a point ---------------------------------------------------------------------------- */
	{
		NSDictionary *d = CGPointCreateDictionaryRepresentation(CGPointMake(5, 7));
		CGPoint back = CGPointMake(-1, -1);

		check("a point's representation is made, and owns a reference (release is the caller's)", d != nil);
		check("...and its keys are X and Y, with the point's numbers", [d count] == 2
		      && num(d, @"X") == 5.0 && num(d, @"Y") == 7.0);
		check("...and the Make form reads it back", CGPointMakeWithDictionaryRepresentation(d, &back)
		      && back.x == 5.0 && back.y == 7.0);
		[d release];
	}

	/* --- a size, and its own two keys --------------------------------------------------------- */
	{
		NSDictionary *d = CGSizeCreateDictionaryRepresentation(CGSizeMake(3.5, 4.25));
		CGSize back = CGSizeMake(-1, -1);

		check("a size's keys are Width and Height", d != nil && [d count] == 2
		      && num(d, @"Width") == 3.5 && num(d, @"Height") == 4.25);
		check("...and it reads back", CGSizeMakeWithDictionaryRepresentation(d, &back)
		      && back.width == 3.5 && back.height == 4.25);
		[d release];
	}

	/* --- a rectangle, four keys, and the struct as stored ------------------------------------- */
	{
		CGRect rect = CGRectMake(1, 2, -30, 40);	/* negative width: NOT normalised on the way out */
		NSDictionary *d = CGRectCreateDictionaryRepresentation(rect);
		CGRect back = CGRectMake(-1, -1, -1, -1);

		check("a rectangle's keys are X, Y, Width and Height", d != nil && [d count] == 4
		      && num(d, @"X") == 1.0 && num(d, @"Y") == 2.0 && num(d, @"Width") == -30.0
		      && num(d, @"Height") == 40.0);
		check("...and it reads back EXACTLY as stored, negative width and all",
		      CGRectMakeWithDictionaryRepresentation(d, &back)
		      && CGRectEqualToRect(back, rect));
		[d release];
	}

	/* --- and what is NOT a representation ---------------------------------------------------- */
	{
		CGPoint out = CGPointMake(11, 22);
		NSDictionary *partial = [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:1]
								    forKey:@"X"];
		NSDictionary *wrong_type = [NSDictionary dictionaryWithObjectsAndKeys:
						@"five", @"X", [NSNumber numberWithInt:7], @"Y", nil];

		check("an empty dictionary is not a point", !CGPointMakeWithDictionaryRepresentation(
			[NSDictionary dictionary], &out));
		check("...and a HALF dictionary is not one either", !CGPointMakeWithDictionaryRepresentation(
			partial, &out));
		check("...and a value that is not a number is not one", !CGPointMakeWithDictionaryRepresentation(
			wrong_type, &out));
		check("AND A REFUSED READ LEAVES THE OUTPUT STRUCT UNTOUCHED", out.x == 11 && out.y == 22);
		check("...as does a nil dictionary, which is not a crash",
		      !CGPointMakeWithDictionaryRepresentation(nil, &out));
		check("...and a NULL output, likewise",
		      !CGSizeMakeWithDictionaryRepresentation(
			[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:1] forKey:@"Width"], NULL));
		check("...and the rectangle form refuses the same way",
		      !CGRectMakeWithDictionaryRepresentation(partial, NULL));
	}

	/* --- the third comparison ----------------------------------------------------------------- */
	{
		CGRect a = CGRectMake(1, 2, 3, 4);

		check("a rectangle equals itself", CGRectEqualToRect(a, a));
		check("...and differs when its ORIGIN differs",
		      !CGRectEqualToRect(a, CGRectMake(9, 2, 3, 4)));
		check("...and when its SIZE differs", !CGRectEqualToRect(a, CGRectMake(1, 2, 3, 9)));
	}

	printf("CG-GEOMETRY: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
