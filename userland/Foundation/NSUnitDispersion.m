/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitDispersion.m — parts per million, and nothing to convert.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitDispersion.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

static NSUnitDispersion *fn_udp_unit = nil;

@implementation NSUnitDispersion

+ (NSUnit *)baseUnit
{
	return [self partsPerMillion];
}

+ (NSUnitDispersion *)partsPerMillion
{
	if (fn_udp_unit == nil) {
		NSUnitConverterLinear *converter = [[NSUnitConverterLinear alloc] initWithCoefficient:1.0];

		fn_udp_unit = [[NSUnitDispersion alloc] initWithSymbol:@"ppm" converter:converter];
		[converter release];
	}
	return fn_udp_unit;
}

@end
