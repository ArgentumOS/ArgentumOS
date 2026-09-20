/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitVolume — thirty-one units, the largest family in W12. docs/design/foundation-plan.md §12.3 W12.
 *
 * **THE BASE UNIT IS THE CUBIC METRE, AND THAT ONE IS A CHOICE.** The SI derived unit for volume is the
 * cubic metre, and the rule this unit has followed for every other metric family is "the SI base"; Apple's
 * page lists the LITRE series first, which suggests litres, and Apple publishes no `+baseUnit`. The rule
 * wins here because it keeps the whole set consistent — and because a conversion is a ratio, so nothing a
 * caller can observe changes.
 *
 * FOUR SERIES, EACH WITH ITS OWN DERIVATION:
 *   * the LITRE series (mega- down to milli-), and a litre IS a cubic decimetre — so it is 0.001 m³;
 *   * the CUBIC series, which are the NSUnitLength definitions CUBED and are written as those products;
 *   * the US CUSTOMARY liquid measures, which are DIVISIONS OF THE GALLON: a quart is a quarter of it, a
 *     pint a half of a quart, a cup a half of a pint, a fluid ounce an eighth of a cup, a tablespoon a half
 *     of a fluid ounce and a teaspoon a third of a tablespoon;
 *   * the IMPERIAL measures, which are themselves divisions of the imperial gallon (160 fluid ounces).
 *
 * Written as those relations rather than as thirty-one decimals, because a table of decimals agrees with
 * nothing — and the probe asserts three of the divisions rather than the numbers they come to.
 */

#ifndef FOUNDATION_NSUNITVOLUME_H
#define FOUNDATION_NSUNITVOLUME_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitVolume : NSDimension

+ (NSUnitVolume *)megaliters;
+ (NSUnitVolume *)kiloliters;
+ (NSUnitVolume *)liters;
+ (NSUnitVolume *)deciliters;
+ (NSUnitVolume *)centiliters;
+ (NSUnitVolume *)milliliters;
+ (NSUnitVolume *)cubicKilometers;
+ (NSUnitVolume *)cubicMeters;
+ (NSUnitVolume *)cubicDecimeters;
+ (NSUnitVolume *)cubicCentimeters;
+ (NSUnitVolume *)cubicMillimeters;
+ (NSUnitVolume *)cubicInches;
+ (NSUnitVolume *)cubicFeet;
+ (NSUnitVolume *)cubicYards;
+ (NSUnitVolume *)cubicMiles;
+ (NSUnitVolume *)acreFeet;
+ (NSUnitVolume *)bushels;
+ (NSUnitVolume *)teaspoons;
+ (NSUnitVolume *)tablespoons;
+ (NSUnitVolume *)fluidOunces;
+ (NSUnitVolume *)cups;
+ (NSUnitVolume *)pints;
+ (NSUnitVolume *)quarts;
+ (NSUnitVolume *)gallons;
+ (NSUnitVolume *)imperialTeaspoons;
+ (NSUnitVolume *)imperialTablespoons;
+ (NSUnitVolume *)imperialFluidOunces;
+ (NSUnitVolume *)imperialPints;
+ (NSUnitVolume *)imperialQuarts;
+ (NSUnitVolume *)imperialGallons;
+ (NSUnitVolume *)metricCups;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITVOLUME_H */
