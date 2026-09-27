/*
 * FNTextBreaking.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The ICU break iterator, wrapped twice: a walk and a containment question. Both take the SAME buffer, because a
 * containment answered with a different breaking than the walk would be two answers to one question.
 *
 * THE PARAGRAPH RULE IS STATED HERE BECAUSE ICU DOES NOT HAVE ONE: `ubrk_*` breaks LINES, and a paragraph is a RUN
 * OF LINES with no blank line between them — which is Apple's own definition and is a rule over what the iterator
 * gives, not a table. A terminated line whose successor starts after a blank line ends the paragraph.
 */

#import <Foundation/FNTextBreaking.h>
#import <Foundation/NSString.h>

#include <unicode/ubrk.h>
#include <unicode/utypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* THE BUFFER IS COPIED ONCE PER CALL AND FREED ONCE, and the iterator is closed on every path: ICU's iterators are
 * allocations, and a walk that returned early through `stop` while leaking one would be a leak per enumeration. */
static UBreakIterator *fn_open_iterator(FNTextUnit unit, const UChar *buffer, int32_t length, UErrorCode *status)
{
	switch (unit) {
	case FNTextUnitSentence:
		return ubrk_open(UBRK_SENTENCE, NULL, buffer, length, status);
	case FNTextUnitParagraph:
		return ubrk_open(UBRK_LINE, NULL, buffer, length, status);
	case FNTextUnitComposedCharacter:
		return ubrk_open(UBRK_CHARACTER, NULL, buffer, length, status);
	case FNTextUnitWord:
	default:
		return ubrk_open(UBRK_WORD, NULL, buffer, length, status);
	}
}

/* IS THERE A BLANK LINE BETWEEN TWO OFFSETS? That is what separates two PARAGRAPHS, and it is asked of the buffer
 * rather than of a second iterator: a gap holding two line breaks (or a line break and other whitespace) ends one. */
static BOOL fn_gap_has_a_blank_line(const UChar *buffer, int32_t from, int32_t to)
{
	int32_t i;
	int32_t breaks = 0;

	for (i = from; i < to; i++) {
		UChar c = buffer[i];

		if (c == 0x000a || c == 0x000d || c == 0x2028 || c == 0x2029) {
			if (++breaks >= 2) {
				return YES;
			}
		}
	}
	return NO;
}

@implementation FNTextBreaking

/* THE BOUNDARIES ARE COLLECTED FIRST AND THE UNITS ARE BUILT FROM THEM, which is a decision rather than a style:
 * ICU's iterator is a cursor, and a cursor that has been advanced twice to find where a paragraph ends is a cursor
 * whose position the next step has to reason about. A LIST OF BOUNDARIES cannot be mis-stepped — the paragraph
 * rule becomes arithmetic over the gaps — and the first version of this function, which walked the cursor through
 * a merge, was exactly the kind of loop that is wrong in a way its author cannot see. */
+ (void)fnEnumerate:(FNTextUnit)unit
	   inString:(NSString *)string
	      range:(NSRange)range
	 usingBlock:(void (^)(NSRange unitRange, BOOL *stop))block
{
	NSUInteger length = [string length];
	UChar *buffer;
	UBreakIterator *iterator;
	UErrorCode status = U_ZERO_ERROR;
	int32_t *bounds = NULL;
	NSUInteger count = 0, capacity = 0;
	NSUInteger from, to, i;

	if (block == nil || length == 0) {
		return;
	}
	/* CLAMPED, NOT REFUSED: a range that runs past the end asks about the tail. */
	from = range.location > length ? length : range.location;
	to = range.length > (length - from) ? length : (from + range.length);
	if (to <= from) {
		return;
	}
	buffer = (UChar *)malloc(sizeof(UChar) * length);
	if (buffer == NULL) {
		return;
	}
	[string getCharacters:buffer range:NSMakeRange(0, length)];
	iterator = fn_open_iterator(unit, buffer, (int32_t)length, &status);
	if (U_FAILURE(status) || iterator == NULL) {
		free(buffer);
		return;
	}
	for (i = 0, status = U_ZERO_ERROR; ; ) {
		int32_t at = (count == 0) ? ubrk_first(iterator) : ubrk_next(iterator);

		if (at == UBRK_DONE) {
			break;
		}
		if (count == capacity) {
			capacity = capacity == 0 ? 16 : capacity * 2;
			bounds = (int32_t *)realloc(bounds, sizeof(int32_t) * capacity);
			if (bounds == NULL) {
				ubrk_close(iterator);
				free(buffer);
				return;
			}
		}
		bounds[count++] = at;
		(void)i;
	}
	ubrk_close(iterator);

	/* EVERY UNIT IS A PAIR OF ADJACENT BOUNDARIES, and the last boundary is the text's end. */
	for (i = 0; i + 1 < count; i++) {
		NSUInteger unitStart = (NSUInteger)bounds[i];
		NSUInteger unitEnd = (NSUInteger)bounds[i + 1];
		BOOL stop = NO;

		if (unitStart < from) {
			continue;
		}
		if (unitStart >= to) {
			break;
		}
		if (unit == FNTextUnitParagraph) {
			/* MERGE THE RUN: a paragraph is every following line that does NOT start after a blank line. */
			while (i + 2 < count &&
			       !fn_gap_has_a_blank_line(buffer, bounds[i + 1], bounds[i + 2])) {
				i++;
				unitEnd = (NSUInteger)bounds[i + 1];
			}
		}
		block(NSMakeRange(unitStart, unitEnd - unitStart), &stop);
		if (stop) {
			break;
		}
	}
	free(bounds);
	free(buffer);
}

+ (NSRange)fnUnitContaining:(FNTextUnit)unit inString:(NSString *)string atIndex:(NSUInteger)index
{
	__block NSRange answer = NSMakeRange(0, [string length]);

	if (index >= [string length]) {
		return NSMakeRange(NSNotFound, 0);
	}
	[self fnEnumerate:unit
		 inString:string
		    range:NSMakeRange(0, [string length])
	       usingBlock:^(NSRange unitRange, BOOL *stop) {
		if (index >= unitRange.location && index < (unitRange.location + unitRange.length)) {
			answer = unitRange;
			*stop = YES;
		}
	}];
	return answer;
}

@end
