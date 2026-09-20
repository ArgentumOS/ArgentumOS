/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTimeZone — a zone, by NAME or by offset. docs/design/foundation-plan.md, F7 and §10.
 *
 * THIS WAS THE PLAN'S CLEAREST REFUSAL, and F13 takes it back. F7 shipped an offset in seconds and
 * refused the database by name, with the reason written down: "hundreds of IANA identifiers, an
 * abbreviation dictionary, and a transition table that differs per zone and is revised several
 * times a year by governments". The reason was CORRECT — and §10 is the answer to it: ICU ships
 * exactly that table, so this class now reads it instead of refusing it.
 *
 * WHAT CHANGED:
 *   * a zone can be made BY NAME (`+timeZoneWithName:`), and -name answers the IANA identifier;
 *   * -secondsFromGMTForDate: is DATE-DEPENDENT for a named zone, because daylight saving is what a
 *     name buys. For a fixed-offset zone it is still the constant it always was — that is not a
 *     shortcut, it is the definition;
 *   * -isDaylightSavingTimeForDate: answers from the transition rules instead of NO;
 *   * +knownTimeZoneNames enumerates the database, and the DST transition after a date can be asked
 *     for.
 *
 * WHAT IS STILL REFUSED, named here rather than left to be discovered:
 *   * `+abbreviationDictionary` / `+timeZoneWithAbbreviation:` — Apple's is a CURATED map from an
 *     abbreviation to ONE chosen zone ("EST" -> "America/New_York"). ICU has abbreviations but not
 *     that curation, so deriving one would be a guess dressed as data: it is its own sub-step;
 *   * the deprecated `+timeZoneWithName:data:` / `-data` pair — Apple's blob is a serialization
 *     format, and belongs to the coder family this library has not built.
 */

#ifndef FOUNDATION_NSTIMEZONE_H
#define FOUNDATION_NSTIMEZONE_H

#import <Foundation/NSObject.h>

@class NSDate;
@class NSArray;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSTimeZone : NSObject <NSCopying>
{
	NSString *_name;		/* the IANA identifier; nil for a fixed-offset zone */
	NSInteger _secondsFromGMT;	/* the offset, and for a fixed zone the whole story */
}

/* NULLABLE, all of them, for the reason the F6 notes give: each goes through an initialiser that
 * answers nil when [super init] does, and -initWithName: also answers nil for a name the database
 * does NOT know — which is Apple's answer for an unknown name as well. */
+ (nullable NSTimeZone *)timeZoneForSecondsFromGMT:(NSInteger)seconds;
+ (nullable NSTimeZone *)timeZoneWithName:(NSString *)name;
+ (nullable NSTimeZone *)systemTimeZone;	/* ICU's default zone, which follows TZ */
+ (nullable NSTimeZone *)localTimeZone;		/* no per-app override ships: this is the system's */

- (nullable id)initWithSecondsFromGMT:(NSInteger)seconds;
- (nullable id)initWithName:(NSString *)name;

/* THE DATABASE, enumerated: the IANA identifiers ICU knows. */
+ (NSArray *)knownTimeZoneNames;

/* For a NAMED zone the offset depends on the instant (that is DST); for a fixed-offset zone the
 * date is not consulted, and -secondsFromGMT without a date means "now", as in Cocoa. */
- (NSInteger)secondsFromGMT;
- (NSInteger)secondsFromGMTForDate:(NSDate *)date;

/* The IANA identifier for a named zone; the rendered offset ("GMT", "GMT+0530") for a fixed one,
 * which is all such a zone can say. */
- (NSString *)name;

- (BOOL)isDaylightSavingTime;
- (BOOL)isDaylightSavingTimeForDate:(NSDate *)date;

/* The next transition in the zone's rules, or nil when there is none — which includes every
 * fixed-offset zone, because a transition IS a table entry. */
- (nullable NSDate *)nextDaylightSavingTimeTransition;
- (nullable NSDate *)nextDaylightSavingTimeTransitionAfterDate:(NSDate *)date;

- (BOOL)isEqualToTimeZone:(NSTimeZone *)other;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

- (id)copy;

@end

/* HOW A ZONE RENDERS ITS NAME (2026-09-20): "GMT+05:30" or "India Standard Time",
 * long or abbreviated, standard or daylight. Names from Apple's documentation index;
 * values are ours (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSTimeZoneNameStyleStandard = 0,
	NSTimeZoneNameStyleShortStandard = 1,
	NSTimeZoneNameStyleDaylightSaving = 2,
	NSTimeZoneNameStyleShortDaylightSaving = 3,
	NSTimeZoneNameStyleGeneric = 4,
	NSTimeZoneNameStyleShortGeneric = 5
} NSTimeZoneNameStyle;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTIMEZONE_H */
