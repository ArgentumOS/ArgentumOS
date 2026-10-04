/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNSpellServerDispatch.h — THE DISPATCH HALF OF `NSSpellServer`: what a client's request does once it arrives
 * (§62.85). INTERNAL, and deliberately not public API.
 *
 * WHY IT EXISTS AS A SEPARATE, NAMED SEAM RATHER THAN AS PRIVATE METHODS: `NSSpellServerDelegate`'s doors are
 * REACHED BY A CLIENT, and Apple's wire between client and server is a private distributed-objects protocol that
 * this tree does not implement (no `NSSpellChecker` is shipped). The doors would therefore be declarations with no
 * caller — a protocol nobody can drive. These functions are that caller: each one performs exactly what a request
 * would perform — find the delegate door, call it if it is written, and otherwise answer the value Apple's docs
 * give for "not implemented". A future client protocol calls these and nothing else.
 *
 * THE ONE ENTRY WITH NO MATCHING DELEGATE DOOR is `FNSpellServerDispatchIgnoreWord`: Apple's protocol has no
 * "ignore this word for this document" door because that list travels WITH the client's request. It is here so the
 * store behind `-isWordInUserDictionaries:caseSensitive:` can be exercised from both directions, and it is called
 * out rather than slipped in.
 */

#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSTextCheckingResult.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSDictionary;
@class NSString;
@class NSOrthography;
@class NSSpellServer;

/* Each answers what the delegate answered, or the documented default when the door is not written:
 * `NSMakeRange(NSNotFound, 0)` for a range, a nil array, and a word count of zero. */
extern NSRange FNSpellServerDispatchFindMisspelledWord(NSSpellServer *server,
						       NSString *string,
						       NSString *_Nullable language,
						       int *wordCount, BOOL countOnly);
extern NSArray *_Nullable FNSpellServerDispatchSuggestGuesses(NSSpellServer *server,
							      NSString *word,
							      NSString *_Nullable language);
extern NSArray *_Nullable FNSpellServerDispatchSuggestCompletions(NSSpellServer *server, NSRange range,
								  NSString *string,
								  NSString *_Nullable language);
extern NSRange FNSpellServerDispatchCheckGrammar(NSSpellServer *server, NSString *string,
						 NSString *_Nullable language,
						 NSArray *_Nullable *_Nonnull details);
extern NSArray *_Nullable FNSpellServerDispatchCheckString(NSSpellServer *server, NSString *string,
							   NSUInteger offset, NSTextCheckingTypes types,
							   NSDictionary *_Nullable options,
							   NSOrthography *_Nullable orthography,
							   NSInteger *wordCount);

/* LEARNING AND FORGETTING GO THROUGH THE SERVER'S STORE, THEN TO THE DELEGATE — Apple's order: the notifications
 * tell the delegate "the sender added/removed the word from the user's list". */
extern void FNSpellServerDispatchLearnWord(NSSpellServer *server, NSString *word,
					   NSString *_Nullable language);
extern void FNSpellServerDispatchForgetWord(NSSpellServer *server, NSString *word,
					    NSString *_Nullable language);
extern void FNSpellServerDispatchIgnoreWord(NSSpellServer *server, NSString *word);

NS_ASSUME_NONNULL_END
