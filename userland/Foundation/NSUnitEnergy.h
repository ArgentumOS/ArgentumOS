/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitEnergy — five units against the joule. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE CALORIE IS THE ONE COEFFICIENT HERE THAT IS A CHOICE RATHER THAN A DEFINITION: there are several
 * calories (the thermochemical one is 4.184 J, the international table one 4.1868), and Apple publishes the
 * unit and not which. This file takes the THERMOCHEMICAL value — the one the SI brochure and chemistry both
 * use — and the probe pins it, so the choice is visible rather than implied.
 *
 * A KILOWATT-HOUR IS A DERIVED DEFINITION: a thousand watts for 3600 seconds, which is 3.6e6 joules.
 */

#ifndef FOUNDATION_NSUNITENERGY_H
#define FOUNDATION_NSUNITENERGY_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitEnergy : NSDimension

+ (NSUnitEnergy *)kilojoules;
+ (NSUnitEnergy *)joules;
+ (NSUnitEnergy *)kilocalories;
+ (NSUnitEnergy *)calories;
+ (NSUnitEnergy *)kilowattHours;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITENERGY_H */
