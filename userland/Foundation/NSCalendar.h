/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCalendar — every calendar ICU has, through ICU. docs/design/foundation-plan.md, F7 and §10.
 *
 * THIS CLASS WAS THE PLAN'S SECOND CLEAREST REFUSAL. F7 shipped the Gregorian calendar "as rules"
 * on libc's field arithmetic and refused the rest BY NAME, with the reason written down: Hebrew's
 * moladot, Islamic's sighting convention, Japanese's era list are TABLES, and this library had none
 * of them. The reason was correct — and §10 is the answer to it, which F13.7b is where the answer
 * landed: ICU HAS the tables, so the identifiers are read out of it.
 *
 * WHAT REMAINS A RULE is what was always a rule: the conversion, THE CLAMP (31 January + 1 month is
 * the last day of February), the range questions (how many days does THIS month have, how many
 * months does THIS year have), and the week rule. ICU answers each through one entry point — see
 * nscalendar.m's own note — so this class is a BINDING now, and the arithmetic it used to do by
 * hand is the part that was already provable.
 *
 * A NAMED TIME ZONE now carries its own offset rules with it, because F13.7a gave NSTimeZone the
 * database; `-timeZone` defaults to whatever the system reports.
 */

#ifndef FOUNDATION_NSCALENDAR_H
#define FOUNDATION_NSCALENDAR_H

#import <Foundation/NSObject.h>
#import <Foundation/NSNotification.h>	/* NSNotificationName */

@class NSDate;
@class NSTimeZone;
@class NSDateComponents;

/* NULLABILITY (F6's standing rule): the region opens here and closes at the foot
 * of the file, so every declaration below — the identifier constants, the two
 * typedef'd option sets and the class — is nonnull unless it says otherwise. */
NS_ASSUME_NONNULL_BEGIN

/*
 * EVERY CALENDAR ICU HAS, BY NAME. This list was a list of REFUSALS when F7 wrote it — the era
 * offsets, the molad, the sighting convention, the solstice table — and F13.7b is where those
 * refusals were un-refused: the tables are ICU's now (§10), and `+calendarWithIdentifier:` answers
 * nil only for a name this class does not know at all. The spellings are Cocoa's.
 */
extern NSString *const NSCalendarIdentifierGregorian;
extern NSString *const NSCalendarIdentifierISO8601;
extern NSString *const NSCalendarIdentifierBuddhist;
extern NSString *const NSCalendarIdentifierChinese;
extern NSString *const NSCalendarIdentifierCoptic;
extern NSString *const NSCalendarIdentifierEthiopicAmeteMihret;
extern NSString *const NSCalendarIdentifierEthiopicAmeteAlem;
extern NSString *const NSCalendarIdentifierHebrew;
extern NSString *const NSCalendarIdentifierIndian;
extern NSString *const NSCalendarIdentifierIslamic;
extern NSString *const NSCalendarIdentifierIslamicCivil;
extern NSString *const NSCalendarIdentifierIslamicTabular;
extern NSString *const NSCalendarIdentifierIslamicUmmAlQura;
extern NSString *const NSCalendarIdentifierJapanese;
extern NSString *const NSCalendarIdentifierPersian;
extern NSString *const NSCalendarIdentifierRepublicOfChina;

/* Cocoa's unit bitmask, and the VALUES ARE COCOA'S so a numeric test in existing
 * code still means the same thing. Every unit below is derived from the date
 * fields by a rule; none needs a table. */
typedef enum {
	NSCalendarUnitEra			= (1UL << 1),
	NSCalendarUnitYear			= (1UL << 2),
	NSCalendarUnitMonth			= (1UL << 3),
	NSCalendarUnitDay			= (1UL << 4),
	NSCalendarUnitHour			= (1UL << 5),
	NSCalendarUnitMinute			= (1UL << 6),
	NSCalendarUnitSecond			= (1UL << 7),
	NSCalendarUnitWeekday			= (1UL << 9),
	NSCalendarUnitWeekdayOrdinal		= (1UL << 10),
	NSCalendarUnitQuarter			= (1UL << 11),
	NSCalendarUnitWeekOfMonth		= (1UL << 12),
	NSCalendarUnitWeekOfYear		= (1UL << 13),
	NSCalendarUnitYearForWeekOfYear		= (1UL << 14),
	NSCalendarUnitNanosecond		= (1UL << 15),
	NSCalendarUnitDayOfYear		= (1UL << 8),
	NSCalendarUnitCalendar		= (1UL << 16),
	NSCalendarUnitTimeZone		= (1UL << 17),
	/* THE THREE ABOVE ARE DECLARED SO A CONFORMING PROGRAM COMPILES, AND THE DOOR REFUSES THEM. Apple
	 * declares all three; what this class does not do is FILL them - DayOfYear is a derivation it does not
	 * offer, and Calendar/TimeZone ask for the calendar and zone OBJECTS in the result, which
	 * NSDateComponents does not carry. A refusal belongs in the door that would answer, not in a missing
	 * name: refusing to declare them only broke callers who spelled a name Apple's header spells. */
	NSCalendarUnitCount			= 0
} NSCalendarUnit;

/* The match options -nextDateAfterDate:matchingComponents:options: takes. None is honoured: this calendar
 * answers by arithmetic rather than by searching a table of candidate dates, so each one names a search this
 * class does not perform - which is why they are here with their reasons and the door refuses them. */
typedef enum {
	NSCalendarMatchStrictly				= (1UL << 8),
	NSCalendarMatchPreviousTimePreservingSmallerUnits = (1UL << 9),
	NSCalendarMatchNextTime				= (1UL << 10),
	NSCalendarMatchNextTimePreservingSmallerUnits	= (1UL << 11),
	NSCalendarMatchLast				= (1UL << 12),
	NSCalendarMatchFirst				= (1UL << 13)
} NSCalendarMatchOptions;

/* THE CALENDAR IDENTIFIERS, AS WIRE STRINGS: Apple's value for NSCalendarIdentifierGregorian is @"gregorian",
 * which is what this class already accepts, and every identifier below follows the same lowercase rule. THE
 * IMPLEMENTATION SUPPORTS THE GREGORIAN ONE ONLY - -initWithCalendarIdentifier: recognises these names and
 * refuses the rest - so the names ship as vocabulary and the refusal stays where the work would be. */
typedef NSString *NSCalendarIdentifier;


/*
 * The options -dateByAdding… takes. ZERO is the only honoured value: the other
 * two are named and REFUSED with NSInvalidArgumentException, because
 * `wrapComponents` and `searchBackwards` describe a search over a table of
 * candidate dates, and this calendar answers by arithmetic instead.
 */
typedef enum {
	NSCalendarOptionsNone		= 0,
	NSCalendarOptionsWrapComponents	= (1UL << 0),		/* refused */
	NSCalendarOptionsSearchBackwards = (1UL << 2)		/* refused */
} NSCalendarOptions;

/* Apple's spelling of the two options this class already had under its own names, aliases rather than
 * renames because the house names have callers and Apple's have to compile. */
typedef enum {
	NSCalendarWrapComponents		= NSCalendarOptionsWrapComponents,
	NSCalendarSearchBackwards		= NSCalendarOptionsSearchBackwards
} NSCalendarOptionAliases;

@interface NSCalendar : NSObject <NSCopying>
{
	NSString *_identifier;
	NSTimeZone *_timeZone;
	NSUInteger _firstWeekday;		/* 1 = Sunday, ... 7 = Saturday */
	NSUInteger _minimumDaysInFirstWeek;
}

/* +currentCalendar is the Gregorian one with its defaults: UTC, Sunday first,
 * one day minimum. Cocoa's reads the user's settings, which are a database this
 * library does not have (the same call NSLocale made). */
+ (nullable NSCalendar *)currentCalendar;
/* nil for an identifier this library does not implement — see the list above. */
+ (nullable NSCalendar *)calendarWithIdentifier:(NSString *)identifier;

/* -init is inherited from NSObject and is NOT re-declared: a nullable re-declaration
 * of it conflicts with the inherited nonnull specifier (the warning F10's first build
 * named, after F7 had carried it silently). */
- (nullable id)initWithCalendarIdentifier:(NSString *)identifier;	/* nil for the refused ones */

- (NSString *)identifier;

/* A fixed-offset time zone; never nil (the default is UTC). */
- (NSTimeZone *)timeZone;
- (void)setTimeZone:(NSTimeZone *)zone;

/* The two week RULES. Sunday/1 are the Gregorian defaults. */
- (NSUInteger)firstWeekday;
- (void)setFirstWeekday:(NSUInteger)weekday;
- (NSUInteger)minimumDaysInFirstWeek;
- (void)setMinimumDaysInFirstWeek:(NSUInteger)days;

/* CONVERSION, both ways. -components:fromDate: fills ONLY the units asked for
 * and leaves the rest UNDEFINED; the week-based units are derived here (they are
 * answers, not inputs) using firstWeekday and minimumDaysInFirstWeek. */
- (NSDateComponents *)components:(NSCalendarUnit)units fromDate:(NSDate *)date;
/* THE FIELD-WISE DIFFERENCE — the method F7 refused BY NAME, because its option semantics are "a
 * table of cases". ICU HAS that table (ucal_getFieldDifference), which is the whole reason §10
 * exists. Each requested field is counted in turn, LARGEST FIRST, and the calendar is walked
 * forward as they are taken, so "1 month and 1 day" is MEASURED rather than divided.
 *
 * `options` takes NSCalendarOptionsNone here: asking the smaller units to WRAP instead of borrow is
 * a different question, and ICU's difference does not answer it — refused by name rather than
 * approximated. */
- (NSDateComponents *)components:(NSCalendarUnit)units
			fromDate:(NSDate *)startingDate
			  toDate:(NSDate *)resultDate
			 options:(NSCalendarOptions)options;
/* nil for a component that is not a date: one that sets only the WEEK fields
 * (they are answers, not a second way to say when) or no year at all. */
- (nullable NSDate *)dateFromComponents:(NSDateComponents *)components;

/* ARITHMETIC. `components` carries the quantity per unit — year 1, month -2,
 * day 7 — and the result is NORMALISED: adding a month to the 31st lands on the
 * last day of the target month (2026-01-31 + 1 month = 2026-02-28, or the 29th in
 * a leap year), and adding days lets libc roll the fields. */
- (NSDate *)dateByAddingComponents:(NSDateComponents *)components
			    toDate:(NSDate *)date
			   options:(NSCalendarOptions)options;
- (NSDate *)dateByAddingUnit:(NSCalendarUnit)unit
		       value:(NSInteger)value
		      toDate:(NSDate *)date
		     options:(NSCalendarOptions)options;

/* RANGES. -rangeOfUnit:inUnit:forDate: answers a count: of days in a month, of
 * months in a year, of hours in a day (always 24 here — a fixed offset has no
 * transition). The location is 1 where the count starts at 1 (months, days) and 0
 * where it starts at 0 (hours). */
- (NSRange)rangeOfUnit:(NSCalendarUnit)smaller
		inUnit:(NSCalendarUnit)larger
	       forDate:(NSDate *)date;
/* The same question asked as a start and a length, which is what a caller
 * iterating a calendar usually wants. Both out-parameters may be NULL. */
- (BOOL)rangeOfUnit:(NSCalendarUnit)unit
	  startDate:(NSDate * _Nullable * _Nullable)datep
	   interval:(double * _Nullable)tip
	    forDate:(NSDate *)date;

- (BOOL)isDate:(NSDate *)date inSameDayAsDate:(NSDate *)other;

- (BOOL)isEqualToCalendar:(NSCalendar *)other;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

/* A calendar is MUTABLE (its time zone and week rules), so a copy is a real one
 * rather than `self`. */
- (id)copy;

@end

/*
 * THE PRIVATE HALF, folded in from fncalendar.h: the declarations this library shares internally.
 * They are HERE because the public headers are now the only headers - what used to be a
 * private file two units imported is a section of the class's own header. The region is its
 * own one only when this point in the header is outside the header's own (a nested region
 * does not compile, and neither does an unclosed one).
 */
/* The ICU keyword, or NULL for an identifier this library does not know. A C string rather than an
 * object because every caller feeds it straight into a locale string. */
const char * _Nullable fn_calendar_keyword(NSString *identifier);


/* Apple's name for the day-changed notification, spelled with the type the tree's notifications use. */
extern NSNotificationName const NSCalendarDayChangedNotification;

extern NSCalendarIdentifier const NSCalendarIdentifierBangla;
extern NSCalendarIdentifier const NSCalendarIdentifierDangi;
extern NSCalendarIdentifier const NSCalendarIdentifierGujarati;
extern NSCalendarIdentifier const NSCalendarIdentifierKannada;
extern NSCalendarIdentifier const NSCalendarIdentifierMalayalam;
extern NSCalendarIdentifier const NSCalendarIdentifierMarathi;
extern NSCalendarIdentifier const NSCalendarIdentifierOdia;
extern NSCalendarIdentifier const NSCalendarIdentifierTamil;
extern NSCalendarIdentifier const NSCalendarIdentifierTelugu;
extern NSCalendarIdentifier const NSCalendarIdentifierVietnamese;
extern NSCalendarIdentifier const NSCalendarIdentifierVikram;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCALENDAR_H */
