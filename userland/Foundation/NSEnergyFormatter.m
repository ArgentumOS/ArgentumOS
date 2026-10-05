/*
 * NSEnergyFormatter.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The same shape again (see NSLengthFormatter.m): a table, a base unit, one chooser, and one flag that chooses the
 * FAMILY — which for this quantity is joule versus calorie rather than metric versus imperial, because energy has
 * no imperial units.
 */

#import <Foundation/NSEnergyFormatter.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSNumber.h>
#include <stdio.h>

typedef struct {
	NSEnergyFormatterUnit unit;
	const char *symbol;
	double perJoule;		/* how many of this unit one joule holds */
	BOOL food;			/* the calorie family, which `forFoodEnergyUse` asks for */
} FNEnergyUnitInfo;

static const FNEnergyUnitInfo fn_ef_units[] = {
	{ NSEnergyFormatterUnitJoule,		"J",	1.0,				NO },
	{ NSEnergyFormatterUnitKilojoule,	"kJ",	0.001,				NO },
	{ NSEnergyFormatterUnitCalorie,		"cal",	0.2390057361376673,		YES },
	{ NSEnergyFormatterUnitKilocalorie,	"kcal",	0.0002390057361376673,		YES }
};

#define FN_EF_UNIT_COUNT	(sizeof(fn_ef_units) / sizeof(fn_ef_units[0]))

static const FNEnergyUnitInfo *fn_ef_find(NSEnergyFormatterUnit unit)
{
	NSUInteger i;

	for (i = 0; i < FN_EF_UNIT_COUNT; i++) {
		if (fn_ef_units[i].unit == unit) {
			return &fn_ef_units[i];
		}
	}
	return NULL;
}

static double fn_ef_fromJoules(double joules, NSEnergyFormatterUnit unit)
{
	const FNEnergyUnitInfo *info = fn_ef_find(unit);

	return info == NULL ? joules : joules * info->perJoule;
}

static double fn_ef_toJoules(double value, NSEnergyFormatterUnit unit)
{
	const FNEnergyUnitInfo *info = fn_ef_find(unit);

	return info == NULL ? value : value / info->perJoule;
}

static void fn_ef_natural(BOOL food, double joules, NSEnergyFormatterUnit *outUnit, double *outValue)
{
	NSUInteger i;
	NSEnergyFormatterUnit chosen = 0;
	BOOL haveChosen = NO;

	for (i = 0; i < FN_EF_UNIT_COUNT; i++) {
		if (fn_ef_units[i].food != food) {
			continue;
		}
		if (!haveChosen || fn_ef_fromJoules(joules, fn_ef_units[i].unit) >= 1.0) {
			chosen = fn_ef_units[i].unit;
		}
		haveChosen = YES;
	}
	if (!haveChosen) {
		chosen = NSEnergyFormatterUnitJoule;
	}
	*outUnit = chosen;
	*outValue = fn_ef_fromJoules(joules, chosen);
}

@implementation NSEnergyFormatter

- (id)init
{
	self = [super init];
	if (self != nil) {
		_numberFormatter = nil;
		_forFoodEnergyUse = NO;
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

- (BOOL)isForFoodEnergyUse { return _forFoodEnergyUse; }
- (void)setForFoodEnergyUse:(BOOL)use { _forFoodEnergyUse = use; }

- (NSString *)fn_value:(double)value symbol:(const char *)symbol
{
	if (_numberFormatter != nil) {
		return [NSString stringWithFormat:@"%@ %s",
			[_numberFormatter stringFromNumber:[NSNumber numberWithDouble:value]], symbol];
	}
	return [NSString stringWithFormat:@"%.4g %s", value, symbol];
}

- (NSString *)stringFromValue:(double)value unit:(NSEnergyFormatterUnit)unit
{
	const FNEnergyUnitInfo *info = fn_ef_find(unit);

	if (info == NULL) {
		return [NSString stringWithFormat:@"%.4g", value];
	}
	return [self fn_value:value symbol:info->symbol];
}

- (NSString *)unitStringFromValue:(double)value unit:(NSEnergyFormatterUnit)unit
{
	const FNEnergyUnitInfo *info = fn_ef_find(unit);

	(void)value;
	return info == NULL ? @"" : [NSString stringWithUTF8String:info->symbol];
}

- (NSString *)unitStringFromJoules:(double)number usedUnit:(NSEnergyFormatterUnit *)unitp
{
	NSEnergyFormatterUnit chosen;
	double value;

	fn_ef_natural(_forFoodEnergyUse, number, &chosen, &value);
	if (unitp != NULL) {
		*unitp = chosen;
	}
	return [self unitStringFromValue:value unit:chosen];
}

- (NSString *)stringFromJoules:(double)number
{
	NSEnergyFormatterUnit chosen;
	double value;

	/* FOOD ENERGY IS KILOCALORIES — the unit a label uses — and the joule family is the default for everything
	 * else. The flag chooses a FAMILY here rather than a measurement system, because energy has no imperial units. */
	if (_forFoodEnergyUse) {
		value = fn_ef_fromJoules(number, NSEnergyFormatterUnitKilocalorie);
		return [self stringFromValue:value unit:NSEnergyFormatterUnitKilocalorie];
	}
	fn_ef_natural(NO, number, &chosen, &value);
	return [self stringFromValue:value unit:chosen];
}

- (void)dealloc
{
	[_numberFormatter release];
	[super dealloc];
}

@end
