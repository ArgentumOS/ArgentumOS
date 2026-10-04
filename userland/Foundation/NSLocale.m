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
#import <Foundation/NSNumber.h>		/* +ISOCurrencyCodes and the locale sources answer with objects */
#import <Foundation/NSCoder.h>		/* §63.22: the coder PRIMITIVES the doors are written over */
/* ICU ANSWERS THE DISPLAY NAMES (see -displayNameForKey:value: below). This library already links
 * icuuc, and NSLocale.m joins the per-file ICU include table in the build for it. */
#include <unicode/uloc.h>
/* THE REST OF ICU THAT THIS CLASS NOW READS. uloc/ulocdata/ucurr/uenum/ustring are libicuuc;
 * ucal/ucol/unum are libicui18n — the library links BOTH (-licui18n -licuuc -licudata). */
#include <unicode/ucal.h>
#include <unicode/ucol.h>
#include <unicode/ucurr.h>
#include <unicode/uenum.h>
#include <unicode/unum.h>
#include <unicode/uset.h>
#include <unicode/ustring.h>
#include <unicode/ulocdata.h>
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

/* The variant SUBTAG: the first subtag after the language that is neither a script (a four-letter
 * subtag) nor a region (two letters or three digits) — the shape rule fn_region uses, one slot over.
 * It is an approximation this library's canonicaliser forces: it does not distinguish a variant from
 * any other leftover subtag, so the identifier's own subtags are all -objectForKey: can derive. */
static NSString *fn_variant(NSArray *parts)
{
	size_t k;

	for (k = 1; k < [parts count]; k++) {
		NSString *part = [parts objectAtIndex:k];
		size_t len = [part lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

		if (len == 4 || len == 2 || len == 3) {
			continue;	/* a script or a region, by the shape rule fn_region uses */
		}
		return part;
	}
	return nil;
}

/* ------------------------------------------------------------------------------------------------
 * THE ICU BRIDGE. Every helper below is a thin wrapper over one ICU call, so the "does ICU answer
 * this?" question is answered by the call itself and not by a comment. Each was MEASURED on the host
 * against ICU 76.1 (the version the guest stages) before the method that uses it was written.
 * ------------------------------------------------------------------------------------------------ */

#define FN_ICU_OUT 256

/* ULayoutType -> the NSLocaleLanguageDirection this header documents. */
static NSLocaleLanguageDirection fn_direction(ULayoutType layout)
{
	switch (layout) {
	case ULOC_LAYOUT_LTR: return NSLocaleLanguageDirectionLeftToRight;
	case ULOC_LAYOUT_RTL: return NSLocaleLanguageDirectionRightToLeft;
	case ULOC_LAYOUT_TTB: return NSLocaleLanguageDirectionTopToBottom;
	case ULOC_LAYOUT_BTT: return NSLocaleLanguageDirectionBottomToTop;
	default: return NSLocaleLanguageDirectionUnknown;
	}
}

static NSArray *fn_currency_catalogue(uint32_t type)
{
	UErrorCode status = U_ZERO_ERROR;
	UEnumeration *codes = ucurr_openISOCurrencies(type, &status);
	NSMutableArray *out = [NSMutableArray array];

	if (U_FAILURE(status) || codes == NULL) {
		return out;
	}
	for (;;) {
		const char *code = uenum_next(codes, NULL, &status);
		NSString *text;

		if (U_FAILURE(status) || code == NULL) {
			break;
		}
		text = [NSString stringWithUTF8String:code];
		if (text != nil) {
			[out addObject:text];
		}
	}
	uenum_close(codes);
	return out;
}

static NSString *fn_number_symbol(NSString *locale, UNumberFormatSymbol symbol)
{
	UChar buffer[FN_ICU_OUT];
	UErrorCode status = U_ZERO_ERROR;
	UNumberFormat *format = unum_open(UNUM_DECIMAL, NULL, -1, [locale UTF8String], NULL, &status);
	int32_t length;

	if (U_FAILURE(status) || format == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	length = unum_getSymbol(format, symbol, buffer, FN_ICU_OUT, &status);
	unum_close(format);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:buffer length:(NSUInteger)length];
}

static NSString *fn_delimiter(NSString *locale, ULocaleDataDelimiterType type)
{
	UChar buffer[64];
	UErrorCode status = U_ZERO_ERROR;
	ULocaleData *data = ulocdata_open([locale UTF8String], &status);
	int32_t length;

	if (U_FAILURE(status) || data == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	length = ulocdata_getDelimiter(data, type, buffer, 64, &status);
	ulocdata_close(data);
	/* ICU reports U_USING_FALLBACK_WARNING / U_USING_DEFAULT_WARNING for a locale that inherits the
	 * delimiter from a parent, and that is a real answer, not a failure — so only U_FAILURE stops us. */
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:buffer length:(NSUInteger)length];
}

/* The FIRST keyword value for a locale, which is ICU's DEFAULT for that keyword. MEASURED: en_US's
 * calendar is "gregorian" (ja_JP's too; th_TH's is "buddhist"), and en_US's collation is "standard". */
static NSString *fn_keyword_default(NSString *locale, const char *keyword, int calendar)
{
	UErrorCode status = U_ZERO_ERROR;
	UEnumeration *values = calendar
		? ucal_getKeywordValuesForLocale(keyword, [locale UTF8String], 0, &status)
		: ucol_getKeywordValuesForLocale(keyword, [locale UTF8String], 0, &status);
	NSString *first = nil;

	if (U_FAILURE(status)) {
		if (values != NULL) {
			uenum_close(values);
		}
		return nil;
	}
	status = U_ZERO_ERROR;
	if (values != NULL) {
		const char *value = uenum_next(values, NULL, &status);

		if (!U_FAILURE(status) && value != NULL) {
			first = [NSString stringWithUTF8String:value];
		}
		uenum_close(values);
	}
	return first;
}

/* A keyword VALUE's display name in `displayLocale`. ICU names a keyword value inside a LOCALE ID, so
 * the value rides in the keyword slot of an English carrier ("en@calendar=hebrew"), which is what a
 * keyword value is. MEASURED: "Hijri Calendar (tabular, civil epoch)" for calendar=islamic-civil. */
static NSString *fn_keyword_display(NSString *displayLocale, const char *keyword, NSString *value)
{
	UChar buffer[FN_ICU_OUT];
	UErrorCode status = U_ZERO_ERROR;
	NSString *carrier = [NSString stringWithFormat:@"en@%s=%@", keyword, value];
	int32_t length = uloc_getDisplayKeywordValue([carrier UTF8String], keyword,
		[displayLocale UTF8String], buffer, FN_ICU_OUT, &status);

	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:buffer length:(NSUInteger)length];
}

/* A currency CODE's long display name in `displayLocale`. MEASURED: "US Dollar" (en_US), "euro" (fr_FR). */
static NSString *fn_currency_display(NSString *code, NSString *displayLocale)
{
	UChar currency[8];
	UErrorCode status = U_ZERO_ERROR;
	UBool choice = 0;
	int32_t length = 0;
	const UChar *name;

	(void)u_strFromUTF8(currency, 8, NULL, [code UTF8String], -1, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	status = U_ZERO_ERROR;
	name = ucurr_getName(currency, [displayLocale UTF8String], UCURR_LONG_NAME, &choice, &length, &status);
	if (U_FAILURE(status) || name == NULL || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:name length:(NSUInteger)length];
}

/* A USet -> an NSCharacterSet. THIS LIBRARY'S SETS ARE BMP-ONLY (NSCharacterSet.h), so an astral code
 * point in the exemplar set is SKIPPED rather than mis-stored — the same boundary the set family draws,
 * stated rather than hidden. MEASURED: en_US's standard exemplar set is 26 characters (a–z). */
static NSCharacterSet *fn_character_set(USet *set)
{
	int32_t items = uset_getItemCount(set);
	int32_t index;
	NSMutableString *characters = [NSMutableString string];

	for (index = 0; index < items; index++) {
		UChar32 start = 0;
		UChar32 end = 0;
		UChar buffer[8];
		UErrorCode status = U_ZERO_ERROR;
		int32_t length = uset_getItem(set, index, &start, &end, buffer, 8, &status);

		if (U_FAILURE(status)) {
			continue;
		}
		if (length == 0) {
			UChar32 code;

			for (code = start; code <= end && code <= 0xFFFF; code++) {
				[characters appendFormat:@"%C", (unichar)code];
			}
		} else {
			int32_t k;

			for (k = 0; k < length; k++) {
				[characters appendFormat:@"%C", (unichar)buffer[k]];
			}
		}
	}
	if ([characters length] == 0) {
		return nil;
	}
	return [NSCharacterSet characterSetWithCharactersInString:characters];
}

@implementation NSLocale

/* THE NSCoding DOORS (§63.22). THE IDENTIFIER IS THE WHOLE VALUE — that is what the ivar already is — and an
 * identifier the archive does not carry is passed THROUGH to -initWithLocaleIdentifier:, whose own declared
 * answer for it is nil: the door does not invent a locale the archive never named. */
- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_identifier forKey:@"NS.localeIdentifier"];
}

- (id)initWithCoder:(NSCoder *)coder
{
	return [self initWithLocaleIdentifier:[coder decodeObjectForKey:@"NS.localeIdentifier"]];
}

NSString *const NSCurrentLocaleDidChangeNotification = @"NSCurrentLocaleDidChangeNotification";
NSString *const NSLocaleAlternateQuotationBeginDelimiterKey = @"NSLocaleAlternateQuotationBeginDelimiterKey";
NSString *const NSLocaleAlternateQuotationEndDelimiterKey = @"NSLocaleAlternateQuotationEndDelimiterKey";
NSString *const NSLocaleCalendar = @"NSLocaleCalendar";
NSString *const NSLocaleCollationIdentifier = @"NSLocaleCollationIdentifier";
NSString *const NSLocaleCollatorIdentifier = @"NSLocaleCollatorIdentifier";
NSString *const NSLocaleCurrencyCode = @"NSLocaleCurrencyCode";
NSString *const NSLocaleCurrencySymbol = @"NSLocaleCurrencySymbol";
NSString *const NSLocaleDecimalSeparator = @"NSLocaleDecimalSeparator";
NSString *const NSLocaleExemplarCharacterSet = @"NSLocaleExemplarCharacterSet";
NSString *const NSLocaleGroupingSeparator = @"NSLocaleGroupingSeparator";
NSString *const NSLocaleMeasurementSystem = @"NSLocaleMeasurementSystem";
NSString *const NSLocaleQuotationBeginDelimiterKey = @"NSLocaleQuotationBeginDelimiterKey";
NSString *const NSLocaleQuotationEndDelimiterKey = @"NSLocaleQuotationEndDelimiterKey";
NSString *const NSLocaleUsesMetricSystem = @"NSLocaleUsesMetricSystem";
NSString *const NSLocaleVariantCode = @"NSLocaleVariantCode";

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
	/* THE DATA KEYS ANSWER NIL HERE, ON PURPOSE, AND THE STRING PROBE PINS IT (locale-basics asserts
	 * -objectForKey:NSLocaleDecimalSeparator is nil). The database this library now links is reached
	 * through the PROPERTIES and -localizedStringFor…:, NOT through this door. */
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

/* ================================================================================================
 * THE LOCALE SOURCES AND THE ISO CATALOGUES. §62.24 retired the "we have no data" boundary the string
 * probe had drawn around these; each is a thin door onto an ICU call, measured on the host first.
 * ================================================================================================ */

+ (NSLocale *)autoupdatingCurrentLocale
{
	/* No locale store ships, so there is nothing to cache against: each access re-reads the environment,
	 * which is the source +currentLocale reads too. That is the honest reading of "autoupdating" here —
	 * the locale does follow a change (to LC_ALL/LANG), because it is read afresh every time. */
	return [self currentLocale];
}

+ (NSLocale *)systemLocale
{
	/* ICU's default locale is what a system sets — the same source NSTimeZone's +systemTimeZone reads
	 * (ucal_getDefaultTimeZone). Canonicalised like every other identifier this class carries. */
	NSString *identifier = [NSString stringWithUTF8String:uloc_getDefault()];

	if (identifier == nil) {
		return [self localeWithLocaleIdentifier:@"en_US_POSIX"];
	}
	return [self localeWithLocaleIdentifier:identifier];
}

+ (NSArray *)ISOLanguageCodes
{
	NSMutableArray *codes = [NSMutableArray array];
	const char *const *list = uloc_getISOLanguages();
	size_t index;

	for (index = 0; list != NULL && list[index] != NULL; index++) {
		NSString *code = [NSString stringWithUTF8String:list[index]];

		if (code != nil) {
			[codes addObject:code];
		}
	}
	return codes;
}

+ (NSArray *)ISOCountryCodes
{
	NSMutableArray *codes = [NSMutableArray array];
	const char *const *list = uloc_getISOCountries();
	size_t index;

	for (index = 0; list != NULL && list[index] != NULL; index++) {
		NSString *code = [NSString stringWithUTF8String:list[index]];

		if (code != nil) {
			[codes addObject:code];
		}
	}
	return codes;
}

+ (NSArray *)ISOCurrencyCodes
{
	return fn_currency_catalogue(UCURR_ALL);
}

+ (NSArray *)commonISOCurrencyCodes
{
	return fn_currency_catalogue(UCURR_COMMON);
}

+ (nullable NSString *)localeIdentifierFromWindowsLocaleCode:(uint32_t)windowsLocaleCode
{
	char buffer[128];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = uloc_getLocaleForLCID(windowsLocaleCode, buffer, sizeof buffer, &status);

	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return fn_canonical([NSString stringWithUTF8String:buffer]);
}

+ (uint32_t)windowsLocaleCodeFromLocaleIdentifier:(NSString *)localeIdentifier
{
	/* 0 is ICU's "no LCID for this identifier", the natural nothing for a uint32_t return. */
	return (uint32_t)uloc_getLCID([localeIdentifier UTF8String]);
}

+ (NSLocaleLanguageDirection)characterDirectionForLanguage:(NSString *)isoLanguageCode
{
	UErrorCode status = U_ZERO_ERROR;
	ULayoutType layout = uloc_getCharacterOrientation([isoLanguageCode UTF8String], &status);

	if (U_FAILURE(status)) {
		return NSLocaleLanguageDirectionUnknown;
	}
	return fn_direction(layout);
}

+ (NSLocaleLanguageDirection)lineDirectionForLanguage:(NSString *)isoLanguageCode
{
	UErrorCode status = U_ZERO_ERROR;
	ULayoutType layout = uloc_getLineOrientation([isoLanguageCode UTF8String], &status);

	if (U_FAILURE(status)) {
		return NSLocaleLanguageDirectionUnknown;
	}
	return fn_direction(layout);
}

/* ================================================================================================
 * THE SUBTAGS THE IDENTIFIER ITSELF CARRIES — a RULE, no database. -objectForKey: is UNTOUCHED: it
 * still answers the four subtag keys and nils every data key, which is what the SPEC's -locale-basics
 * check pins ("-objectForKey:NSLocaleDecimalSeparator is nil"). These doors are separate.
 * ================================================================================================ */

- (nullable NSString *)languageCode
{
	return fn_language(fn_parts(_identifier));
}

- (nullable NSString *)scriptCode
{
	return fn_script(fn_parts(_identifier));
}

- (nullable NSString *)countryCode
{
	return fn_region(fn_parts(_identifier));
}

- (nullable NSString *)regionCode
{
	return fn_region(fn_parts(_identifier));
}

- (nullable NSString *)variantCode
{
	return fn_variant(fn_parts(_identifier));
}

- (NSString *)languageIdentifier
{
	NSArray *parts = fn_parts(_identifier);
	NSString *language = fn_language(parts);
	NSString *script = fn_script(parts);

	if (language == nil) {
		return @"";
	}
	if (script == nil) {
		return language;
	}
	/* Apple joins the language and its script with a HYPHEN ("zh-Hans"), not the underscore the locale
	 * identifier uses — one is BCP-47's language tag, the other is the identifier's own spelling. */
	return [NSString stringWithFormat:@"%@-%@", language, script];
}

/* ================================================================================================
 * THE KEYS ICU ANSWERS — the DATABASE's half, now bound. Each reads ICU DIRECTLY (NOT through
 * -objectForKey:, which the probe pins to the subtag keys); every value was MEASURED on the host first.
 * ================================================================================================ */

- (nullable NSString *)calendarIdentifier
{
	return fn_keyword_default(_identifier, "calendar", 1);
}

- (nullable NSString *)collationIdentifier
{
	return fn_keyword_default(_identifier, "collation", 0);
}

- (nullable NSString *)collatorIdentifier
{
	/* The collation identifier for the locale — the same ICU keyword as -collationIdentifier. */
	return fn_keyword_default(_identifier, "collation", 0);
}

- (nullable NSString *)currencyCode
{
	UChar buffer[8];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = ucurr_forLocale([_identifier UTF8String], buffer, 8, &status);

	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:buffer length:(NSUInteger)length];
}

- (nullable NSString *)currencySymbol
{
	UChar buffer[8];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = ucurr_forLocale([_identifier UTF8String], buffer, 8, &status);
	UBool choice = 0;
	int32_t symbolLength = 0;
	const UChar *symbol;

	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	status = U_ZERO_ERROR;
	symbol = ucurr_getName(buffer, [_identifier UTF8String], UCURR_SYMBOL_NAME, &choice, &symbolLength, &status);
	if (U_FAILURE(status) || symbol == NULL || symbolLength <= 0) {
		return nil;
	}
	return [NSString stringWithCharacters:symbol length:(NSUInteger)symbolLength];
}

- (nullable NSString *)decimalSeparator
{
	return fn_number_symbol(_identifier, UNUM_DECIMAL_SEPARATOR_SYMBOL);
}

- (nullable NSString *)groupingSeparator
{
	return fn_number_symbol(_identifier, UNUM_GROUPING_SEPARATOR_SYMBOL);
}

- (nullable NSString *)quotationBeginDelimiter
{
	return fn_delimiter(_identifier, ULOCDATA_QUOTATION_START);
}

- (nullable NSString *)quotationEndDelimiter
{
	return fn_delimiter(_identifier, ULOCDATA_QUOTATION_END);
}

- (nullable NSString *)alternateQuotationBeginDelimiter
{
	return fn_delimiter(_identifier, ULOCDATA_ALT_QUOTATION_START);
}

- (nullable NSString *)alternateQuotationEndDelimiter
{
	return fn_delimiter(_identifier, ULOCDATA_ALT_QUOTATION_END);
}

- (nullable NSCharacterSet *)exemplarCharacterSet
{
	UErrorCode status = U_ZERO_ERROR;
	ULocaleData *data = ulocdata_open([_identifier UTF8String], &status);
	USet *set;
	NSCharacterSet *result;

	if (U_FAILURE(status) || data == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	set = ulocdata_getExemplarSet(data, NULL, 0, ULOCDATA_ES_STANDARD, &status);
	ulocdata_close(data);
	if (U_FAILURE(status) || set == NULL) {
		if (set != NULL) {
			uset_close(set);
		}
		return nil;
	}
	result = fn_character_set(set);
	uset_close(set);
	return result;
}

- (BOOL)usesMetricSystem
{
	UErrorCode status = U_ZERO_ERROR;
	UMeasurementSystem system = ulocdata_getMeasurementSystem([_identifier UTF8String], &status);

	if (U_FAILURE(status)) {
		return NO;
	}
	/* MEASURED: en_US is UMS_US (1), every metric locale is UMS_SI (0), and en_GB (which is officially
	 * metric) is UMS_UK (2), ICU's "mix". "Uses the metric system" is TRUE for SI AND for the UK mix and
	 * FALSE only for the US customary system — the reading that makes en_GB answer YES, like Apple. */
	return system != UMS_US;
}

/* ================================================================================================
 * THE DISPLAY-NAME DOORS. THE FOUR SUBTAG DOORS delegate to -displayNameForKey:value: — that IS Apple's
 * identity for them, and the spec's -locale-display-names check pins those answers. THE FOUR DATA DOORS
 * read ICU directly, because -displayNameForKey:value: is pinned to nil for the data keys (the same
 * check asserts its currency key is nil). -localizedStringForVariantCode: is NOT here — see
 * -displayNameForKey:value:'s neighbour for the measured reason.
 * ================================================================================================ */

- (nullable NSString *)localizedStringForLocaleIdentifier:(NSString *)localeIdentifier
{
	return [self displayNameForKey:NSLocaleIdentifier value:localeIdentifier];
}

- (nullable NSString *)localizedStringForLanguageCode:(NSString *)languageCode
{
	return [self displayNameForKey:NSLocaleLanguageCode value:languageCode];
}

- (nullable NSString *)localizedStringForCountryCode:(NSString *)countryCode
{
	return [self displayNameForKey:NSLocaleCountryCode value:countryCode];
}

- (nullable NSString *)localizedStringForScriptCode:(NSString *)scriptCode
{
	return [self displayNameForKey:NSLocaleScriptCode value:scriptCode];
}

- (nullable NSString *)localizedStringForCalendarIdentifier:(NSString *)calendarIdentifier
{
	return fn_keyword_display(_identifier, "calendar", calendarIdentifier);
}

- (nullable NSString *)localizedStringForCollationIdentifier:(NSString *)collationIdentifier
{
	return fn_keyword_display(_identifier, "collation", collationIdentifier);
}

- (nullable NSString *)localizedStringForCollatorIdentifier:(NSString *)collatorIdentifier
{
	return fn_keyword_display(_identifier, "collation", collatorIdentifier);
}

- (nullable NSString *)localizedStringForCurrencyCode:(NSString *)currencyCode
{
	return fn_currency_display(currencyCode, _identifier);
}


+ (NSArray *)preferredLanguages
{
	NSString *code = [[self currentLocale] objectForKey:NSLocaleLanguageCode];

	return code != nil ? [NSArray arrayWithObject:code] : [NSArray array];
}
@end
