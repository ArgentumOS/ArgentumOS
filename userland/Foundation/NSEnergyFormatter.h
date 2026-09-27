/*
 * NSEnergyFormatter.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSEnergyFormatter` (§62.50), the third of Apple's formatter trio. See NSLengthFormatter.h for the value scheme
 * (§11.6.1 D2) and the locale stance.
 *
 * THIS ONE'S FLAG IS NOT METRIC VERSUS IMPERIAL, because energy has no imperial units: `forFoodEnergyUse` asks for
 * the CALORIE family — which is what a food label uses — where the default is the joule family.
 */

#ifndef FOUNDATION_NSENERGYFORMATTER_H
#define FOUNDATION_NSENERGYFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSString.h>

@class NSNumberFormatter;

NS_ASSUME_NONNULL_BEGIN

typedef enum {
	NSEnergyFormatterUnitJoule	= 1,
	NSEnergyFormatterUnitKilojoule	= 2,
	NSEnergyFormatterUnitCalorie	= 3,
	NSEnergyFormatterUnitKilocalorie	= 4
} NSEnergyFormatterUnit;

@interface NSEnergyFormatter : NSFormatter
{
	NSNumberFormatter *_numberFormatter;
	BOOL _forFoodEnergyUse;
}

- (nullable NSNumberFormatter *)numberFormatter;
- (void)setNumberFormatter:(nullable NSNumberFormatter *)numberFormatter;
- (BOOL)isForFoodEnergyUse;
- (void)setForFoodEnergyUse:(BOOL)use;

- (NSString *)stringFromValue:(double)value unit:(NSEnergyFormatterUnit)unit;
- (NSString *)stringFromJoules:(double)number;
- (NSString *)unitStringFromValue:(double)value unit:(NSEnergyFormatterUnit)unit;
- (NSString *)unitStringFromJoules:(double)number usedUnit:(nullable NSEnergyFormatterUnit *)unitp;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSENERGYFORMATTER_H */
