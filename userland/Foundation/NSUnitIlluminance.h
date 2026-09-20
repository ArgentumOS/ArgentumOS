/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitIlluminance — one unit, the lux. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE SMALLEST FAMILY APPLE SHIPS, and it is not a stub: the SI unit of illuminance IS the lux (a lumen per
 * square metre), so `+lux` is both the only unit and the base, and its coefficient is exactly 1. A family
 * with no conversion to perform still has to EXIST, because a measurement of illuminance needs a unit and
 * because `NSMeasurementFormatter` takes any NSDimension.
 *
 * THE SHAPE IS THE SAME AS EVERY OTHER FAMILY'S (a table, a cache, and `+baseUnit`), so this file's brevity
 * is the family's size rather than a shortcut taken here.
 */

#ifndef FOUNDATION_NSUNITILLUMINANCE_H
#define FOUNDATION_NSUNITILLUMINANCE_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitIlluminance : NSDimension

+ (NSUnitIlluminance *)lux;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITILLUMINANCE_H */
