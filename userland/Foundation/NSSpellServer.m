/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSpellServer.m — the server side of a spell-checking service (§62.85). MANUAL OWNERSHIP.
 *
 * THE CLASS OWNS STATE AND NOT KNOWLEDGE: a language registry, a delegate that is NOT retained, and the two word
 * stores the delegate asks about. It contains no notion of what a word is, which is what lets it ship while the
 * engine question is open (docs/design/spelling-plan.md §3).
 *
 * THE STORE IS TWO SETS AND ONE RULE. `_learned` is what the user learned, `_ignored` is what a document asked to
 * ignore, and `-isWordInUserDictionaries:caseSensitive:` answers YES for a member of either. **IN MEMORY ONLY in
 * v1, and the header says so**: Apple keeps learned words in a per-user spelling directory, this system has no
 * such store yet, and a service that wants persistence owns it.
 */

#import <Foundation/NSSpellServer.h>
#import <Foundation/FNSpellServerDispatch.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>	/* NSMutableDictionary lives here */
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSSet.h>		/* NSMutableSet lives here */
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>

/* THE VALUES ARE THE NAMES. Apple publishes these as keys and their spelling is what third-party bindings record;
 * a consumer treats them as opaque keys, so the spelling is not a contract either way. */
NSString * const NSGrammarCorrections = @"NSGrammarCorrections";
NSString * const NSGrammarRange = @"NSGrammarRange";
NSString * const NSGrammarUserDescription = @"NSGrammarUserDescription";

/* THE PRIVATE HALF THE SEAM CALLS: one method per delegate door, in the same file, so the seam never reaches into
 * the class's storage from outside and the class's storage is never public. */
@interface NSSpellServer (FNPrivate)
- (NSRange)fnFindMisspelledWordInString:(NSString *)string language:(nullable NSString *)language
			      wordCount:(int *)wordCount countOnly:(BOOL)countOnly;
- (nullable NSArray *)fnSuggestGuessesForWord:(NSString *)word inLanguage:(nullable NSString *)language;
- (nullable NSArray *)fnSuggestCompletionsForPartialWordRange:(NSRange)range inString:(NSString *)string
						    language:(nullable NSString *)language;
- (NSRange)fnCheckGrammarInString:(NSString *)string language:(nullable NSString *)language
			  details:(NSArray *_Nullable *_Nonnull)details;
- (nullable NSArray *)fnCheckString:(NSString *)string offset:(NSUInteger)offset types:(NSTextCheckingTypes)types
			    options:(nullable NSDictionary *)options
			orthography:(nullable NSOrthography *)orthography
			  wordCount:(NSInteger *)wordCount;
- (void)fnLearnWord:(NSString *)word;
- (void)fnForgetWord:(NSString *)word;
- (void)fnIgnoreWord:(NSString *)word;
@end

@implementation NSSpellServer

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_registrations = [[NSMutableDictionary alloc] init];
		_learned = [[NSMutableSet alloc] init];
		_ignored = [[NSMutableSet alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[_registrations release];
	[_learned release];
	[_ignored release];
	[super dealloc];
}

/* --- THE DELEGATE: NOT RETAINED ------------------------------------------------------------------- */

- (nullable id <NSSpellServerDelegate>)delegate { return _delegate; }

- (void)setDelegate:(nullable id <NSSpellServerDelegate>)anObject { _delegate = anObject; }

/* --- THE LANGUAGE REGISTRY ------------------------------------------------------------------------ */

- (BOOL)registerLanguage:(NSString *)language byVendor:(NSString *)vendor
{
	NSMutableSet *vendors;

	if (language == nil || [language length] == 0 || vendor == nil || [vendor length] == 0) {
		return NO;	/* a registration that identifies nothing cannot be honoured - see the header */
	}
	vendors = [_registrations objectForKey:language];
	if (vendors == nil) {
		vendors = [NSMutableSet set];
		[_registrations setObject:vendors forKey:language];
	}
	/* REPEATING A REGISTRATION IS THE SAME INTENT, NOT A SECOND ENTRY (§62.83's rule, same reason). */
	[vendors addObject:vendor];
	return YES;
}

/* --- THE WORDS THE USER ACCEPTS ------------------------------------------------------------------- */

- (BOOL)isWordInUserDictionaries:(NSString *)word caseSensitive:(BOOL)flag
{
	NSEnumerator *sources = [_learned objectEnumerator];
	id candidate;

	if (word == nil) {
		return NO;
	}
	for (;;) {
		candidate = [sources nextObject];
		if (candidate == nil) {
			sources = [_ignored objectEnumerator];
			candidate = [sources nextObject];
			/* ONE PASS OVER EACH SET, WITH THE SAME TEST: the two stores differ in WHERE a word came from,
			 * not in how membership is decided. */
			while (candidate != nil) {
				if (flag ? [candidate isEqualToString:word]
					 : [candidate caseInsensitiveCompare:word] == NSOrderedSame) {
					return YES;
				}
				candidate = [sources nextObject];
			}
			return NO;
		}
		if (flag ? [candidate isEqualToString:word]
			 : [candidate caseInsensitiveCompare:word] == NSOrderedSame) {
			return YES;
		}
	}
}

/* --- RUN: THE LOOP THAT DOES NOT RETURN ----------------------------------------------------------- */

- (void)run
{
	/* APPLE'S CONTRACT IS THAT THIS NEVER RETURNS, and a loop that returned would be a service that exited. The
	 * loop it runs is the CURRENT run loop, so a caller may schedule work before calling it - and NOTHING IS
	 * CONNECTED to it yet: no client is shipped (see the header), so the only door a request could use today is the
	 * dispatch seam. */
	[[NSRunLoop currentRunLoop] run];
}

@end

/* --- THE SEAM: WHAT A CLIENT'S REQUEST DOES WHEN IT ARRIVES ---------------------------------------- */

@implementation NSSpellServer (FNPrivate)

- (NSRange)fnFindMisspelledWordInString:(NSString *)string language:(nullable NSString *)language
			      wordCount:(int *)wordCount countOnly:(BOOL)countOnly
{
	if ([(id)_delegate respondsToSelector:
	     @selector(spellServer:findMisspelledWordInString:language:wordCount:countOnly:)]) {
		return [(id <NSSpellServerDelegate>)_delegate spellServer:self
					      findMisspelledWordInString:string
							       language:language
							      wordCount:wordCount
							      countOnly:countOnly];
	}
	*wordCount = 0;
	return NSMakeRange(NSNotFound, 0);
}

- (nullable NSArray *)fnSuggestGuessesForWord:(NSString *)word inLanguage:(nullable NSString *)language
{
	if ([(id)_delegate respondsToSelector:
	     @selector(spellServer:suggestGuessesForWord:inLanguage:)]) {
		return [(id <NSSpellServerDelegate>)_delegate spellServer:self
					      suggestGuessesForWord:word
							   inLanguage:language];
	}
	return nil;
}

- (nullable NSArray *)fnSuggestCompletionsForPartialWordRange:(NSRange)range inString:(NSString *)string
						    language:(nullable NSString *)language
{
	if ([(id)_delegate respondsToSelector:
	     @selector(spellServer:suggestCompletionsForPartialWordRange:inString:language:)]) {
		return [(id <NSSpellServerDelegate>)_delegate spellServer:self
				 suggestCompletionsForPartialWordRange:range
							    inString:string
							    language:language];
	}
	return nil;
}

- (NSRange)fnCheckGrammarInString:(NSString *)string language:(nullable NSString *)language
			  details:(NSArray *_Nullable *_Nonnull)details
{
	if ([(id)_delegate respondsToSelector:
	     @selector(spellServer:checkGrammarInString:language:details:)]) {
		return [(id <NSSpellServerDelegate>)_delegate spellServer:self
					       checkGrammarInString:string
							  language:language
							   details:details];
	}
	*details = nil;
	return NSMakeRange(NSNotFound, 0);
}

- (nullable NSArray *)fnCheckString:(NSString *)string offset:(NSUInteger)offset types:(NSTextCheckingTypes)types
			    options:(nullable NSDictionary *)options
			orthography:(nullable NSOrthography *)orthography
			  wordCount:(NSInteger *)wordCount
{
	if ([(id)_delegate respondsToSelector:
	     @selector(spellServer:checkString:offset:types:options:orthography:wordCount:)]) {
		return [(id <NSSpellServerDelegate>)_delegate spellServer:self
							     checkString:string
								  offset:offset
								   types:types
								 options:options
							     orthography:orthography
							       wordCount:wordCount];
	}
	*wordCount = 0;
	return nil;
}

- (void)fnLearnWord:(NSString *)word
{
	if (word != nil) {
		[_learned addObject:word];
	}
}

- (void)fnForgetWord:(NSString *)word
{
	if (word != nil) {
		[_learned removeObject:word];
	}
}

- (void)fnIgnoreWord:(NSString *)word
{
	if (word != nil) {
		[_ignored addObject:word];
	}
}

@end

/* --- THE FUNCTIONS A CLIENT WILL CALL --------------------------------------------------------------- */

NSRange FNSpellServerDispatchFindMisspelledWord(NSSpellServer *server, NSString *string,
					       NSString *_Nullable language, int *wordCount, BOOL countOnly)
{
	return [server fnFindMisspelledWordInString:string language:language
					  wordCount:wordCount countOnly:countOnly];
}

NSArray *_Nullable FNSpellServerDispatchSuggestGuesses(NSSpellServer *server, NSString *word,
						       NSString *_Nullable language)
{
	return [server fnSuggestGuessesForWord:word inLanguage:language];
}

NSArray *_Nullable FNSpellServerDispatchSuggestCompletions(NSSpellServer *server, NSRange range,
							   NSString *string, NSString *_Nullable language)
{
	return [server fnSuggestCompletionsForPartialWordRange:range inString:string language:language];
}

NSRange FNSpellServerDispatchCheckGrammar(NSSpellServer *server, NSString *string,
					  NSString *_Nullable language,
					  NSArray *_Nullable *_Nonnull details)
{
	return [server fnCheckGrammarInString:string language:language details:details];
}

NSArray *_Nullable FNSpellServerDispatchCheckString(NSSpellServer *server, NSString *string,
						    NSUInteger offset, NSTextCheckingTypes types,
						    NSDictionary *_Nullable options,
						    NSOrthography *_Nullable orthography,
						    NSInteger *wordCount)
{
	return [server fnCheckString:string offset:offset types:types options:options
			  orthography:orthography wordCount:wordCount];
}

void FNSpellServerDispatchLearnWord(NSSpellServer *server, NSString *word, NSString *_Nullable language)
{
	[server fnLearnWord:word];
	if ([(id)[server delegate] respondsToSelector:@selector(spellServer:didLearnWord:inLanguage:)]) {
		[(id <NSSpellServerDelegate>)[server delegate] spellServer:server
							     didLearnWord:word
							      inLanguage:language];
	}
}

void FNSpellServerDispatchForgetWord(NSSpellServer *server, NSString *word, NSString *_Nullable language)
{
	[server fnForgetWord:word];
	if ([(id)[server delegate] respondsToSelector:@selector(spellServer:didForgetWord:inLanguage:)]) {
		[(id <NSSpellServerDelegate>)[server delegate] spellServer:server
							    didForgetWord:word
							     inLanguage:language];
	}
}

void FNSpellServerDispatchIgnoreWord(NSSpellServer *server, NSString *word)
{
	/* NO DELEGATE DOOR EXISTS FOR THIS (see the seam header's note): the document's ignored words travel with the
	 * client's request, so there is nothing to notify. */
	[server fnIgnoreWord:word];
}
