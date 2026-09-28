/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSTermOfAddress — HOW TO ADDRESS SOMEONE, AS A VALUE (§62.79), and with the two rule classes the last rows of
 * `Fundamentals / Automatic grammar agreement`.
 *
 * THE CLASS CARRIES TWO THINGS: an ISO LANGUAGE IDENTIFIER, when the term is localized, and the PRONOUNS the term
 * uses. Both are values here, so both are real - and what makes the three PREDEFINED terms a boundary worth
 * stating is that the ONLY field that distinguishes them is the pronoun list: Apple's own pages describe
 * `+feminine` as "a term of address that uses feminine pronouns (e.g. she/her/hers in English), and a feminine
 * grammatical gender", and THIS SYSTEM CARRIES NO PRONOUN DATA. So the three predefined terms exist, are distinct
 * objects, and carry NO pronouns - which in this state makes them equal values, and that is said here rather than
 * papered over with pronoun strings this library would have to invent (the page's own "(e.g. …)" is an example,
 * not a specification of which NSMorphologyPronoun values to build).
 *
 * `+currentUser` ANSWERS A VALUE for the reason `+userMorphology` does: Apple's is "the term of address that
 * should be used for addressing the user", and a system where nobody has stated one has a user with NO TERM - so
 * the answer is a term with no language and no pronouns, which is exactly what "no preference" is here.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSString;

@interface NSTermOfAddress : NSObject <NSCopying, NSSecureCoding>
{
@private
	NSString *_languageIdentifier;
	NSArray *_pronouns;
}

/* THE THREE PREDEFINED TERMS, distinct values that carry no pronouns in this system - see the note above. */
+ (instancetype)feminine;
+ (instancetype)masculine;
+ (instancetype)neutral;

/* THE USER'S TERM, which this system does not have: the answer is a term with nothing stated. */
+ (instancetype)currentUser;

/* A TERM RESTRICTED TO A LANGUAGE, carrying that language's identifier and the pronouns the caller supplies -
 * which is the one door here that builds a term out of real data, and it refuses a nil or empty language the way
 * the other value doors in this library refuse a missing name. */
+ (instancetype)localizedForLanguageIdentifier:(NSString *)language
				  withPronouns:(NSArray *)pronouns;

- (nullable NSString *)languageIdentifier;
- (nullable NSArray *)pronouns;

@end

NS_ASSUME_NONNULL_END
