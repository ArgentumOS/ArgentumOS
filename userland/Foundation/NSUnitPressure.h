/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitPressure — ten units against the pascal. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE IS THE PASCAL, which Apple spells `newtonsPerMetersSquared` — one unit of the family IS the base
 * under its other name, exactly as framesPerSecond is the hertz and poundsMass is the pound. It is worth
 * naming because a hand-written table tends to give such a row a coefficient of its own, and to get it
 * slightly wrong.
 *
 * THE MERCURY COLUMNS ARE DEFINITIONS AT A STATED TEMPERATURE (a millimetre of mercury is 133.322387415 Pa
 * at 0 °C by the 1954 definition, and an inch is 25.4 of those millimetres), and a pound-force per square
 * inch is the pound-force over the square inch — two of this tree's own length and force definitions.
 */

#ifndef FOUNDATION_NSUNITPRESSURE_H
#define FOUNDATION_NSUNITPRESSURE_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitPressure : NSDimension

+ (NSUnitPressure *)gigapascals;
+ (NSUnitPressure *)megapascals;
+ (NSUnitPressure *)kilopascals;
+ (NSUnitPressure *)hectopascals;
+ (NSUnitPressure *)inchesOfMercury;
+ (NSUnitPressure *)bars;
+ (NSUnitPressure *)millibars;
+ (NSUnitPressure *)millimetersOfMercury;
+ (NSUnitPressure *)newtonsPerMetersSquared;
+ (NSUnitPressure *)poundsForcePerSquareInch;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITPRESSURE_H */
