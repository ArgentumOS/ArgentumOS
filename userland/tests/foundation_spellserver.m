/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_spellserver.m — THE PROBE FOR NSSpellServer (§62.85), the server side of a spell-checking service.
 *
 * IT LINKS NOTHING BUT FOUNDATION, AND THAT IS THE POINT: the engine is DEFERRED (docs/design/spelling-plan.md §3
 * — the first candidate was withdrawn on LANGUAGE grounds, because a word list cannot recognise an inflection or a
 * compound, nor find where a Chinese or Thai word ends). This class is the API a SERVICE implements, so it can be
 * complete and tested while the engine question is open — nothing here knows what a word is.
 *
 * THE DISPATCH SEAM IS DRIVEN DIRECTLY, WHICH IS WHAT MAKES THE PROTOCOL LIVE. `NSSpellServerDelegate`'s seven
 * doors are reached by a CLIENT, and no client is shipped (Apple's wire is a private distributed-objects protocol),
 * so a probe that only compiled the protocol would assert nothing. Every door below is exercised through
 * `FNSpellServerDispatch*` — the same functions a future client protocol will call.
 *
 * WAITING IS NOT NEEDED HERE: nothing in this probe is asynchronous. The one door that never returns, `-run`, is
 * asserted PRESENT and deliberately not called, with its ground stated where it is skipped.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNSpellServerDispatch.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-SPELLSERVER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-SPELLSERVER %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* AN OPTIONAL DOOR'S PRESENCE IS ASKED OF THE PROTOCOL OBJECT (the idiom the URL probes record): a protocol's
 * methods are DECLARED, and `protocol_getMethodDescription` with isRequiredMethod=NO is the door that answers
 * whether a declaration is there. */
static BOOL fn_protocol_has_optional(Protocol *p, SEL sel)
{
	struct objc_method_description d = protocol_getMethodDescription(p, sel, NO, YES);

	return d.name != NULL;
}

/* THE DELEGATE: EVERY OPTIONAL DOOR IS WRITTEN OUT, because a door that is never implemented is a door the seam's
 * call to `-respondsToSelector:` cannot be shown to guard. It also answers with recognisable values, so a check can
 * say WHOSE answer it saw. */
@interface FNProbeSpellDelegate : NSObject <NSSpellServerDelegate>
{
@public
	int finds, guesses, completions, grammars, unifieds, learns, forgets;
	int lastWordCount;
	NSInteger lastUnifiedWordCount;
}
@end

@implementation FNProbeSpellDelegate

- (NSRange)spellServer:(NSSpellServer *)sender
    findMisspelledWordInString:(NSString *)stringToCheck
		      language:(NSString *)language
		     wordCount:(int *)wordCount
		     countOnly:(BOOL)countOnly
{
	(void)sender;
	(void)language;
	(void)countOnly;
	finds++;
	*wordCount = 3;
	lastWordCount = 3;
	/* A DELIBERATE, RECOGNISABLE RANGE: the caller must see the DELEGATE's answer, not a default of the class's. */
	return [stringToCheck rangeOfString:@"mispelt"];
}

- (NSArray *)spellServer:(NSSpellServer *)sender
      suggestGuessesForWord:(NSString *)word
	       inLanguage:(NSString *)language
{
	(void)sender;
	(void)language;
	guesses++;
	return [NSArray arrayWithObject:[word stringByAppendingString:@"-guess"]];
}

- (NSArray *)spellServer:(NSSpellServer *)sender
    suggestCompletionsForPartialWordRange:(NSRange)range
				 inString:(NSString *)string
				 language:(NSString *)language
{
	(void)sender;
	(void)language;
	completions++;
	return [NSArray arrayWithObject:[string substringWithRange:range]];
}

- (NSRange)spellServer:(NSSpellServer *)sender
     checkGrammarInString:(NSString *)string
		language:(NSString *)language
		 details:(NSArray **)outDetails
{
	(void)sender;
	(void)language;
	grammars++;
	/* THE DETAILS ARRAY IS BUILT FROM THE THREE KEYS, in the shapes Apple documents: corrections as an array of
	 * strings, the range BOXED IN AN NSValue (a range is a C struct and cannot be stored directly), and a sentence
	 * for the user. */
	*outDetails = [NSArray arrayWithObject:
		[NSDictionary dictionaryWithObjectsAndKeys:
			[NSArray arrayWithObject:@"are"], NSGrammarCorrections,
			[NSValue valueWithRange:NSMakeRange(1, 2)], NSGrammarRange,
			@"verb agreement", NSGrammarUserDescription,
			nil]];
	return [string rangeOfString:@"is"];
}

- (NSArray *)spellServer:(NSSpellServer *)sender
	     checkString:(NSString *)stringToCheck
		  offset:(NSUInteger)offset
		   types:(NSTextCheckingTypes)checkingTypes
		 options:(NSDictionary *)options
	     orthography:(NSOrthography *)orthography
	       wordCount:(NSInteger *)wordCount
{
	(void)sender;
	(void)offset;
	(void)checkingTypes;
	(void)options;
	(void)orthography;
	unifieds++;
	*wordCount = 7;
	lastUnifiedWordCount = 7;
	return [NSArray arrayWithObject:
		[NSDictionary dictionaryWithObjectsAndKeys:
			@"grammar", @"Kind",
			[NSValue valueWithRange:[stringToCheck rangeOfString:@"is"]], NSGrammarRange,
			nil]];
}

- (void)spellServer:(NSSpellServer *)sender didLearnWord:(NSString *)word inLanguage:(NSString *)language
{
	(void)sender;
	(void)word;
	(void)language;
	learns++;
}

- (void)spellServer:(NSSpellServer *)sender didForgetWord:(NSString *)word inLanguage:(NSString *)language
{
	(void)sender;
	(void)word;
	(void)language;
	forgets++;
}

@end

/* A DELEGATE THAT WRITES NOTHING: every optional door must fall back to the documented "not implemented" value. */
@interface FNQuietSpellDelegate : NSObject <NSSpellServerDelegate>
@end
@implementation FNQuietSpellDelegate
@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE CLASS, THE PROTOCOL AND THE THREE KEYS -------------------------------------------- */
	{
		Class cls = objc_getClass("NSSpellServer");
		Protocol *p = objc_getProtocol("NSSpellServerDelegate");

		check("class-and-protocol-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] && p != NULL,
		      @"NSSpellServer exists, is an NSObject, and NSSpellServerDelegate is declared");
		check("every-delegate-door-is-declared-optional",
		      fn_protocol_has_optional(p, @selector(spellServer:findMisspelledWordInString:language:wordCount:countOnly:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:suggestGuessesForWord:inLanguage:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:suggestCompletionsForPartialWordRange:inString:language:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:checkGrammarInString:language:details:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:checkString:offset:types:options:orthography:wordCount:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:didLearnWord:inLanguage:)) &&
		      fn_protocol_has_optional(p, @selector(spellServer:didForgetWord:inLanguage:)),
		      @"all seven optional doors are declared by the protocol (asked of the PROTOCOL, not a class)");
		check("the-three-grammar-keys-are-their-names",
		      [NSGrammarCorrections isEqualToString:@"NSGrammarCorrections"] &&
		      [NSGrammarRange isEqualToString:@"NSGrammarRange"] &&
		      [NSGrammarUserDescription isEqualToString:@"NSGrammarUserDescription"],
		      @"the three keys are exported and their values are their names");
	}

	/* --- 2. THE LANGUAGE REGISTRY ------------------------------------------------------------------ */
	{
		NSSpellServer *server = [[NSSpellServer alloc] init];
		BOOL ok = [server registerLanguage:@"English" byVendor:@"Argentum"];
		BOOL again = [server registerLanguage:@"English" byVendor:@"Argentum"];
		int refusals = (![server registerLanguage:@"" byVendor:@"Argentum"]) +
			       (![server registerLanguage:nil byVendor:@"Argentum"]) +
			       (![server registerLanguage:@"English" byVendor:@""]) +
			       (![server registerLanguage:@"English" byVendor:nil]);

		check("registration-accepts-a-language-and-vendor", ok, @"a language and vendor register");
		check("repeating-a-registration-is-the-same-intent", again,
		      @"the same pair answers YES again (the §62.83 rule: repetition is the same intent, not a second entry)");
		check("registration-refuses-empty-names", refusals == 4,
		      [NSString stringWithFormat:@"all four empty-name registrations refused (%d of 4)", refusals]);
	}

	/* --- 3. THE DELEGATE IS HELD, NOT OWNED --------------------------------------------------------- */
	{
		NSSpellServer *server = [[NSSpellServer alloc] init];
		FNQuietSpellDelegate *d = [[FNQuietSpellDelegate alloc] init];

		check("the-delegate-round-trips-and-starts-nil",
		      [server delegate] == nil && (([server setDelegate:d], [server delegate] == d)),
		      @"a fresh server has no delegate; setting one returns it unchanged");
	}

	/* --- 4. THE USER'S WORDS, IN BOTH DIRECTIONS ---------------------------------------------------- */
	{
		NSSpellServer *server = [[NSSpellServer alloc] init];
		FNProbeSpellDelegate *d = [[FNProbeSpellDelegate alloc] init];

		[server setDelegate:d];
		check("an-unknown-word-is-not-a-user-word",
		      ![server isWordInUserDictionaries:@"argnumbe" caseSensitive:YES],
		      @"nothing has been learned yet");
		FNSpellServerDispatchLearnWord(server, @"argnumbe", @"English");
		check("learning-a-word-makes-it-a-user-word",
		      [server isWordInUserDictionaries:@"argnumbe" caseSensitive:YES] &&
		      [server isWordInUserDictionaries:@"ARGNUMBE" caseSensitive:NO] &&
		      ![server isWordInUserDictionaries:@"ARGNUMBE" caseSensitive:YES] && d->learns == 1,
		      @"the learned word answers case-insensitively but not case-sensitively, and the delegate was told");
		FNSpellServerDispatchForgetWord(server, @"argnumbe", @"English");
		check("forgetting-removes-it-from-both-answers",
		      ![server isWordInUserDictionaries:@"argnumbe" caseSensitive:YES] &&
		      ![server isWordInUserDictionaries:@"argnumbe" caseSensitive:NO] && d->forgets == 1,
		      @"the forgotten word is gone at either sensitivity");
		FNSpellServerDispatchIgnoreWord(server, @"gibberish");
		check("the-documents-ignored-word-answers-too",
		      [server isWordInUserDictionaries:@"gibberish" caseSensitive:YES],
		      @"the ignored store answers through the same door (and has no delegate notification, by design)");
	}

	/* --- 5. THE SEVEN DOORS, REACHED THROUGH THE SEAM ------------------------------------------------ */
	{
		NSSpellServer *server = [[NSSpellServer alloc] init];
		FNProbeSpellDelegate *d = [[FNProbeSpellDelegate alloc] init];
		int wordCount = -1;
		NSRange found;
		NSArray *guesses, *completions, *details = nil, *unified;
		NSRange grammar;
		NSInteger unifiedWords = -1;

		[server setDelegate:d];

		found = FNSpellServerDispatchFindMisspelledWord(server, @"a mispelt word", @"English",
							       &wordCount, NO);
		/* THE EXPECTED LOCATION IS COUNTED, NOT GUESSED: "mispelt" begins at index 2 in "a mispelt word"
		 * (the first run of this probe asserted 5 and failed on a CORRECT answer from the delegate). */
		check("the-classic-doors-reach-the-delegate",
		      found.location == 2 && found.length == 7 && wordCount == 3 && d->finds == 1,
		      [NSString stringWithFormat:@"the delegate's own range and count came back (loc=%lu len=%lu count=%d)",
						(unsigned long)found.location, (unsigned long)found.length, wordCount]);

		guesses = FNSpellServerDispatchSuggestGuesses(server, @"mispelt", @"English");
		check("the-guess-door-answers-the-delegates-array",
		      [guesses count] == 1 && [[guesses objectAtIndex:0] isEqualToString:@"mispelt-guess"] &&
		      d->guesses == 1,
		      @"the guesses are the delegate's, unchanged");

		completions = FNSpellServerDispatchSuggestCompletions(server, NSMakeRange(2, 4), @"a mispelt word",
								     @"English");
		check("the-completion-door-passes-the-range-through",
		      [completions count] == 1 && [[completions objectAtIndex:0] isEqualToString:@"misp"] &&
		      d->completions == 1,
		      @"the range reached the delegate and its answer came back");

		grammar = FNSpellServerDispatchCheckGrammar(server, @"this are wrong", @"English", &details);
		{
			NSDictionary *unit = [details count] == 1 ? [details objectAtIndex:0] : nil;
			NSValue *boxed = [unit objectForKey:NSGrammarRange];

			/* Same lesson here: "is" begins at index 2 in "this are wrong". */
			check("the-grammar-door-answers-details-in-the-three-shapes",
			      grammar.location == 2 && [details count] == 1 && d->grammars == 1 &&
			      [[unit objectForKey:NSGrammarCorrections] isKindOfClass:[NSArray class]] &&
			      [[[unit objectForKey:NSGrammarCorrections] objectAtIndex:0] isEqualToString:@"are"] &&
			      [boxed isKindOfClass:[NSValue class]] &&
			      [boxed rangeValue].location == 1 && [boxed rangeValue].length == 2 &&
			      [[unit objectForKey:NSGrammarUserDescription] isEqualToString:@"verb agreement"],
			      @"corrections are an array of strings, the range is BOXED in an NSValue, and the user "
			      @"description is a string");
		}

		unified = FNSpellServerDispatchCheckString(server, @"this are wrong", 0, NSTextCheckingTypeGrammar,
							  nil, nil, &unifiedWords);
		check("the-unified-door-answers-and-counts",
		      [unified count] == 1 && unifiedWords == 7 && d->unifieds == 1 &&
		      [[[unified objectAtIndex:0] objectForKey:NSGrammarRange] isKindOfClass:[NSValue class]],
		      @"the 10.6 door answers with the same keyed dictionaries and reports its own word count");
	}

	/* --- 6. A DELEGATE THAT WRITES NOTHING GETS THE DOCUMENTED DEFAULTS ------------------------------ */
	{
		NSSpellServer *server = [[NSSpellServer alloc] init];
		FNQuietSpellDelegate *quiet = [[FNQuietSpellDelegate alloc] init];
		int wordCount = -1;
		NSArray *details = (id)[NSNumber numberWithInt:1];	/* deliberately non-nil: the door must CLEAR it */
		NSInteger unifiedWords = -1;
		NSRange found, grammar;

		[server setDelegate:quiet];
		found = FNSpellServerDispatchFindMisspelledWord(server, @"a mispelt word", nil, &wordCount, NO);
		grammar = FNSpellServerDispatchCheckGrammar(server, @"this are wrong", nil, &details);
		(void)FNSpellServerDispatchCheckString(server, @"x", 0, NSTextCheckingTypeSpelling, nil, nil,
						      &unifiedWords);
		check("a-delegate-that-writes-nothing-gets-the-documented-defaults",
		      found.location == NSNotFound && found.length == 0 && wordCount == 0 &&
		      grammar.location == NSNotFound && details == nil && unifiedWords == 0 &&
		      FNSpellServerDispatchSuggestGuesses(server, @"x", nil) == nil &&
		      FNSpellServerDispatchSuggestCompletions(server, NSMakeRange(0, 1), @"x", nil) == nil,
		      @"an unwritten door answers NSMakeRange(NSNotFound, 0), a nil array and a zero count — never an "
		      @"exception and never a fabricated answer");
	}

	/* --- 7. THE DOOR THAT NEVER RETURNS --------------------------------------------------------------- */
	check("run-is-declared-and-deliberately-not-called",
	      [[NSSpellServer alloc] respondsToSelector:@selector(run)],
	      @"-run is present; it is NOT called because Apple's contract is that it runs a loop that never "
	      @"returns, and nothing is connected to it yet (no client is shipped — see the probe's note)");

	printf("FOUNDATION-SPELLSERVER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-SPELLSERVER-STATUS=%d\n", (failc || okc != 18) ? 1 : 0);
	printf("FOUNDATION-SPELLSERVER DONE\n");
	return (failc || okc != 18) ? 1 : 0;
}
