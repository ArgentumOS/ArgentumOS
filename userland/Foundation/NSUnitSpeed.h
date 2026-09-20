/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitSpeed — four units against the metre per second. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE COEFFICIENTS ARE OTHER FAMILIES' DEFINITIONS DIVIDED, which is the whole content of this class: a
 * kilometre per hour is NSUnitLength's kilometre over NSUnitDuration's hour, a mile per hour is the mile
 * over the hour, a knot is the NAUTICAL mile over the hour. They are written as those divisions rather
 * than as decimals, so the three families cannot drift apart — and the probe asserts the relation.
 */

#ifndef FOUNDATION_NSUNITSPEED_H
#define FOUNDATION_NSUNITSPEED_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitSpeed : NSDimension

+ (NSUnitSpeed *)metersPerSecond;
+ (NSUnitSpeed *)kilometersPerHour;
+ (NSUnitSpeed *)milesPerHour;
+ (NSUnitSpeed *)knots;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITSPEED_H */
