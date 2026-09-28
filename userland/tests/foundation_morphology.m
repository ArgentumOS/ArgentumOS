/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_morphology — §62.78's acceptance: NSMorphology + NSMorphologyCustomPronoun + NSMorphologyPronoun,
 * and the five grammatical enums they are asked with.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT THIS CLASS IS IS THE VALUE, so that is what is checked: eight fields whose unset state is the vocabulary's
 * own `...NotSet`, -isUnspecified reading all eight, equality and hashing, a copy that does not follow the
 * original, and secure coding both ways. THE ENGINE IS NOT HERE AND THE HEADER SAYS SO - nothing inflects - so
 * the probe asserts the two doors that report that honestly: +isSupportedForLanguage: answers NO, and the
 * per-language setter REFUSES BY NAME with the reason in the error rather than accepting a value nothing can read
 * back.
 *
 * THE ONE DOOR THAT ANSWERS A VALUE RATHER THAN REFUSING is +userMorphology: this system has no user morphology,
 * so the answer is an UNSPECIFIED one - and a FRESH one each call, because a morphology is mutable and a shared
 * instance would let one caller's -setNumber: change every other caller's answer. Both halves are checked.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-MORPHOLOGY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-MORPHOLOGY %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	/* THE FIVE SETS THAT ARRIVED WITH THE CLASS, and the one rule they all obey: `...NotSet` is ZERO, because a
	 * fresh morphology has to read as "nothing said" rather than as a grammatical fact. */
	check("the-five-new-enums-have-notset-at-zero-and-distinct-cases",
	      NSGrammaticalDefinitenessNotSet == 0 && NSGrammaticalDeterminationNotSet == 0 &&
	      NSGrammaticalCaseNotSet == 0 && NSGrammaticalPersonNotSet == 0 &&
	      NSGrammaticalPronounTypeNotSet == 0 &&
	      NSGrammaticalCaseNominative != NSGrammaticalCaseGenitive &&
	      NSGrammaticalPersonFirst != NSGrammaticalPersonThird &&
	      NSGrammaticalPronounTypePersonal != NSGrammaticalPronounTypeReflexive,
	      [NSString stringWithFormat:@"case=%d person=%d pronounType=%d",
		(int)NSGrammaticalCaseNotSet, (int)NSGrammaticalPersonNotSet,
		(int)NSGrammaticalPronounTypeNotSet]);

	/* A FRESH MORPHOLOGY SAYS NOTHING, AND SAYS SO. */
	{
		NSMorphology *fresh = [[NSMorphology alloc] init];

		check("a-fresh-morphology-is-unspecified-in-every-field",
		      [fresh isUnspecified] &&
		      [fresh grammaticalGender] == NSGrammaticalGenderNotSet &&
		      [fresh number] == NSGrammaticalNumberNotSet &&
		      [fresh partOfSpeech] == NSGrammaticalPartOfSpeechNotSet &&
		      [fresh definiteness] == NSGrammaticalDefinitenessNotSet &&
		      [fresh determination] == NSGrammaticalDeterminationNotSet &&
		      [fresh grammaticalCase] == NSGrammaticalCaseNotSet &&
		      [fresh grammaticalPerson] == NSGrammaticalPersonNotSet &&
		      [fresh pronounType] == NSGrammaticalPronounTypeNotSet,
		      [NSString stringWithFormat:@"unspecified=%d gender=%d case=%d", (int)[fresh isUnspecified],
			(int)[fresh grammaticalGender], (int)[fresh grammaticalCase]]);
	}

	/* THE EIGHT FIELDS ROUND-TRIP, and one of them is enough to stop being unspecified. */
	{
		NSMorphology *m = [[NSMorphology alloc] init];

		[m setGrammaticalGender:NSGrammaticalGenderFeminine];
		[m setNumber:NSGrammaticalNumberPlural];
		[m setPartOfSpeech:NSGrammaticalPartOfSpeechNoun];
		[m setDefiniteness:NSGrammaticalDefinitenessDefinite];
		[m setDetermination:NSGrammaticalDeterminationDependent];
		[m setGrammaticalCase:NSGrammaticalCaseGenitive];
		[m setGrammaticalPerson:NSGrammaticalPersonThird];
		[m setPronounType:NSGrammaticalPronounTypePossessive];
		check("the-eight-fields-round-trip-and-stop-being-unspecified",
		      ![m isUnspecified] && [m grammaticalGender] == NSGrammaticalGenderFeminine &&
		      [m number] == NSGrammaticalNumberPlural &&
		      [m partOfSpeech] == NSGrammaticalPartOfSpeechNoun &&
		      [m definiteness] == NSGrammaticalDefinitenessDefinite &&
		      [m determination] == NSGrammaticalDeterminationDependent &&
		      [m grammaticalCase] == NSGrammaticalCaseGenitive &&
		      [m grammaticalPerson] == NSGrammaticalPersonThird &&
		      [m pronounType] == NSGrammaticalPronounTypePossessive,
		      [NSString stringWithFormat:@"gender=%d number=%d pos=%d case=%d person=%d pronoun=%d",
			(int)[m grammaticalGender], (int)[m number], (int)[m partOfSpeech],
			(int)[m grammaticalCase], (int)[m grammaticalPerson], (int)[m pronounType]]);

		/* A COPY IS A VALUE, and the original keeps changing without the copy following. */
		{
			NSMorphology *copy = [m copy];

			[m setNumber:NSGrammaticalNumberSingular];
			[m setGrammaticalCase:NSGrammaticalCaseDative];
			check("a-copy-is-a-value-and-not-a-reference",
			      copy != m && [copy number] == NSGrammaticalNumberPlural &&
			      [copy grammaticalCase] == NSGrammaticalCaseGenitive &&
			      [m number] == NSGrammaticalNumberSingular,
			      [NSString stringWithFormat:@"copy.number=%d copy.case=%d original.number=%d",
				(int)[copy number], (int)[copy grammaticalCase], (int)[m number]]);
		}

		/* EQUALITY FOLLOWS THE FIELDS, and two morphologies that differ in one of them are not equal. */
		{
			NSMorphology *same = [[NSMorphology alloc] init];
			NSMorphology *other = [[NSMorphology alloc] init];
			NSMorphology *empty = [[NSMorphology alloc] init];

			[same setGrammaticalGender:NSGrammaticalGenderMasculine];
			[same setNumber:NSGrammaticalNumberSingular];
			[other setGrammaticalGender:NSGrammaticalGenderMasculine];
			[other setNumber:NSGrammaticalNumberSingular];
			check("equality-follows-the-fields",
			      [same isEqual:other] && [same hash] == [other hash] && ![same isEqual:empty],
			      [NSString stringWithFormat:@"same==other %d same==empty %d",
				(int)[same isEqual:other], (int)[same isEqual:empty]]);
		}

		/* SECURE CODING BOTH WAYS. */
		{
			id archiveData = [NSKeyedArchiver archivedDataWithRootObject:m];
			id back = nil;

			if (archiveData != nil) {
				back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
			}
			check("a-morphology-round-trips-through-a-keyed-archiver",
			      back != nil && [back isKindOfClass:[NSMorphology class]] && [back isEqual:m] &&
			      [NSMorphology supportsSecureCoding],
			      [NSString stringWithFormat:@"back=%@ equal=%d", back, (int)[back isEqual:m]]);
		}
	}

	/* +userMorphology IS A VALUE, UNSPECIFIED, AND FRESH EACH TIME. */
	{
		NSMorphology *first = [NSMorphology userMorphology];
		NSMorphology *second = [NSMorphology userMorphology];

		[first setNumber:NSGrammaticalNumberPlural];
		check("the-user-morphology-is-unspecified-and-fresh-each-call",
		      first != nil && second != nil && first != second && [second isUnspecified] &&
		      [second number] == NSGrammaticalNumberNotSet,
		      [NSString stringWithFormat:@"same=%d second.unspecified=%d second.number=%d",
			(int)(first == second), (int)[second isUnspecified], (int)[second number]]);
	}

	/* THE PER-LANGUAGE DOOR REFUSES BY NAME, WITH THE REASON IN THE ERROR. */
	{
		NSMorphology *m = [[NSMorphology alloc] init];
		NSMorphologyCustomPronoun *pronoun = [[NSMorphologyCustomPronoun alloc] init];
		NSError *error = nil;
		BOOL accepted = [m setCustomPronoun:pronoun forLanguage:@"en" error:&error];

		check("the-per-language-pronoun-door-refuses-by-name",
		      accepted == NO && error != nil &&
		      [[[error userInfo] objectForKey:NSLocalizedDescriptionKey]
			rangeOfString:@"en"].location != NSNotFound &&
		      [m customPronounForLanguage:@"en"] == nil,
		      [NSString stringWithFormat:@"accepted=%d error=%@", (int)accepted,
			[[error userInfo] objectForKey:NSLocalizedDescriptionKey]]);
	}

	/* THE CUSTOM-PRONOUN VALUE ITSELF IS REAL: five forms, a copy, the keys a caller builds against, and the
	 * honest NO about languages. */
	{
		NSMorphologyCustomPronoun *forms = [[NSMorphologyCustomPronoun alloc] init];
		NSArray *keys = [NSMorphologyCustomPronoun requiredKeysForLanguage:@"en"];
		NSMorphologyCustomPronoun *copy;

		[forms setSubjectForm:@"they"];
		[forms setObjectForm:@"them"];
		[forms setPossessiveForm:@"theirs"];
		[forms setPossessiveAdjectiveForm:@"their"];
		[forms setReflexiveForm:@"themself"];
		copy = [forms copy];
		[forms setSubjectForm:@"CHANGED"];
		check("a-custom-pronoun-carries-five-forms-and-copies-as-a-value",
		      [keys count] == 5 && [keys containsObject:@"subjectForm"] &&
		      [[copy subjectForm] isEqualToString:@"they"] &&
		      [[copy objectForm] isEqualToString:@"them"] &&
		      [[copy possessiveForm] isEqualToString:@"theirs"] &&
		      [[copy possessiveAdjectiveForm] isEqualToString:@"their"] &&
		      [[copy reflexiveForm] isEqualToString:@"themself"] &&
		      ![NSMorphologyCustomPronoun isSupportedForLanguage:@"en"],
		      [NSString stringWithFormat:@"keys=%lu subject=%@ supported=%d",
			(unsigned long)[keys count], [copy subjectForm],
			(int)[NSMorphologyCustomPronoun isSupportedForLanguage:@"en"]]);
	}

	/* A PRONOUN CARRIES THE MORPHOLOGY IT AGREES WITH, and a pronoun with no word is refused at the door. */
	{
		NSMorphology *agreesWith = [[NSMorphology alloc] init];
		NSMorphology *owner = [[NSMorphology alloc] init];
		NSMorphologyPronoun *pronoun;
		id archiveData;
		id back = nil;
		BOOL refused = NO;

		[agreesWith setGrammaticalPerson:NSGrammaticalPersonThird];
		[agreesWith setNumber:NSGrammaticalNumberPlural];
		[owner setGrammaticalGender:NSGrammaticalGenderNeuter];
		pronoun = [[NSMorphologyPronoun alloc] initWithPronoun:@"they"
							    morphology:agreesWith
						   dependentMorphology:owner];
		check("a-pronoun-carries-its-own-and-its-dependents-morphology",
		      [[pronoun pronoun] isEqualToString:@"they"] &&
		      [[pronoun morphology] grammaticalPerson] == NSGrammaticalPersonThird &&
		      [[pronoun morphology] number] == NSGrammaticalNumberPlural &&
		      [[pronoun dependentMorphology] grammaticalGender] == NSGrammaticalGenderNeuter,
		      [NSString stringWithFormat:@"pronoun=%@ person=%d dependent=%d", [pronoun pronoun],
			(int)[[pronoun morphology] grammaticalPerson],
			(int)[[pronoun dependentMorphology] grammaticalGender]]);
		archiveData = [NSKeyedArchiver archivedDataWithRootObject:pronoun];
		if (archiveData != nil) {
			back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
		}
		check("a-pronoun-round-trips-through-a-keyed-archiver",
		      back != nil && [[back pronoun] isEqualToString:@"they"] &&
		      [[back morphology] number] == NSGrammaticalNumberPlural &&
		      [[back dependentMorphology] grammaticalGender] == NSGrammaticalGenderNeuter,
		      [NSString stringWithFormat:@"back=%@ number=%d", [back pronoun], (int)[[back morphology] number]]);
		@try {
			(void)[[NSMorphologyPronoun alloc] initWithPronoun:@""
								morphology:agreesWith
						       dependentMorphology:nil];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("a-pronoun-with-no-word-is-refused-at-the-door",
		      refused,
		      [NSString stringWithFormat:@"refused=%d", (int)refused]);
	}

	printf("FOUNDATION-MORPHOLOGY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-MORPHOLOGY DONE\n");
	return failc == 0 ? 0 : 1;
}
