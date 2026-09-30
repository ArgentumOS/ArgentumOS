/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLocale — a locale identifier, and the locale rules this Foundation honours.
 * docs/design/foundation-plan.md, the dependency queue (stage E).
 *
 * WHAT IT IS HERE: an immutable value wrapping a locale IDENTIFIER. The identifier
 * is canonicalised and parsed into the subtags it has (language, script, region),
 * and -objectForKey: answers exactly the keys those subtags derive.
 *
 * WHAT IT IS NOT, and why the queue's promise is refined rather than met whole:
 * Cocoa's NSLocale is also the door to a locale DATABASE — thousands of
 * identifiers, display names, collation. This Foundation ships no such data, so
 * the boundary is drawn where a RULE is needed instead of a TABLE:
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
 * The data-driven half is EXCLUDED by name, with a reason, in the inventory the
 * string probe asserts (docs/design/foundation-plan.md).
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

/* NULLABILITY (F6, the last slice): NONNULL by default, and two documented
 * exceptions, both MEASURED in NSLocale.m:
 *   - -initWithLocaleIdentifier: is a `return nil;` site, and
 *     +localeWithLocaleIdentifier: is `return [[self alloc] initWithLocaleIdentifier:]`,
 *     so it inherits that — a garbage identifier can be refused;
 *   - -objectForKey: answers nil "for a key that needs the database", which the
 *     header below has said all along.
 * +currentLocale is NOT: it reads LC_ALL then LANG and falls back to en_US_POSIX,
 * so it always answers a locale (that is the POSIX convention, not a guess). */
/* FOR `NSCoding`: ABOVE THE ASSUME-NONNULL REGION, which is where NSArray.h puts it too — a `#include` inside a
 * nullability region is refused by the compiler, because the imported header's declarations would be dragged
 * into somebody else's region. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

/* The keys -objectForKey: answers. Cocoa's other NSLocale… keys — the decimal
 * separator, the currency, the collation — need the locale database this library
 * does not ship, and answer nil here. */
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

/* The identifiers whose CASE rules this library implements — not Cocoa's
 * catalogue of hundreds: every other identifier behaves as the neutral default. */
+ (NSArray *)availableLocaleIdentifiers;

+ (NSDictionary *)componentsFromLocaleIdentifier:(NSString *)identifier;
+ (NSString *)localeIdentifierFromComponents:(NSDictionary *)components;
+ (NSString *)canonicalLanguageIdentifierFromString:(NSString *)string;
+ (NSString *)canonicalLocaleIdentifierFromString:(NSString *)string;

- (nullable id)initWithLocaleIdentifier:(NSString *)identifier;		/* designated */

- (NSString *)localeIdentifier;
- (nullable id)objectForKey:(NSString *)key;	/* nil for a key that needs the database */

/*
 * THE DISPLAY NAME FOR A LOCALE COMPONENT, LOCALIZED INTO THIS LOCALE (D7's kind (D), landed §18):
 * the RECEIVER names the language the answer comes back IN, and `value` is the locale (for
 * NSLocaleIdentifier) or the subtag being named. Apple's page allows nil — "not all locale property
 * keys have values with display name values" — which is what this answers for a key it has no name
 * for, and for a value that is not a string.
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
 * a wire name only WITHIN this library: -objectForKey: is asked with what is declared here). The change
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

NS_ASSUME_NONNULL_END

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
