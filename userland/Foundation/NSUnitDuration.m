/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitDuration.m — seven units, every coefficient an exact power of ten.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is). The units are built lazily and kept, for the identity
 * reason NSUnitInformationStorage's comment gives.
 */

#import <Foundation/NSUnitDuration.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

typedef enum {
	FN_UD_HOURS = 0, FN_UD_MINUTES, FN_UD_SECONDS, FN_UD_MILLISECONDS,
	FN_UD_MICROSECONDS, FN_UD_NANOSECONDS, FN_UD_PICOSECONDS, FN_UD_COUNT
} fn_ud_index;

static const struct {
	const char *symbol;
	double seconds;		/* seconds per unit — the base unit is the second */
} fn_ud_table[FN_UD_COUNT] = {
	{ "hr",	3600.0 },
	{ "min", 60.0 },
	{ "s",	1.0 },
	{ "ms",	1e-3 },
	{ "µs",	1e-6 },
	{ "ns",	1e-9 },
	{ "ps",	1e-12 }
};

static NSUnitDuration *fn_ud_cache[FN_UD_COUNT];

static NSUnitDuration *fn_ud_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitDuration *unit;

	if (fn_ud_cache[index] != nil) {
		return fn_ud_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ud_table[index].seconds];
	unit = [[NSUnitDuration alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ud_table[index].symbol]
					    converter:converter];
	[converter release];
	fn_ud_cache[index] = unit;
	return fn_ud_cache[index];
}

@implementation NSUnitDuration

+ (NSUnit *)baseUnit { return fn_ud_unit(FN_UD_SECONDS); }
+ (NSUnitDuration *)hours { return fn_ud_unit(FN_UD_HOURS); }
+ (NSUnitDuration *)minutes { return fn_ud_unit(FN_UD_MINUTES); }
+ (NSUnitDuration *)seconds { return fn_ud_unit(FN_UD_SECONDS); }
+ (NSUnitDuration *)milliseconds { return fn_ud_unit(FN_UD_MILLISECONDS); }
+ (NSUnitDuration *)microseconds { return fn_ud_unit(FN_UD_MICROSECONDS); }
+ (NSUnitDuration *)nanoseconds { return fn_ud_unit(FN_UD_NANOSECONDS); }
+ (NSUnitDuration *)picoseconds { return fn_ud_unit(FN_UD_PICOSECONDS); }

@end
