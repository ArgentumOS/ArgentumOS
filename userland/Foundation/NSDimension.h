/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDimension — a unit that knows its own arithmetic. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE CLASS THAT MAKES CONVERSION POSSIBLE. NSUnit is a symbol and an identity; NSDimension adds the
 * CONVERTER, and the converter's job is defined against the dimension's BASE UNIT — the one unit of the
 * family that everything else is expressed in. So a dimension holds exactly two things beyond its parent:
 * the converter, and (through `+baseUnit`) the identity of the base.
 *
 * `+baseUnit` IS A CLASS METHOD AND THE BASE CLASS ANSWERS nil. Apple declares it on NSDimension and
 * returns the base from each concrete family (NSUnitInformationStorage answers bits, NSUnitLength
 * answers meters, …); there is no base unit OF DIMENSIONS IN GENERAL, so an abstract answer here would be
 * a lie. nil says "this dimension has not named one", which is the truth.
 *
 * A DIMENSION WITHOUT A CONVERTER IS ALSO A LEGAL STATE, and it is not a bug: Apple's own
 * `-initWithSymbol:` is inherited from NSUnit, so a subclass that has not been given a converter yet can
 * be built. `-converter` answers nil and a conversion against it fails through NSUnitConverter's own
 * raise, which names the missing piece instead of guessing one.
 */

#ifndef FOUNDATION_NSDIMENSION_H
#define FOUNDATION_NSDIMENSION_H

#import <Foundation/NSUnit.h>

@class NSUnitConverter;

NS_ASSUME_NONNULL_BEGIN

@interface NSDimension : NSUnit <NSSecureCoding>
{
	NSUnitConverter *_converter;
}

/* The designated initializer: a symbol and the converter that relates this unit to the base unit. */
- (instancetype)initWithSymbol:(NSString *)symbol converter:(NSUnitConverter *)converter;

/* The unit converter that represents the unit in terms of the dimension's base unit. */
- (nullable NSUnitConverter *)converter;

/* Returns the base unit — the unit the family's converters are defined against. nil in the abstract
 * class; each concrete dimension names its own. */
+ (nullable NSUnit *)baseUnit;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDIMENSION_H */
