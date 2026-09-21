/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMeasurementFormatter — a measurement, written the way a locale writes it.
 * docs/design/foundation-plan.md §12.3 W12, and §32's closing item.
 *
 * **THIS CLASS IS A MAPPING RATHER THAN A TABLE, WHICH IS WHY IT CLOSES THE UNIT RATHER THAN RIDING IT.** ICU
 * can name a unit only by its CLDR IDENTIFIER — `length-meter`, `digital-kilobyte`, `temperature-celsius` —
 * and this tree's units are objects with symbols. So the work here is a TRANSLATION from (family, symbol) to
 * that identifier, and then one `unumf` call with a `measure-unit/…` skeleton, which §29.1 measured as the C
 * surface that exists (ICU's own measure formatting is C++-only).
 *
 * **A UNIT THIS TREE CANNOT NAME ANSWERS nil, AND THAT IS DELIBERATE.** Where the CLDR identifier for one of
 * our units could not be verified, the mapping omits it and `-stringFromUnit:` answers nil. The alternative —
 * guessing an identifier — produces a WRONG NAME, which is worse than an absent one: nil says "this formatter
 * cannot name that", and a wrong name says nothing at all. The probe pins the mapped units so a missing entry
 * is visible.
 *
 * THE THREE UNIT OPTIONS ARE IMPLEMENTED FROM THEIR NAMES, because Apple publishes the names and NO
 * ABSTRACTS (measured: all three pages are empty). Two of them read unambiguously:
 *
 *   * `ProvidedUnit` — the unit as given (the default), i.e. no scaling at all;
 *   * `TemperatureWithoutUnit` — a temperature with no unit letter, which ICU has an identifier for
 *     (`temperature-generic` renders "20°" rather than "20°C"), so this one is DATA rather than arithmetic;
 *   * and **`NaturalScale` is REGISTERED AS NOT IMPLEMENTED**, which the probe asserts by name. Auto-scaling
 *     1500 m to 1.5 km needs a THRESHOLD per family, Apple publishes none, and ICU has no auto-scaling at
 *     all — so this is the §11.2 pattern: an absence with a check on it rather than a silent no-op.
 *
 * THE UNIT STYLE IS MAPPED TO ICU'S WIDTHS, and the mapping is stated because Apple publishes no mapping:
 * `Short` → narrow, `Medium` → short, `Long` → full name. That gives the three styles three DISTINCT
 * behaviours, which is the point of having three.
 */

#ifndef FOUNDATION_NSMEASUREMENTFORMATTER_H
#define FOUNDATION_NSMEASUREMENTFORMATTER_H

#import <Foundation/NSFormatter.h>

@class NSLocale;
@class NSMeasurement;
@class NSNumberFormatter;
@class NSUnit;

NS_ASSUME_NONNULL_BEGIN

/* Apple's three cases, and their abstracts are empty on its own pages: the names are all there is. */
typedef unsigned long NSMeasurementFormatterUnitOptions;

#define NSMeasurementFormatterUnitOptionsProvidedUnit		(1UL << 0)
#define NSMeasurementFormatterUnitOptionsNaturalScale		(1UL << 1)
#define NSMeasurementFormatterUnitOptionsTemperatureWithoutUnit	(1UL << 2)

@interface NSMeasurementFormatter : NSFormatter
{
	NSMeasurementFormatterUnitOptions _unitOptions;
	NSFormattingUnitStyle _unitStyle;
	NSLocale *_locale;
	NSNumberFormatter *_numberFormatter;
}

/* The options for how the unit is formatted. NaturalScale is NOT implemented — see the header's note. */
- (NSMeasurementFormatterUnitOptions)unitOptions;
- (void)setUnitOptions:(NSMeasurementFormatterUnitOptions)options;

/* The unit style: how long-winded the unit's name is. */
- (NSFormattingUnitStyle)unitStyle;
- (void)setUnitStyle:(NSFormattingUnitStyle)style;

/* The locale of the formatter. nil means the current locale. */
- (NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)value;

/* The number formatter used to format the QUANTITY. It is retained rather than copied: a number formatter is
 * mutable and a caller who sets one expects to be able to keep configuring the object they set. */
- (NSNumberFormatter *)numberFormatter;
- (void)setNumberFormatter:(nullable NSNumberFormatter *)value;

/* The two doors. A measurement of a unit this formatter cannot name answers nil (see the header). */
- (nullable NSString *)stringFromMeasurement:(NSMeasurement *)measurement;
- (nullable NSString *)stringFromUnit:(NSUnit *)unit;

/* NSFormatter's door, narrowed to the value this class formats. */
- (nullable NSString *)stringForObjectValue:(nullable id)object;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMEASUREMENTFORMATTER_H */
