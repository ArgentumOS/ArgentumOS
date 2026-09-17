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
 *     case-insensitive comparison (nstring.m's localised section);
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

#import <foundation/NSObject.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>

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
+ (instancetype)localeWithLocaleIdentifier:(NSString *)identifier;

/* The identifiers whose CASE rules this library implements — not Cocoa's
 * catalogue of hundreds: every other identifier behaves as the neutral default. */
+ (NSArray *)availableLocaleIdentifiers;

+ (NSDictionary *)componentsFromLocaleIdentifier:(NSString *)identifier;
+ (NSString *)localeIdentifierFromComponents:(NSDictionary *)components;
+ (NSString *)canonicalLanguageIdentifierFromString:(NSString *)string;
+ (NSString *)canonicalLocaleIdentifierFromString:(NSString *)string;

- (id)initWithLocaleIdentifier:(NSString *)identifier;		/* designated */

- (NSString *)localeIdentifier;
- (id)objectForKey:(NSString *)key;	/* nil for a key that needs the database */

@end

#endif /* FOUNDATION_NSLOCALE_H */
