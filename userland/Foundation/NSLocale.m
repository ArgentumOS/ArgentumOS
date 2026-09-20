/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLocale.m — the locale identifier, and the subtags it derives.
 *
 * MANUAL OWNERSHIP: it owns an NSString and implements no -retain/-release.
 */

#import <Foundation/NSLocale.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
/* ICU ANSWERS THE DISPLAY NAMES (see -displayNameForKey:value: below). This library already links
 * icuuc, and NSLocale.m joins the per-file ICU include table in the build for it. */
#include <unicode/uloc.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

NSString *const NSLocaleIdentifier = @"NSLocaleIdentifier";
NSString *const NSLocaleLanguageCode = @"NSLocaleLanguageCode";
NSString *const NSLocaleScriptCode = @"NSLocaleScriptCode";
NSString *const NSLocaleCountryCode = @"NSLocaleCountryCode";

/* THE CANONICAL FORM, in one place: subtags joined by '_', the language
 * lower-cased, a four-letter script Title-cased, everything else upper-cased (a
 * region, or a variant). The POSIX charset and modifier tails — ".UTF-8", "@euro"
 * — are DROPPED, because they are not part of a locale identifier, and the POSIX
 * names C and POSIX become en_US_POSIX, which is what the convention they stand
 * for is called here. */
#define FN_SUBTAG_MAX 8

static NSString *fn_canonical(NSString *identifier)
{
	const char *raw;
	char *out;
	size_t starts[FN_SUBTAG_MAX], lengths[FN_SUBTAG_MAX];
	size_t len, i, n = 0, count = 0, k;
	int first;

	if (identifier == nil) {
		return @"";
	}
	raw = [identifier UTF8String];
	if (raw == NULL) {
		return @"";
	}
	if (strcmp(raw, "C") == 0 || strcmp(raw, "POSIX") == 0) {
		return @"en_US_POSIX";
	}
	len = strlen(raw);
	/* Tokenise first, because how a subtag is cased depends on its LENGTH: a
	 * four-letter one is a script (Title), a two-letter or three-digit one is a
	 * region (upper), and there is no way to tell from its position alone —
	 * "en_us" puts a region second, "zh_hans_cn" puts a script there. */
	for (i = 0; i < len && raw[i] != '.' && raw[i] != '@'; ) {
		size_t start = i;

		while (i < len && raw[i] != '.' && raw[i] != '@' &&
		       raw[i] != '-' && raw[i] != '_') {
			i++;
		}
		if (i > start && count < FN_SUBTAG_MAX) {
			starts[count] = start;
			lengths[count] = i - start;
			count++;
		}
		if (i < len && (raw[i] == '-' || raw[i] == '_')) {
			i++;
		}
	}
	out = (char *)malloc(len + 1);
	if (out == NULL) {
		return @"";
	}
	for (k = 0; k < count; k++) {
		size_t j;

		if (k > 0) {
			out[n++] = '_';
		}
		first = 1;
		for (j = 0; j < lengths[k]; j++) {
			unsigned char c = (unsigned char)raw[starts[k] + j];

			if (k == 0) {
				c = (unsigned char)tolower(c);
			} else if (lengths[k] == 4) {
				c = (unsigned char)(first ? toupper(c) : tolower(c));
			} else {
				c = (unsigned char)toupper(c);
			}
			out[n++] = (char)c;
			first = 0;
		}
	}
	out[n] = '\0';
	{
		NSString *canonical = [[NSString alloc] initWithUTF8String:out];

		free(out);
		return canonical;
	}
}

/* The canonical identifier's subtags. An empty identifier has none. */
static NSArray *fn_parts(NSString *canonical)
{
	if ([canonical lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		return [[NSArray alloc] init];
	}
	return [canonical componentsSeparatedByString:@"_"];
}

static NSString *fn_subtag(NSArray *parts, size_t index, size_t length)
{
	NSString *part;

	if ([parts count] <= index) {
		return nil;
	}
	part = [parts objectAtIndex:index];
	if (length != 0 && [part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] != length) {
		return nil;
	}
	if ([part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		return nil;
	}
	return part;
}

static NSString *fn_language(NSArray *parts)
{
	return fn_subtag(parts, 0, 0);
}

static NSString *fn_script(NSArray *parts)
{
	return fn_subtag(parts, 1, 4);
}

/* The region is the FIRST subtag after the language that is a region SHAPE (two
 * letters or three digits), which is what makes "zh_Hans_CN" and "en_US" both
 * work without a per-position rule. */
static NSString *fn_region(NSArray *parts)
{
	size_t k;

	for (k = 1; k < [parts count]; k++) {
		NSString *part = [parts objectAtIndex:k];

		if ([part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 2 || [part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 3) {
			return part;
		}
	}
	return nil;
}

@implementation NSLocale

+ (instancetype)currentLocale
{
	const char *env = getenv("LC_ALL");

	if (env == NULL || env[0] == '\0') {
		env = getenv("LANG");
	}
	if (env == NULL || env[0] == '\0') {
		return [self localeWithLocaleIdentifier:@"en_US_POSIX"];
	}
	return [self localeWithLocaleIdentifier:[NSString stringWithUTF8String:env]];
}

+ (instancetype)localeWithLocaleIdentifier:(NSString *)identifier
{
	return [[self alloc] initWithLocaleIdentifier:identifier];
}

+ (NSArray *)availableLocaleIdentifiers
{
	/* The Turkic pair, and only they: they are the locales whose case rules this
	 * library implements. Cocoa's list is a catalogue of hundreds — that needs the
	 * database, and claiming it here would be a lie about what the library does. */
	return [[NSArray alloc] initWithObjects:@"az", @"tr", nil];
}

+ (NSDictionary *)componentsFromLocaleIdentifier:(NSString *)identifier
{
	NSArray *parts = fn_parts(fn_canonical(identifier));
	NSMutableDictionary *components = [[NSMutableDictionary alloc] init];
	NSString *value;

	value = fn_language(parts);
	if (value != nil) {
		[components setObject:value forKey:NSLocaleLanguageCode];
	}
	value = fn_script(parts);
	if (value != nil) {
		[components setObject:value forKey:NSLocaleScriptCode];
	}
	value = fn_region(parts);
	if (value != nil) {
		[components setObject:value forKey:NSLocaleCountryCode];
	}
	[components setObject:fn_canonical(identifier) forKey:NSLocaleIdentifier];
	return components;
}

+ (NSString *)localeIdentifierFromComponents:(NSDictionary *)components
{
	NSMutableString *identifier = [[NSMutableString alloc] init];
	id value;

	if (components == nil) {
		return @"";
	}
	value = [components objectForKey:NSLocaleLanguageCode];
	if (value != nil) {
		[identifier appendString:(NSString *)value];
	}
	value = [components objectForKey:NSLocaleScriptCode];
	if (value != nil) {
		[identifier appendString:@"_"];
		[identifier appendString:(NSString *)value];
	}
	value = [components objectForKey:NSLocaleCountryCode];
	if (value != nil) {
		[identifier appendString:@"_"];
		[identifier appendString:(NSString *)value];
	}
	return fn_canonical(identifier);
}

+ (NSString *)canonicalLanguageIdentifierFromString:(NSString *)string
{
	NSArray *parts = fn_parts(fn_canonical(string));
	NSString *language = fn_language(parts);

	return (language == nil) ? @"" : language;
}

+ (NSString *)canonicalLocaleIdentifierFromString:(NSString *)string
{
	return fn_canonical(string);
}

- (id)initWithLocaleIdentifier:(NSString *)identifier
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_identifier = fn_canonical(identifier);
	return self;
}

- (NSString *)localeIdentifier
{
	return _identifier;
}

/*
 * THE DISPLAY NAMES (D7's kind (D), landed §18 — the last item on that list). FOUR PUBLISHED KEYS,
 * and the two roles are the whole design: the RECEIVER's identifier is the DISPLAY locale (the
 * language the answer is written in) and `value` is the locale or subtag being named. ICU's
 * uloc_getDisplay* take UTF-8 locale ids and answer UTF-16, so no conversion of the input is needed.
 *
 * A KEY WITH NO DISPLAY NAME ANSWERS nil, which Apple's own page allows for in as many words ("not
 * all locale property keys have values with display name values") — and so does a value that is not
 * a string, which is what Cocoa's own contract implies for these keys rather than a guess:
 * NSLocaleIdentifier, NSLocaleLanguageCode, NSLocaleCountryCode and NSLocaleScriptCode all carry
 * strings.
 */
#define FN_DISPLAY_NAME_MAX 256

- (nullable NSString *)displayNameForKey:(id)key value:(id)value
{
	UChar out[FN_DISPLAY_NAME_MAX];
	UErrorCode status = U_ZERO_ERROR;
	const char *here;
	const char *there;
	int32_t n;

	if (![value isKindOfClass:[NSString class]] || ![key isKindOfClass:[NSString class]]) {
		return nil;
	}
	here = [_identifier UTF8String];
	there = [(NSString *)value UTF8String];
	if (here == NULL || there == NULL) {
		return nil;
	}
	if ([(NSString *)key isEqualToString:NSLocaleIdentifier]) {
		n = uloc_getDisplayName(there, here, out, FN_DISPLAY_NAME_MAX, &status);
	} else if ([(NSString *)key isEqualToString:NSLocaleLanguageCode]) {
		n = uloc_getDisplayLanguage(there, here, out, FN_DISPLAY_NAME_MAX, &status);
	} else if ([(NSString *)key isEqualToString:NSLocaleCountryCode]) {
		/* A BARE SUBTAG IS NOT A LOCALE ID. ICU takes the country out of the id it is handed, and
		 * "GB" parses as a LANGUAGE, so the lookup answered nothing at all (measured: nil, on the
		 * host, before this line existed). The country subtag goes in the REGION slot of an
		 * undetermined-language id, which is what "und_GB" spells. */
		there = [[@"und_" stringByAppendingString:(NSString *)value] UTF8String];
		n = uloc_getDisplayCountry(there, here, out, FN_DISPLAY_NAME_MAX, &status);
	} else if ([(NSString *)key isEqualToString:NSLocaleScriptCode]) {
		/* The same shape one slot over: "und_Latn" is a language with a SCRIPT. */
		there = [[@"und_" stringByAppendingString:(NSString *)value] UTF8String];
		n = uloc_getDisplayScript(there, here, out, FN_DISPLAY_NAME_MAX, &status);
	} else {
		return nil;		/* a key this class has no name table for */
	}
	if (U_FAILURE(status) || n <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:out length:(NSUInteger)n];
}

- (id)objectForKey:(NSString *)key
{
	NSArray *parts = fn_parts(_identifier);

	if ([key isEqualToString:NSLocaleIdentifier]) {
		return _identifier;
	}
	if ([key isEqualToString:NSLocaleLanguageCode]) {
		return fn_language(parts);
	}
	if ([key isEqualToString:NSLocaleScriptCode]) {
		return fn_script(parts);
	}
	if ([key isEqualToString:NSLocaleCountryCode]) {
		return fn_region(parts);
	}
	/* The data-driven keys: no locale database here, so they answer nil. */
	return nil;
}

- (BOOL)isEqual:(id)other
{
	NSLocale *locale;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSLocale class]]) {
		return NO;
	}
	locale = (NSLocale *)other;
	return [_identifier isEqualToString:locale->_identifier];
}

- (NSUInteger)hash
{
	return [_identifier hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSLocale: %@>", _identifier];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

@end
