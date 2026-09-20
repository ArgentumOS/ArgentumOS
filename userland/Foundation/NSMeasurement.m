/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMeasurement.m — a quantity, its unit, and the path through the base.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * MANUAL OWNERSHIP: one owned unit, released on dealloc. The value is a double and needs no ownership.
 *
 * THE CONVERSION PATH IS WRITTEN ONCE, in fn_measurement_convert: value -> the source unit's base value ->
 * the target unit's value. Every door that converts (the explicit one, the addition, the subtraction) goes
 * through it, so there is exactly one place where the base-unit convention can be wrong.
 *
 * THE CONVERTIBILITY TEST IS THE SAME CLASS PLUS A CONVERTER ON EACH SIDE. Comparing CLASSES rather than
 * dimensions-by-identity is deliberate: the units a caller compares are usually the same object (the
 * predefined units are cached), but a caller may also have built its own NSUnitInformationStorage with the
 * same converter, and those ARE convertible — the family is the type, not the instance.
 */

#import <Foundation/NSMeasurement.h>
#import <Foundation/NSUnit.h>
#import <Foundation/NSDimension.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import <Foundation/NSCoder.h>

#include <stdint.h>		/* uintptr_t, for the identity half of -hash */

static BOOL fn_measurement_compatible(NSUnit *a, NSUnit *b)
{
	if (a == nil || b == nil) {
		return NO;
	}
	if (![a isKindOfClass:[NSDimension class]] || ![b isKindOfClass:[NSDimension class]]) {
		return NO;
	}
	if (![a isKindOfClass:[b class]]) {
		return NO;	/* different dimensions: metres and seconds do not convert */
	}
	return ([(NSDimension *)a converter] != nil && [(NSDimension *)b converter] != nil);
}

@implementation NSMeasurement

- (instancetype)initWithDoubleValue:(double)doubleValue unit:(NSUnit *)unit
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (unit == nil) {
		[self release];
		[NSException raise:NSInvalidArgumentException
			    format:@"[NSMeasurement initWithDoubleValue:unit:]: a measurement without a unit is "
				   "the thing this class exists to prevent"];
		return nil;
	}
	_doubleValue = doubleValue;
	_unit = [unit retain];
	return self;
}

- (void)dealloc
{
	[_unit release];
	[super dealloc];
}

- (double)doubleValue { return _doubleValue; }
- (NSUnit *)unit { return _unit; }

- (BOOL)canBeConvertedToUnit:(NSUnit *)unit
{
	/* THE SAME OBJECT IS ALWAYS CONVERTIBLE, even for a plain unit with no converter: converting a value
	 * to the unit it is already in is the identity and needs no arithmetic at all. */
	if (unit == _unit) {
		return YES;
	}
	return fn_measurement_compatible(_unit, unit);
}

/* THE ONE CONVERSION PATH: source -> base -> target. */
- (double)fnValueInUnit:(NSUnit *)unit
{
	NSUnitConverter *from = [(NSDimension *)_unit converter];
	NSUnitConverter *to = [(NSDimension *)unit converter];

	if (unit == _unit) {
		return _doubleValue;
	}
	if (!fn_measurement_compatible(_unit, unit)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"[NSMeasurement %@]: %@ and %@ are not the same dimension, so there is no "
				   "conversion between them (-canBeConvertedToUnit: answers NO)",
				   @"measurementByConvertingToUnit:", [_unit symbol], [unit symbol]];
	}
	return [to valueFromBaseUnitValue:[from baseUnitValueFromValue:_doubleValue]];
}

- (NSMeasurement *)measurementByConvertingToUnit:(NSUnit *)unit
{
	NSMeasurement *answer;

	if (unit == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"[NSMeasurement %@]: a nil unit names no conversion",
				   @"measurementByConvertingToUnit:"];
	}
	if (unit == _unit) {
		/* The identity: a NEW measurement, because a value type asked for a value should not hand back
		 * the object it was asked about (and the two are equal by value — see -isEqual:). */
		return [[[NSMeasurement alloc] initWithDoubleValue:_doubleValue unit:_unit] autorelease];
	}
	answer = [[NSMeasurement alloc] initWithDoubleValue:[self fnValueInUnit:unit] unit:unit];
	return [answer autorelease];
}

- (NSMeasurement *)measurementByAddingMeasurement:(NSMeasurement *)measurement
{
	double other;

	if (measurement == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"[NSMeasurement %@]: a nil measurement is not a quantity",
				   @"measurementByAddingMeasurement:"];
	}
	/* THE ANSWER IS IN THE RECEIVER'S UNIT: the argument is converted INTO it first. */
	other = [measurement fnValueInUnit:_unit];
	return [[[NSMeasurement alloc] initWithDoubleValue:(_doubleValue + other) unit:_unit] autorelease];
}

- (NSMeasurement *)measurementBySubtractingMeasurement:(NSMeasurement *)measurement
{
	double other;

	if (measurement == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"[NSMeasurement %@]: a nil measurement is not a quantity",
				   @"measurementBySubtractingMeasurement:"];
	}
	other = [measurement fnValueInUnit:_unit];
	return [[[NSMeasurement alloc] initWithDoubleValue:(_doubleValue - other) unit:_unit] autorelease];
}

- (BOOL)isEqual:(id)other
{
	NSMeasurement *m;

	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSMeasurement class]]) {
		return NO;
	}
	m = (NSMeasurement *)other;
	/* BY VALUE **AND** UNIT: two measurements of 1 kB and 1 KiB are different values even though both are
	 * "1", which is the argument this class exists to end. Equal units are the same object (NSUnit's
	 * identity rule), so this needs no conversion to compare. */
	return ([m unit] == _unit && [m doubleValue] == _doubleValue);
}

- (NSUInteger)hash
{
	return (NSUInteger)(_doubleValue * 31.0) ^ (NSUInteger)(uintptr_t)_unit;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %g %@>", [self class], _doubleValue, [_unit symbol]];
}

- (id)copy
{
	/* A measurement is immutable, so its copy is the same object (+1: `copy` is an OWNED family). */
	return [self retain];
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeDouble:_doubleValue forKey:@"NS.measurementValue"];
	[coder encodeObject:_unit forKey:@"NS.measurementUnit"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithDoubleValue:[coder decodeDoubleForKey:@"NS.measurementValue"]
				    unit:[coder decodeObjectForKey:@"NS.measurementUnit"]];
}

@end
