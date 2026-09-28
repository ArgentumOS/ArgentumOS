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
#import <Foundation/NSCoding.h>	/* NSCopying/NSSecureCoding: all three classes conform */

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

/* THE FIVE SETS NSMorphology's OWN PROPERTIES NEED, added with the class itself (§62.78). The VALUES are ours
 * (§11.6.1 D2: Apple publishes the case names and neither the numbers nor an order), and in every one of them
 * `...NotSet` is ZERO - an unset field is what a fresh morphology carries, so zero has to mean "nothing said"
 * rather than a grammatical fact. */
typedef enum {
	NSGrammaticalDefinitenessNotSet = 0,
	NSGrammaticalDefinitenessDefinite = 1,
	NSGrammaticalDefinitenessIndefinite = 2
} NSGrammaticalDefiniteness;

typedef enum {
	NSGrammaticalDeterminationNotSet = 0,
	NSGrammaticalDeterminationDependent = 1,
	NSGrammaticalDeterminationIndependent = 2
} NSGrammaticalDetermination;

typedef enum {
	NSGrammaticalCaseNotSet = 0,
	NSGrammaticalCaseAblative = 1,
	NSGrammaticalCaseAccusative = 2,
	NSGrammaticalCaseAdessive = 3,
	NSGrammaticalCaseAllative = 4,
	NSGrammaticalCaseDative = 5,
	NSGrammaticalCaseElative = 6,
	NSGrammaticalCaseEssive = 7,
	NSGrammaticalCaseGenitive = 8,
	NSGrammaticalCaseIllative = 9,
	NSGrammaticalCaseInessive = 10,
	NSGrammaticalCaseLocative = 11,
	NSGrammaticalCaseNominative = 12,
	NSGrammaticalCasePrepositional = 13,
	NSGrammaticalCaseTranslative = 14
} NSGrammaticalCase;

typedef enum {
	NSGrammaticalPersonNotSet = 0,
	NSGrammaticalPersonFirst = 1,
	NSGrammaticalPersonSecond = 2,
	NSGrammaticalPersonThird = 3
} NSGrammaticalPerson;

typedef enum {
	NSGrammaticalPronounTypeNotSet = 0,
	NSGrammaticalPronounTypePersonal = 1,
	NSGrammaticalPronounTypePossessive = 2,
	NSGrammaticalPronounTypeReflexive = 3
} NSGrammaticalPronounType;

/*
 * THE THREE VALUE TYPES, and the boundary the note at the top of this header states applies to them unchanged:
 * THEY CARRY A MORPHOLOGY AND THEY DO NOT INFLECT WITH IT. What is real here is the vocabulary as VALUES - a
 * morphology with its fields set, copied, compared, archived, and asked whether it says anything at all - because
 * that is what an attributed string's attribute store needs and what a caller can check.
 *
 * TWO DOORS ARE REFUSALS WITH A GROUND RATHER THAN ABSENCES:
 *
 *   * `-setCustomPronoun:forLanguage:error:` is PER-LANGUAGE PRONOUN DATA ("the custom pronouns for a language",
 *     in Apple's words) and this system carries no such data, so it refuses BY NAME and fills the NSError - the
 *     same shape the property-list and coder doors use - rather than accepting a value nothing can read back;
 *   * `+isSupportedForLanguage:` answers NO for the same reason: there is no language whose custom pronouns this
 *     system knows.
 *
 * AND `+userMorphology` ANSWERS A VALUE RATHER THAN REFUSING, WHICH IS THE HONEST READING: Apple's returns the
 * USER'S morphological preferences, and a system that has none has a user whose morphology is UNSPECIFIED - so
 * that is what it answers, and -isUnspecified says so.
 */

@class NSArray;
@class NSError;
@class NSString;

@interface NSMorphologyCustomPronoun : NSObject <NSCopying, NSSecureCoding>
{
@private
	NSString *_subjectForm;
	NSString *_objectForm;
	NSString *_possessiveForm;
	NSString *_possessiveAdjectiveForm;
	NSString *_reflexiveForm;
}

/* THE FIVE FORMS A LANGUAGE MAY NEED. Apple's page publishes them per language through
 * +requiredKeysForLanguage:, and every one is an optional value a caller sets. */
- (nullable NSString *)subjectForm;
- (void)setSubjectForm:(nullable NSString *)form;
- (nullable NSString *)objectForm;
- (void)setObjectForm:(nullable NSString *)form;
- (nullable NSString *)possessiveForm;
- (void)setPossessiveForm:(nullable NSString *)form;
- (nullable NSString *)possessiveAdjectiveForm;
- (void)setPossessiveAdjectiveForm:(nullable NSString *)form;
- (nullable NSString *)reflexiveForm;
- (void)setReflexiveForm:(nullable NSString *)form;

/* NO, AND THE GROUND IS THE ABSENCE OF PRONOUN DATA rather than an unimplemented door - see the note above. */
+ (BOOL)isSupportedForLanguage:(NSString *)language;
/* THE KEYS THIS SYSTEM CARRIES, which is the five above for every language, so a caller can build a pronoun whose
 * forms are the ones a reader will look for. */
+ (NSArray *)requiredKeysForLanguage:(NSString *)language;

@end

@interface NSMorphology : NSObject <NSCopying, NSSecureCoding>
{
@private
	NSGrammaticalGender _grammaticalGender;
	NSGrammaticalNumber _number;
	NSGrammaticalPartOfSpeech _partOfSpeech;
	NSGrammaticalDefiniteness _definiteness;
	NSGrammaticalDetermination _determination;
	NSGrammaticalCase _grammaticalCase;
	NSGrammaticalPerson _grammaticalPerson;
	NSGrammaticalPronounType _pronounType;
}

/* THE USER'S MORPHOLOGY, which in this system is UNSPECIFIED: the value says nothing, and -isUnspecified says
 * that it says nothing, rather than refusing a door Apple's API expects to answer. */
+ (NSMorphology *)userMorphology;

/* THE EIGHT GRAMMATICAL PROPERTIES, each defaulting to its `...NotSet` case. */
- (NSGrammaticalGender)grammaticalGender;
- (void)setGrammaticalGender:(NSGrammaticalGender)value;
- (NSGrammaticalNumber)number;
- (void)setNumber:(NSGrammaticalNumber)value;
- (NSGrammaticalPartOfSpeech)partOfSpeech;
- (void)setPartOfSpeech:(NSGrammaticalPartOfSpeech)value;
- (NSGrammaticalDefiniteness)definiteness;
- (void)setDefiniteness:(NSGrammaticalDefiniteness)value;
- (NSGrammaticalDetermination)determination;
- (void)setDetermination:(NSGrammaticalDetermination)value;
- (NSGrammaticalCase)grammaticalCase;
- (void)setGrammaticalCase:(NSGrammaticalCase)value;
- (NSGrammaticalPerson)grammaticalPerson;
- (void)setGrammaticalPerson:(NSGrammaticalPerson)value;
- (NSGrammaticalPronounType)pronounType;
- (void)setPronounType:(NSGrammaticalPronounType)value;

/* YES WHEN NOT ONE OF THE EIGHT IS SET, which is what a fresh morphology is. */
- (BOOL)isUnspecified;

/* THE PER-LANGUAGE PRONOUN DOORS: a getter that answers nil because nothing can have been stored, and a setter
 * that REFUSES BY NAME with the reason in the error - see the note above. */
- (nullable NSMorphologyCustomPronoun *)customPronounForLanguage:(NSString *)language;
- (BOOL)setCustomPronoun:(nullable NSMorphologyCustomPronoun *)features
	     forLanguage:(NSString *)language
		   error:(NSError ** _Nullable)error;

@end

@interface NSMorphologyPronoun : NSObject <NSCopying, NSSecureCoding>
{
@private
	NSString *_pronoun;
	NSMorphology *_morphology;
	NSMorphology *_dependentMorphology;
}

/* A PRONOUN WITH THE MORPHOLOGY IT AGREES WITH and the morphology of whatever it depends on (a possessive's
 * owner, in Apple's description) - a value, refused by Apple's own initialiser when the pronoun is nil. */
- (instancetype)initWithPronoun:(NSString *)pronoun
		     morphology:(NSMorphology *)morphology
	    dependentMorphology:(nullable NSMorphology *)dependentMorphology;

- (NSString *)pronoun;
- (NSMorphology *)morphology;
- (nullable NSMorphology *)dependentMorphology;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMORPHOLOGY_H */