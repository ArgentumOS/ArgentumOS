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
NS_ASSUME_NONNULL_BEGIN

/* The keys -objectForKey: answers. Cocoa's other NSLocale… keys — the decimal
 * separator, the currency, the collation — need the locale database this library
 * does not ship, and answer nil here. */
extern NSString *const NSLocaleIdentifier;
extern NSString *const NSLocaleLanguageCode;
extern NSString *const NSLocaleScriptCode;
extern NSString *const NSLocaleCountryCode;

@interface NSLocale : NSObject <NSCopying>
{
	NSString *_identifier;		/* canonical, e.g. "tr_TR" */
}

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

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSLOCALE_H */
