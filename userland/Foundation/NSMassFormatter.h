/*
 * NSMassFormatter.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSMassFormatter` (§62.50), the second of Apple's formatter trio. See NSLengthFormatter.h for the value scheme
 * (§11.6.1 D2: the numbers are ours, the order is the documentation's) and for the locale stance that decides the
 * default units — this library formats with the root locale, so METRIC is the default and `forPersonMassUse` is
 * what asks for pounds.
 */

#ifndef FOUNDATION_NSMASSFORMATTER_H
#define FOUNDATION_NSMASSFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSString.h>

@class NSNumberFormatter;

NS_ASSUME_NONNULL_BEGIN

typedef enum {
	NSMassFormatterUnitGram		= 1,
	NSMassFormatterUnitKilogram	= 2,
	NSMassFormatterUnitOunce	= 3,
	NSMassFormatterUnitPound	= 4,
	NSMassFormatterUnitStone	= 5
} NSMassFormatterUnit;

@interface NSMassFormatter : NSFormatter
{
	NSNumberFormatter *_numberFormatter;
	BOOL _forPersonMassUse;
}

- (nullable NSNumberFormatter *)numberFormatter;
- (void)setNumberFormatter:(nullable NSNumberFormatter *)numberFormatter;
- (BOOL)isForPersonMassUse;
- (void)setForPersonMassUse:(BOOL)use;

- (NSString *)stringFromValue:(double)value unit:(NSMassFormatterUnit)unit;
- (NSString *)stringFromKilograms:(double)number;
- (NSString *)unitStringFromValue:(double)value unit:(NSMassFormatterUnit)unit;
- (NSString *)unitStringFromKilograms:(double)number usedUnit:(nullable NSMassFormatterUnit *)unitp;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMASSFORMATTER_H */
