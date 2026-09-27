/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLinguisticTagger — THE DEPRECATED CLASS AND ITS WHOLE VOCABULARY, LANDED BECAUSE THE DEPRECATION GROUND WAS
 * RETIRED. §62.24 (the user's policy, 2026-09-26): "to support porting older Mac applications, all items removed
 * for being deprecated are un-deprecated in Argentum Foundation, and added to the work list." Apple replaced this
 * class with `NLTagger` in 10.15, and it is the largest single family on that list: ONE CLASS AND FIFTY-ONE
 * VOCABULARY NAMES, which is what makes it a unit rather than a project.
 *
 * WHAT IS IMPLEMENTED, AND IT IS THE HALF THIS SYSTEM CAN DO HONESTLY:
 *
 *   * TOKENIZATION, OVER THE SAME KIND OF DEPENDENCY THE REST OF THIS LIBRARY USES. Words, sentences and
 *     paragraphs are found with ICU's break iterator (`ubrk_*`, through the internal `FNTextBreaking` bridge),
 *     which is the same arrangement NSScanner's numeric grammars and NSLocale's identifications already rest on:
 *     a RULE OVER A LIBRARY THIS TREE LINKS, rather than a table somebody typed.
 *   * THE TWO SCHEMES THAT ARE PROPERTIES OF THE TEXT ITSELF: `NSLinguisticTagSchemeTokenType` (word,
 *     whitespace, punctuation, or other — a classification of each token, which is where the break iterator
 *     lands anyway) and `NSLinguisticTagSchemeScript` (ICU's `uscript_getScript` for the token's first
 *     character).
 *   * THE ORTHOGRAPHY DOORS, because `NSOrthography` is a class this library already has (§62.21).
 *
 * WHAT IS REFUSED BY NAME, WITH GROUNDS (§11.6's register), AND REFUSED WITHOUT BEING DECLARED — because a door
 * that is declared and never callable is worse than an absent one (§11.2):
 *
 *   * THE MORPHOLOGICAL SCHEMES — `NSLinguisticTagSchemeLexicalClass`, `...NameType`,
 *     `...NameTypeOrLexicalClass` and `...Lemma`. THESE NEED A PART-OF-SPEECH TAGGER, AND ICU PUBLISHES NONE in
 *     its C API: it ships text BREAKING (`ubrk_*`), Unicode PROPERTIES (`uchar.h`, `uscript.h`), and
 *     collation — but no tagger, no tagger model, and nothing that would answer "is this token a noun". A
 *     caller who asks for one of these gets an EMPTY result rather than a guess, and `+availableTagSchemesForUnit:
 *     language:` is where that is readable BEFORE anything is built (Apple's own door, used for exactly this).
 *   * `NSLinguisticTagSchemeLanguage` — LANGUAGE IDENTIFICATION: ICU can say what SCRIPT a character is in, which
 *     §62.21's `NSOrthography` already exposes as a LANGUAGE MAP, and it cannot say that a sentence is English.
 *     The scheme is therefore refused rather than approximated by a script guess that would be wrong for every
 *     language written in Latin script.
 *   * THE LATTICE DOOR, `-possibleTagsAtIndex:scheme:tokenRange:sentenceRange:scores:`, exists to hand back the
 *     ALTERNATIVES a tagger weighed. With no tagger there are no alternatives to weigh, and an array holding one
 *     invented answer would be a worse lie than an empty one.
 *
 * AND ONE THING THIS HEADER DELIBERATELY DOES NOT DO: it does not spell the tag values as Apple's *strings* and
 * then pretend that is fidelity. Apple publishes the NAMES and their spellings ("Word", "LexicalClass", …), and
 * this library publishes the same spellings — but a tag is compared BY EQUALITY, so the values are a contract
 * between this header and its own implementation under §11.6.1 D2, which is why the probe pins each one.
 */

#ifndef FOUNDATION_NSLINGUISTICTAGGER_H
#define FOUNDATION_NSLINGUISTICTAGGER_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSMutableArray;
@class NSMutableDictionary;
@class NSOrthography;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* --- THE UNITS, AND WHAT A CALLER ASKS ABOUT THE TEXT ------------------------------------------------ */

/* THE FOUR UNITS Apple names: the granularity a caller asks a question at. THE VALUES ARE OURS UNDER §11.6.1 D2 as
 * every enum's are — Apple publishes the case names — and the probe pins the numbers. */
typedef NS_ENUM(NSInteger, NSLinguisticTaggerUnit) {
	NSLinguisticTaggerUnitWord = 0,
	NSLinguisticTaggerUnitSentence = 1,
	NSLinguisticTaggerUnitParagraph = 2,
	NSLinguisticTaggerUnitDocument = 3
};

/* THE OPTIONS ARE A MASK, AND THE FOUR OMISSIONS ARE HONOURED: `OmitWhitespace`, `OmitPunctuation` and
 * `OmitOther` drop tokens by their TOKEN TYPE — which this library answers — and `OmitWords` drops the tags for
 * word tokens. THE FIFTH, `JoinNames`, IS REFUSED RATHER THAN ACCEPTED AND IGNORED: it asks for a personal name to
 * come back as ONE token, which means joining name-typed tokens, and this library has no name tagger (see the
 * morphological refusals above). The constant exists because a ported caller's mask must compile; the bit is
 * documented here as having no effect, which is the honest form of a constant nothing can honour. */
typedef NS_OPTIONS(NSUInteger, NSLinguisticTaggerOptions) {
	NSLinguisticTaggerOmitWhitespace = 1 << 0,
	NSLinguisticTaggerOmitPunctuation = 1 << 1,
	NSLinguisticTaggerOmitOther = 1 << 2,
	NSLinguisticTaggerOmitWords = 1 << 3,
	/* AND THE ONE WITH NO EFFECT ON ANY RESULT: `JoinNames` asks for a personal name as ONE token, which means
	 * joining NAME-TYPED tokens, and this library has no name tagger (the morphological refusals above). IT IS
	 * STILL A CASE WITH ITS OWN BIT, because a ported caller's mask has to compile — what it does not do is
	 * change anything, and this comment is where that is recorded rather than left to be discovered. */
	NSLinguisticTaggerJoinNames = 1 << 4
};

/* THE TWO VOCABULARY TYPES: a tag and a scheme are both strings, and the typealiases exist so a caller's
 * declarations read the way Apple's do. */
typedef NSString * NSLinguisticTag;
typedef NSString * NSLinguisticTagScheme;

/* --- THE SCHEMES ------------------------------------------------------------------------------------ */

extern NSLinguisticTagScheme const NSLinguisticTagSchemeTokenType;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeLexicalClass;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeNameType;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeNameTypeOrLexicalClass;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeLemma;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeLanguage;
extern NSLinguisticTagScheme const NSLinguisticTagSchemeScript;

/* --- THE TAGS, IN THREE GROUPS, BECAUSE THEY BELONG TO DIFFERENT SCHEMES ---------------------------- */

/* THE TOKEN TYPES: the only tags this library's tagger produces, and the ones the `TokenType` scheme answers. */
extern NSLinguisticTag const NSLinguisticTagWord;
extern NSLinguisticTag const NSLinguisticTagWhitespace;
extern NSLinguisticTag const NSLinguisticTagPunctuation;
extern NSLinguisticTag const NSLinguisticTagOther;

/* THE PUNCTUATION AND WORD-JOINER REFINEMENTS: Apple refines `Punctuation` and `Other` with these, and a scheme
 * this library refuses still has to NAME them, because a caller's switch compares against them. */
extern NSLinguisticTag const NSLinguisticTagSentenceTerminator;
extern NSLinguisticTag const NSLinguisticTagOpenQuote;
extern NSLinguisticTag const NSLinguisticTagCloseQuote;
extern NSLinguisticTag const NSLinguisticTagOpenParenthesis;
extern NSLinguisticTag const NSLinguisticTagCloseParenthesis;
extern NSLinguisticTag const NSLinguisticTagWordJoiner;
extern NSLinguisticTag const NSLinguisticTagDash;
extern NSLinguisticTag const NSLinguisticTagOtherPunctuation;
extern NSLinguisticTag const NSLinguisticTagParagraphBreak;
extern NSLinguisticTag const NSLinguisticTagOtherWhitespace;
extern NSLinguisticTag const NSLinguisticTagOtherWord;

/* THE LEXICAL CLASSES: the tags `NSLinguisticTagSchemeLexicalClass` would answer, which this library REFUSES —
 * named here because a ported caller's switch on them must compile, and because refusing a scheme is not the same
 * as pretending its vocabulary does not exist. */
extern NSLinguisticTag const NSLinguisticTagNoun;
extern NSLinguisticTag const NSLinguisticTagVerb;
extern NSLinguisticTag const NSLinguisticTagAdjective;
extern NSLinguisticTag const NSLinguisticTagAdverb;
extern NSLinguisticTag const NSLinguisticTagPronoun;
extern NSLinguisticTag const NSLinguisticTagDeterminer;
extern NSLinguisticTag const NSLinguisticTagParticle;
extern NSLinguisticTag const NSLinguisticTagPreposition;
extern NSLinguisticTag const NSLinguisticTagNumber;
extern NSLinguisticTag const NSLinguisticTagConjunction;
extern NSLinguisticTag const NSLinguisticTagInterjection;
extern NSLinguisticTag const NSLinguisticTagClassifier;
extern NSLinguisticTag const NSLinguisticTagIdiom;

/* AND THE THREE NAME TYPES, whose scheme is refused with the lexical classes and for the same reason. */
extern NSLinguisticTag const NSLinguisticTagPersonalName;
extern NSLinguisticTag const NSLinguisticTagPlaceName;
extern NSLinguisticTag const NSLinguisticTagOrganizationName;

/*
 * THE TAGGER. Its behaviour is in NSLinguisticTagger.m; what a caller needs to know is the shape, and the shape is
 * that it is a VIEW OVER A STRING plus a set of schemes: the string and the orthography are set on it, and the
 * questions are then asked of RANGES.
 */
@interface NSLinguisticTagger : NSObject
{
	NSArray *_tagSchemes;
	NSString *_string;
	NSOrthography *_orthography;
	NSRange _orthographyRange;
	NSUInteger _options;
}

/* THE SCHEMES THIS LIBRARY CAN ANSWER FOR A UNIT. Apple's own door, and the one that makes a refusal READABLE
 * BEFORE anything is built: a caller that asks for `LexicalClass` here gets nil, and knows why without asking a
 * tagger that would answer an empty string. */
+ (nullable NSArray *)availableTagSchemesForUnit:(NSLinguisticTaggerUnit)unit
							       language:(nullable NSString *)language;

/* A TAGGER IS MADE FROM SCHEMES AND OPTIONS, and it holds no string until one is set (Apple's arrangement, and
 * the reason `-string` is nullable: a tagger with no string answers an EMPTY range rather than crashing). */
- (instancetype)initWithTagSchemes:(NSArray *)tagSchemes
			   options:(NSUInteger)options;

@property (readonly, copy) NSArray *tagSchemes;

/* THE TEXT IT IS A VIEW OVER, and `NIL` IS A VALID STATE rather than a broken one. */
@property (nullable, copy) NSString *string;

/* THE ORTHOGRAPHY, WHICH IS A CLASS THIS LIBRARY ALREADY HAS (§62.21), and the range it applies to. */
- (void)setOrthography:(nullable NSOrthography *)orthography range:(NSRange)range;
- (nullable NSOrthography *)orthographyAtIndex:(NSUInteger)charIndex effectiveRange:(nullable NSRangePointer)effectiveRange;

/* THE ONE TAG AT A POSITION, with the token it covers and the SENTENCE it sits in — both of which the break
 * iterator already knows, and neither of which is a guess. */
- (nullable NSLinguisticTag)tagAtIndex:(NSUInteger)charIndex
				scheme:(NSLinguisticTagScheme)scheme
			    tokenRange:(nullable NSRangePointer)tokenRange
			 sentenceRange:(nullable NSRangePointer)sentenceRange;

/* AND THE ENUMERATION, which is the door most callers use: every tag in a range, one block call per token. */
- (void)enumerateTagsInRange:(NSRange)range
		       scheme:(NSLinguisticTagScheme)scheme
		      options:(NSLinguisticTaggerOptions)options
		   usingBlock:(void (^)(NSLinguisticTag _Nullable tag,
					NSRange tokenRange,
					BOOL *stop))block;
- (void)enumerateTagsInRange:(NSRange)range
			unit:(NSLinguisticTaggerUnit)unit
		      scheme:(NSLinguisticTagScheme)scheme
		     options:(NSLinguisticTaggerOptions)options
		  usingBlock:(void (^)(NSLinguisticTag _Nullable tag,
				       NSRange tokenRange,
				       BOOL *stop))block;

/* THE SENTENCE THAT CONTAINS A RANGE, which is a break-iterator question rather than a tagging one and is
 * therefore answered even for a scheme this library refuses. */
- (NSRange)sentenceRangeForRange:(NSRange)range;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSLINGUISTICTAGGER_H */
