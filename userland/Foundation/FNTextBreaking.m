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
	case FNTextUnitLine:
		/* THE SAME ITERATOR AS A PARAGRAPH, AND THE DIFFERENCE IS THE MERGE BELOW: a paragraph is a run of these. */
		return ubrk_open(UBRK_LINE, NULL, buffer, length, status);
	case FNTextUnitComposedCharacter:
		return ubrk_open(UBRK_CHARACTER, NULL, buffer, length, status);
	case FNTextUnitWord:
	default:
		return ubrk_open(UBRK_WORD, NULL, buffer, length, status);
	}
}

/* LINES ARE NOT ICU'S, AND THAT IS THE CORRECTION THIS FILE CARRIES (2026-09-26). `ubrk_*`'s line iterator answers
 * WHERE A LINE COULD WRAP — UAX#14 offers a break after a space — and not where a line ENDS: asked for the lines of
 * "first line" it answers "first " and "line". The paragraph rule is written over LINES (a run that does not meet a
 * blank one), so it cannot be written over wrap opportunities, and its first version looked at the gap between two
 * of them FOR TWO LINE BREAKS — a gap that holds a space. The result was ONE PARAGRAPH PER STRING, and nothing
 * caught it until `-enumerateSubstringsInRange:options:` asked for lines and printed what it was given.
 *
 * A LINE HERE IS ITS TEXT AND NOT ITS TERMINATOR: the terminator is what separates one unit from the next, which is
 * why the two helpers below are separate rather than one index. */
static int32_t fn_line_text_end(const UChar *buffer, int32_t from, int32_t length)
{
	int32_t i = from;

	while (i < length) {
		UChar c = buffer[i];

		/* NEL (U+0085) IS A LINE TERMINATOR AND THIS PREDICATE DID NOT SAY SO (fixed §63, 2026-09-28).
		 * The four below were here from the first version; the fifth is on Apple's published list for
		 * -getLineStart:end:contentsEnd:forRange: — LF, CR, NEL, LS, PS — and it is the easy one to miss,
		 * because a caller who has only ever typed \n will not notice until a string arrives from a
		 * producer that uses it. WHAT MADE IT A DEFECT RATHER THAN A CHOICE is that NSString's own line
		 * doors are documented against that list, so this engine and they answered "how many lines"
		 * differently for the same string — two rules where the library promises one. */
		if (c == 0x000a || c == 0x000d || c == 0x0085 || c == 0x2028 || c == 0x2029) {
			return i;
		}
		i++;
	}
	return length;
}

/* PAST ONE TERMINATOR, with `\r\n` counted once: the index where the next line's text begins. */
static int32_t fn_line_next(const UChar *buffer, int32_t textEnd, int32_t length)
{
	if (textEnd >= length) {
		return length;
	}
	if (buffer[textEnd] == 0x000d && (textEnd + 1) < length && buffer[textEnd + 1] == 0x000a) {
		return textEnd + 2;
	}
	return textEnd + 1;
}

/* IS THE LINE WHOSE TEXT IS `from..textEnd` BLANK? Other whitespace counts as blank too: a line holding only
 * spaces separates two paragraphs to a reader, and this rule says so. */
static BOOL fn_line_is_blank(const UChar *buffer, int32_t from, int32_t textEnd)
{
	int32_t i;

	for (i = from; i < textEnd; i++) {
		UChar c = buffer[i];

		if (c != 0x0020 && c != 0x0009) {
			return NO;
		}
	}
	return YES;
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
	/* LINES AND PARAGRAPHS ARE BUILT FROM THE LINE RULE RATHER THAN FROM AN ITERATOR, and they are built HERE so
	 * that `+fnUnitContaining:` — which delegates to this method — cannot answer differently from this walk. */
	if (unit == FNTextUnitLine || unit == FNTextUnitParagraph) {
		int32_t at = (int32_t)from;

		while (at < (int32_t)to) {
			int32_t textEnd = fn_line_text_end(buffer, at, (int32_t)length);
			int32_t unitEnd = textEnd;
			BOOL stop = NO;

			if (unit == FNTextUnitParagraph) {
				int32_t lineStart;
				int32_t lastTextEnd;

				if (fn_line_is_blank(buffer, at, textEnd)) {
					/* A BLANK LINE IS THE SEPARATOR AND BELONGS TO NO PARAGRAPH. */
					at = fn_line_next(buffer, textEnd, (int32_t)length);
					continue;
				}
				/* WALK FORWARD WHILE THE NEXT LINE EXISTS AND IS NOT BLANK, KEEPING WHERE THE LAST ONE'S TEXT
				 * ENDS: the paragraph runs to there, so it includes its own line breaks and not a separator's. */
				lineStart = at;
				lastTextEnd = textEnd;
				while (1) {
					int32_t nextStart = fn_line_next(buffer,
						fn_line_text_end(buffer, lineStart, (int32_t)length),
						(int32_t)length);
					int32_t nextTextEnd;

					if (nextStart >= (int32_t)to) {
						break;
					}
					nextTextEnd = fn_line_text_end(buffer, nextStart, (int32_t)length);
					if (fn_line_is_blank(buffer, nextStart, nextTextEnd)) {
						break;
					}
					lineStart = nextStart;
					lastTextEnd = nextTextEnd;
				}
				unitEnd = lastTextEnd;
			}
			block(NSMakeRange((NSUInteger)at, (NSUInteger)(unitEnd - at)), &stop);
			if (stop) {
				break;
			}
			at = (unit == FNTextUnitParagraph) ? unitEnd : fn_line_next(buffer, textEnd, (int32_t)length);
		}
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

	/* ICU'S UNITS ARE HANDED ON AS THEY ARE, INCLUDING THE PUNCTUATION AND THE WHITESPACE, AND THAT IS A DECISION
	 * RATHER THAN AN OVERSIGHT: `NSLinguisticTagger` builds on this method and ITS contract is to tag every token,
	 * punctuation included. A caller who asked Apple's `NSStringEnumerationByWords` is asking a different question
	 * — and the policy that answers it lives in `-enumerateSubstringsInRange:options:usingBlock:`, which is where
	 * the question is asked. (Measured: applying the word policy HERE broke four of the tagger's checks, and its
	 * probe said which.) */
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
	__block NSRange answer = NSMakeRange(NSNotFound, 0);

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
