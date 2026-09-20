/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitElectric.m — four families, four SI bases, and the amp-hour derivation.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * EACH FAMILY IS A TABLE AND THE SHAPE IS THE SAME ONE every other family uses: a cached unit per row, the
 * coefficient being the unit's value in the family's base.
 *
 * THE CHARGE FAMILY IS THE ONE WITH A DERIVATION IN IT: an ampere-hour is a current for a time, so it is
 * exactly 3600 coulombs, and the prefixed forms are that times the prefix (a kiloampere-hour is 3.6e6
 * coulombs, a milliampere-hour 3.6). Those are the numbers a battery is specified in, and they are derived
 * rather than measured.
 *
 * Manual ownership (MRC: the whole library is). Lazy and kept, for the identity reason §31 records.
 */

#import <Foundation/NSUnitElectric.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>

/* ---- current, against the ampere ---- */

typedef enum {
	FN_UI_MEGAAMPERES = 0, FN_UI_KILOAMPERES, FN_UI_AMPERES,
	FN_UI_MILLIAMPERES, FN_UI_MICROAMPERES, FN_UI_COUNT
} fn_ui_index;

static const struct { const char *symbol; double amperes; } fn_ui_table[FN_UI_COUNT] = {
	{ "MA",	1e6 },
	{ "kA",	1e3 },
	{ "A",	1.0 },
	{ "mA",	1e-3 },
	{ "µA",	1e-6 }
};

static NSUnitElectricCurrent *fn_ui_cache[FN_UI_COUNT];

static NSUnitElectricCurrent *fn_ui_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitElectricCurrent *unit;

	if (fn_ui_cache[index] != nil) {
		return fn_ui_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ui_table[index].amperes];
	unit = [[NSUnitElectricCurrent alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ui_table[index].symbol]
						   converter:converter];
	[converter release];
	fn_ui_cache[index] = unit;
	return fn_ui_cache[index];
}

@implementation NSUnitElectricCurrent

+ (NSUnit *)baseUnit { return fn_ui_unit(FN_UI_AMPERES); }
+ (NSUnitElectricCurrent *)megaamperes { return fn_ui_unit(FN_UI_MEGAAMPERES); }
+ (NSUnitElectricCurrent *)kiloamperes { return fn_ui_unit(FN_UI_KILOAMPERES); }
+ (NSUnitElectricCurrent *)amperes { return fn_ui_unit(FN_UI_AMPERES); }
+ (NSUnitElectricCurrent *)milliamperes { return fn_ui_unit(FN_UI_MILLIAMPERES); }
+ (NSUnitElectricCurrent *)microamperes { return fn_ui_unit(FN_UI_MICROAMPERES); }

@end

/* ---- charge, against the coulomb ---- */

typedef enum {
	FN_UQ_COULOMBS = 0, FN_UQ_MEGAAMPEREHOURS, FN_UQ_KILOAMPEREHOURS,
	FN_UQ_AMPEREHOURS, FN_UQ_MILLIAMPEREHOURS, FN_UQ_MICROAMPEREHOURS, FN_UQ_COUNT
} fn_uq_index;

static const struct { const char *symbol; double coulombs; } fn_uq_table[FN_UQ_COUNT] = {
	{ "C",	1.0 },
	{ "MAh", 3600.0 * 1e6 },
	{ "kAh", 3600.0 * 1e3 },
	{ "Ah",	3600.0 },		/* AN AMPERE-HOUR IS A CURRENT FOR A TIME: 3600 coulombs */
	{ "mAh", 3600.0 * 1e-3 },
	{ "µAh", 3600.0 * 1e-6 }
};

static NSUnitElectricCharge *fn_uq_cache[FN_UQ_COUNT];

static NSUnitElectricCharge *fn_uq_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitElectricCharge *unit;

	if (fn_uq_cache[index] != nil) {
		return fn_uq_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_uq_table[index].coulombs];
	unit = [[NSUnitElectricCharge alloc] initWithSymbol:[NSString stringWithUTF8String:fn_uq_table[index].symbol]
						  converter:converter];
	[converter release];
	fn_uq_cache[index] = unit;
	return fn_uq_cache[index];
}

@implementation NSUnitElectricCharge

+ (NSUnit *)baseUnit { return fn_uq_unit(FN_UQ_COULOMBS); }
+ (NSUnitElectricCharge *)coulombs { return fn_uq_unit(FN_UQ_COULOMBS); }
+ (NSUnitElectricCharge *)megaampereHours { return fn_uq_unit(FN_UQ_MEGAAMPEREHOURS); }
+ (NSUnitElectricCharge *)kiloampereHours { return fn_uq_unit(FN_UQ_KILOAMPEREHOURS); }
+ (NSUnitElectricCharge *)ampereHours { return fn_uq_unit(FN_UQ_AMPEREHOURS); }
+ (NSUnitElectricCharge *)milliampereHours { return fn_uq_unit(FN_UQ_MILLIAMPEREHOURS); }
+ (NSUnitElectricCharge *)microampereHours { return fn_uq_unit(FN_UQ_MICROAMPEREHOURS); }

@end

/* ---- potential difference, against the volt ---- */

typedef enum {
	FN_UV_MEGAVOLTS = 0, FN_UV_KILOVOLTS, FN_UV_VOLTS,
	FN_UV_MILLIVOLTS, FN_UV_MICROVOLTS, FN_UV_COUNT
} fn_uv_index;

static const struct { const char *symbol; double volts; } fn_uv_table[FN_UV_COUNT] = {
	{ "MV",	1e6 },
	{ "kV",	1e3 },
	{ "V",	1.0 },
	{ "mV",	1e-3 },
	{ "µV",	1e-6 }
};

static NSUnitElectricPotentialDifference *fn_uv_cache[FN_UV_COUNT];

static NSUnitElectricPotentialDifference *fn_uv_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitElectricPotentialDifference *unit;

	if (fn_uv_cache[index] != nil) {
		return fn_uv_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_uv_table[index].volts];
	unit = [[NSUnitElectricPotentialDifference alloc]
			initWithSymbol:[NSString stringWithUTF8String:fn_uv_table[index].symbol]
			     converter:converter];
	[converter release];
	fn_uv_cache[index] = unit;
	return fn_uv_cache[index];
}

@implementation NSUnitElectricPotentialDifference

+ (NSUnit *)baseUnit { return fn_uv_unit(FN_UV_VOLTS); }
+ (NSUnitElectricPotentialDifference *)megavolts { return fn_uv_unit(FN_UV_MEGAVOLTS); }
+ (NSUnitElectricPotentialDifference *)kilovolts { return fn_uv_unit(FN_UV_KILOVOLTS); }
+ (NSUnitElectricPotentialDifference *)volts { return fn_uv_unit(FN_UV_VOLTS); }
+ (NSUnitElectricPotentialDifference *)millivolts { return fn_uv_unit(FN_UV_MILLIVOLTS); }
+ (NSUnitElectricPotentialDifference *)microvolts { return fn_uv_unit(FN_UV_MICROVOLTS); }

@end

/* ---- resistance, against the ohm ---- */

typedef enum {
	FN_UR_MEGAOHMS = 0, FN_UR_KILOOHMS, FN_UR_OHMS,
	FN_UR_MILLIOHMS, FN_UR_MICROOHMS, FN_UR_COUNT
} fn_ur_index;

static const struct { const char *symbol; double ohms; } fn_ur_table[FN_UR_COUNT] = {
	{ "MΩ",	1e6 },
	{ "kΩ",	1e3 },
	{ "Ω",	1.0 },
	{ "mΩ",	1e-3 },
	{ "µΩ",	1e-6 }
};

static NSUnitElectricResistance *fn_ur_cache[FN_UR_COUNT];

static NSUnitElectricResistance *fn_ur_unit(int index)
{
	NSUnitConverterLinear *converter;
	NSUnitElectricResistance *unit;

	if (fn_ur_cache[index] != nil) {
		return fn_ur_cache[index];
	}
	converter = [[NSUnitConverterLinear alloc] initWithCoefficient:fn_ur_table[index].ohms];
	unit = [[NSUnitElectricResistance alloc] initWithSymbol:[NSString stringWithUTF8String:fn_ur_table[index].symbol]
						       converter:converter];
	[converter release];
	fn_ur_cache[index] = unit;
	return fn_ur_cache[index];
}

@implementation NSUnitElectricResistance

+ (NSUnit *)baseUnit { return fn_ur_unit(FN_UR_OHMS); }
+ (NSUnitElectricResistance *)megaohms { return fn_ur_unit(FN_UR_MEGAOHMS); }
+ (NSUnitElectricResistance *)kiloohms { return fn_ur_unit(FN_UR_KILOOHMS); }
+ (NSUnitElectricResistance *)ohms { return fn_ur_unit(FN_UR_OHMS); }
+ (NSUnitElectricResistance *)milliohms { return fn_ur_unit(FN_UR_MILLIOHMS); }
+ (NSUnitElectricResistance *)microohms { return fn_ur_unit(FN_UR_MICROOHMS); }

@end
