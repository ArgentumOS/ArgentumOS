/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitMass — seventeen units against the kilogram. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE UNIT IS THE KILOGRAM, and it is the one SI base that is not prefixed: the kilogram is the base
 * unit OF mass, so `grams` is a thousandth of the base rather than the other way round. That inversion is
 * why this family reads oddly at first glance and why the table below is written in kilograms throughout.
 *
 * THE IMPERIAL AND CUSTOMARY RATIOS ARE DEFINITIONS (the international avoirdupois pound is exactly
 * 0.45359237 kg since 1959, an ounce is a sixteenth of it, a stone is fourteen pounds, a troy ounce is
 * exactly 0.0311034768 kg), and `slugs` is the one quantity here that is DERIVED rather than defined — the
 * mass that one pound-force accelerates at one foot per second squared — which is why its literal carries
 * more digits than the rest.
 */

#ifndef FOUNDATION_NSUNITMASS_H
#define FOUNDATION_NSUNITMASS_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitMass : NSDimension

+ (NSUnitMass *)kilograms;
+ (NSUnitMass *)grams;
+ (NSUnitMass *)decigrams;
+ (NSUnitMass *)centigrams;
+ (NSUnitMass *)milligrams;
+ (NSUnitMass *)micrograms;
+ (NSUnitMass *)nanograms;
+ (NSUnitMass *)picograms;
+ (NSUnitMass *)ounces;
+ (NSUnitMass *)pounds;
+ (NSUnitMass *)poundsMass;
+ (NSUnitMass *)stones;
+ (NSUnitMass *)metricTons;
+ (NSUnitMass *)shortTons;
+ (NSUnitMass *)carats;
+ (NSUnitMass *)ouncesTroy;
+ (NSUnitMass *)slugs;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITMASS_H */
