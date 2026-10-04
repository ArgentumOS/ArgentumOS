/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLocale — a locale identifier, and the locale rules AND DATA this Foundation honours.
 * docs/design/foundation-plan.md, the dependency queue (stage E).
 *
 * WHAT IT IS HERE: an immutable value wrapping a locale IDENTIFIER. The identifier
 * is canonicalised and parsed into the subtags it has (language, script, region),
 * and the SUBTAG doors answer exactly those.
 *
 * THE DATA HALF, NOW THAT ICU IS BOUND (the 2026-09-30 locale slice): Cocoa's NSLocale is also the door
 * to a locale DATABASE — display names, collation, currency, delimiters, the measurement system, the ISO
 * registries — and this library LINKS that database (libicuuc/libicui18n; NSLocale.m is in the build's
 * per-file ICU table). So the boundary is no longer "a RULE instead of a TABLE": the property doors below
 * read ICU's data. TWO rows stay open, each named in NSLocale.m: +preferredLanguages (a user preference
 * list, not locale data) and -localizedStringForVariantCode: (ICU's variant display table answers EMPTY
 * here). The RULE half is still the one drawn first:
 *
 *   - CASE is localised. The Turkic languages (tr, az) are the locales whose case
 *     mapping is CONDITIONAL in Unicode's SpecialCasing: upper-case i is İ, and
 *     lower-case I is ı. A Turkic locale therefore changes
 *     -uppercaseStringWithLocale:, -lowercaseStringWithLocale: and every
 *     case-insensitive comparison (NSString.m's localised section);
 *   - ORDERING is NOT localised: there are no collation tables, so
 *     -localizedCompare: compares by byte exactly as -compare: does;
 *   - SEARCH folding is NOT localised: -rangeOfString: answers a RANGE into the
 *     receiver, and the Turkic fold changes lengths, so a folded search would
 *     report offsets the searched string does not have.
 *
 * -objectForKey: AND -displayNameForKey:value: STAY NARROW, AND THE STRING PROBE PINS THEM. §9's
 * -objectForKey: answers the four SUBTAG keys and nil for every DATA key (the probe asserts
 * -objectForKey:NSLocaleDecimalSeparator is nil); §18's -displayNameForKey:value: names the four SUBTAG
 * keys and nil otherwise (the probe asserts the currency key is nil). The DATA is reached through the
 * PROPERTIES (-decimalSeparator, -currencyCode, …) and the -localizedStringFor…: family, which read ICU
 * DIRECTLY — so a caller keeps the exact answers the earlier landings promised while the new doors open
 * on top of them.
 *
 * +currentLocale reads LC_ALL, then LANG, and with neither set it is en_US_POSIX:
 * "no locale" is the POSIX convention, not a guess at a user's.
 */

#ifndef FOUNDATION_NSLOCALE_H
#define FOUNDATION_NSLOCALE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSCharacterSet.h>	/* -exemplarCharacterSet answers one */

/* NULLABILITY (F6, the last slice): NONNULL by default, and two documented
 * exceptions, both MEASURED in NSLocale.m:
 *   - -initWithLocaleIdentifier: is a `return nil;` site, and
 *     +localeWithLocaleIdentifier: is `return [[self alloc] initWithLocaleIdentifier:]`,
 *     so it inherits that — a garbage identifier can be refused;
 *   - -objectForKey: answers nil only for a key ICU has no value for — which today is every DATA key,
 *     because it answers the four SUBTAG keys alone; -displayNameForKey:value: answers nil for a key or
 *     a non-string value it has no name for.
 * +currentLocale is NOT: it reads LC_ALL then LANG and falls back to en_US_POSIX,
 * so it always answers a locale (that is the POSIX convention, not a guess). */
/* FOR `NSCoding`: ABOVE THE ASSUME-NONNULL REGION, which is where NSArray.h puts it too — a `#include` inside a
 * nullability region is refused by the compiler, because the imported header's declarations would be dragged
 * into somebody else's region. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

/* The keys -objectForKey: answers. The four below are DERIVED from the identifier's own subtags; the
 * sixteen declared further down are NOT answered by -objectForKey: (which the string probe pins to the
 * subtag keys) — they are the keys the PROPERTIES read out of ICU's database. */
extern NSString *const NSLocaleIdentifier;
extern NSString *const NSLocaleLanguageCode;
extern NSString *const NSLocaleScriptCode;
extern NSString *const NSLocaleCountryCode;

@interface NSLocale : NSObject <NSCopying, NSCoding>
{
	NSString *_identifier;		/* canonical, e.g. "tr_TR" */
}

/* THE NSCoding DOORS (§63.22). THE WIRE IS OURS (Apple's spelling for this class's coded form is not
 * published in a source this project may read). THE IDENTIFIER IS THE WHOLE VALUE, which is what the ivar
 * below already is — and an identifier the archive does not carry leaves -initWithLocaleIdentifier: free to
 * answer nil, its own documented answer, rather than this door inventing a locale. */
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;

+ (instancetype)currentLocale;
+ (nullable instancetype)localeWithLocaleIdentifier:(NSString *)identifier;

/* ---- THE LOCALE SOURCES, AND THE ISO CODE CATALOGUES -----------------------------------------------
 *
 * THE DATABASE'S HALF, WHICH THIS TIME ICU HAS. §62.24 retired the "we have no data" boundary the string
 * probe had drawn around these: this library LINKS ICU (libicuuc/libicui18n — NSLocale.m is in the build's
 * per-file ICU table for exactly this), and ICU ships the catalogue, so a row that used to answer nil now
 * answers ICU's data. Each was MEASURED on the host against ICU 76.1 before it was written. */

/* THE USER'S LOCALE, UPDATED WHEN IT CHANGES. There is no locale store to watch here — the process
 * environment IS the source (the same LC_ALL/LANG +currentLocale reads) — so this is a locale that
 * re-reads it on every access, which is what "autoupdating" can mean with no store behind it. */
+ (NSLocale *)autoupdatingCurrentLocale;

/* THE SYSTEM'S LOCALE. ICU's `uloc_getDefault` is what a system sets — the same source NSTimeZone's
 * `+systemTimeZone` reads (`ucal_getDefaultTimeZone`) — so this is that default, canonicalised. */
+ (NSLocale *)systemLocale;

/* THE ISO REGISTRIES, from ICU. Measured on the host: 601 language codes (aa…zza), 254 country codes
 * (AD…ZW), 308 ISO-4217 currencies, of which 270 are the "commonly used" set. */
+ (NSArray *)ISOLanguageCodes;
+ (NSArray *)ISOCountryCodes;
+ (NSArray *)ISOCurrencyCodes;
+ (NSArray *)commonISOCurrencyCodes;

/* WINDOWS LOCALE CODES: ICU's `uloc_getLCID`/`uloc_getLocaleForLCID`. Measured: en_US <-> 1033,
 * fr_FR <-> 1036. An LCID with no locale answers nil, and an identifier with no LCID answers 0. */
+ (nullable NSString *)localeIdentifierFromWindowsLocaleCode:(uint32_t)windowsLocaleCode;
+ (uint32_t)windowsLocaleCodeFromLocaleIdentifier:(NSString *)localeIdentifier;

/* The identifiers whose CASE rules this library implements — not Cocoa's
 * catalogue of hundreds: every other identifier behaves as the neutral default. */
+ (NSArray *)availableLocaleIdentifiers;

+ (NSDictionary *)componentsFromLocaleIdentifier:(NSString *)identifier;
+ (NSString *)localeIdentifierFromComponents:(NSDictionary *)components;
+ (NSString *)canonicalLanguageIdentifierFromString:(NSString *)string;
+ (NSString *)canonicalLocaleIdentifierFromString:(NSString *)string;

- (nullable id)initWithLocaleIdentifier:(NSString *)identifier;		/* designated */

- (NSString *)localeIdentifier;
- (nullable id)objectForKey:(NSString *)key;	/* the four SUBTAG keys; nil for a DATA key */

/*
 * THE DISPLAY NAME FOR A LOCALE COMPONENT, LOCALIZED INTO THIS LOCALE (D7's kind (D), landed §18):
 * the RECEIVER names the language the answer comes back IN, and `value` is the locale (for
 * NSLocaleIdentifier) or the subtag being named. Apple's page allows nil — "not all locale property
 * keys have values with display name values" — which is what this answers for a key it has no name
 * for, and for a value that is not a string. The four COMPONENT keys are the whole table here; the
 * -localizedStringFor…: doors for the currency/calendar/collation keys read ICU directly instead.
 */
- (nullable NSString *)displayNameForKey:(id)key value:(id)value;

/* The direction a locale's script runs in (2026-09-20). The RIGHT-TO-LEFT answers
 * here are about the SCRIPT's own direction and need no database, which is why this
 * type ships while the locale's data-driven half does not. Names from Apple's
 * documentation index; values are ours (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSLocaleLanguageDirectionUnknown = 0,
	NSLocaleLanguageDirectionLeftToRight = 1,
	NSLocaleLanguageDirectionRightToLeft = 2,
	NSLocaleLanguageDirectionTopToBottom = 3,
	NSLocaleLanguageDirectionBottomToTop = 4
} NSLocaleLanguageDirection;

/* ---- THE REMAINING LOCALE KEYS, AND THE LOCALE-CHANGE NOTIFICATION (the coverage slice) -------------
 *
 * THE VALUE OF EVERY KEY HERE IS ITS OWN NAME, which is this header's convention for all of them (NSLocaleKey is
 * a wire name only WITHIN this library: the PROPERTIES are asked with what is declared here). The change
 * notification is declared as a STRING until NSNotificationName exists, which the notification landing below
 * brings in. */
typedef NSString *NSLocaleKey;

extern NSString *const NSCurrentLocaleDidChangeNotification;
extern NSString *const NSLocaleAlternateQuotationBeginDelimiterKey;
extern NSString *const NSLocaleAlternateQuotationEndDelimiterKey;
extern NSString *const NSLocaleCalendar;
extern NSString *const NSLocaleCollationIdentifier;
extern NSString *const NSLocaleCollatorIdentifier;
extern NSString *const NSLocaleCurrencyCode;
extern NSString *const NSLocaleCurrencySymbol;
extern NSString *const NSLocaleDecimalSeparator;
extern NSString *const NSLocaleExemplarCharacterSet;
extern NSString *const NSLocaleGroupingSeparator;
extern NSString *const NSLocaleMeasurementSystem;
extern NSString *const NSLocaleQuotationBeginDelimiterKey;
extern NSString *const NSLocaleQuotationEndDelimiterKey;
extern NSString *const NSLocaleUsesMetricSystem;
extern NSString *const NSLocaleVariantCode;

/* ---- THE LOCALE'S DIRECTION, THE DISPLAY-NAME DOORS, AND THE KEY-BACKED PROPERTIES ------------------
 *
 * THE LANGUAGE DIRECTION (D2's names): ICU's `uloc_getCharacterOrientation`/`uloc_getLineOrientation`.
 * Measured on the host: en/tr/ja are LeftToRight, ar/he/fa/ur are RightToLeft, and LINE direction is
 * TopToBottom for every language tried — which is the top-to-bottom script layout, not a bug. */
+ (NSLocaleLanguageDirection)characterDirectionForLanguage:(NSString *)isoLanguageCode;
+ (NSLocaleLanguageDirection)lineDirectionForLanguage:(NSString *)isoLanguageCode;

/* THE DISPLAY-NAME DOORS (Apple's -localizedStringFor…: are -displayNameForKey:value: with a fixed key).
 * The RECEIVER is the language the answer comes back IN; the argument is the identifier being named. All
 * nullable: a value this class has no name for answers nil, Apple's own contract.
 *
 * THE FOUR SUBTAG DOORS DELEGATE to -displayNameForKey:value: (that IS Apple's identity for them). THE
 * FOUR DATA DOORS DO NOT: -displayNameForKey:value: is pinned to the subtag keys (the string probe
 * asserts its currency key is nil), so -localizedStringForCurrencyCode:, -…CalendarIdentifier:,
 * -…CollationIdentifier: and -…CollatorIdentifier: read ICU directly — measured, the same answers ICU
 * gives the display door would. `VariantCode` is the ONE omitted: ICU's variant display table answers
 * empty on this data (measured), so the row stays open rather than shipping a door that can never open. */
- (nullable NSString *)localizedStringForLocaleIdentifier:(NSString *)localeIdentifier;
- (nullable NSString *)localizedStringForLanguageCode:(NSString *)languageCode;
- (nullable NSString *)localizedStringForCountryCode:(NSString *)countryCode;
- (nullable NSString *)localizedStringForScriptCode:(NSString *)scriptCode;
- (nullable NSString *)localizedStringForCalendarIdentifier:(NSString *)calendarIdentifier;
- (nullable NSString *)localizedStringForCollationIdentifier:(NSString *)collationIdentifier;
- (nullable NSString *)localizedStringForCollatorIdentifier:(NSString *)collatorIdentifier;
- (nullable NSString *)localizedStringForCurrencyCode:(NSString *)currencyCode;

/* ---- THE SUBTAGS THE IDENTIFIER ITSELF CARRIES (a RULE — no database) -------------------------------- */
- (nullable NSString *)languageCode;		/* the language subtag */
- (nullable NSString *)scriptCode;
- (nullable NSString *)countryCode;		/* DEPRECATED in Cocoa in favour of NSLocaleCountryCode */
- (nullable NSString *)variantCode;
- (nullable NSString *)regionCode;		/* the region subtag, whatever its position */
- (NSString *)languageIdentifier;		/* language, and script when one is present, joined by '-' */

/* ---- THE KEYS ICU ANSWERS (the DATABASE's half, now bound) ------------------------------------------
 *
 * Each reads ICU directly (NOT through -objectForKey:, which stays on the subtag keys). Nullable because
 * a key ICU has no value for answers nil. `usesMetricSystem` answers BOOL, `exemplarCharacterSet` a set. */
- (nullable NSString *)calendarIdentifier;
- (nullable NSString *)collationIdentifier;
- (nullable NSString *)collatorIdentifier;
- (nullable NSString *)currencyCode;
- (nullable NSString *)currencySymbol;
- (nullable NSString *)decimalSeparator;
- (nullable NSString *)groupingSeparator;
- (nullable NSString *)quotationBeginDelimiter;
- (nullable NSString *)quotationEndDelimiter;
- (nullable NSString *)alternateQuotationBeginDelimiter;
- (nullable NSString *)alternateQuotationEndDelimiter;
- (nullable NSCharacterSet *)exemplarCharacterSet;
- (BOOL)usesMetricSystem;

NS_ASSUME_NONNULL_END


/* §63.235: THE PREFERRED LANGUAGES, from the ONE locale this system resolves (its own), so the list is the
 * current locale's language rather than a preference chain a caller could reorder — the reading is stated. */
+ (NSArray * _Nonnull)preferredLanguages;
@end


/* ===================================================================================================
 * THE LEGACY CALENDAR IDENTIFIERS (§62.49)
 *
 * Apple deprecated these eleven names at 10.9 in favour of the `NSCalendarIdentifier*` spelling, and §62.24's
 * policy puts them back. EACH ONE IS ITS MODERN NAME RATHER THAN A SECOND STRING WITH THE SAME CHARACTERS: a
 * `NSString *const` of its own would be A DIFFERENT OBJECT, so a caller comparing identities — which is what a
 * calendar identifier is for — would get a different answer from the same program, and this family's whole job is
 * that a pre-2013 program behaves as it did. The macro is the only spelling that keeps the object identical, and
 * the sentinel `NSUndefinedDateComponent` is recorded the same way for the same reason.
 * =================================================================================================== */
#define NSGregorianCalendar		NSCalendarIdentifierGregorian
#define NSBuddhistCalendar		NSCalendarIdentifierBuddhist
#define NSChineseCalendar		NSCalendarIdentifierChinese
#define NSHebrewCalendar		NSCalendarIdentifierHebrew
#define NSIndianCalendar		NSCalendarIdentifierIndian
#define NSIslamicCalendar		NSCalendarIdentifierIslamic
#define NSIslamicCivilCalendar		NSCalendarIdentifierIslamicCivil
#define NSISO8601Calendar		NSCalendarIdentifierISO8601
#define NSJapaneseCalendar		NSCalendarIdentifierJapanese
#define NSPersianCalendar		NSCalendarIdentifierPersian
#define NSRepublicOfChinaCalendar	NSCalendarIdentifierRepublicOfChina

#endif /* FOUNDATION_NSLOCALE_H */
