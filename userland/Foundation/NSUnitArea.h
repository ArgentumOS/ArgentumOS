/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitArea — fourteen units against the square metre. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE COEFFICIENTS ARE SQUARES, AND THAT IS THE WHOLE FAMILY: a square inch is 0.0254², a square foot is
 * 0.3048², a square mile is 1609.344², and the families agree because the LENGTH coefficients are the same
 * definitions. So this file's numbers are derivable from NSUnitLength's, which is exactly what the probe
 * checks for one of them rather than trusting a table of squares typed out by hand.
 *
 * THE TWO LAND UNITS ARE THE EXCEPTION and are not squares of anything: an acre is 4046.8564224 m² by
 * definition (4840 square yards, but the definition is the number), and `ares`/`hectares` are the metric
 * land units — 100 m² and 10000 m², which IS a hundred ares.
 */

#ifndef FOUNDATION_NSUNITAREA_H
#define FOUNDATION_NSUNITAREA_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitArea : NSDimension

+ (NSUnitArea *)squareMegameters;
+ (NSUnitArea *)squareKilometers;
+ (NSUnitArea *)squareMeters;
+ (NSUnitArea *)squareCentimeters;
+ (NSUnitArea *)squareMillimeters;
+ (NSUnitArea *)squareMicrometers;
+ (NSUnitArea *)squareNanometers;
+ (NSUnitArea *)squareInches;
+ (NSUnitArea *)squareFeet;
+ (NSUnitArea *)squareYards;
+ (NSUnitArea *)squareMiles;
+ (NSUnitArea *)acres;
+ (NSUnitArea *)ares;
+ (NSUnitArea *)hectares;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITAREA_H */
