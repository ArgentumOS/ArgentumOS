/*
 * NSLengthFormatter.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSLengthFormatter`, ITS UNITS, AND WHAT A CALLER GETS (§62.50). Apple added the formatter trio at 10.8 — one
 * class per KIND OF QUANTITY, each with an enum of the units it knows and four doors for converting, naming and
 * rendering — and this is the length one.
 *
 * THE NUMBERS ARE OURS (§11.6.1 D2) AND THE ORDER IS APPLE'S: Apple's documentation lists the units in a fixed
 * order and never prints their values, so the values here are a stated scheme — the METRIC units ascending, then
 * the IMPERIAL ones in the order the documentation lists them — and a caller who used a NAME is unaffected by the
 * choice, which is the only kind of caller these names were ever for.
 *
 * WHAT IT DOES WITHOUT A LOCALE, STATED RATHER THAN LEFT TO BE DISCOVERED: this library formats with the ROOT
 * locale's rules (the stance NSLocale's own unit records), so the DEFAULT unit system here is METRIC and the
 * `forPersonHeightUse` flag is what asks for the imperial one — feet and inches, rendered the way a person's
 * height is written. A caller who wants a unit of their own choosing says so with `-stringFromValue:unit:` and
 * the flag is not consulted.
 */

#ifndef FOUNDATION_NSLENGTHFORMATTER_H
#define FOUNDATION_NSLENGTHFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSString.h>

@class NSNumberFormatter;

NS_ASSUME_NONNULL_BEGIN

typedef enum {
	NSLengthFormatterUnitMillimeter		= 1,
	NSLengthFormatterUnitCentimeter		= 2,
	NSLengthFormatterUnitMeter		= 3,
	NSLengthFormatterUnitKilometer		= 4,
	NSLengthFormatterUnitInch		= 5,
	NSLengthFormatterUnitFoot		= 6,
	NSLengthFormatterUnitYard		= 7,
	NSLengthFormatterUnitMile		= 8
} NSLengthFormatterUnit;

@interface NSLengthFormatter : NSFormatter
{
	NSNumberFormatter *_numberFormatter;
	BOOL _forPersonHeightUse;
}

/* A NUMBER FORMATTER IS HONOURED WHEN ONE IS SET and a local render is used when it is not, which is what makes
 * the property mean something on a library whose default is the root locale. */
- (nullable NSNumberFormatter *)numberFormatter;
- (void)setNumberFormatter:(nullable NSNumberFormatter *)numberFormatter;

- (BOOL)isForPersonHeightUse;
- (void)setForPersonHeightUse:(BOOL)use;

/* THE FOUR DOORS: render a value in a unit the caller names, or in the unit this formatter would choose, and ask
 * for the unit's own name. `usedUnit` is an out-parameter and may be NULL — a caller who only wants the string
 * should not have to hold a variable for an answer they did not ask for. */
- (NSString *)stringFromValue:(double)value unit:(NSLengthFormatterUnit)unit;
- (NSString *)stringFromMeters:(double)number;
- (NSString *)unitStringFromValue:(double)value unit:(NSLengthFormatterUnit)unit;
- (NSString *)unitStringFromMeters:(double)number usedUnit:(nullable NSLengthFormatterUnit *)unitp;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSLENGTHFORMATTER_H */
