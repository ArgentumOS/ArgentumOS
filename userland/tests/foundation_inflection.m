/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_inflection — §62.79's acceptance: NSInflectionRule, NSInflectionRuleExplicit and NSTermOfAddress,
 * which with §62.78's three classes complete `Fundamentals / Automatic grammar agreement`.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE TWO HALVES OF THIS CLASS ARE THE SAME SHAPE AS THE MORPHOLOGY HALF: what is real is the VALUE (a rule that
 * carries a morphology, a term that carries a language and a pronoun list - copied, compared, archived), and what
 * is absent is the ENGINE, which the two capability doors report as a FACT ("can this impose inflect language X?"
 * - no, because nothing here agrees a grammar).
 *
 * AND ONE CHECK ASSERTS THE STATE THE HEADER STATES, rather than something flattering: the three PREDEFINED TERMS
 * ARE EQUAL in this system, because the only field that distinguishes them is the pronoun list it does not carry.
 * That is a boundary made visible where a reader can see it, not a gap hidden behind a passing check.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-INFLECTION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-INFLECTION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	/* THE TWO CAPABILITY DOORS: NO, AS A FACT ABOUT THIS SYSTEM. */
	check("the-capability-doors-answer-no-for-any-language",
	      ![NSInflectionRule canInflectLanguage:@"en"] && ![NSInflectionRule canInflectLanguage:@"de"] &&
	      ![NSInflectionRule canInflectPreferredLocalization],
	      [NSString stringWithFormat:@"en=%d preferred=%d",
		(int)[NSInflectionRule canInflectLanguage:@"en"],
		(int)[NSInflectionRule canInflectPreferredLocalization]]);

	/* THE AUTOMATIC RULE IS A VALUE: fresh each call, copyable, secure-codable. */
	{
		NSInflectionRule *rule = [NSInflectionRule automaticRule];
		NSInflectionRule *again = [NSInflectionRule automaticRule];
		NSInflectionRule *copy = [rule copy];

		check("the-automatic-rule-is-a-value-and-fresh-each-call",
		      rule != nil && again != nil && rule != again && copy != rule && [copy isEqual:rule] &&
		      [NSInflectionRule supportsSecureCoding],
		      [NSString stringWithFormat:@"same=%d copy==rule %d", (int)(rule == again),
			(int)[copy isEqual:rule]]);
	}

	/* AN EXPLICIT RULE CARRIES THE MORPHOLOGY IT WOULD INFLECT WITH, as a COPY. */
	{
		NSMorphology *morphology = [[NSMorphology alloc] init];
		NSInflectionRuleExplicit *rule;
		id archiveData;
		id back = nil;

		[morphology setGrammaticalGender:NSGrammaticalGenderFeminine];
		[morphology setNumber:NSGrammaticalNumberPlural];
		rule = [[NSInflectionRuleExplicit alloc] initWithMorphology:morphology];
		[morphology setNumber:NSGrammaticalNumberSingular];	/* the rule must not follow */
		check("an-explicit-rule-carries-a-copy-of-its-morphology",
		      [[rule morphology] grammaticalGender] == NSGrammaticalGenderFeminine &&
		      [[rule morphology] number] == NSGrammaticalNumberPlural &&
		      ![rule isEqual:[NSInflectionRule automaticRule]],
		      [NSString stringWithFormat:@"gender=%d number=%d",
			(int)[[rule morphology] grammaticalGender], (int)[[rule morphology] number]]);

		archiveData = [NSKeyedArchiver archivedDataWithRootObject:rule];
		if (archiveData != nil) {
			back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
		}
		check("an-explicit-rule-round-trips-through-a-keyed-archiver",
		      back != nil && [back isKindOfClass:[NSInflectionRuleExplicit class]] &&
		      [[back morphology] number] == NSGrammaticalNumberPlural,
		      [NSString stringWithFormat:@"back=%@ number=%d", back, (int)[[back morphology] number]]);
	}

	/* THE FOUR TERMS WITH NOTHING STATED, AND THE STATE THE HEADER STATES: the three predefined ones are EQUAL
	 * here, because the field that would distinguish them is the pronoun data this system does not carry. */
	{
		NSTermOfAddress *feminine = [NSTermOfAddress feminine];
		NSTermOfAddress *masculine = [NSTermOfAddress masculine];
		NSTermOfAddress *neutral = [NSTermOfAddress neutral];
		NSTermOfAddress *user = [NSTermOfAddress currentUser];

		check("the-predefined-terms-are-distinct-values-with-nothing-stated",
		      feminine != nil && masculine != nil && neutral != nil && user != nil &&
		      feminine != masculine && [feminine languageIdentifier] == nil &&
		      [feminine pronouns] == nil && [user pronouns] == nil,
		      [NSString stringWithFormat:@"language=%@ pronouns=%@", [feminine languageIdentifier],
			[feminine pronouns]]);
		check("the-predefined-terms-are-equal-because-the-pronoun-data-is-absent",
		      [feminine isEqual:neutral] && [neutral isEqual:masculine] && [feminine hash] == [neutral hash],
		      [NSString stringWithFormat:@"feminine==neutral %d neutral==masculine %d",
			(int)[feminine isEqual:neutral], (int)[neutral isEqual:masculine]]);
	}

	/* A LOCALIZED TERM IS THE ONE DOOR THAT BUILDS A TERM FROM REAL DATA, and it snapshots what it is given. */
	{
		NSMutableArray *pronouns = [[NSMutableArray alloc] init];
		NSMorphology *agreesWith = [[NSMorphology alloc] init];
		NSMorphologyPronoun *one = [[NSMorphologyPronoun alloc] initWithPronoun:@"they"
									    morphology:agreesWith
								   dependentMorphology:nil];
		NSTermOfAddress *term;
		id archiveData;
		id back = nil;

		[pronouns addObject:one];
		term = [NSTermOfAddress localizedForLanguageIdentifier:@"en" withPronouns:pronouns];
		[pronouns addObject:one];	/* the term must not follow */
		check("a-localized-term-carries-its-language-and-a-copy-of-its-pronouns",
		      [[term languageIdentifier] isEqualToString:@"en"] && [[term pronouns] count] == 1 &&
		      [[[term pronouns] objectAtIndex:0] isEqual:one],
		      [NSString stringWithFormat:@"language=%@ pronouns=%lu", [term languageIdentifier],
			(unsigned long)[[term pronouns] count]]);

		archiveData = [NSKeyedArchiver archivedDataWithRootObject:term];
		if (archiveData != nil) {
			back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
		}
		check("a-localized-term-round-trips-through-a-keyed-archiver",
		      back != nil && [[back languageIdentifier] isEqualToString:@"en"] &&
		      [[back pronouns] count] == 1,
		      [NSString stringWithFormat:@"back=%@ language=%@", back, [back languageIdentifier]]);

		/* AND THE SAME FIELDS DECIDE EQUALITY: two terms built the same way are equal, and neither equals a
		 * predefined term or a term in another language. */
		check("terms-compare-by-their-fields",
		      [term isEqual:[NSTermOfAddress localizedForLanguageIdentifier:@"en"
							       withPronouns:[NSArray arrayWithObject:one]]] &&
		      ![term isEqual:[NSTermOfAddress neutral]] &&
		      ![term isEqual:[NSTermOfAddress localizedForLanguageIdentifier:@"de"
							       withPronouns:[NSArray arrayWithObject:one]]],
		      [NSString stringWithFormat:@"same-language-and-pronouns %d vs-neutral %d vs-other-language %d",
			(int)[term isEqual:[NSTermOfAddress localizedForLanguageIdentifier:@"en"
										 withPronouns:[NSArray arrayWithObject:one]]],
			(int)[term isEqual:[NSTermOfAddress neutral]],
			(int)[term isEqual:[NSTermOfAddress localizedForLanguageIdentifier:@"de"
										 withPronouns:[NSArray arrayWithObject:one]]]]);
	}

	/* A MISSING LANGUAGE IS REFUSED AT THE DOOR. */
	{
		BOOL refused = NO;

		@try {
			(void)[NSTermOfAddress localizedForLanguageIdentifier:@"" withPronouns:[NSArray array]];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("a-localized-term-refuses-a-missing-language",
		      refused,
		      [NSString stringWithFormat:@"refused=%d", (int)refused]);
	}

	printf("FOUNDATION-INFLECTION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-INFLECTION DONE\n");
	return failc == 0 ? 0 : 1;
}
