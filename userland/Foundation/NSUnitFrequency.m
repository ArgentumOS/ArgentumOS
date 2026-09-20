/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitFrequency.m — eight powers of ten and one alias. docs/design/foundation-plan.md §12.3 W12.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitFrequency.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_UF_TERAHERTZ = 0, FN_UF_GIGAHERTZ, FN_UF_MEGAHERTZ, FN_UF_KILOHERTZ, FN_UF_HERTZ,
	FN_UF_MILLIHERTZ, FN_UF_MICROHERTZ, FN_UF_NANOHERTZ, FN_UF_FRAMESPERSECOND, FN_UF_COUNT
} fn_uf_index;

static const struct {
	const char *symbol;
	double hertz;
} fn_uf_table[FN_UF_COUNT] = {
	{ "THz",	1e12 },
	{ "GHz",	1e9 },
	{ "MHz",	1e6 },
	{ "kHz",	1e3 },
	{ "Hz",		1.0 },
	{ "mHz",	1e-3 },
	{ "µHz",	1e-6 },
	{ "nHz",	1e-9 },
	{ "fps",	1.0 }		/* FRAMES PER SECOND IS THE HERTZ, under the name a display uses */
};

static NSUnitFrequency *fn_uf_cache[FN_UF_COUNT];

static NSUnitFrequency *fn_uf_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitFrequency *unit;

	if (fn_uf_cache[index] != nil) {
		return fn_uf_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_uf_table[index].hertz];
	unit = [[NSUnitFrequency alloc] initWithSymbol:[NSString stringWithUTF8String:fn_uf_table[index].symbol]
					      converter:converter];
	[converter release];
	fn_uf_cache[index] = unit;
	return fn_uf_cache[index];
}

@implementation NSUnitFrequency

+ (NSUnit *)baseUnit { return fn_uf_unit(FN_UF_HERTZ); }

+ (NSUnitFrequency *)terahertz { return fn_uf_unit(FN_UF_TERAHERTZ); }
+ (NSUnitFrequency *)gigahertz { return fn_uf_unit(FN_UF_GIGAHERTZ); }
+ (NSUnitFrequency *)megahertz { return fn_uf_unit(FN_UF_MEGAHERTZ); }
+ (NSUnitFrequency *)kilohertz { return fn_uf_unit(FN_UF_KILOHERTZ); }
+ (NSUnitFrequency *)hertz { return fn_uf_unit(FN_UF_HERTZ); }
+ (NSUnitFrequency *)millihertz { return fn_uf_unit(FN_UF_MILLIHERTZ); }
+ (NSUnitFrequency *)microhertz { return fn_uf_unit(FN_UF_MICROHERTZ); }
+ (NSUnitFrequency *)nanohertz { return fn_uf_unit(FN_UF_NANOHERTZ); }
+ (NSUnitFrequency *)framesPerSecond { return fn_uf_unit(FN_UF_FRAMESPERSECOND); }

@end
