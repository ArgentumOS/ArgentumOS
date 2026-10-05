/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateComponentsFormatter — a duration, said the way a locale says durations.
 * docs/design/foundation-plan.md §12.3 W11 and §29/§29.1 (W11b's design pass and its measurement).
 *
 * THE ONE CLASS IN THIS FAMILY THAT IS A COMPOSITION RATHER THAN A BINDING. Every other W11 formatter
 * hands its whole question to one ICU call. ICU's `MeasureFormat` — the thing that would do that here —
 * has NO C surface in this build (measured: `measfmt.h` and `measure.h` declare zero `U_CAPI`), so this
 * class is built the other way round: **the RULES are ours and the DATA is ICU's.** §29.1 records the
 * measurement that made that viable:
 *
 *   * a unit's name and plurality come from `unumf` with a unit skeleton —
 *     `unit-width-full-name measure-unit/duration-hour` answers "2 hours" and, at 1, "**1 hour**";
 *   * the three widths are full-name / short / narrow ("2 hours" / "2 hr" / "2h"), which is what Apple's
 *     `Full`, `Short` and `Brief` cases describe;
 *   * `unum_open(UNUM_SPELLOUT)` spells the quantity ("two"), which is what `SpellOut` adds.
 * **AND THE SKELETON GRAMMAR IS A TRAP WORTH NAMING: named options are written BARE and only precision
 * stems take a dot.** `unit-width-narrow measure-unit/duration-hour` works;
 * `measure-unit/duration-hour .unit-width-narrow` is a syntax error. The design pass's own first probe
 * got that wrong and concluded the widths were unreachable.
 *
 * WHAT IS OURS, AS THE DESIGN PASS REQUIRED THIS HEADER TO SAY:
 *   1. **the JOINER**: ", " between units. The design pass first proposed the locale's list pattern via
 *      `ulistfmt`, and that is WRONG — `ulistfmt` is the CONJUNCTION formatter and would produce "1 hour
 *      and 2 minutes" where a duration wants "1 hour, 2 minutes". Upstream CLDR keeps these separate; a
 *      comma is punctuation, not list data.
 *   2. **`Positional`**: ICU's numeric/duration skeleton is NOT reachable here (`duration` is a syntax
 *      error), so the clock form is arithmetic — the kept units joined by ":" with every field but the
 *      first zero-padded to two digits.
 *   3. **`collapsesLargestUnit`'s THRESHOLD**, which Apple describes as "a certain threshold" and never
 *      states. This file's reading: with it YES, the largest unit present is not printed at all and is
 *      folded into the next smaller ALLOWED unit (1 hour 30 minutes → "90 minutes").
 *   4. **the two PHRASE options' wording.** Apple publishes each option's EFFECT ("reflect an inexact
 *      time value" / "reflect the amount of time remaining") and not its words, and the words are
 *      per-locale resources of Apple's own rather than CLDR data this build can reach. So the affixes are
 *      ENGLISH ("about ", " left") and that is a REGISTERED DEVIATION (§11.6) rather than a silent one.
 *   5. **the properties' DEFAULT values**, unpublished except for `allowedUnits` and `referenceDate`.
 *
 * TWO MEMBERS APPLE'S OWN PAGES SETTLE, SO NEITHER IS A JUDGEMENT CALL:
 *   * **`formattingContext`** — Apple's abstract is literally **"Not yet supported."** The property ships
 *     and does nothing, and that is Apple's statement rather than our excuse.
 *   * **`-getObjectValue:forString:errorDescription:`** — *"currently only implements formatting, not
 *     parsing. Until it implements parsing, this will always return NO."* It returns NO; it does NOT
 *     raise, which is what `NSFormatter`'s base does and what this class must not.
 *
 * THE ZERO BEHAVIOURS ARE PUBLISHED RULES WITH WORKED EXAMPLES, and this file implements each of Apple's
 * seven definitions as written — including `DropMiddle` ("units whose values are 0 are dropped from
 * anywhere in the middle of a sequence") and `Pad` ("pads values with zeroes as appropriate"). `Default`
 * is the one Apple's text describes only for the positional case, so this file reads it as drop-leading
 * AND drop-trailing for the other styles and says so.
 */

#ifndef FOUNDATION_NSDATECOMPONENTSFORMATTER_H
#define FOUNDATION_NSDATECOMPONENTSFORMATTER_H

#import <Foundation/NSFormatter.h>
/* The parameter type of the time-interval door, and the type of allowedUnits: both are scalars defined
 * in headers NSFormatter.h does not reach. */
#import <Foundation/NSDate.h>
#import <Foundation/NSCalendar.h>

@class NSDateComponents;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* Apple's order, so a stored integer keeps its meaning. */
typedef enum {
	NSDateComponentsFormatterUnitsStylePositional = 0,
	NSDateComponentsFormatterUnitsStyleAbbreviated = 1,
	NSDateComponentsFormatterUnitsStyleShort = 2,
	NSDateComponentsFormatterUnitsStyleFull = 3,
	NSDateComponentsFormatterUnitsStyleSpellOut = 4,
	NSDateComponentsFormatterUnitsStyleBrief = 5
} NSDateComponentsFormatterUnitsStyle;

/* A bitmask, and `DropAll` is an OR of its three members rather than a bit of its own — which is what
 * "drops all units whose values are 0" means. */
typedef unsigned long NSDateComponentsFormatterZeroFormattingBehavior;

#define NSDateComponentsFormatterZeroFormattingBehaviorNone		(0UL)
#define NSDateComponentsFormatterZeroFormattingBehaviorDefault		(1UL << 0)
#define NSDateComponentsFormatterZeroFormattingBehaviorDropLeading	(1UL << 1)
#define NSDateComponentsFormatterZeroFormattingBehaviorDropMiddle	(1UL << 2)
#define NSDateComponentsFormatterZeroFormattingBehaviorDropTrailing	(1UL << 3)
#define NSDateComponentsFormatterZeroFormattingBehaviorDropAll		\
	(NSDateComponentsFormatterZeroFormattingBehaviorDropLeading |	\
	 NSDateComponentsFormatterZeroFormattingBehaviorDropMiddle |	\
	 NSDateComponentsFormatterZeroFormattingBehaviorDropTrailing)
#define NSDateComponentsFormatterZeroFormattingBehaviorPad		(1UL << 16)

@interface NSDateComponentsFormatter : NSFormatter
{
	NSCalendarUnit _allowedUnits;
	NSDateComponentsFormatterUnitsStyle _unitsStyle;
	NSDateComponentsFormatterZeroFormattingBehavior _zeroFormattingBehavior;
	NSInteger _maximumUnitCount;
	NSCalendar *_calendar;
	NSDate *_referenceDate;
	NSFormattingContext _formattingContext;
	BOOL _allowsFractionalUnits;
	BOOL _collapsesLargestUnit;
	BOOL _includesApproximationPhrase;
	BOOL _includesTimeRemainingPhrase;
}

/* THE FIVE DOORS. The three conversions, the object door, and the localised class shortcut. */
- (nullable NSString *)stringFromDateComponents:(NSDateComponents *)components;
- (nullable NSString *)stringFromDate:(NSDate *)startDate toDate:(NSDate *)endDate;
- (nullable NSString *)stringFromTimeInterval:(NSTimeInterval)timeInterval;
- (nullable NSString *)stringForObjectValue:(nullable id)object;

/* The whole operation with no object to configure. */
+ (nullable NSString *)localizedStringFromDateComponents:(NSDateComponents *)components
					      unitsStyle:(NSDateComponentsFormatterUnitsStyle)unitsStyle;

/* APPLE'S OWN DOCUMENTED ANSWER, quoted in the file header: this class "only implements formatting, not
 * parsing", so this returns NO — and it ANSWERS rather than raising, which is the difference between it
 * and the abstract base it overrides. */
- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error;

/* The bitmask of calendrical units such as day and month to include in the output string. A zero mask
 * means the default set: years through seconds. */
- (NSCalendarUnit)allowedUnits;
- (void)setAllowedUnits:(NSCalendarUnit)units;

/* The formatting style for unit names. */
- (NSDateComponentsFormatterUnitsStyle)unitsStyle;
- (void)setUnitsStyle:(NSDateComponentsFormatterUnitsStyle)style;

/* The formatting style for units whose value is 0, implemented case by case from Apple's definitions. */
- (NSDateComponentsFormatterZeroFormattingBehavior)zeroFormattingBehavior;
- (void)setZeroFormattingBehavior:(NSDateComponentsFormatterZeroFormattingBehavior)behavior;

/* The maximum number of time units to include in the output string. Zero means no limit. */
- (NSInteger)maximumUnitCount;
- (void)setMaximumUnitCount:(NSInteger)count;

/* The default calendar to use. nil means the current calendar. */
- (NSCalendar *)calendar;
- (void)setCalendar:(nullable NSCalendar *)value;

/* Where units of variable length are measured from: Apple's page says the calculation runs "as though
 * counting from the date specified", and that a nil value means NOW — "the default behavior". */
- (nullable NSDate *)referenceDate;
- (void)setReferenceDate:(nullable NSDate *)value;

/* WHERE THE TEXT APPEARS — and Apple's abstract for this one is "Not yet supported." The property
 * exists, is stored and does nothing, which is Apple's own statement of its state. */
- (NSFormattingContext)formattingContext;
- (void)setFormattingContext:(NSFormattingContext)context;

/* Whether non-integer units may be used for values. */
- (BOOL)allowsFractionalUnits;
- (void)setAllowsFractionalUnits:(BOOL)value;

/* Whether to collapse the largest unit into smaller units. See the file header for the threshold this
 * file implements, which Apple does not publish. */
- (BOOL)collapsesLargestUnit;
- (void)setCollapsesLargestUnit:(BOOL)value;

/* An inexact time value: "about 1 hour". THE AFFIX IS ENGLISH HERE, and that is a registered deviation
 * — see the file header. */
- (BOOL)includesApproximationPhrase;
- (void)setIncludesApproximationPhrase:(BOOL)value;

/* The amount of time remaining: "1 hour left". Same deviation, same note. */
- (BOOL)includesTimeRemainingPhrase;
- (void)setIncludesTimeRemainingPhrase:(BOOL)value;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDATECOMPONENTSFORMATTER_H */
