/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitLength — the largest family in W12, and the one where the coefficients are DEFINITIONS.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE UNIT IS METERS — the SI base, and no choice. Twenty-two units: eleven decimal multiples of the
 * metre, and eleven others whose ratios are exact by definition rather than by measurement (an inch IS
 * 0.0254 m since the 1959 agreement, a nautical mile IS 1852 m, a light-year IS the distance light travels
 * in a Julian year). The two that are ASTRONOMICAL and therefore look different — the astronomical unit
 * and the parsec — are defined constants too, and their literals carry the full precision this file has.
 *
 * WHY THE NUMBERS ARE OURS RATHER THAN APPLE'S: Apple publishes the units and their NAMES, and not their
 * ratios (the same finding §31 recorded for information storage). So every coefficient here comes from the
 * unit's own definition, and the probe pins the ones a mistake would show up in — an inch, a mile, a
 * nautical mile and a light-year.
 */

#ifndef FOUNDATION_NSUNITLENGTH_H
#define FOUNDATION_NSUNITLENGTH_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitLength : NSDimension

/* Decimal multiples of the metre. */
+ (NSUnitLength *)megameters;
+ (NSUnitLength *)kilometers;
+ (NSUnitLength *)hectometers;
+ (NSUnitLength *)decameters;
+ (NSUnitLength *)meters;
+ (NSUnitLength *)decimeters;
+ (NSUnitLength *)centimeters;
+ (NSUnitLength *)millimeters;
+ (NSUnitLength *)micrometers;
+ (NSUnitLength *)nanometers;
+ (NSUnitLength *)picometers;

/* The imperial and customary units, and the astronomical ones. */
+ (NSUnitLength *)inches;
+ (NSUnitLength *)feet;
+ (NSUnitLength *)yards;
+ (NSUnitLength *)miles;
+ (NSUnitLength *)scandinavianMiles;
+ (NSUnitLength *)lightyears;
+ (NSUnitLength *)nauticalMiles;
+ (NSUnitLength *)fathoms;
+ (NSUnitLength *)furlongs;
+ (NSUnitLength *)astronomicalUnits;
+ (NSUnitLength *)parsecs;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITLENGTH_H */
