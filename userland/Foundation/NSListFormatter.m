/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSListFormatter.m — the CLDR list-pattern binding.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words).
 *
 * THE ICU HANDLE IS CACHED AND REBUILT, which is NSDateFormatter's shape one door over: ICU bakes the
 * locale into the formatter at open time, so every setting change must discard the handle rather than
 * mutate it. `-locale` is the only setting this class has that ICU reads at open time; the item
 * formatter is applied per item and needs no rebuild.
 *
 * BUFFERS ARE HEAP HERE, NOT FIXED, and the difference from NSDateFormatter is deliberate rather than
 * sloppy: a date's text has a bounded length (FN_DF_MAX = 256 is generous for it), while a LIST's
 * length is the sum of its items' — unbounded by construction. So this file sizes its buffers from
 * the items and frees them, and it never truncates a list to fit a constant.
 *
 * FAILURE IS nil, in three places and for one reason: no handle, an item that could not be
 * converted, or an ICU call that failed. The header states the nullability contract, so a caller is
 * not surprised by any of them.
 */

#import <Foundation/NSListFormatter.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSException.h>

#include <unicode/ulistformatter.h>
#include <unicode/ustring.h>

#include <stdlib.h>
#include <string.h>

@implementation NSListFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_locale = nil;			/* nil == "the current locale", decided at use */
	_itemFormatter = nil;
	_formatter = NULL;
	return self;
}

- (void)dealloc
{
	[_locale release];
	[_itemFormatter release];
	if (_formatter != NULL) {
		ulistfmt_close((UListFormatter *)_formatter);
		_formatter = NULL;
	}
	[super dealloc];
}

/*
 * (Re)open the ICU formatter for the EFFECTIVE locale. A locale that was never set is the current
 * one, read HERE rather than at init, so a formatter made before the environment changed does not
 * keep a stale answer.
 */
- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	NSLocale *locale = [self locale];

	if (_formatter != NULL) {
		ulistfmt_close((UListFormatter *)_formatter);
		_formatter = NULL;
	}
	if (locale == nil) {
		return;
	}
	/* CONJUNCTION, WIDE: Apple's list formatter is the "and" list — a list of things, not a list of
	 * alternatives (that is ICU's TYPE_OR, which no Foundation class exposes) and not a measurement
	 * list (TYPE_UNITS, which is NSMeasurementFormatter's business in W12). */
	_formatter = ulistfmt_openForType([[locale localeIdentifier] UTF8String],
					  ULISTFMT_TYPE_AND, ULISTFMT_WIDTH_WIDE, &status);
	if (U_FAILURE(status)) {
		_formatter = NULL;
	}
}

- (nullable NSString *)stringFromItems:(NSArray *)items
{
	NSUInteger count, i;
	NSString **texts = NULL;
	UChar **strings = NULL;
	int32_t *lengths = NULL;
	UChar *result = NULL;
	char *bytes = NULL;
	int32_t needed = 0;
	int32_t capacity;
	int32_t written;
	UErrorCode status = U_ZERO_ERROR;
	NSString *answer = nil;

	if (items == nil) {
		/* A nil array is a caller error, not a list of no items: the annotation says nonnull and
		 * the library raises where the annotation is broken (-isValidDateInCalendar: does the same). */
		[NSException raise:NSInvalidArgumentException
			    format:@"-stringFromItems: needs an array (an EMPTY array is an empty list)"];
	}
	/* BUILT LAZILY AS WELL AS ON A SETTING (the first version of this file built the handle ONLY in
	 * -setLocale:, so a freshly allocated formatter answered nil for a list — the current locale is a
	 * legitimate configuration and `[[NSListFormatter alloc] init]` has to read it). The second test
	 * is the real failure: an open ICU refused, which leaves the handle NULL on purpose rather than
	 * caching a broken one. */
	if (_formatter == NULL) {
		[self fnRebuild];
	}
	if (_formatter == NULL) {
		return nil;
	}
	count = [items count];
	if (count == 0) {
		/* The conjunction of no items is the empty string, which is what ICU would also answer —
		 * returned here so the general path never has to allocate a zero-item call. */
		return @"";
	}

	texts = (NSString **)malloc(count * sizeof(NSString *));
	strings = (UChar **)malloc(count * sizeof(UChar *));
	lengths = (int32_t *)malloc(count * sizeof(int32_t));
	if (texts == NULL || strings == NULL || lengths == NULL) {
		goto done;
	}
	for (i = 0; i < count; i++) {
		strings[i] = NULL;
		lengths[i] = 0;
	}

	/* PASS ONE: decide each item's TEXT. The item formatter is asked per item when one is set;
	 * otherwise a string is itself and anything else answers -description. A formatter that answers
	 * nil for an item contributes the empty string, which keeps the list's ARITY intact — dropping
	 * the item instead would silently change how many things the caller listed. */
	for (i = 0; i < count; i++) {
		id item = [items objectAtIndex:i];
		NSString *text;
		if (_itemFormatter != nil) {
			text = [_itemFormatter stringForObjectValue:item];
		} else if ([item isKindOfClass:[NSString class]]) {
			text = (NSString *)item;
		} else {
			text = [item description];
		}
		texts[i] = text != nil ? text : @"";
	}

	/* PASS TWO: UTF-8 -> UTF-16, one heap buffer per item, which is the shape ICU's array-of-pointers
	 * argument wants. */
	for (i = 0; i < count; i++) {
		const char *utf8 = [texts[i] UTF8String];
		int32_t utf8len = (int32_t)strlen(utf8);
		status = U_ZERO_ERROR;
		strings[i] = (UChar *)malloc(((size_t)utf8len + 1) * sizeof(UChar));
		if (strings[i] == NULL) {
			goto done;
		}
		u_strFromUTF8(strings[i], utf8len + 1, &lengths[i], utf8, utf8len, &status);
		if (U_FAILURE(status)) {
			goto done;
		}
		needed += lengths[i];
	}

	/* The result's capacity is bounded by the items plus the pattern's own punctuation, which is a
	 * handful of code units per item; the slack is generous rather than tight because a tight bound
	 * would be a guess about CLDR data this file does not hold. */
	capacity = needed + (int32_t)count * 8 + 64;
	result = (UChar *)malloc((size_t)capacity * sizeof(UChar));
	if (result == NULL) {
		goto done;
	}
	status = U_ZERO_ERROR;
	written = ulistfmt_format((const UListFormatter *)_formatter,
				  (const UChar *const *)strings, lengths, (int32_t)count,
				  result, capacity, &status);
	if (U_FAILURE(status) || written <= 0) {
		goto done;
	}

	/* UTF-16 -> UTF-8. The bytes buffer is sized from the code-unit count, which is an upper bound on
	 * the UTF-8 length for BMP text and cannot be shorter than 4x for anything (max 3 bytes per BMP
	 * unit, so *4 is slack, never truncation). */
	bytes = (char *)malloc(((size_t)written * 4) + 1);
	if (bytes == NULL) {
		goto done;
	}
	status = U_ZERO_ERROR;
	{
		int32_t used = 0;
		u_strToUTF8(bytes, (int32_t)((size_t)written * 4), &used, result, written, &status);
		if (U_FAILURE(status)) {
			goto done;
		}
		bytes[used] = '\0';
	}
	answer = [NSString stringWithUTF8String:bytes];

done:
	if (strings != NULL) {
		for (i = 0; i < count; i++) {
			free(strings[i]);
		}
	}
	free(strings);
	free(lengths);
	free(texts);
	free(result);
	free(bytes);
	return answer;
}

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	/* "not my kind of value" is nil, which is the convention NSFormatter's subclasses follow; the
	 * cast is safe because the class check precedes it. */
	if (![object isKindOfClass:[NSArray class]]) {
		return nil;
	}
	return [self stringFromItems:(NSArray *)object];
}

+ (nullable NSString *)localizedStringByJoiningStrings:(NSArray *)strings
{
	NSListFormatter *formatter = [[[self alloc] init] autorelease];
	NSString *answer;

	if (strings == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+localizedStringByJoiningStrings: needs an array"];
	}
	/* A fresh formatter, so the current locale is read now and no caller's settings are involved:
	 * Apple's method is the whole list operation with no object to configure. */
	[formatter fnRebuild];
	answer = [formatter stringFromItems:strings];
	return answer;
}

- (nullable NSFormatter *)itemFormatter
{
	return _itemFormatter;
}

- (void)setItemFormatter:(nullable NSFormatter *)value
{
	/* COPIED, not retained: Apple declares it `copy`, and the reason is the mutable-NSMutableString
	 * reason one level up — a formatter is mutable and a caller that changed its settings later must
	 * not thereby change how this list renders. */
	id copy = [value copy];
	[_itemFormatter release];
	_itemFormatter = copy;
}

- (NSLocale *)locale
{
	/* RESETTABLE, not optional: nil storage means "the current locale", so the getter never answers
	 * nil even though the setter accepts it. */
	if (_locale != nil) {
		return _locale;
	}
	return [NSLocale currentLocale];
}

- (void)setLocale:(nullable NSLocale *)value
{
	if (value == _locale) {
		return;
	}
	[_locale release];
	_locale = [value retain];
	[self fnRebuild];
}

@end
