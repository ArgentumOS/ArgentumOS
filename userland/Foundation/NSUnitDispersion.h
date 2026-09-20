/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitDispersion — one unit, parts per million. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE ONE FAMILY WHOSE UNIT IS DIMENSIONLESS: a part per million is a RATIO, not a quantity, so there is
 * nothing to convert it to and the base is itself. Apple ships it as a dimension anyway, and the reason is
 * the same as illuminance's — a measurement needs a unit, and a formatter takes any dimension.
 */

#ifndef FOUNDATION_NSUNITDISPERSION_H
#define FOUNDATION_NSUNITDISPERSION_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitDispersion : NSDimension

+ (NSUnitDispersion *)partsPerMillion;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITDISPERSION_H */
