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
 *     that curation, so deriving one would be a guess dressed as data. **AND THE REFUSAL STANDS NOW THAT
 *     `+knownTimeZoneNames` AND `-abbreviation` BOTH WORK (§63.125): a scan of the database for a zone
 *     whose abbreviation is "EST" answers WHICHEVER ZONE COMES FIRST, which is the same guess with more
 *     arithmetic behind it.** The row is DECLINED in the sweep's own ledger, not left open;
 *   * ⚠ AND ONE REFUSAL IN THIS NOTE WAS WRONG AND IS CORRECTED RATHER THAN LEFT: `+timeZoneWithName:data:`,
 *     `-initWithName:data:` and `-data` were listed here as DEPRECATED. **THEY ARE NOT — Apple's own header
 *     carries no `API_DEPRECATED` on any of them — AND THEY ARE NOW IMPLEMENTED** (§63.124), with the data's
 *     format documented on the declarations themselves. *A stale note is a wrong note, and this one would
 *     have told the next reader that a shipped family was refused.*
 */

#ifndef FOUNDATION_NSTIMEZONE_H
#define FOUNDATION_NSTIMEZONE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>	/* the type of -data and of the data-taking doors */
#import <Foundation/NSDate.h>	/* named by the ...ForDate: doors below */
#import <Foundation/NSLocale.h>	/* named by -localizedName:locale: below */

@class NSDate;
@class NSArray;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSTimeZone : NSObject <NSCopying>
{
	NSString *_name;		/* the IANA identifier; nil for a fixed-offset zone */
	NSInteger _secondsFromGMT;	/* the offset, and for a fixed zone the whole story */
	NSData *_data;		/* OURS: the blob -data answers, set by the data-taking doors */
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

@interface NSTimeZone (NSExtendedTimeZone)
/* ⚠⚠ FOUR OF `NSTimeZone`'S TEN, THROUGH ICU — WHICH THIS CLASS ALREADY IS ("ICU's default zone", says its own
 * `+systemTimeZone`). **AND ONE ICU DISCOVERY IS RECORDED HERE RATHER THAN HIDDEN: APPLE PUBLISHES SIX
 * `NSTimeZoneNameStyle` CASES WITH THEIR MEANINGS, AND ICU HAS NO GENERIC DISPLAY NAME AT ALL** —
 * `UCalendarDisplayNameType` carries `UCAL_STANDARD`, `UCAL_SHORT_STANDARD`, `UCAL_DST` and `UCAL_SHORT_DST` and
 * NOTHING for “Central Time” or “CT”. **THE TWO GENERIC STYLES ANSWER THE STANDARD NAME — a choice of ours, written
 * down (§11.6.1 D2), because the alternative is answering nil to a caller who asked for a name a human would
 * recognise.** */
- (nullable NSString *)abbreviation;
- (nullable NSString *)abbreviationForDate:(NSDate *)date;
- (NSTimeInterval)daylightSavingTimeOffsetForDate:(NSDate *)date;
- (nullable NSString *)localizedName:(NSTimeZoneNameStyle)style locale:(nullable NSLocale *)locale;
@property (class, copy) NSTimeZone *defaultTimeZone;
/* ⚠⚠ FOUR MORE ROWS, AND THE DATA-TAKING PAIR IS APPLE'S OWN COMMENT READ FIRST: "Primary creation method is
 * `+timeZoneWithName:`; THE DATA-TAKING VARIANTS SHOULD RARELY BE USED DIRECTLY" — WHICH IS ALL APPLE PUBLISHES ABOUT
 * THEM, so THE DATA'S CONTENT IS OURS (§11.6.1 D2) AND IS WRITTEN DOWN HERE: **a zone's data is its name and its offset
 * as a small text form (‘name|seconds’), which round-trips through this library and is honestly NOT Apple's opaque
 * blob.** A caller who stores our data and reads it back gets the zone it names; a caller who expects Apple's format
 * has a format this system does not have — the same boundary as every other format here.
 *
 * ⚠ AND `-data` ANSWERS WHAT THE ZONE IS, NOT ONLY WHAT IT WAS GIVEN: a zone made with no data still answers one, and a
 * door that answered nil for the commonest case would be a door that can only answer zero. */
@property (readonly, copy) NSData *data;
+ (nullable instancetype)timeZoneWithName:(NSString *)tzName data:(nullable NSData *)aData;
- (nullable instancetype)initWithName:(NSString *)tzName data:(nullable NSData *)aData;
+ (void)resetSystemTimeZone;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTIMEZONE_H */
