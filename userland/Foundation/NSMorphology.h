/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMorphology.h — the grammatical vocabulary Foundation's attributed strings name attributes with.
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS (§11.6.1 D2: Apple publishes these case names and neither their
 *numbers nor a stable order). The three sets below are the whole of what this system has to say about
 * morphology: there is NO morphology ENGINE here — nothing inflects, agrees or resolves a referent — and the
 * attribute that carries them (NSAttributedString's NSMorphologyAttributeName) is stored and copied like any
 * other attribute without being acted on. They are declared because a conforming program compiles against a
 * vocabulary it cannot yet use, and that is stated rather than implied.
 */

#ifndef FOUNDATION_NSMORPHOLOGY_H
#define FOUNDATION_NSMORPHOLOGY_H

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

typedef enum {
	NSGrammaticalGenderFeminine = 0,
	NSGrammaticalGenderMasculine = 1,
	NSGrammaticalGenderNeuter = 2,
	NSGrammaticalGenderNotSet = 3
} NSGrammaticalGender;

typedef enum {
	NSGrammaticalNumberNotSet = 0,
	NSGrammaticalNumberPlural = 1,
	NSGrammaticalNumberPluralFew = 2,
	NSGrammaticalNumberPluralMany = 3,
	NSGrammaticalNumberPluralTwo = 4,
	NSGrammaticalNumberSingular = 5,
	NSGrammaticalNumberZero = 6
} NSGrammaticalNumber;

typedef enum {
	NSGrammaticalPartOfSpeechAbbreviation = 0,
	NSGrammaticalPartOfSpeechAdjective = 1,
	NSGrammaticalPartOfSpeechAdposition = 2,
	NSGrammaticalPartOfSpeechAdverb = 3,
	NSGrammaticalPartOfSpeechConjunction = 4,
	NSGrammaticalPartOfSpeechDeterminer = 5,
	NSGrammaticalPartOfSpeechInterjection = 6,
	NSGrammaticalPartOfSpeechLetter = 7,
	NSGrammaticalPartOfSpeechNotSet = 8,
	NSGrammaticalPartOfSpeechNoun = 9,
	NSGrammaticalPartOfSpeechNumeral = 10,
	NSGrammaticalPartOfSpeechParticle = 11,
	NSGrammaticalPartOfSpeechPreposition = 12,
	NSGrammaticalPartOfSpeechPronoun = 13,
	NSGrammaticalPartOfSpeechVerb = 14
} NSGrammaticalPartOfSpeech;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMORPHOLOGY_H */
