/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRelativeDateTimeFormatter — a span of time, named from a reference point.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * THE CLASS'S TWO STYLES ARE THE TWO THINGS CLDR DATA CAN SAY ABOUT A RELATIVE DATE, and Apple's own
 * case pages are the specification for both, quoted rather than paraphrased:
 *
 *   Named    "A style that uses named styles to describe relative dates, such as 'yesterday',
 *            'last week', or 'next week'."
 *   Numeric  "A style that uses a numeric style to describe relative dates, such as '1 day ago' or
 *            'in 3 weeks'."
 *
 * So Named asks ICU for the IDIOM and Numeric for the ARITHMETIC, which is exactly ICU's
 * ureldatefmt_format (named) versus ureldatefmt_formatNumeric. Both exist in this image, so this class
 * binds rather than computes — the same standing as NSListFormatter and NSDateIntervalFormatter.
 *
 * THE UNITS STYLE IS THE NUMBER'S SPELLING, and Apple's own four case examples show it: Full "2 months
 * ago", SpellOut "two months ago", Short and Abbreviated both "2 mo. ago". THE LAST PAIR IS NOT A TYPO
 * IN THIS HEADER — Apple's two pages use the SAME example string, so Full/Short/SpellOut map to ICU's
 * long/short widths and a spell-out NUMBER FORMATTER, while Abbreviated shares Short's width. Inventing
 * a difference Apple's own examples do not show would be inventing data.
 *
 * WHAT IS OURS: WHICH UNIT a span is expressed in. Apple publishes no table for it, so the rule is
 * stated here and implemented in one place — the span is taken as a CALENDAR difference (the shipped
 * NSCalendar, so months and years are real, not 30-day approximations), and the LARGEST unit that has a
 * non-zero count names it. A one-day span is therefore "1 day ago" and not "24 hours ago", which is the
 * difference between this class and a duration formatter.
 *
 * THE RESETTABLE PROPERTIES FOLLOW APPLE'S `null_resettable`: nil means "back to the default" — the
 * current calendar and the current locale — resolved when the ICU formatter is built.
 *
 * NOT HERE, NAMED: nothing. This class's whole documented surface lands here.
 */

#ifndef FOUNDATION_NSRELATIVEDATETIMEFORMATTER_H
#define FOUNDATION_NSRELATIVEDATETIMEFORMATTER_H

#import <Foundation/NSFormatter.h>
/* NSDate.h is what defines NSTimeInterval (typedef double) — the time-interval door's parameter type.
 * NSFormatter.h does not reach it, and a forward declaration cannot help for a scalar typedef. */
#import <Foundation/NSDate.h>

@class NSDate;
@class NSDateComponents;
@class NSCalendar;
@class NSLocale;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* The raw values are Apple's order (Named first), as the family's other style enums keep theirs. */
typedef enum {
	NSRelativeDateTimeFormatterStyleNamed = 0,
	NSRelativeDateTimeFormatterStyleNumeric = 1
} NSRelativeDateTimeFormatterStyle;

typedef enum {
	NSRelativeDateTimeFormatterUnitsStyleFull = 0,
	NSRelativeDateTimeFormatterUnitsStyleSpellOut = 1,
	NSRelativeDateTimeFormatterUnitsStyleShort = 2,
	NSRelativeDateTimeFormatterUnitsStyleAbbreviated = 3
} NSRelativeDateTimeFormatterUnitsStyle;

@interface NSRelativeDateTimeFormatter : NSFormatter
{
	NSRelativeDateTimeFormatterStyle _dateTimeStyle;
	NSRelativeDateTimeFormatterUnitsStyle _unitsStyle;
	NSFormattingContext _formattingContext;
	NSCalendar *_calendar;
	NSLocale *_locale;
	void *_formatter;		/* URelativeDateTimeFormatter *, rebuilt by every setting */
}

/* The three conversions. A nil date or components answers nil rather than a guess. */
- (nullable NSString *)localizedStringForDate:(NSDate *)date relativeToDate:(NSDate *)referenceDate;
- (nullable NSString *)localizedStringFromDateComponents:(NSDateComponents *)components;
- (nullable NSString *)localizedStringFromTimeInterval:(NSTimeInterval)timeInterval;

/* NSFormatter's door: a date is formatted relative to NOW — "the current date and time" being the one
 * reference Apple documents for it — and anything that is not a date answers nil. */
- (nullable NSString *)stringForObjectValue:(nullable id)object;

/* The style to use when describing a relative date: an idiom or arithmetic (see the header's note). */
- (NSRelativeDateTimeFormatterStyle)dateTimeStyle;
- (void)setDateTimeStyle:(NSRelativeDateTimeFormatterStyle)style;

/* The style to use when formatting the quantity or the name of the unit — how the NUMBER is spelled. */
- (NSRelativeDateTimeFormatterUnitsStyle)unitsStyle;
- (void)setUnitsStyle:(NSRelativeDateTimeFormatterUnitsStyle)style;

/* The calendar to use for values that do not have an inherent calendar of their own: ours is the one
 * the span is MEASURED in, which is why this class takes one at all. nil means the current calendar. */
- (NSCalendar *)calendar;
- (void)setCalendar:(nullable NSCalendar *)value;

/* The locale to use when formatting the date. nil means the current locale. */
- (NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)value;

/* Where the text will appear, which is why ICU capitalises "Yesterday" for the start of a sentence. */
- (NSFormattingContext)formattingContext;
- (void)setFormattingContext:(NSFormattingContext)context;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSRELATIVEDATETIMEFORMATTER_H */
