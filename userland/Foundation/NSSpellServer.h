/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSSpellServer — THE SERVER SIDE OF A SPELL-CHECKING SERVICE (§62.85), with NSSpellServerDelegate and the three
 * grammar keys. This closes `Fundamentals / Spelling and Grammar`.
 *
 * THE SHAPE IS APPLE'S AND IT IS A SPLIT: a service registers the languages it can check, sets a delegate, and
 * calls `-run`; the DELEGATE does the work. That split is the reason this class can be complete while NO ENGINE IS
 * CHOSEN — the engine belongs to the service that implements the delegate, not to this API (see
 * docs/design/spelling-plan.md, where the engine question is open and the first candidate was withdrawn on
 * language grounds). Nothing here knows what a dictionary is.
 *
 * WHAT IS REAL HERE, AND WHAT THE TREE DOES NOT HAVE YET. Real: the language registry, the delegate, the user's
 * learned and ignored words, `-run`'s loop, and the DISPATCH — the seven optional delegate doors reached through
 * the seam in `FNSpellServerDispatch.h`, which is where a client's request will arrive. NOT here: a CLIENT. No
 * `NSSpellChecker` is shipped and Apple's wire between the two is a private distributed-objects protocol, so
 * `-run` currently runs a loop that nothing is connected to. A service may still be written, run, and its delegate
 * driven through the seam — which is what the probe does — and when a client protocol exists it will call exactly
 * those seam functions.
 *
 * `-isWordInUserDictionaries:caseSensitive:` IS THE DELEGATE'S OWN HELPER (Apple's words: the delegate "is expected
 * to call this" before reporting a misspelling), so it answers from the SERVER's store: the words learned and the
 * words the document said to ignore. **THE STORE IS IN MEMORY ONLY in v1** — a learned word lives as long as the
 * service process does, and persistence is a stated debt rather than a hidden one: Apple's learned words live in
 * a per-user spelling directory, and this system has no such store yet.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSTextCheckingResult.h>
#import <Foundation/NSOrthography.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSDictionary;
@class NSString;
@class NSSpellServer;

/* THE DELEGATE: every door is OPTIONAL in Apple's protocol, and the dispatch seam answers the documented "not
 * implemented" value for a door a delegate does not write. */
@protocol NSSpellServerDelegate <NSObject>
@optional

/* THE CLASSIC DOOR: the first misspelled word in `stringToCheck`, as a range, with `wordCount` receiving the
 * number of words scanned and `countOnly` asking for the count alone. `NSMakeRange(NSNotFound, 0)` means "none". */
- (NSRange)spellServer:(NSSpellServer *)sender
    findMisspelledWordInString:(NSString *)stringToCheck
		      language:(nullable NSString *)language
		     wordCount:(int *)wordCount
		     countOnly:(BOOL)countOnly;

/* Corrections for one word, most likely first. */
- (nullable NSArray *)spellServer:(NSSpellServer *)sender
	      suggestGuessesForWord:(NSString *)word
		       inLanguage:(nullable NSString *)language;

/* Completions for a partial word already located in `string`. */
- (nullable NSArray *)spellServer:(NSSpellServer *)sender
    suggestCompletionsForPartialWordRange:(NSRange)range
				 inString:(NSString *)string
				 language:(nullable NSString *)language;

/* Grammar: the range of the first flagged unit, with `outDetails` receiving dictionaries built from the three
 * `NSGrammar*` keys below. */
- (NSRange)spellServer:(NSSpellServer *)sender
     checkGrammarInString:(NSString *)string
		language:(nullable NSString *)language
		 details:(NSArray *_Nullable *_Nonnull)outDetails;

/* THE UNIFIED DOOR (10.6+): spelling, grammar and autocorrection in one pass, answering the same
 * `NSGrammar*`-keyed dictionaries. Implemented alongside the classic doors, not instead of them — a real client
 * still asks the classic ones. */
- (nullable NSArray *)spellServer:(NSSpellServer *)sender
		       checkString:(NSString *)stringToCheck
			    offset:(NSUInteger)offset
			     types:(NSTextCheckingTypes)checkingTypes
			   options:(nullable NSDictionary *)options
		       orthography:(nullable NSOrthography *)orthography
			 wordCount:(NSInteger *)wordCount;

/* The user learned this word, or asked for it to be forgotten. `didLearnWord:` is ALSO what puts a word into the
 * store `-isWordInUserDictionaries:caseSensitive:` answers from — so a service that never reports a learning
 * leaves that door answering only the ignored words. */
- (void)spellServer:(NSSpellServer *)sender
       didLearnWord:(NSString *)word
	 inLanguage:(nullable NSString *)language;
- (void)spellServer:(NSSpellServer *)sender
      didForgetWord:(NSString *)word
	 inLanguage:(nullable NSString *)language;


/* §63.235: THE DELEGATE'S RECORD RESPONSE. A protocol's methods are its declaration — a spell server asks its
 * delegate to record what the user chose for a correction, and this is the door it asks through. */
- (void)spellServer:(NSSpellServer *)sender
     recordResponse:(NSUInteger)response
       toCorrection:(NSString * _Nonnull)correction
	    forWord:(NSString * _Nonnull)word
	   language:(NSString * _Nonnull)language;
@end

@interface NSSpellServer : NSObject
{
@private
	id _delegate;			/* NOT retained: the service owns both, as Apple's does */
	id _registrations;		/* language -> mutable set of vendors */
	id _learned;			/* the words the user learned */
	id _ignored;			/* the words a document asked to ignore */
}

/* The delegate is NOT retained — the service holds both objects, and a server outliving its delegate is a
 * lifetime error rather than a reason to keep it alive (the reason NSURLDownload's delegate is not retained
 * either). */
- (nullable id <NSSpellServerDelegate>)delegate;
- (void)setDelegate:(nullable id <NSSpellServerDelegate>)anObject;

/* ONE CALL PER LANGUAGE, BEFORE `-run` (Apple's order). `language` is the English name of a language and `vendor`
 * distinguishes this service's checker from another offering the same language.
 *
 * THE TWO REFUSALS ARE OURS AND ARE STATED: an empty or nil `language` or `vendor` answers NO, because a
 * registration that identifies nothing cannot be honoured. **APPLE'S OTHER NO CASE IS NOT CHECKED HERE** — Apple
 * documents NO for a language that is not on its list of languages, and this library ships no such list; inventing
 * one would be a table to maintain that no caller can correct. Registering the SAME language and vendor twice is a
 * successful NO-OP (YES), for the same reason `NSURLProtocol`'s `+registerClass:` is (§62.83): repeating a
 * registration is the same intent, not a second entry. */
- (BOOL)registerLanguage:(nullable NSString *)language byVendor:(nullable NSString *)vendor;

/* WHETHER THE USER ACCEPTS THIS WORD — the delegate's own filter, answered from the words learned and the words
 * ignored (see the file's note on the in-memory v1 store). */
- (BOOL)isWordInUserDictionaries:(nullable NSString *)word caseSensitive:(BOOL)flag;

/* START LISTENING. **THIS METHOD DOES NOT RETURN** — it runs the current run loop, which is what makes it a
 * service's `main`. Nothing is connected to it yet (no client is shipped), so it is a loop that serves nothing
 * today; the seam in `FNSpellServerDispatch.h` is where a request will arrive once a client exists. */
- (void)run;

@end

/* --- THE THREE GRAMMAR KEYS, AND THE SHAPES OF THEIR VALUES ---------------------------------------- */

/* AN `NSArray` OF `NSString` — the substitutions that would correct the flagged unit. Apple: it "may not be
 * available in all cases". */
FOUNDATION_EXPORT NSString * const NSGrammarCorrections;
/* AN `NSValue` WRAPPING AN `NSRange` — and the range is relative to the SENTENCE, not to the checked string: a
 * consumer adds the flagged unit's own location. ABSENT means the whole sentence range, which is a documented
 * default and not an error. */
FOUNDATION_EXPORT NSString * const NSGrammarRange;
/* AN `NSString` TO PRESENT TO THE USER. Apple requires that a unit carry THIS or `NSGrammarCorrections` for
 * correction guidance to be offered at all — so a detail dictionary with neither is a flagged unit the user can
 * see and do nothing about. */
FOUNDATION_EXPORT NSString * const NSGrammarUserDescription;

NS_ASSUME_NONNULL_END
