/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitAcceleration — two units against the metre per second squared.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THE SMALLEST FAMILY IN W12, and its second unit is the only one in this whole dimensional set that is a
 * MEASURED PHYSICAL CONSTANT rather than a definition or a prefix: standard gravity is exactly 9.80665
 * m/s² by the 1901 CGPM definition, which is why it can be written as a literal and asserted with `==`.
 */

#ifndef FOUNDATION_NSUNITACCELERATION_H
#define FOUNDATION_NSUNITACCELERATION_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitAcceleration : NSDimension

+ (NSUnitAcceleration *)metersPerSecondSquared;
+ (NSUnitAcceleration *)gravity;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITACCELERATION_H */
