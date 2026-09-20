/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitFuelEfficiency — THE FAMILY WHOSE RELATION IS INVERSE, shipped as a DOCUMENTED APPROXIMATION.
 * docs/design/foundation-plan.md §12.3 W12, and §11.6's deviation register.
 *
 * **LITRES PER 100 KILOMETRES IS INVERSE TO MILES PER GALLON.** L/100km = 235.214583 / mpg — a reciprocal,
 * not a multiple — and `NSUnitConverterLinear`, which is a coefficient and a constant, CANNOT EXPRESS ONE.
 * Apple's API has the same shape, so Apple's own conversion for that pair is not correct either; what Apple
 * publishes is the unit and not its ratio.
 *
 * THE DECISION (user's, 2026-09-20) IS TO SHIP ALL THREE WITH THE APPROXIMATION DOCUMENTED rather than to
 * refuse the unit, and this file therefore ANCHORS THE LINEAR FORM AT THE ONE POINT IT CAN BE RIGHT: the
 * value at which the two scales READ THE SAME NUMBER. That crossing is the fixed point of v = 235.214583 / v,
 * which is √235.214583 ≈ 15.3362 — the same shape as the temperature family's -40, and chosen for the same
 * reason (a relation that is exact somewhere and says so).
 *
 *     base is litres per 100 kilometres, so its coefficient is 1
 *     milesPerGallon   = 1                                       exact AT the crossing, linear away from it
 *     milesPerImperialGallon = 4.54609 / 3.785411784             exact ALWAYS, being a ratio of two volumes
 *
 * **AND THE mpg COEFFICIENT IS 1 WHEREAS THE CROSSING IS 15.3362, WHICH LOOKS LIKE A MISTAKE AND IS THE
 * ALGEBRA.** The linear form is `l100 = c * mpg` and the true relation is `l100 = F / mpg`; the two agree at
 * exactly one point, `c = F / M²`. Choosing the crossing as that point fixes `M² = F`, so `c = 1` — at the
 * crossing the two scales COINCIDE, and the coefficient that expresses that is one. (The first version of
 * this file wrote the crossing value in as the coefficient, and the probe caught it: 15.34 mpg converted to
 * 235.2 L/100km instead of to 15.34.)
 *
 * **AWAY FROM THE CROSSING THE mpg CONVERSION IS A LINEAR APPROXIMATION AND SAYS SO** — 30 mpg answers 30
 * where the true value is about 7.84. The probe asserts BOTH the crossing (exact) and that divergence
 * (named as the deviation), so the limitation is recorded in the instrument instead of hiding in a table.
 * The work item behind it is a converter that can express an inverse relation.
 * instrument instead of hiding in a table. The work item behind it is a converter that can express an
 * inverse relation.
 *
 * THE TWO CONSTANTS ARE DERIVATIONS AND ARE WRITTEN AS SUCH: 3.785411784 litres in a US gallon and 4.54609
 * in an imperial one are definitions, and 1.609344 kilometres in a mile is one.
 */

#ifndef FOUNDATION_NSUNITFULEFFICIENCY_H
#define FOUNDATION_NSUNITFULEFFICIENCY_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitFuelEfficiency : NSDimension

/* A litre per 100 kilometres: the dimension's base, and the unit the inverse relation is defined from. */
+ (NSUnitFuelEfficiency *)litersPer100Kilometers;

/* Miles per US gallon. EXACT AT THE CROSSING (≈15.3362 mpg, where both scales read the same number) and a
 * linear approximation away from it — see the file header. */
+ (NSUnitFuelEfficiency *)milesPerGallon;

/* Miles per imperial gallon: exact ALWAYS, because it is the US gallon above scaled by a ratio of two
 * volumes. */
+ (NSUnitFuelEfficiency *)milesPerImperialGallon;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITFULEFFICIENCY_H */
