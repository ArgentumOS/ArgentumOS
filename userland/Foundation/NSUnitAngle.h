/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitAngle — six units against the RADIAN. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE UNIT IS THE RADIAN, which is the SI derived unit for angle and the unit the other five are
 * trigonometrically defined in: a degree is π/180, a gradian is π/200, a revolution is 2π, and the two
 * arc units are sixtieths of a degree. Every coefficient here is therefore π divided by something, and the
 * probe pins the two that a truncated π would show up in (a degree and a revolution) as relations rather
 * than as literals.
 *
 * THE SYMBOLS ARE OURS, as everywhere in this family of classes: Apple publishes the units and not the
 * strings they carry (the same finding §31 recorded).
 */

#ifndef FOUNDATION_NSUNITANGLE_H
#define FOUNDATION_NSUNITANGLE_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitAngle : NSDimension

+ (NSUnitAngle *)degrees;
+ (NSUnitAngle *)arcMinutes;
+ (NSUnitAngle *)arcSeconds;
+ (NSUnitAngle *)radians;
+ (NSUnitAngle *)gradians;
+ (NSUnitAngle *)revolutions;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITANGLE_H */
