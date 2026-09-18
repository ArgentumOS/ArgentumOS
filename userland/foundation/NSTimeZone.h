/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTimeZone — a FIXED OFFSET from UTC. docs/design/foundation-plan.md, F7.
 *
 * THE BOUNDARY IS A RULE, NOT A TABLE, and a time zone is where that matters
 * most: Cocoa's NSTimeZone is a door to tzdata — hundreds of IANA identifiers,
 * an abbreviation dictionary, and a transition table that differs per zone and
 * is revised several times a year by governments. This library ships no such
 * data, so it ships what is a RULE: an offset in seconds. Everything that would
 * need the database is refused by name rather than half-answered (the header
 * below names them), which is the same call NSLocale made for collation.
 *
 * WHAT FOLLOWS FROM A FIXED OFFSET, and is stated rather than implied:
 *   - a day is exactly 86400 seconds, so NSCalendar's arithmetic never has a
 *     transition to step over;
 *   - -isDaylightSavingTime answers NO. Not "unknown": a fixed offset has no
 *     transition rules, so there is no DST to be in. (A zone that DID observe it
 *     would need the table.)
 *   - +systemTimeZone and +localTimeZone are UTC. The honest reading is that this
 *     system has no configured zone to report: the kernel clock is UTC, and
 *     guessing a user's from the environment reaches for data nothing sets.
 */

#ifndef FOUNDATION_NSTIMEZONE_H
#define FOUNDATION_NSTIMEZONE_H

#import <foundation/NSObject.h>

@class NSDate;
@class NSArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSTimeZone : NSObject <NSCopying>
{
	NSInteger _secondsFromGMT;
}

/* The two ways to make one. `+timeZoneForSecondsFromGMT:` is the ONLY
 * constructor that takes data; the system/local ones are UTC by the note above. */
/* NULLABLE, all four, for the same reason NSError's constructors are: each goes
 * through -initWithSecondsFromGMT:, which answers nil when [super init] does
 * (nstimezone.m). The annotation states what the WRITER does. */
+ (nullable NSTimeZone *)timeZoneForSecondsFromGMT:(NSInteger)seconds;
+ (nullable NSTimeZone *)systemTimeZone;		/* UTC — see the header note */
+ (nullable NSTimeZone *)localTimeZone;		/* UTC — see the header note */

- (nullable id)initWithSecondsFromGMT:(NSInteger)seconds;

/* The offset is a constant, so the date-taking form answers the same thing for
 * every date. It exists because Cocoa's callers use it, and a fixed-offset zone
 * is exactly the case where that is well defined. */
- (NSInteger)secondsFromGMT;
- (NSInteger)secondsFromGMTForDate:(NSDate *)date;

/* "-name" is OURS and says the offset: "GMT", "GMT+0530". Cocoa's is an IANA
 * identifier, which is the database this class does not have. */
- (NSString *)name;

/* NO. See the header note: a fixed offset has no transition rules. */
- (BOOL)isDaylightSavingTime;
- (BOOL)isDaylightSavingTimeForDate:(NSDate *)date;

- (BOOL)isEqualToTimeZone:(NSTimeZone *)other;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

- (id)copyWithZone:(nullable NSZone *)zone;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTIMEZONE_H */
