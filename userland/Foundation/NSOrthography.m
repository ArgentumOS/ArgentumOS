/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOrthography.m — the implementation, MANUAL OWNERSHIP. docs/design/foundation-plan.md §62.21.
 *
 * TWO VALUES AND FIVE OVERRIDES, which is the whole class: the state is a script tag and a map, and every other
 * door either derives something from them or is one of the object protocol's (equality, hashing, description,
 * copying, coding). The one piece of machinery is the ICU call the factory makes.
 *
 * ICU IS ON THIS FILE'S INCLUDE PATH FOR ONE QUESTION: WHICH SCRIPT IS A LANGUAGE WRITTEN IN. That is data - the
 * likely-subtags table - and it is the same binding the rest of this library uses for the data-driven families.
 * The answer for Hindi is `Deva`, which is Apple's own worked example, so the table this system already carries
 * agrees with the example Apple publishes.
 */

#import <Foundation/NSOrthography.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

#include <unicode/uloc.h>
#include <unicode/utypes.h>

#include <string.h>

/* THE ISO 15924 CODE FOR AN UNDETERMINED SCRIPT, which is what ICU itself calls that case. It is used when the
 * likely-subtags data has no script for a tag at all — measured: `xx` comes back from ICU unchanged, while `und`
 * and the empty tag are maximised to `en_Latn_US` and therefore answer `Latn`. */
static NSString *const FN_UNDETERMINED_SCRIPT = @"Zyyy";

/* THE SCRIPT ICU'S LIKELY-SUBTAGS DATA GIVES A LANGUAGE, or nil when it has none. The tag's UTF-8 BYTES ARE
 * COPIED OUT FIRST: `-UTF8String` hands out a borrowed buffer that a later call in the same expression may
 * overwrite (the trap F11 recorded), and ICU reads the copy. */
static NSString *fn_script_for_language(NSString *language)
{
	char tag[128];
	char maximized[128];
	char script[32];
	const char *borrowed = [language UTF8String];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (borrowed == NULL || borrowed[0] == '\0' || strlen(borrowed) >= sizeof tag) {
		return nil;
	}
	memcpy(tag, borrowed, strlen(borrowed) + 1);
	uloc_addLikelySubtags(tag, maximized, (int32_t)sizeof maximized, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	status = U_ZERO_ERROR;
	length = uloc_getScript(maximized, script, (int32_t)sizeof script, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithUTF8String:script];
}

@implementation NSOrthography

+ (instancetype)defaultOrthographyForLanguage:(NSString *)language
{
	NSString *script;
	NSDictionary *map;

	if (language == nil || [language length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+[NSOrthography defaultOrthographyForLanguage:]: a language tag is required"];
	}
	script = fn_script_for_language(language);
	if (script == nil) {
		script = FN_UNDETERMINED_SCRIPT;
	}
	/* THE LANGUAGE IS THE TAG AS GIVEN, NOT ICU'S MAXIMISED FORM: Apple's own example stores `hi` beside `Deva`,
	 * not `hi_Deva_IN`. */
	map = [NSDictionary dictionaryWithObject:[NSArray arrayWithObject:language] forKey:script];
	return [[[self alloc] initWithDominantScript:script languageMap:map] autorelease];
}

+ (instancetype)orthographyWithDominantScript:(NSString *)script languageMap:(NSDictionary *)map
{
	return [[[self alloc] initWithDominantScript:script languageMap:map] autorelease];
}

- (instancetype)initWithDominantScript:(NSString *)script languageMap:(NSDictionary *)map
{
	NSArray *languages;
	NSArray *keys;
	NSUInteger i;

	if (script == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSOrthography initWithDominantScript:languageMap:]: a script is required"];
	}
	if (map == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSOrthography initWithDominantScript:languageMap:]: a language map is required"];
	}
	/* EVERY VALUE MUST BE AN ARRAY, because four of this class's doors walk the values — the check is here so that
	 * -allLanguages cannot be the place a malformed map is discovered. THE KEYS ARE WALKED RATHER THAN THE VALUES,
	 * because a message has to name the offending KEY and `-allKeys` and `-allValues` are two separate walks of a
	 * hash table: pairing them by index would name the wrong one. */
	keys = [map allKeys];
	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];

		if (![[map objectForKey:key] isKindOfClass:[NSArray class]]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"-[NSOrthography initWithDominantScript:languageMap:]: the value for "
					   @"%@ is not an array", key];
		}
	}
	/* AND THE INVARIANT: the dominant script is one of the map's keys, with at least one language. Apple publishes
	 * no rule here, and this is the refusal that makes `-dominantLanguage` total rather than a nil the annotation
	 * forbids (§11.6.1 D4's precedent). */
	languages = [map objectForKey:script];
	if (languages == nil || [languages count] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSOrthography initWithDominantScript:languageMap:]: the map carries no "
				   @"language for the dominant script %@", script];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_dominantScript = [script copy];
	_languageMap = [map copy];
	return self;
}

- (void)dealloc
{
	NSString *script = _dominantScript;
	NSDictionary *map = _languageMap;

	_dominantScript = nil;
	_languageMap = nil;
	[script release];
	[map release];
	[super dealloc];
}

/* ---- THE TWO VALUES, AND WHAT IS DERIVED FROM THEM ------------------------------------------------ */

- (NSString *)dominantScript
{
	return _dominantScript;
}

- (NSDictionary *)languageMap
{
	return _languageMap;
}

- (nullable NSArray *)languagesForScript:(nullable NSString *)script
{
	/* A MISS ANSWERS NIL rather than raising: the argument is arbitrary data, not this object's own state — the
	 * dominant script is the one that is checked, and it is checked at construction. */
	return script == nil ? nil : [_languageMap objectForKey:script];
}

- (nullable NSString *)dominantLanguageForScript:(nullable NSString *)script
{
	NSArray *languages = [self languagesForScript:script];

	return [languages count] > 0 ? [languages objectAtIndex:0] : nil;
}

- (NSString *)dominantLanguage
{
	return [self dominantLanguageForScript:_dominantScript];
}

/* THE SORT IS THE ONE PART OF THIS CLASS THAT IS NOT A LOOKUP: see the header for why the scripts are ordered and
 * the languages are grouped by script rather than sorted across it. */
- (NSArray *)allScripts
{
	return [[_languageMap allKeys] sortedArrayUsingSelector:@selector(compare:)];
}

- (NSArray *)allLanguages
{
	NSArray *scripts = [self allScripts];
	NSMutableArray *languages = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [scripts count]; i++) {
		[languages addObjectsFromArray:[self languagesForScript:[scripts objectAtIndex:i]]];
	}
	return languages;
}

/* ---- THE OBJECT PROTOCOL, OVERRIDDEN BECAUSE THIS IS A VALUE --------------------------------------
 *
 * These four are NSObject's own doors, so nothing here is an addition to the API: what they add is VALUE
 * semantics, without which two orthographies built the same way would be unequal and a dictionary keyed by one
 * would miss. The comparison is the two fields; `-hash` reads the same two, so equal objects hash equally. */

- (BOOL)isEqual:(id)other
{
	NSOrthography *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSOrthography class]]) {
		return NO;
	}
	them = (NSOrthography *)other;
	return [_dominantScript isEqualToString:them->_dominantScript] &&
	       [_languageMap isEqual:them->_languageMap];
}

- (NSUInteger)hash
{
	return [_dominantScript hash] ^ [_languageMap hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSOrthography %@ %@>", _dominantScript, _languageMap];
}

- (id)copy
{
	return [self retain];	/* +1 and self: an orthography has no mutable state (§62's rule) */
}

/* ---- CODING --------------------------------------------------------------------------------------
 *
 * TWO VALUES UNDER THIS LIBRARY'S OWN KEY NAMES: a program never reads them, which is why they are spellings
 * rather than transcriptions of anything, and the map travels as itself because it is already plist-shaped. */

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_dominantScript forKey:@"NSOrthographyDominantScript"];
	[coder encodeObject:_languageMap forKey:@"NSOrthographyLanguageMap"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSString *script = [coder decodeObjectForKey:@"NSOrthographyDominantScript"];
	NSDictionary *map = [coder decodeObjectForKey:@"NSOrthographyLanguageMap"];

	if (![script isKindOfClass:[NSString class]] || ![map isKindOfClass:[NSDictionary class]]) {
		[self release];
		return nil;
	}
	/* THROUGH THE DESIGNATED DOOR, so an archive cannot carry a state the initializer would have refused. */
	return [self initWithDominantScript:script languageMap:map];
}

+ (BOOL)supportsSecureCoding
{
	/* APPLE'S ANSWER, WITH THE GAP NAMED WHERE IT LIVES: NSCoding.h records that this tree's archiver does not yet
	 * ENFORCE secure coding, so the conformance says what it can guard and the coder's own debt is that file's. */
	return YES;
}

@end
