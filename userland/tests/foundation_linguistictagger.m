/*
 * foundation_linguistictagger.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSLinguisticTagger — §62.42's acceptance, and THE LARGEST SINGLE FAMILY ON §62.24'S WORK LIST: one deprecated
 * class and FIFTY-ONE VOCABULARY NAMES.
 *
 * WHAT IT PINS, AND WHY THE VOCABULARY IS PINNED IN FULL RATHER THAN SAMPLED: the tag values are this library's
 * under §11.6.1 D2, so the contract they carry is BETWEEN THE HEADER AND THIS FILE — every constant is checked to
 * exist, to be non-empty and to be DISTINCT WITHIN ITS SCHEME, which is the property a caller's switch depends on
 * and the one a copy-paste in the header would silently break.
 *
 * AND WHAT IT PINS ABOUT THE HONEST HALF: the two schemes this library answers are properties of the text
 * (TokenType and Script), the four omissions drop what they say, and THE MORPHOLOGICAL SCHEMES AND JoinNames ARE
 * CHECKED TO DO NOTHING — as measurements, not as claims, because a refusal that is only described in a comment is
 * a refusal nobody can tell from an omission.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-LINGUISTICTAGGER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-LINGUISTICTAGGER %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* ALL OF THESE EXIST, ARE NON-EMPTY, AND ARE DIFFERENT FROM EACH OTHER - the three properties a vocabulary has to
 * have, checked in one place so a group's check reads as one sentence rather than twenty. */
static BOOL fn_vocabulary(NSArray *names, NSString **offender)
{
	NSMutableSet *seen = [[NSMutableSet alloc] init];
	NSUInteger i;

	for(i = 0; i < [names count]; i++) {
		id name = [names objectAtIndex:i];

		if(name == nil || [name length] == 0 || [seen containsObject:name]) {
			if(offender != NULL) {
				*offender = name != nil ? name : @"(nil)";
			}
			return NO;
		}
		[seen addObject:name];
	}
	return YES;
}

/* WHAT A TAGGER SAYS ABOUT ONE PIECE OF TEXT, as a string of "tag" tokens in order: the shape every classification
 * check below compares. */
static NSString *fn_tag_sequence(NSString *text, NSLinguisticTagScheme scheme, NSLinguisticTaggerOptions options)
{
	NSLinguisticTagger *tagger = [[NSLinguisticTagger alloc] initWithTagSchemes:
		[NSArray arrayWithObject:scheme] options:0];
	NSMutableArray *seen = [[NSMutableArray alloc] init];

	if([tagger respondsToSelector:@selector(setString:)]) {
		[tagger setString:text];
	}
	[tagger enumerateTagsInRange:NSMakeRange(0, [text length])
			      scheme:scheme
			     options:options
			  usingBlock:^(NSLinguisticTag tag, NSRange tokenRange, BOOL *stop) {
		(void)tokenRange;
		(void)stop;
		[seen addObject:tag != nil ? tag : @"(nil)"];
	}];
	return [seen componentsJoinedByString:@" "];
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE VOCABULARY, IN THE THREE GROUPS THE SCHEMES PUT IT IN ---------------------------------- */
	{
		NSString *offender = nil;
		BOOL schemes = fn_vocabulary([NSArray arrayWithObjects:
			NSLinguisticTagSchemeTokenType, NSLinguisticTagSchemeLexicalClass,
			NSLinguisticTagSchemeNameType, NSLinguisticTagSchemeNameTypeOrLexicalClass,
			NSLinguisticTagSchemeLemma, NSLinguisticTagSchemeLanguage,
			NSLinguisticTagSchemeScript, nil], &offender);

		check("the-seven-schemes-are-pinned",
		      schemes && [NSLinguisticTagSchemeTokenType isEqualToString:@"TokenType"] &&
		      [NSLinguisticTagSchemeNameTypeOrLexicalClass isEqualToString:@"NameTypeOrLexicalClass"],
		      [NSString stringWithFormat:@"seven schemes, all non-empty and distinct, and the two spellings a "
			@"caller is most likely to compare against are the ones Apple's own documentation prints "
			@"(offender: %@)", offender]);

		offender = nil;
		check("the-token-types-and-their-refinements-are-pinned",
		      fn_vocabulary([NSArray arrayWithObjects:
			NSLinguisticTagWord, NSLinguisticTagWhitespace, NSLinguisticTagPunctuation,
			NSLinguisticTagOther, NSLinguisticTagSentenceTerminator, NSLinguisticTagOpenQuote,
			NSLinguisticTagCloseQuote, NSLinguisticTagOpenParenthesis,
			NSLinguisticTagCloseParenthesis, NSLinguisticTagWordJoiner,
			NSLinguisticTagDash, NSLinguisticTagOtherPunctuation,
			NSLinguisticTagParagraphBreak, NSLinguisticTagOtherWhitespace,
			NSLinguisticTagOtherWord, nil], &offender) &&
		      [NSLinguisticTagWord isEqualToString:@"Word"],
		      @"fifteen token-type tags, distinct within the scheme they share");

		offender = nil;
		check("the-lexical-classes-and-the-name-types-are-pinned",
		      fn_vocabulary([NSArray arrayWithObjects:
			NSLinguisticTagNoun, NSLinguisticTagVerb, NSLinguisticTagAdjective,
			NSLinguisticTagAdverb, NSLinguisticTagPronoun, NSLinguisticTagDeterminer,
			NSLinguisticTagParticle, NSLinguisticTagPreposition, NSLinguisticTagNumber,
			NSLinguisticTagConjunction, NSLinguisticTagInterjection,
			NSLinguisticTagClassifier, NSLinguisticTagIdiom, NSLinguisticTagPersonalName,
			NSLinguisticTagPlaceName, NSLinguisticTagOrganizationName, nil], &offender),
		      @"thirteen lexical classes and three name types - the schemes this library REFUSES, whose "
		      @"vocabulary a ported caller's switch still has to name");
	}

	/* --- THE NUMBERS, WHICH ARE OURS UNDER D2 -------------------------------------------------------- */
	{
		check("the-four-units-are-pinned",
		      NSLinguisticTaggerUnitWord == 0 && NSLinguisticTaggerUnitSentence == 1 &&
		      NSLinguisticTaggerUnitParagraph == 2 && NSLinguisticTaggerUnitDocument == 3,
		      @"word, sentence, paragraph, document - the values are this library's, and the probe is where "
		      @"they are fixed (D2)");
		check("the-five-options-are-single-bits",
		      NSLinguisticTaggerOmitWhitespace == 1 && NSLinguisticTaggerOmitPunctuation == 2 &&
		      NSLinguisticTaggerOmitOther == 4 && NSLinguisticTaggerOmitWords == 8 &&
		      NSLinguisticTaggerJoinNames == 16,
		      @"a mask of five distinct single bits");
	}

	/* --- WHAT THE TAGGER SAYS IT CAN DO, BEFORE ANYTHING IS BUILT ----------------------------------- */
	{
		NSArray *words = [NSLinguisticTagger availableTagSchemesForUnit:NSLinguisticTaggerUnitWord language:@"en"];
		NSArray *sentences = [NSLinguisticTagger availableTagSchemesForUnit:NSLinguisticTaggerUnitSentence language:@"en"];

		check("available-schemes-is-the-honest-list",
		      words != nil && [words count] == 2 &&
		      [words containsObject:NSLinguisticTagSchemeTokenType] &&
		      [words containsObject:NSLinguisticTagSchemeScript] &&
		      sentences != nil && [sentences count] == 0,
		      [NSString stringWithFormat:@"a word has two schemes (%@) and a sentence has none - and NO list "
			@"contains a morphological scheme, which is where their refusal is readable before anything "
			@"is built", [words componentsJoinedByString:@","]]);
	}

	/* --- THE TAGGER'S HALF: TOKEN TYPES, REFINEMENTS, SCRIPT ---------------------------------------- */
	{
		NSString *text = @"Hello, world. 42";
		NSString *sequence = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType, 0);

		check("the-token-types-are-classified",
		      [[text substringWithRange:NSMakeRange(0, 5)] length] == 5 &&
		      [sequence rangeOfString:@"Word"].location != NSNotFound &&
		      [sequence rangeOfString:@"Whitespace"].location != NSNotFound &&
		      [sequence rangeOfString:@"SentenceTerminator"].location != NSNotFound &&
		      [sequence rangeOfString:@"Other"].location != NSNotFound,
		      [NSString stringWithFormat:@"the sequence is: %@", sequence]);
		{
			NSString *quoted = fn_tag_sequence(@"\"quote\"", NSLinguisticTagSchemeTokenType, 0);
			NSString *parens = fn_tag_sequence(@"(x)", NSLinguisticTagSchemeTokenType, 0);
			NSString *dashed = fn_tag_sequence(@"-", NSLinguisticTagSchemeTokenType, 0);

			/* `quote=OpenQuote Word CloseQuote` IS WHAT THIS NOW READS, and the check is the same one that
			 * showed the OLD answer. */
			check("the-refined-punctuation-has-its-own-tags",
			      [quoted rangeOfString:@"OpenQuote"].location != NSNotFound &&
			      [quoted rangeOfString:@"CloseQuote"].location != NSNotFound &&
			      [parens rangeOfString:@"OpenParenthesis"].location != NSNotFound &&
			      [dashed rangeOfString:@"Dash"].location != NSNotFound,
			      [NSString stringWithFormat:@"quote=%@ parens=%@ dash=%@", quoted, parens, dashed]);
		}
		check("the-script-is-icus-short-name",
		      [fn_tag_sequence(@"abc", NSLinguisticTagSchemeScript, 0) isEqualToString:@"Latn"] &&
		      [fn_tag_sequence(@"\u65e5\u672c", NSLinguisticTagSchemeScript, 0) isEqualToString:@"Hani"],
		      /* "Hani" AND NOT "Han": the first version of this check expected the name a human would write and
		       * ICU answers its own SHORT name, which is what the implementation returns. THE CODE WAS RIGHT AND
		       * THE EXPECTATION WAS WRONG - which is the one kind of failure a check with a message this detailed
		       * makes obvious, and the reason the message prints both answers. */
		      [NSString stringWithFormat:@"Latin text answers %@ and Han text answers %@",
			fn_tag_sequence(@"abc", NSLinguisticTagSchemeScript, 0),
			fn_tag_sequence(@"\u65e5\u672c", NSLinguisticTagSchemeScript, 0)]);
	}

	/* --- THE REFUSALS, AS MEASUREMENTS RATHER THAN CLAIMS ------------------------------------------- */
	{
		NSLinguisticTagger *tagger = [[NSLinguisticTagger alloc] initWithTagSchemes:
			[NSArray arrayWithObject:NSLinguisticTagSchemeTokenType] options:0];
		NSString *text = @"The cat sat on the mat.";
		NSRange token = NSMakeRange(NSNotFound, 0);
		NSLinguisticTag tokenType;

		[tagger setString:text];
		tokenType = [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeTokenType
				    tokenRange:&token sentenceRange:NULL];
		check("the-morphological-schemes-answer-nothing",
		      tokenType != nil && [tokenType isEqualToString:NSLinguisticTagWord] &&
		      token.length == 3 &&
		      [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeLexicalClass
			     tokenRange:NULL sentenceRange:NULL] == nil &&
		      [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeNameType
			     tokenRange:NULL sentenceRange:NULL] == nil &&
		      [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeNameTypeOrLexicalClass
			     tokenRange:NULL sentenceRange:NULL] == nil &&
		      [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeLemma
			     tokenRange:NULL sentenceRange:NULL] == nil &&
		      [tagger tagAtIndex:4 scheme:NSLinguisticTagSchemeLanguage
			     tokenRange:NULL sentenceRange:NULL] == nil,
		      @"the SAME token answers Word under TokenType and NOTHING under LexicalClass, NameType, "
		      @"NameTypeOrLexicalClass, Lemma or Language - which is what a refusal looks like when it is "
		      @"measured next to a success");
	}

	/* --- THE OMISSIONS, AND THE OPTION THAT DOES NOTHING ------------------------------------------- */
	{
		NSString *text = @"one two. three";
		NSString *all = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType, 0);
		NSString *noSpace = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType,
						    NSLinguisticTaggerOmitWhitespace);
		NSString *noPunct = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType,
						    NSLinguisticTaggerOmitPunctuation);
		NSString *noWords = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType,
						    NSLinguisticTaggerOmitWords);
		NSString *joined = fn_tag_sequence(text, NSLinguisticTagSchemeTokenType,
						   NSLinguisticTaggerJoinNames);

		check("the-omissions-drop-what-they-say",
		      [all rangeOfString:@"Whitespace"].location != NSNotFound &&
		      [noSpace rangeOfString:@"Whitespace"].location == NSNotFound &&
		      [noPunct rangeOfString:@"SentenceTerminator"].location == NSNotFound &&
		      [noWords rangeOfString:@"(nil)"].location != NSNotFound &&
		      [joined isEqualToString:all],
		      [NSString stringWithFormat:@"all=%@ / noWhitespace=%@ / noPunctuation=%@ / noWords=%@ - and "
			@"JoinNames changes NOTHING, which is the documented limitation measured rather than "
			@"asserted", all, noSpace, noPunct, noWords]);
	}

	/* --- THE SENTENCE AND THE ORTHOGRAPHY, WHICH ARE NOT TAGGING QUESTIONS -------------------------- */
	{
		NSLinguisticTagger *tagger = [[NSLinguisticTagger alloc] initWithTagSchemes:
			[NSArray arrayWithObject:NSLinguisticTagSchemeTokenType] options:0];
		NSString *text = @"First one. Second two.";
		NSRange first = NSMakeRange(NSNotFound, 0);
		NSRange second = NSMakeRange(NSNotFound, 0);
		/* A REAL MAP, BECAUSE THE CLASS REQUIRES ONE: `-initWithDominantScript:languageMap:` RAISES for a nil map
		 * (its own header says a map is required, and its implementation says so in those words). The first
		 * version of this probe passed nil and died with an uncaught exception - WHICH WAS THE PROBE'S BUG AND
		 * THE LIBRARY BEHAVING CORRECTLY, the third time in this session that a check's premise was the thing
		 * that was wrong. */
		NSDictionary *map = [NSDictionary dictionaryWithObject:[NSArray arrayWithObject:@"en"]
							       forKey:@"Latn"];
		NSOrthography *orthography = [[NSOrthography alloc] initWithDominantScript:@"Latn"
									 languageMap:map];
		NSRange effective = NSMakeRange(NSNotFound, 0);
		NSRangePointer where = &effective;

		[tagger setString:text];
		first = [tagger sentenceRangeForRange:NSMakeRange(2, 0)];
		second = [tagger sentenceRangeForRange:NSMakeRange(13, 0)];
		check("the-sentence-range-is-the-sentence",
		      first.location == 0 && first.length == 11 && second.location == 11 &&
		      second.length == 11,
		      [NSString stringWithFormat:@"the first sentence is %d@%d and the second is %d@%d",
			(int)first.length, (int)first.location, (int)second.length, (int)second.location]);

		[tagger setOrthography:orthography range:NSMakeRange(0, 5)];
		check("the-orthography-doors-round-trip",
		      [[tagger orthographyAtIndex:2 effectiveRange:where] isEqual:orthography] &&
		      effective.location == 0 && effective.length == 5 &&
		      [tagger orthographyAtIndex:8 effectiveRange:NULL] == nil,
		      @"the orthography comes back for its own range and NOTHING comes back outside it");
	}

	printf("FOUNDATION-LINGUISTICTAGGER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-LINGUISTICTAGGER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-LINGUISTICTAGGER DONE\n");
	return failc ? 1 : 0;
}
