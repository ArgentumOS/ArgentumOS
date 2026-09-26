/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOrthography — a description of the LINGUISTIC CONTENT of a piece of text: which scripts it is written in, and
 * which languages are written in each. §62.21 of docs/design/foundation-plan.md, and the last open name in the
 * Localization family.
 *
 * IT EXISTS HERE FOR A REASON THAT IS NOT ITSELF: §62.18 gave `NSTextCheckingResult` Apple's whole surface,
 * including `+orthographyCheckingResultWithRange:orthography:` and its `-orthography` payload — and the class
 * those two members speak in did not exist, so the pair could only be handed nil. This closes that. The class is
 * otherwise what Apple's pages say it is: TWO VALUES, a dominant script and a language map, both spelled in the
 * tags a program already uses elsewhere (ISO 15924 for scripts, BCP-47 for languages).
 *
 * THE DOMINANT SCRIPT IS ONE OF THE MAP'S KEYS, AND THAT IS AN INVARIANT THIS CLASS ENFORCES. Apple's pages do not
 * say so, and its `-dominantLanguage` is DECLARED to answer a string while a map missing the dominant script would
 * leave it with none to answer — so `-initWithDominantScript:languageMap:` REFUSES such a map rather than
 * accepting it and answering nil later. That is this library's rule for a declared contract its state could break
 * (§11.6.1 D4 is the precedent: `+dataWithBytes:length:` RAISES instead of answering the nil its annotation
 * forbids), and the choice is ours because Apple publishes no behaviour here (§11.6's first gate). The five
 * refusals are: a nil script, a nil map, a value that is not an array, no entry for the dominant script, and an
 * empty array for it. `-languagesForScript:` and `-dominantLanguageForScript:` are the two doors that CAN miss,
 * because their argument is arbitrary data rather than this object's own state, and they answer nil.
 *
 * AND APPLE'S PAGES DO NOT PUBLISH NULLABILITY, which is worth stating where the annotations are: the doc pages
 * render every declaration with the qualifiers stripped — measured, because `NSScanner`'s `intoString:` parameter
 * appears there as `(NSString **)` when it must be nullable in the header (§62.20). So the two query doors above
 * are annotated `nullable` here to say what the documented behaviour IS, not to disagree with a published
 * qualifier that does not exist.
 *
 * THE ORDER OF `-allScripts` AND `-allLanguages` IS OURS AND IS STATED, because a map has no order a value type
 * should expose: this library's `NSDictionary` is a HASH TABLE (its enumeration order is not a fact about the
 * value), so the scripts are answered SORTED by the same byte-wise comparison the rest of the library uses. The
 * languages are then the concatenation, in that script order, of each script's OWN array — and THAT order is real
 * data, which is why it is preserved: `{"Latn": ["en", "fr"]}` says English is the dominant language of the Latin
 * script, and `-dominantLanguage` is the first element of the dominant script's array.
 *
 * `+defaultOrthographyForLanguage:` DETERMINES THE SCRIPT FROM ICU'S LIKELY-SUBTAGS DATA — the same binding this
 * library already uses for the data-driven families — and APPLE'S OWN DOCUMENTED EXAMPLE IS A CHECK RATHER THAN AN
 * ILLUSTRATION: for Hindi the map has one key, `Deva`, whose array holds `hi`. Three things about the answer are
 * stated because they are ours: the language stored in the map is THE TAG AS GIVEN (ICU's maximised form is used
 * to find the script and is not stored, again Apple's example); a tag ICU's data does not know at all (`xx` comes
 * back unchanged) yields the ISO 15924 code for an undetermined script, `Zyyy`, so the factory is total; and an
 * EMPTY tag is refused, because this door's argument IS a language tag.
 *
 * CODING SHIPS, because Apple's class conforms to `NSCoding` and `NSSecureCoding`: the two values travel under
 * this library's own key names (a program never sees them, which is why they are spellings rather than
 * transcriptions of anything). The ENFORCEMENT gap is `NSCoding.h`'s and is named there — this tree's unarchiver
 * does not yet ask `+supportsSecureCoding` — so `+supportsSecureCoding` answers YES and guards what the coder
 * checks, which is the same position `NSAttributedString` takes.
 *
 * NOT HERE, NAMED: the SWIFT-ONLY `LocalizedStringResource` conformance Apple's Swift view shows is excluded by
 * §11.5's swift-only ground, as every Swift-only conformance in this library is.
 */

#ifndef FOUNDATION_NSORTHOGRAPHY_H
#define FOUNDATION_NSORTHOGRAPHY_H

#import <Foundation/NSCoding.h>
#import <Foundation/NSObject.h>

@class NSArray;
@class NSDictionary;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSOrthography : NSObject <NSCopying, NSSecureCoding>
{
	NSString *_dominantScript;
	NSDictionary *_languageMap;
}

/* THE SCRIPT, WHICH THIS CLASS DETERMINES RATHER THAN ASKS FOR. Apple's example is the check: Hindi answers
 * `{"Deva": ["hi"]}`. See the file comment for the two cases that are ours (the `Zyyy` fallback, and the refusal of
 * an empty tag). */
+ (instancetype)defaultOrthographyForLanguage:(NSString *)language;

/* THE DESIGNATED DOOR, and the one that ENFORCES the invariant: the dominant script must be a key of the map, and
 * that key's array must not be empty. Apple's pages describe this initializer as the way to OVERRIDE the script a
 * language is usually written in, which is why a caller still has to name one. */
- (instancetype)initWithDominantScript:(NSString *)script languageMap:(NSDictionary *)map;
+ (instancetype)orthographyWithDominantScript:(NSString *)script languageMap:(NSDictionary *)map;

/* THE DOMINANT SCRIPT, and the first language of ITS array (never nil: the invariant is what guarantees it). */
@property (readonly, copy) NSString *dominantScript;
@property (readonly, copy) NSString *dominantLanguage;

/* THE MAP ITSELF, as an immutable snapshot: a caller that changes the dictionary it passed must not change this
 * object's content. */
@property (readonly, copy) NSDictionary *languageMap;

/* THE KEYS AND THE FLATTENED LANGUAGES — sorted, and grouped by script, for the reason the file comment gives. */
@property (readonly, copy) NSArray *allScripts;
@property (readonly, copy) NSArray *allLanguages;

/* THE TWO QUERIES THAT CAN MISS, and both answer nil for a script the map does not carry. */
- (nullable NSArray *)languagesForScript:(nullable NSString *)script;
- (nullable NSString *)dominantLanguageForScript:(nullable NSString *)script;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSORTHOGRAPHY_H */
