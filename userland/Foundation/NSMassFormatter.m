/*
 * NSMassFormatter.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The same shape as NSLengthFormatter.m, one quantity over: a table, a base unit, one chooser, and the
 * person flag as the only thing that changes the family.
 */

#import <Foundation/NSMassFormatter.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSNumber.h>
#include <stdio.h>

typedef struct {
	NSMassFormatterUnit unit;
	const char *symbol;
	double perKilogram;		/* how many of this unit one kilogram holds */
	BOOL metric;
} FNMassUnitInfo;

static const FNMassUnitInfo fn_mf_units[] = {
	{ NSMassFormatterUnitGram,	"g",	1000.0,			YES },
	{ NSMassFormatterUnitKilogram,	"kg",	1.0,			YES },
	{ NSMassFormatterUnitOunce,	"oz",	35.27396194958041,	NO },
	{ NSMassFormatterUnitPound,	"lb",	2.2046226218487757,	NO },
	{ NSMassFormatterUnitStone,	"st",	0.15747304441776972,	NO }
};

#define FN_MF_UNIT_COUNT	(sizeof(fn_mf_units) / sizeof(fn_mf_units[0]))

static const FNMassUnitInfo *fn_mf_find(NSMassFormatterUnit unit)
{
	NSUInteger i;

	for (i = 0; i < FN_MF_UNIT_COUNT; i++) {
		if (fn_mf_units[i].unit == unit) {
			return &fn_mf_units[i];
		}
	}
	return NULL;
}

static double fn_mf_fromKilograms(double kilograms, NSMassFormatterUnit unit)
{
	const FNMassUnitInfo *info = fn_mf_find(unit);

	return info == NULL ? kilograms : kilograms * info->perKilogram;
}

static double fn_mf_toKilograms(double value, NSMassFormatterUnit unit)
{
	const FNMassUnitInfo *info = fn_mf_find(unit);

	return info == NULL ? value : value / info->perKilogram;
}

static void fn_mf_natural(BOOL metric, double kilograms, NSMassFormatterUnit *outUnit, double *outValue)
{
	NSUInteger i;
	NSMassFormatterUnit chosen = 0;
	BOOL haveChosen = NO;

	for (i = 0; i < FN_MF_UNIT_COUNT; i++) {
		if (fn_mf_units[i].metric != metric) {
			continue;
		}
		if (!haveChosen || fn_mf_fromKilograms(kilograms, fn_mf_units[i].unit) >= 1.0) {
			chosen = fn_mf_units[i].unit;
		}
		haveChosen = YES;
	}
	if (!haveChosen) {
		chosen = NSMassFormatterUnitKilogram;
	}
	*outUnit = chosen;
	*outValue = fn_mf_fromKilograms(kilograms, chosen);
}

@implementation NSMassFormatter

- (id)init
{
	self = [super init];
	if (self != nil) {
		_numberFormatter = nil;
		_forPersonMassUse = NO;
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

- (BOOL)isForPersonMassUse { return _forPersonMassUse; }
- (void)setForPersonMassUse:(BOOL)use { _forPersonMassUse = use; }

- (NSString *)fn_value:(double)value symbol:(const char *)symbol
{
	if (_numberFormatter != nil) {
		return [NSString stringWithFormat:@"%@ %s",
			[_numberFormatter stringFromNumber:[NSNumber numberWithDouble:value]], symbol];
	}
	return [NSString stringWithFormat:@"%.4g %s", value, symbol];
}

- (NSString *)stringFromValue:(double)value unit:(NSMassFormatterUnit)unit
{
	const FNMassUnitInfo *info = fn_mf_find(unit);

	if (info == NULL) {
		return [NSString stringWithFormat:@"%.4g", value];
	}
	return [self fn_value:value symbol:info->symbol];
}

- (NSString *)unitStringFromValue:(double)value unit:(NSMassFormatterUnit)unit
{
	const FNMassUnitInfo *info = fn_mf_find(unit);

	(void)value;
	return info == NULL ? @"" : [NSString stringWithUTF8String:info->symbol];
}

- (NSString *)unitStringFromKilograms:(double)number usedUnit:(NSMassFormatterUnit *)unitp
{
	NSMassFormatterUnit chosen;
	double value;

	fn_mf_natural(!_forPersonMassUse, number, &chosen, &value);
	if (unitp != NULL) {
		*unitp = chosen;
	}
	return [self unitStringFromValue:value unit:chosen];
}

- (NSString *)stringFromKilograms:(double)number
{
	NSMassFormatterUnit chosen;
	double value;

	/* A PERSON'S MASS IS POUNDS, AND IT IS ONE UNIT RATHER THAN TWO: stones exist here as a unit a caller may ask
	 * for by name, but a person-mass render is pounds and says so. (The length formatter's person-height case
	 * gives two units because a height is written that way; a mass is not.) */
	if (_forPersonMassUse) {
		value = fn_mf_fromKilograms(number, NSMassFormatterUnitPound);
		return [self stringFromValue:value unit:NSMassFormatterUnitPound];
	}
	fn_mf_natural(YES, number, &chosen, &value);
	return [self stringFromValue:value unit:chosen];
}

- (void)dealloc
{
	[_numberFormatter release];
	[super dealloc];
}

@end
