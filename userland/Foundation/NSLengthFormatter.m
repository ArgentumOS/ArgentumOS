/*
 * NSLengthFormatter.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSLengthFormatter.h for the family, the value scheme, and the locale stance that decides the default units.
 */

#import <Foundation/NSLengthFormatter.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSNumber.h>
#include <stdio.h>

/* THE TABLE, IN ONE PLACE: what a unit is abbreviated to, how many of it ONE METRE holds, and which family it
 * belongs to. Every conversion below is this table read one way or the other, and the natural-unit chooser walks
 * it — a second table would be a second place for a factor to be wrong. */
typedef struct {
	NSLengthFormatterUnit unit;
	const char *symbol;
	double perMetre;		/* how many of this unit one metre holds */
	BOOL metric;
} FNLengthUnitInfo;

static const FNLengthUnitInfo fn_lf_units[] = {
	{ NSLengthFormatterUnitMillimeter,	"mm",	1000.0,			YES },
	{ NSLengthFormatterUnitCentimeter,	"cm",	100.0,			YES },
	{ NSLengthFormatterUnitMeter,		"m",	1.0,			YES },
	{ NSLengthFormatterUnitKilometer,	"km",	0.001,			YES },
	{ NSLengthFormatterUnitInch,		"in",	39.37007874015748,	NO },
	{ NSLengthFormatterUnitFoot,		"ft",	3.280839895013123,	NO },
	{ NSLengthFormatterUnitYard,		"yd",	1.0936132983377078,	NO },
	{ NSLengthFormatterUnitMile,		"mi",	0.000621371192237334,	NO }
};

#define FN_LF_UNIT_COUNT	(sizeof(fn_lf_units) / sizeof(fn_lf_units[0]))

static const FNLengthUnitInfo *fn_lf_find(NSLengthFormatterUnit unit)
{
	NSUInteger i;

	for (i = 0; i < FN_LF_UNIT_COUNT; i++) {
		if (fn_lf_units[i].unit == unit) {
			return &fn_lf_units[i];
		}
	}
	return NULL;
}

/* EVERY MEASUREMENT IS CARRIED IN METRES between the doors, so a caller may hand in any unit and ask for any
 * other without a pair per combination. */
static double fn_lf_toMetres(double value, NSLengthFormatterUnit unit)
{
	const FNLengthUnitInfo *info = fn_lf_find(unit);

	return info == NULL ? value : value / info->perMetre;
}

static double fn_lf_fromMetres(double metres, NSLengthFormatterUnit unit)
{
	const FNLengthUnitInfo *info = fn_lf_find(unit);

	return info == NULL ? metres : metres * info->perMetre;
}

/* THE NATURAL UNIT IS THE LARGEST ONE OF THE FAMILY WHOSE VALUE IS AT LEAST ONE, and the smallest when even that
 * is not — the rule NSByteCountFormatter uses one quantity over, with "largest" meaning the furthest along this
 * table. A ZERO OR NEGATIVE MEASUREMENT has no larger unit to prefer, so it takes the family's smallest. */
static void fn_lf_natural(BOOL metric, double metres, NSLengthFormatterUnit *outUnit, double *outValue)
{
	NSUInteger i;
	NSLengthFormatterUnit chosen = 0;
	BOOL haveChosen = NO;

	/* THE TABLE ASCENDS WITHIN EACH FAMILY, SO "THE LARGEST UNIT WHOSE VALUE IS AT LEAST ONE" IS SIMPLY THE LAST
	 * ONE THAT QUALIFIES WHILE WALKING IT UPWARDS. The first unit always sets `chosen`, which is what gives the
	 * smallest unit of the family to a measurement too small for any of them. */
	for (i = 0; i < FN_LF_UNIT_COUNT; i++) {
		if (fn_lf_units[i].metric != metric) {
			continue;
		}
		if (!haveChosen || fn_lf_fromMetres(metres, fn_lf_units[i].unit) >= 1.0) {
			chosen = fn_lf_units[i].unit;
		}
		haveChosen = YES;
	}
	if (!haveChosen) {
		chosen = NSLengthFormatterUnitMeter;	/* no unit of the family at all: the base one names the value */
	}
	*outUnit = chosen;
	*outValue = fn_lf_fromMetres(metres, chosen);
}

@implementation NSLengthFormatter

- (id)init
{
	self = [super init];
	if (self != nil) {
		_numberFormatter = nil;
		_forPersonHeightUse = NO;
	}
	return self;
}

- (NSNumberFormatter *)numberFormatter { return _numberFormatter; }

- (void)setNumberFormatter:(NSNumberFormatter *)numberFormatter
{
	NSNumberFormatter *old = _numberFormatter;

	_numberFormatter = [numberFormatter retain];
	[old release];
}

- (BOOL)isForPersonHeightUse { return _forPersonHeightUse; }
- (void)setForPersonHeightUse:(BOOL)use { _forPersonHeightUse = use; }

/* THE RENDER, AND THE NUMBER FORMATTER IS HONOURED WHEN THERE IS ONE. Without one the value is printed with a
 * decimal point and four significant digits, which is the root locale's separator and the same stance the
 * byte-count formatter takes — this library has no locale to take another from. */
- (NSString *)fn_value:(double)value symbol:(const char *)symbol
{
	if (_numberFormatter != nil) {
		return [NSString stringWithFormat:@"%@ %s",
			[_numberFormatter stringFromNumber:[NSNumber numberWithDouble:value]], symbol];
	}
	return [NSString stringWithFormat:@"%.4g %s", value, symbol];
}

- (NSString *)stringFromValue:(double)value unit:(NSLengthFormatterUnit)unit
{
	const FNLengthUnitInfo *info = fn_lf_find(unit);

	if (info == NULL) {
		return [NSString stringWithFormat:@"%.4g", value];
	}
	return [self fn_value:value symbol:info->symbol];
}

- (NSString *)unitStringFromValue:(double)value unit:(NSLengthFormatterUnit)unit
{
	const FNLengthUnitInfo *info = fn_lf_find(unit);

	(void)value;
	return info == NULL ? @"" : [NSString stringWithUTF8String:info->symbol];
}

- (NSString *)unitStringFromMeters:(double)number usedUnit:(NSLengthFormatterUnit *)unitp
{
	NSLengthFormatterUnit chosen;
	double value;

	fn_lf_natural(!_forPersonHeightUse, number, &chosen, &value);
	if (unitp != NULL) {
		*unitp = chosen;
	}
	return [self unitStringFromValue:value unit:chosen];
}

- (NSString *)stringFromMeters:(double)number
{
	NSLengthFormatterUnit chosen;
	double value;

	if (_forPersonHeightUse) {
		/* A PERSON'S HEIGHT IS WRITTEN IN FEET AND INCHES, NOT AS A SINGLE UNIT: 1.75 m is "5 ft 8.9 in" and
		 * not "5.7 ft", and Apple's flag exists for exactly this rendering. It is stated here because it is the
		 * one place this class gives two units at once. */
		double totalInches = fn_lf_fromMetres(number, NSLengthFormatterUnitInch);
		double feet = (double)(long)(totalInches / 12.0);
		double inches = totalInches - (feet * 12.0);

		if (_numberFormatter != nil) {
			return [NSString stringWithFormat:@"%@ ft %@ in",
				[_numberFormatter stringFromNumber:[NSNumber numberWithDouble:feet]],
				[_numberFormatter stringFromNumber:[NSNumber numberWithDouble:inches]]];
		}
		return [NSString stringWithFormat:@"%.4g ft %.3g in", feet, inches];
	}
	fn_lf_natural(YES, number, &chosen, &value);
	return [self stringFromValue:value unit:chosen];
}

- (void)dealloc
{
	[_numberFormatter release];
	[super dealloc];
}

@end
