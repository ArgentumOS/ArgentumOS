/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateFormatter — a date as text, and text as a date, in a locale's own conventions.
 * docs/design/foundation-plan.md §10 (the un-refusal program), slice F13.
 *
 * THIS CLASS IS EXACTLY WHAT F7 REFUSED. "The parser and formatter family" was refused because
 * formats ARE a table — CLDR's patterns, month names, era names, and a locale's idea of where the
 * day goes. That was true, and it is why ICU is now a dependency (§10): the table ships, it is
 * simply not ours to write. Nothing in this file encodes a format.
 *
 * WHAT SHIPS HERE (F13.6). The class, the style pair, the locale and the zone, patterns, both
 * directions (format and parse), leniency, the NSFormatter door, and CLDR's SKELETON machinery
 * (`+dateFormatFromTemplate:options:locale:` asks for the FIELDS and lets the locale order them).
 *
 * WHAT F13.7 ADDS, named here so the gap is not discovered later: `-calendar:`/`-setCalendar:`
 * (it needs the non-Gregorian calendars, which is that slice's own work), the SYMBOL arrays
 * (`-monthSymbols`, `-weekdaySymbols`, `-eraSymbols`, and their standalone/short variants), and
 * the formatter-behaviour knobs (`-formatterBehavior`, `-defaultDate`, `-twoDigitStartDate`,
 * `-generatesCalendarDates`, `-setLocalizedDateFormatFromTemplate:`).
 *
 * STORAGE: the ICU formatter is an opaque `void *` on purpose. This header is staged for an
 * ON-GUEST Objective-C rebuild (foundation-plan §4.3), and a public header that included
 * <unicode/udat.h> would drag ICU's headers onto that guest. The handle is opened and REOPEND in
 * the implementation, which is the only file that knows ICU exists.
 */

#ifndef FOUNDATION_NSDATEFORMATTER_H
#define FOUNDATION_NSDATEFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSObjCRuntime.h>

@class NSString;
@class NSDate;
@class NSLocale;
@class NSTimeZone;
@class NSCalendar;
@class NSArray;

/* Apple's styles, and the raw values are APPLE'S so a stored integer still means the same thing. */
typedef enum {
	NSDateFormatterNoStyle = 0,
	NSDateFormatterShortStyle = 1,
	NSDateFormatterMediumStyle = 2,
	NSDateFormatterLongStyle = 3,
	NSDateFormatterFullStyle = 4
} NSDateFormatterStyle;

NS_ASSUME_NONNULL_BEGIN

@interface NSDateFormatter : NSFormatter
{
	void *_formatter;			/* a UDateFormat *; opaque so this header needs no ICU */
	NSDateFormatterStyle _dateStyle;
	NSDateFormatterStyle _timeStyle;
	BOOL _lenient;
	NSString *_pattern;			/* nil = the styles decide */
	NSLocale *_locale;			/* nil = +[NSLocale currentLocale] at build time */
	NSTimeZone *_timeZone;			/* nil = the system zone */
	NSCalendar *_calendar;			/* nil = the system calendar (Gregorian) */
}

- (instancetype)init;

/* ONE DATE, ONE CALL, no formatter to keep: Apple's convenience, and the reason a caller does not
 * have to build one for a single string. */
+ (nullable NSString *)localizedStringFromDate:(NSDate *)date
				     dateStyle:(NSDateFormatterStyle)dateStyle
				     timeStyle:(NSDateFormatterStyle)timeStyle;

/* CLDR'S SKELETONS, which are not patterns: `@"yMMMd"` asks for a year, a month and a day and
 * lets the LOCALE decide the order, the separators and the widths. That is a data question, so it
 * is answered by the data. `options` is Apple's reserved bitmask and is ignored here. */
+ (nullable NSString *)dateFormatFromTemplate:(NSString *)template
				      options:(NSUInteger)options
				       locale:(nullable NSLocale *)locale;

/* BOTH DIRECTIONS. `-stringFromDate:` answers nil for a nil date, and the EMPTY STRING when the
 * formatter has been given nothing to say (no pattern and no style) — that is Apple's behaviour
 * for a formatter with no fields requested, and it is not an error. `-dateFromString:` answers nil
 * when the text does not parse. */
- (nullable NSString *)stringFromDate:(NSDate *)date;
- (nullable NSDate *)dateFromString:(NSString *)string;

/* A PATTERN OVERRIDES THE STYLES (ICU's own rule, and Apple's): set one and the style pair stops
 * mattering until it is cleared with nil. */
- (nullable NSString *)dateFormat;
- (void)setDateFormat:(nullable NSString *)string;
- (NSDateFormatterStyle)dateStyle;
- (void)setDateStyle:(NSDateFormatterStyle)style;
- (NSDateFormatterStyle)timeStyle;
- (void)setTimeStyle:(NSDateFormatterStyle)style;

- (nullable NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)locale;
- (nullable NSTimeZone *)timeZone;
- (void)setTimeZone:(nullable NSTimeZone *)timeZone;

/* LENIENCY IS ABOUT PARSING: a lenient parse accepts text whose fields overrun and guesses at the
 * reading; a strict one (the default is LENIENT, as in Cocoa) refuses it. */
- (BOOL)isLenient;
- (void)setLenient:(BOOL)flag;

/* THE CALENDAR, and the reason this class needed a shared keyword map (NSCalendar.h): setting it
 * puts `@calendar=<keyword>` into the formatter's locale, so every calendar NSCalendar supports is
 * one a DATE FORMATTER can render — including the ones whose names are data (the Hebrew months, the
 * Japanese eras). nil means the system's calendar. */
- (nullable NSCalendar *)calendar;
- (void)setCalendar:(nullable NSCalendar *)calendar;

/* THE SYMBOL ARRAYS, read out of the formatter's own data: the month, weekday, era and quarter
 * names and AM/PM, in the locale's spellings and its own COUNT — a thirteen-month Hebrew year has
 * thirteen month symbols, which is the kind of thing a hand-written table gets wrong. */
- (nullable NSArray *)eraSymbols;
- (nullable NSArray *)monthSymbols;
- (nullable NSArray *)shortMonthSymbols;
- (nullable NSArray *)veryShortMonthSymbols;
- (nullable NSArray *)standaloneMonthSymbols;
- (nullable NSArray *)weekdaySymbols;
- (nullable NSArray *)shortWeekdaySymbols;
- (nullable NSArray *)veryShortWeekdaySymbols;
- (nullable NSArray *)standaloneWeekdaySymbols;
- (nullable NSArray *)quarterSymbols;
- (nullable NSArray *)shortQuarterSymbols;
- (nullable NSString *)amSymbol;
- (nullable NSString *)pmSymbol;

/* CLDR'S SKELETON, APPLIED: the same question +dateFormatFromTemplate:options:locale: answers, but
 * asked of THIS formatter, so its locale, calendar and zone decide the pattern. */
- (void)setLocalizedDateFormatFromTemplate:(NSString *)template;

/* THE BEHAVIOUR KNOBS. Only ONE behaviour exists here and it is the modern one — Apple's
 * NSDateFormatterBehavior10_4, which is what NSDateFormatterBehaviorDefault MEANS on any current
 * system. The legacy behaviour is refused rather than accepted and ignored. */
typedef enum {
	NSDateFormatterBehaviorDefault = 0,
	NSDateFormatterBehavior10_0 = 1000,
	NSDateFormatterBehavior10_4 = 1040
} NSDateFormatterBehavior;

- (NSDateFormatterBehavior)formatterBehavior;
- (void)setFormatterBehavior:(NSDateFormatterBehavior)behavior;
/* NO, always: this class answers NSDate values and nothing else. */
- (BOOL)generatesCalendarDates;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDATEFORMATTER_H */
