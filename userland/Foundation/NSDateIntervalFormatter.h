/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateIntervalFormatter — TWO dates as one user-readable range.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * THE POINT OF THE CLASS IS WHAT IT DOES NOT PRINT. Two calls to NSDateFormatter joined by " - " would
 * say "1/16/15 - 1/17/15" and would also say "1/16/15, 9:00 AM - 1/16/15, 10:00 AM" for two times on
 * ONE day — where the second date is noise the reader has to diff out. An interval formatter prints
 * the COMMON PART ONCE and the difference after it, and that is CLDR's interval data rather than a
 * rule anyone should write here. So this class is a binding, like NSListFormatter: ICU's
 * UDateIntervalFormat holds the patterns, and this file owns the settings and the error contract.
 *
 * THREE WAYS TO SAY WHAT TO PRINT, and the precedence is Apple's: `dateTemplate` is a SKELETON that
 * replaces both styles when it is set; otherwise `dateStyle` and `timeStyle` each contribute their
 * fields (a date skeleton and a time skeleton, concatenated), and a formatter with none of the three
 * has no field to render — so its doors answer nil rather than an empty string, the same decision
 * NSISO8601DateFormatter makes for zero options.
 *
 * THE DEFAULT IS "NOTHING CONFIGURED", and that is a CHOICE rather than a copy: Apple publishes no
 * default for either style (checked against the two property pages — both describe what the style
 * DOES and neither states an initial value), so under §11.6 gate 1 any value conforms. The choice is
 * NoStyle/NoStyle because it keeps this class consistent with NSDateFormatter here: a formatter that
 * was never told what to render says so by answering nil, instead of inventing a shape.
 *
 * THE FOUR RESETTABLE PROPERTIES (`dateTemplate`, `calendar`, `locale`, `timeZone`) FOLLOW APPLE'S
 * `null_resettable`: setting nil is not "no value", it is "back to the default" — the current locale,
 * the current calendar and the current zone respectively, resolved when the formatter is built.
 *
 * NULLABILITY ON THE ICU-BACKED FAMILY: a formatter that could not be opened, or whose ICU call
 * failed, answers nil, as NSDateFormatter's own string door does.
 */

#ifndef FOUNDATION_NSDATEINTERVALFORMATTER_H
#define FOUNDATION_NSDATEINTERVALFORMATTER_H

#import <Foundation/NSFormatter.h>

@class NSDate;
@class NSDateInterval;
@class NSCalendar;
@class NSLocale;
@class NSString;
@class NSTimeZone;

NS_ASSUME_NONNULL_BEGIN

/* The styles, and the raw values MIRROR NSDateFormatterStyle's — Apple's own, which that class's
 * header already states it keeps, so a stored integer means the same thing in either formatter. */
typedef enum {
	NSDateIntervalFormatterNoStyle = 0,
	NSDateIntervalFormatterShortStyle = 1,
	NSDateIntervalFormatterMediumStyle = 2,
	NSDateIntervalFormatterLongStyle = 3,
	NSDateIntervalFormatterFullStyle = 4
} NSDateIntervalFormatterStyle;

@interface NSDateIntervalFormatter : NSFormatter
{
	NSDateIntervalFormatterStyle _dateStyle;
	NSDateIntervalFormatterStyle _timeStyle;
	NSString *_dateTemplate;
	NSCalendar *_calendar;
	NSLocale *_locale;
	NSTimeZone *_timeZone;
	void *_formatter;		/* UDateIntervalFormat *, rebuilt by every setting */
}

/* The style to use when formatting day, month, and year information. */
- (NSDateIntervalFormatterStyle)dateStyle;
- (void)setDateStyle:(NSDateIntervalFormatterStyle)style;

/* The style to use when formatting hour, minute, and second information. */
- (NSDateIntervalFormatterStyle)timeStyle;
- (void)setTimeStyle:(NSDateIntervalFormatterStyle)style;

/* The template for formatting one date and time value — a SKELETON whose fields the locale arranges.
 * Setting it replaces both styles; setting it to nil restores them. */
- (nullable NSString *)dateTemplate;
- (void)setDateTemplate:(nullable NSString *)value;

/* The calendar to use for date values. nil means the current calendar. */
- (NSCalendar *)calendar;
- (void)setCalendar:(nullable NSCalendar *)value;

/* The locale to use when formatting the values. nil means the current locale. */
- (NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)value;

/* The time zone with which to specify time values. nil means the current zone. */
- (NSTimeZone *)timeZone;
- (void)setTimeZone:(nullable NSTimeZone *)value;

/* The range. Two dates that are out of order are NOT repaired — Apple's contract is a pair the caller
 * supplies, and swapping them silently would answer a question nobody asked. */
- (nullable NSString *)stringFromDate:(NSDate *)fromDate toDate:(NSDate *)toDate;

/* The same thing from an interval object: ONE date-pair door with two spellings, so a caller that
 * already holds an NSDateInterval does not have to take it apart. */
- (nullable NSString *)stringFromDateInterval:(NSDateInterval *)dateInterval;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDATEINTERVALFORMATTER_H */
