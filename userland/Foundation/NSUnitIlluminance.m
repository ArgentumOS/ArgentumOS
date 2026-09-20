/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitIlluminance.m — the lux, and nothing to convert. docs/design/foundation-plan.md §12.3 W12.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitIlluminance.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

static NSUnitIlluminance *fn_uil_unit = nil;

@implementation NSUnitIlluminance

+ (NSUnit *)baseUnit
{
	return [self lux];
}

+ (NSUnitIlluminance *)lux
{
	if (fn_uil_unit == nil) {
		NSUnitConverterLinear *converter = [[NSUnitConverterLinear alloc] initWithCoefficient:1.0];

		fn_uil_unit = [[NSUnitIlluminance alloc] initWithSymbol:@"lx" converter:converter];
		[converter release];
	}
	return fn_uil_unit;
}

@end
