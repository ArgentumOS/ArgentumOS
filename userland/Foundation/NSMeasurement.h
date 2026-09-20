/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMeasurement — a quantity and its unit, as ONE value. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE CLASS EXISTS SO THE UNIT TRAVELS WITH THE NUMBER. A bare `double` of bytes is a number whose meaning
 * lives in the caller's head, which is how "1.44 MB" and "1.44 MiB" became a permanent argument; a
 * measurement cannot lose its unit because the two are one object. Everything below is therefore
 * arithmetic BETWEEN UNITS: `-measurementByAddingMeasurement:` has to pick a unit for its answer, and it
 * picks the RECEIVER's, converting the argument into it first.
 *
 * CONVERSION GOES THROUGH THE DIMENSION'S BASE UNIT, never from one unit directly to another, which is the
 * whole reason `+baseUnit` and the converter pair exist: N units need N converters and not N² conversion
 * rules. So `a -> base -> b` is the single path, and a family whose base unit is wrong would show up as a
 * wrong answer in every pair at once rather than in one.
 *
 * TWO UNITS ARE CONVERTIBLE WHEN THEY ARE THE SAME DIMENSION, and "the same dimension" here means the same
 * CLASS with a converter on each side — the strongest thing a caller can check and the one Apple's
 * `-canBeConvertedToUnit:` exists for. A plain NSUnit has no converter at all, so no conversion is possible
 * with one, which is why a measurement of a dimensionless unit is a legal object that refuses to convert.
 *
 * A CONVERSION THAT CANNOT HAPPEN RAISES rather than answering nil: Apple annotates the two arithmetic
 * doors as answering a measurement, and a nil from a nonnull position is the failure this library refuses
 * to invent (the same choice -isValidDateInCalendar: makes). `-canBeConvertedToUnit:` is the guard, and the
 * message names both units so a caller can see which pair was wrong.
 */

#ifndef FOUNDATION_NSMEASUREMENT_H
#define FOUNDATION_NSMEASUREMENT_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

@class NSUnit;

NS_ASSUME_NONNULL_BEGIN

@interface NSMeasurement : NSObject <NSCopying, NSSecureCoding>
{
	double _doubleValue;
	NSUnit *_unit;
}

/* The designated initializer. A nil unit is a caller error and raises — a quantity without a unit is the
 * thing this class exists to prevent. */
- (instancetype)initWithDoubleValue:(double)doubleValue unit:(NSUnit *)unit;

/* The measurement value, represented as a double-precision floating-point number. */
- (double)doubleValue;

/* The unit of measure. */
- (NSUnit *)unit;

/* Indicates whether the measurement can be converted to the given unit: the same dimension, with a
 * converter on both sides. */
- (BOOL)canBeConvertedToUnit:(NSUnit *)unit;

/* The receiver, converted. RAISES when -canBeConvertedToUnit: is NO — see the header note. Converting to
 * the unit it is already in answers an equal measurement, not the receiver itself, because the two are
 * indistinguishable values and one of them is not this object. */
- (NSMeasurement *)measurementByConvertingToUnit:(NSUnit *)unit;

/* Adding and subtracting: the argument is converted into the RECEIVER's unit and the values are combined,
 * so the answer is in the receiver's unit. RAISES when the argument is not convertible. */
- (NSMeasurement *)measurementByAddingMeasurement:(NSMeasurement *)measurement;
- (NSMeasurement *)measurementBySubtractingMeasurement:(NSMeasurement *)measurement;

- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMEASUREMENT_H */
