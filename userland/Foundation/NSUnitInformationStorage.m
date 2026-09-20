/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitInformationStorage.m — the table, and the units built from it.
 * docs/design/foundation-plan.md §12.3 W12 (first slice).
 *
 * ONE TABLE AND THIRTY-FIVE ONE-LINERS. The whole family is (symbol, multiplier, scale) and the
 * coefficient is `multiplier * scale` — with `scale` being 8.0 for the byte-shaped units, because a byte
 * IS eight bits and the base unit is bits (see the header for why that choice is arithmetic rather than
 * taste). Writing the byte units' coefficients out by hand would mean computing 2^73 and 2^83 as literals;
 * this computes them.
 *
 * THE UNITS ARE BUILT LAZILY AND KEPT FOREVER, and that is load-bearing rather than an optimisation:
 * NSUnit's equality is IDENTITY (its header says so), so `[NSUnitInformationStorage bytes]` must answer
 * the SAME OBJECT every time or a conversion from a measurement built from one to a measurement built
 * from the other would be a conversion between different units. The cache is a fixed array of pointers in
 * this file — no allocation per call, nothing to release, and no lock because the values are immutable
 * once built (a double check on a pointer write is benign here: a race can only build the same value
 * twice).
 */

#import <Foundation/NSUnitInformationStorage.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

#define FN_UIS_COUNT 35

typedef enum {
	FN_UIS_BITS = 0, FN_UIS_NIBBLES, FN_UIS_BYTES,
	FN_UIS_KIBIBITS, FN_UIS_KIBIBYTES, FN_UIS_MEBIBITS, FN_UIS_MEBIBYTES,
	FN_UIS_GIBIBITS, FN_UIS_GIBIBYTES, FN_UIS_TEBIBITS, FN_UIS_TEBIBYTES,
	FN_UIS_PEBIBITS, FN_UIS_PEBIBYTES, FN_UIS_EXBIBITS, FN_UIS_EXBIBYTES,
	FN_UIS_ZEBIBITS, FN_UIS_ZEBIBYTES, FN_UIS_YOBIBITS, FN_UIS_YOBIBYTES,
	FN_UIS_KILOBITS, FN_UIS_KILOBYTES, FN_UIS_MEGABITS, FN_UIS_MEGABYTES,
	FN_UIS_GIGABITS, FN_UIS_GIGABYTES, FN_UIS_TERABITS, FN_UIS_TERABYTES,
	FN_UIS_PETABITS, FN_UIS_PETABYTES, FN_UIS_EXABITS, FN_UIS_EXABYTES,
	FN_UIS_ZETTABITS, FN_UIS_ZETTABYTES, FN_UIS_YOTTABITS, FN_UIS_YOTTABYTES
} fn_uis_index;

static const struct {
	const char *symbol;
	double multiplier;	/* the prefix's factor, before the bit/byte scale */
	double scale;		/* 1.0 for a bit-shaped unit, 8.0 for a byte-shaped one */
} fn_uis_table[FN_UIS_COUNT] = {
	{ "bit",	1.0,		1.0 },
	{ "nibble",	4.0,		1.0 },
	{ "B",		1.0,		8.0 },

	{ "Kibit",	1024.0,		1.0 },
	{ "KiB",	1024.0,		8.0 },
	{ "Mibit",	1048576.0,	1.0 },
	{ "MiB",	1048576.0,	8.0 },
	{ "Gibit",	1073741824.0,	1.0 },
	{ "GiB",	1073741824.0,	8.0 },
	{ "Tibit",	1099511627776.0,	1.0 },
	{ "TiB",	1099511627776.0,	8.0 },
	{ "Pibit",	1125899906842624.0,	1.0 },
	{ "PiB",	1125899906842624.0,	8.0 },
	{ "Eibit",	1152921504606846976.0,	1.0 },
	{ "EiB",	1152921504606846976.0,	8.0 },
	{ "Zibit",	1.1805916207174113e21,	1.0 },
	{ "ZiB",	1.1805916207174113e21,	8.0 },
	{ "Yibit",	1.2089258196146292e24,	1.0 },
	{ "YiB",	1.2089258196146292e24,	8.0 },

	{ "kbit",	1e3,		1.0 },
	{ "kB",		1e3,		8.0 },
	{ "Mbit",	1e6,		1.0 },
	{ "MB",		1e6,		8.0 },
	{ "Gbit",	1e9,		1.0 },
	{ "GB",		1e9,		8.0 },
	{ "Tbit",	1e12,		1.0 },
	{ "TB",		1e12,		8.0 },
	{ "Pbit",	1e15,		1.0 },
	{ "PB",		1e15,		8.0 },
	{ "Ebit",	1e18,		1.0 },
	{ "EB",		1e18,		8.0 },
	{ "Zbit",	1e21,		1.0 },
	{ "ZB",		1e21,		8.0 },
	{ "Ybit",	1e24,		1.0 },
	{ "YB",		1e24,		8.0 }
};

static NSUnitInformationStorage *fn_uis_cache[FN_UIS_COUNT];

/* RETURNS THE CONCRETE TYPE, because that is what it builds and what the thirty-five class methods are
 * declared to answer (Apple types its unit properties as the family, not as NSUnit) — and because a
 * helper typed as the abstract class makes every one of those methods an incompatible-pointer warning. */
static NSUnitInformationStorage *fn_uis_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitInformationStorage *unit;

	if (index < 0 || index >= FN_UIS_COUNT) {
		return nil;
	}
	if (fn_uis_cache[index] != nil) {
		return fn_uis_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc]
			initWithCoefficient:(fn_uis_table[index].multiplier * fn_uis_table[index].scale)];
	unit = [[NSUnitInformationStorage alloc]
			initWithSymbol:[NSString stringWithUTF8String:fn_uis_table[index].symbol]
			     converter:converter];
	[converter release];
	/* KEPT: see the header note on identity equality. The array is this file's, the units are immutable,
	 * and there is nothing in the library's teardown that should be collecting them. */
	fn_uis_cache[index] = unit;
	return fn_uis_cache[index];
}

@implementation NSUnitInformationStorage

+ (NSUnit *)baseUnit
{
	/* BITS — see the header's note. Every coefficient in the family is an integer against it. */
	return fn_uis_unit(FN_UIS_BITS);
}

+ (NSUnitInformationStorage *)bits { return fn_uis_unit(FN_UIS_BITS); }
+ (NSUnitInformationStorage *)nibbles { return fn_uis_unit(FN_UIS_NIBBLES); }
+ (NSUnitInformationStorage *)bytes { return fn_uis_unit(FN_UIS_BYTES); }

+ (NSUnitInformationStorage *)kibibits { return fn_uis_unit(FN_UIS_KIBIBITS); }
+ (NSUnitInformationStorage *)kibibytes { return fn_uis_unit(FN_UIS_KIBIBYTES); }
+ (NSUnitInformationStorage *)mebibits { return fn_uis_unit(FN_UIS_MEBIBITS); }
+ (NSUnitInformationStorage *)mebibytes { return fn_uis_unit(FN_UIS_MEBIBYTES); }
+ (NSUnitInformationStorage *)gibibits { return fn_uis_unit(FN_UIS_GIBIBITS); }
+ (NSUnitInformationStorage *)gibibytes { return fn_uis_unit(FN_UIS_GIBIBYTES); }
+ (NSUnitInformationStorage *)tebibits { return fn_uis_unit(FN_UIS_TEBIBITS); }
+ (NSUnitInformationStorage *)tebibytes { return fn_uis_unit(FN_UIS_TEBIBYTES); }
+ (NSUnitInformationStorage *)pebibits { return fn_uis_unit(FN_UIS_PEBIBITS); }
+ (NSUnitInformationStorage *)pebibytes { return fn_uis_unit(FN_UIS_PEBIBYTES); }
+ (NSUnitInformationStorage *)exbibits { return fn_uis_unit(FN_UIS_EXBIBITS); }
+ (NSUnitInformationStorage *)exbibytes { return fn_uis_unit(FN_UIS_EXBIBYTES); }
+ (NSUnitInformationStorage *)zebibits { return fn_uis_unit(FN_UIS_ZEBIBITS); }
+ (NSUnitInformationStorage *)zebibytes { return fn_uis_unit(FN_UIS_ZEBIBYTES); }
+ (NSUnitInformationStorage *)yobibits { return fn_uis_unit(FN_UIS_YOBIBITS); }
+ (NSUnitInformationStorage *)yobibytes { return fn_uis_unit(FN_UIS_YOBIBYTES); }

+ (NSUnitInformationStorage *)kilobits { return fn_uis_unit(FN_UIS_KILOBITS); }
+ (NSUnitInformationStorage *)kilobytes { return fn_uis_unit(FN_UIS_KILOBYTES); }
+ (NSUnitInformationStorage *)megabits { return fn_uis_unit(FN_UIS_MEGABITS); }
+ (NSUnitInformationStorage *)megabytes { return fn_uis_unit(FN_UIS_MEGABYTES); }
+ (NSUnitInformationStorage *)gigabits { return fn_uis_unit(FN_UIS_GIGABITS); }
+ (NSUnitInformationStorage *)gigabytes { return fn_uis_unit(FN_UIS_GIGABYTES); }
+ (NSUnitInformationStorage *)terabits { return fn_uis_unit(FN_UIS_TERABITS); }
+ (NSUnitInformationStorage *)terabytes { return fn_uis_unit(FN_UIS_TERABYTES); }
+ (NSUnitInformationStorage *)petabits { return fn_uis_unit(FN_UIS_PETABITS); }
+ (NSUnitInformationStorage *)petabytes { return fn_uis_unit(FN_UIS_PETABYTES); }
+ (NSUnitInformationStorage *)exabits { return fn_uis_unit(FN_UIS_EXABITS); }
+ (NSUnitInformationStorage *)exabytes { return fn_uis_unit(FN_UIS_EXABYTES); }
+ (NSUnitInformationStorage *)zettabits { return fn_uis_unit(FN_UIS_ZETTABITS); }
+ (NSUnitInformationStorage *)zettabytes { return fn_uis_unit(FN_UIS_ZETTABYTES); }
+ (NSUnitInformationStorage *)yottabits { return fn_uis_unit(FN_UIS_YOTTABITS); }
+ (NSUnitInformationStorage *)yottabytes { return fn_uis_unit(FN_UIS_YOTTABYTES); }

@end
