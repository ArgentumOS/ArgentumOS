/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_orthography — `NSOrthography`: two values, the invariant that binds them, and the one piece of
 * machinery (ICU's likely-subtags data) that determines a script from a language
 * (docs/design/foundation-plan.md §62.21). ONE unit, importing only <Foundation/Foundation.h>.
 *
 * IT EXISTS HERE BECAUSE §62.18 NEEDED IT: `NSTextCheckingResult` shipped Apple's whole surface, including
 * `+orthographyCheckingResultWithRange:orthography:` and its `-orthography` payload, and the class those two
 * members speak in did not exist — so the pair could only be handed nil. THE LAST CHECK IN THIS PROBE IS THAT
 * DOOR: an orthography goes INTO a text-checking result and comes back out of it.
 *
 * APPLE'S OWN DOCUMENTED EXAMPLE IS A CHECK RATHER THAN AN ILLUSTRATION: for Hindi, the map has one key, `Deva`,
 * whose array holds `hi`. That is the factory's whole job, and it is asserted beside four other languages whose
 * scripts the same table answers (Russian Cyrillic, Arabic Arabic, Japanese Jpan, English Latn).
 *
 * THE INVARIANT AND ITS FIVE REFUSALS ARE ASSERTED FROM BOTH SIDES: a map that carries the dominant script is
 * accepted, and the five ways it cannot are each caught by NAME (`NSInvalidArgumentException`), because the
 * refusal is this library's choice and a caller has to be able to see which one it hit.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-ORTHOGRAPHY"

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf(PREFIX " %s ok\n", name);
	} else {
		failc++;
		printf(PREFIX " %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* covers("NSLocale", "canonicalLocaleIdentifierFromString:") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* ONE REFUSAL, CAUGHT: returns YES when the block raised NSInvalidArgumentException. A macro rather than a
 * function because the block captures what the check needs. */
#define FN_REFUSED(...) ({ \
	BOOL fn_raised = NO; \
	@try { __VA_ARGS__; } @catch (NSException *fn_e) { \
		fn_raised = [[fn_e name] isEqualToString:NSInvalidArgumentException]; \
	} \
	fn_raised; \
})

/* A MAP OF TWO SCRIPTS, in an order the class must not depend on: the probe asks for sorted answers. */
static NSDictionary *fn_two_scripts(void)
{
	return [NSDictionary dictionaryWithObjectsAndKeys:
			[NSArray arrayWithObjects:@"en", @"fr", nil], @"Latn",
			[NSArray arrayWithObject:@"ru"], @"Cyrl",
			nil];
}

int main(void)
{
	/* ---- THE FACTORY, INCLUDING APPLE'S OWN EXAMPLE ------------------------------------------------- */
	{
		NSOrthography *hindi = [NSOrthography defaultOrthographyForLanguage:@"hi"];
		NSDictionary *map = [hindi languageMap];
		NSArray *devanagari = [hindi languagesForScript:@"Deva"];

		/* APPLE'S SENTENCE, VERBATIM IN EFFECT: "the default orthography for the Hindi language has a language map
		 * with a single key, Deva ..., that has a corresponding value of an array containing the element hi". */
		check("apple-s-example-for-hindi-holds-exactly",
		      hindi != nil && [[hindi dominantScript] isEqualToString:@"Deva"] &&
		      [map count] == 1 && [devanagari count] == 1 &&
		      [[devanagari objectAtIndex:0] isEqualToString:@"hi"] &&
		      [[hindi dominantLanguage] isEqualToString:@"hi"] &&
		      [[[hindi allScripts] objectAtIndex:0] isEqualToString:@"Deva"],
		      [[NSString stringWithFormat:@"script=%@ map=%@ languages=%@ language=%@",
			[hindi dominantScript], map, devanagari, [hindi dominantLanguage]] UTF8String]);
	}
	{
		NSOrthography *russian = [NSOrthography defaultOrthographyForLanguage:@"ru"];
		NSOrthography *arabic = [NSOrthography defaultOrthographyForLanguage:@"ar"];
		NSOrthography *japanese = [NSOrthography defaultOrthographyForLanguage:@"ja"];
		NSOrthography *english = [NSOrthography defaultOrthographyForLanguage:@"en"];

		check("four-more-languages-get-the-script-the-same-table-answers",
		      russian != nil && arabic != nil && japanese != nil && english != nil &&
		      [[russian dominantScript] isEqualToString:@"Cyrl"] &&
		      [[russian dominantLanguage] isEqualToString:@"ru"] &&
		      [[arabic dominantScript] isEqualToString:@"Arab"] &&
		      [[japanese dominantScript] isEqualToString:@"Jpan"] &&
		      [[english dominantScript] isEqualToString:@"Latn"],
		      [[NSString stringWithFormat:@"ru=%@ ar=%@ ja=%@ en=%@",
			[russian dominantScript], [arabic dominantScript], [japanese dominantScript],
			[english dominantScript]] UTF8String]);
	}
	{
		/* THE TWO CASES THAT ARE OURS, both stated in the header: a tag ICU's data does not know at all becomes the
		 * ISO 15924 code for an undetermined script, and an EMPTY tag is refused. */
		NSOrthography *unknown = [NSOrthography defaultOrthographyForLanguage:@"xx"];
		BOOL emptyRefused = FN_REFUSED([NSOrthography defaultOrthographyForLanguage:@""]);

		check("an-unknown-tag-falls-back-and-an-empty-one-is-refused",
		      unknown != nil && [[unknown dominantScript] isEqualToString:@"Zyyy"] &&
		      [[unknown dominantLanguage] isEqualToString:@"xx"] && emptyRefused,
		      [[NSString stringWithFormat:@"unknown=%@ language=%@ emptyRefused=%d",
			[unknown dominantScript], [unknown dominantLanguage], (int)emptyRefused] UTF8String]);
	}

	/* ---- THE INVARIANT, FROM BOTH SIDES ------------------------------------------------------------- */
	{
		NSDictionary *map = fn_two_scripts();
		NSOrthography *made = [NSOrthography orthographyWithDominantScript:@"Cyrl" languageMap:map];
		NSOrthography *sameMap = [NSOrthography orthographyWithDominantScript:@"Latn" languageMap:map];

		check("the-identical-door-builds-what-the-initializer-does",
		      made != nil && [[made dominantScript] isEqualToString:@"Cyrl"] &&
		      [[made dominantLanguage] isEqualToString:@"ru"] &&
		      sameMap != nil && [[sameMap dominantLanguage] isEqualToString:@"en"],
		      [[NSString stringWithFormat:@"cyrl=%@ latin=%@",
			made != nil ? [made dominantLanguage] : @"(nil)",
			sameMap != nil ? [sameMap dominantLanguage] : @"(nil)"] UTF8String]);
	}
	{
		/* AN `id` LOCAL HOLDS THE DELIBERATE NIL: a cast to the declared type is STILL nonnull inside an
		 * NS_ASSUME_NONNULL region, so the -Wnonnull warning survives it, while a local makes no nullability
		 * claim at all — the idiom foundation_core records for exactly this. */
		id noScriptValue = nil;
		id noMapValue = nil;
		BOOL noScript = FN_REFUSED([NSOrthography orthographyWithDominantScript:noScriptValue
									 languageMap:fn_two_scripts()]);
		BOOL noMap = FN_REFUSED([NSOrthography orthographyWithDominantScript:@"Latn"
									  languageMap:noMapValue]);
		BOOL notThere = FN_REFUSED([NSOrthography orthographyWithDominantScript:@"Grek"
									     languageMap:fn_two_scripts()]);
		BOOL notAnArray = FN_REFUSED([NSOrthography orthographyWithDominantScript:@"Latn"
			languageMap:[NSDictionary dictionaryWithObject:@"en" forKey:@"Latn"]]);
		BOOL emptyList = FN_REFUSED([NSOrthography orthographyWithDominantScript:@"Latn"
			languageMap:[NSDictionary dictionaryWithObject:[NSArray array]
							      forKey:@"Latn"]]);

		check("the-five-ways-the-invariant-can-be-broken-each-refuse-by-name",
		      noScript && noMap && notThere && notAnArray && emptyList,
		      [[NSString stringWithFormat:@"nilScript=%d nilMap=%d absent=%d notArray=%d empty=%d",
			(int)noScript, (int)noMap, (int)notThere, (int)notAnArray, (int)emptyList]
			UTF8String]);
	}

	/* ---- THE DERIVED ANSWERS, AND THE ORDER THAT IS OURS --------------------------------------------- */
	{
		NSOrthography *two = [NSOrthography orthographyWithDominantScript:@"Latn"
								      languageMap:fn_two_scripts()];

		check("all-scripts-are-sorted-and-all-languages-are-grouped-by-them",
		      [[two allScripts] count] == 2 &&
		      [[[two allScripts] objectAtIndex:0] isEqualToString:@"Cyrl"] &&
		      [[[two allScripts] objectAtIndex:1] isEqualToString:@"Latn"] &&
		      [[two allLanguages] count] == 3 &&
		      [[[two allLanguages] objectAtIndex:0] isEqualToString:@"ru"] &&
		      [[[two allLanguages] objectAtIndex:1] isEqualToString:@"en"] &&
		      [[[two allLanguages] objectAtIndex:2] isEqualToString:@"fr"],
		      [[NSString stringWithFormat:@"scripts=%@ languages=%@",
			[two allScripts], [two allLanguages]] UTF8String]);
	}
	{
		NSOrthography *two = [NSOrthography orthographyWithDominantScript:@"Latn"
								      languageMap:fn_two_scripts()];
		NSArray *latin = [two languagesForScript:@"Latn"];

		check("the-script-queries-answer-in-the-array-s-own-order-and-miss-with-nil",
		      [latin count] == 2 && [[latin objectAtIndex:0] isEqualToString:@"en"] &&
		      [[latin objectAtIndex:1] isEqualToString:@"fr"] &&
		      [[two dominantLanguageForScript:@"Latn"] isEqualToString:@"en"] &&
		      [[two dominantLanguageForScript:@"Cyrl"] isEqualToString:@"ru"] &&
		      [two languagesForScript:@"Grek"] == nil &&
		      [two dominantLanguageForScript:@"Grek"] == nil &&
		      [two languagesForScript:nil] == nil,
		      [[NSString stringWithFormat:@"latn=%@ dominant=%@ miss=%d",
			latin, [two dominantLanguageForScript:@"Latn"],
			(int)([two languagesForScript:@"Grek"] == nil)] UTF8String]);
	}

	/* ---- THE OBJECT PROTOCOL ------------------------------------------------------------------------ */
	{
		NSOrthography *a = [NSOrthography orthographyWithDominantScript:@"Latn"
								      languageMap:fn_two_scripts()];
		NSOrthography *b = [NSOrthography orthographyWithDominantScript:@"Latn"
								      languageMap:fn_two_scripts()];
		NSOrthography *c = [NSOrthography orthographyWithDominantScript:@"Cyrl"
								      languageMap:fn_two_scripts()];
		id copied = [a copy];

		check("a-value-compares-by-its-two-fields-and-copies-to-itself",
		      [a isEqual:b] && [a hash] == [b hash] && ![a isEqual:c] && ![a isEqual:@"not an orthography"] &&
		      copied == a && [a isEqual:a] && [[a description] length] > 0,
		      "two orthographies with the same script and map are equal, and -copy is the receiver");
	}
	{
		/* THE CLASS THIS ONE WAS BUILT FOR: §62.18's orthography door, which could only be handed nil before
		 * today. */
		NSOrthography *hindi = [NSOrthography defaultOrthographyForLanguage:@"hi"];
		NSTextCheckingResult *result =
			[NSTextCheckingResult orthographyCheckingResultWithRange:NSMakeRange(3, 9)
								     orthography:hindi];
		NSTextCheckingResult *plain = [NSTextCheckingResult spellCheckingResultWithRange:NSMakeRange(0, 1)];

		check("a-text-checking-result-carries-an-orthography-through-its-own-door",
		      result != nil && [result resultType] == NSTextCheckingTypeOrthography &&
		      [[result orthography] isEqual:hindi] &&
		      [[[result orthography] dominantScript] isEqualToString:@"Deva"] &&
		      [plain orthography] == nil,
		      [[NSString stringWithFormat:@"type=0x%llx orthography=%@",
			(unsigned long long)[result resultType], [result orthography]] UTF8String]);
	}

	/* ---- CODING ------------------------------------------------------------------------------------- */
	{
		NSOrthography *two = [NSOrthography orthographyWithDominantScript:@"Cyrl"
								      languageMap:fn_two_scripts()];
		NSMutableData *data = [NSMutableData dataWithCapacity:64];
		NSKeyedArchiver *archiver = [[NSKeyedArchiver alloc] initForWritingWithMutableData:data];
		NSKeyedUnarchiver *unarchiver;
		id restored = nil;

		[archiver encodeObject:two forKey:@"orthography"];
		[archiver finishEncoding];
		unarchiver = [[NSKeyedUnarchiver alloc] initForReadingWithData:data];
		restored = [unarchiver decodeObjectForKey:@"orthography"];
		check("the-two-values-survive-an-archive-and-answer-again",
		      [data length] > 0 && [restored isKindOfClass:[NSOrthography class]] &&
		      [restored isEqual:two] &&
		      [[(NSOrthography *)restored dominantLanguage] isEqualToString:@"ru"] &&
		      [NSOrthography supportsSecureCoding],
		      [[NSString stringWithFormat:@"bytes=%lu restored=%@",
			(unsigned long)[data length], restored] UTF8String]);
	covers("NSMutableData", "dataWithCapacity:");
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
