/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSISO8601DateFormatter — a date written in the one format that is not a locale's.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * THIS IS THE ONE FORMATTER IN THE FAMILY WHOSE OUTPUT IS *NOT* DATA. ISO 8601 is a published
 * GRAMMAR — "2026-09-20T12:34:56Z" means the same thing in every locale by definition — so unlike
 * NSDateIntervalFormatter or NSByteCountFormatter there is no CLDR table to bind here, and the
 * formatter's job is to prove the OPTIONS: which fields appear, which separators join them, and
 * whether the date and time are joined by "T" or a space.
 *
 * THE OPTIONS ARE A BITMASK OF FIELDS, and the composites are the shapes people actually want:
 * `WithFullDate` is year-month-day with dashes, `WithFullTime` is the time with a zone, and
 * `WithInternetDateTime` is the two of them — RFC 3339, which is the class's DEFAULT.
 *
 * the values: the bit positions are OURS (§11.6.1 D2, the family's usual position — Apple publishes
 * these names and not their numbers), and they follow the layout Apple's own header uses, including
 * the unused bit 3, because a program that hard-codes a bit position is the only kind of program that
 * could tell the difference and there is no reason to differ from it. Each composite is an OR of its
 * members rather than its own bit, which is also what what a bitmask means.
 *
 * THE TIME ZONE DEFAULTS TO GMT AND IS NOT AN OPTION. Apple's page says so for the property ("When
 * unspecified, GMT is used"), which is why `-stringFromDate:` on a fresh formatter ends in "Z"
 * rather than in the host's offset.
 *
 * NSSecureCoding, as Apple declares: the settings are a bitmask, an owned zone and nothing else.
 */

#ifndef FOUNDATION_NSISO8601DATEFORMATTER_H
#define FOUNDATION_NSISO8601DATEFORMATTER_H

#import <Foundation/NSFormatter.h>
#import <Foundation/NSCoding.h>

@class NSDate;
@class NSString;
@class NSTimeZone;

NS_ASSUME_NONNULL_BEGIN

typedef unsigned long NSISO8601DateFormatOptions;

#define NSISO8601DateFormatWithYear			(1UL << 0)
#define NSISO8601DateFormatWithMonth			(1UL << 1)
#define NSISO8601DateFormatWithWeekOfYear		(1UL << 2)
/* bit 3 is deliberately unused: see the header comment. */
#define NSISO8601DateFormatWithDay			(1UL << 4)
#define NSISO8601DateFormatWithTime			(1UL << 5)
#define NSISO8601DateFormatWithTimeZone			(1UL << 6)
#define NSISO8601DateFormatWithSpaceBetweenDateAndTime	(1UL << 7)
#define NSISO8601DateFormatWithDashSeparatorInDate	(1UL << 8)
#define NSISO8601DateFormatWithColonSeparatorInTime	(1UL << 9)
#define NSISO8601DateFormatWithColonSeparatorInTimeZone	(1UL << 10)
#define NSISO8601DateFormatWithFractionalSeconds	(1UL << 11)

#define NSISO8601DateFormatWithFullDate			\
	(NSISO8601DateFormatWithYear | NSISO8601DateFormatWithMonth | \
	 NSISO8601DateFormatWithDay | NSISO8601DateFormatWithDashSeparatorInDate)

#define NSISO8601DateFormatWithFullTime			\
	(NSISO8601DateFormatWithTime | NSISO8601DateFormatWithTimeZone | \
	 NSISO8601DateFormatWithColonSeparatorInTime | \
	 NSISO8601DateFormatWithColonSeparatorInTimeZone)

#define NSISO8601DateFormatWithInternetDateTime		\
	(NSISO8601DateFormatWithFullDate | NSISO8601DateFormatWithFullTime)

@interface NSISO8601DateFormatter : NSFormatter <NSSecureCoding>
{
	NSISO8601DateFormatOptions _formatOptions;
	NSTimeZone *_timeZone;		/* owned; nil means GMT, decided at build */
	void *_formatter;		/* UDateFormat *, rebuilt by every setting */
}

/* Options for generating and parsing ISO 8601 date representations. Setting them rebuilds the
 * underlying ICU formatter, because a pattern is baked in at open time and cannot be mutated. */
- (NSISO8601DateFormatOptions)formatOptions;
- (void)setFormatOptions:(NSISO8601DateFormatOptions)options;

/* The time zone used to create and parse date representations. When unspecified, GMT is used. */
- (NSTimeZone *)timeZone;
- (void)setTimeZone:(nullable NSTimeZone *)value;

/* The three conversions. A string that cannot be parsed answers nil rather than a guessed date. */
- (nullable NSString *)stringFromDate:(NSDate *)date;
- (nullable NSDate *)dateFromString:(NSString *)string;

/* The same conversion with no object to configure — the whole operation, spelled out. */
+ (nullable NSString *)stringFromDate:(NSDate *)date
			     timeZone:(nullable NSTimeZone *)timeZone
			formatOptions:(NSISO8601DateFormatOptions)formatOptions;

- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSISO8601DATEFORMATTER_H */
