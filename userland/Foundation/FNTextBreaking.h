/*
 * FNTextBreaking.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * TEXT BREAKING, OVER ICU, IN ONE PLACE (§62.42): where a WORD, a SENTENCE and a PARAGRAPH begin and end. Apple's
 * `NSLinguisticTagger` asks those questions, and so does `-enumerateSubstringsInRange:options:usingBlock:` — which
 * THIS LIBRARY DECLARES AND DID NOT IMPLEMENT — and one truth in one place is the reason this is a file rather
 * than a private method inside the tagger.
 *
 * THE DEPENDENCY IS THE SAME KIND THE REST OF THIS LIBRARY USES: a RULE OVER A LIBRARY IT ALREADY LINKS. ICU ships
 * text breaking (`ubrk_*`), Unicode properties (`uchar.h`, `uscript.h`) and collation; NSScanner's numeric grammars
 * and NSLocale's identifications already rest on that arrangement, and this is the fourth use rather than a new
 * kind of thing.
 *
 * INDICES ARE UTF-16 UNITS, WHICH IS WHAT AN `NSString` INDEX IS: the string's characters are copied into a buffer
 * once and the iterator walks THAT, so every range this returns can be handed straight back to a caller as an
 * `NSRange` without a translation nobody would remember to apply.
 */

#ifndef FOUNDATION_FNTEXTBREAKING_H
#define FOUNDATION_FNTEXTBREAKING_H

#import <Foundation/NSObject.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* THE UNITS, AND THE FOURTH IS NOT A TAGGER'S: composed-character sequences are what `NSString`'s own enumeration
 * calls grapheme clustering, and it lives here because it is the same iterator with a different rule. */
typedef NS_ENUM(NSInteger, FNTextUnit) {
	FNTextUnitWord = 0,
	FNTextUnitSentence = 1,
	FNTextUnitParagraph = 2,
	FNTextUnitComposedCharacter = 3,
	/* LINES AND PARAGRAPHS ARE ONE WALK OVER TWO TERMINATOR SETS (the decision of 2026-10-01, §63.46). A LINE
	 * ends at LF, CR, NEL, LS or PS; A PARAGRAPH ENDS AT APPLE'S THREE — LF, CR or PS — which is the rule
	 * `NSString`'s own paragraph door publishes, and Apple's header says `NSStringEnumerationByParagraphs` is
	 * "Equivalent to paragraphRangeForRange:". ⚠ SO A PARAGRAPH IS NOT "A RUN OF LINES WITH NO BLANK LINE
	 * BETWEEN THEM" ANY MORE, which is what this comment used to say: a blank line is now an EMPTY PARAGRAPH,
	 * and NEL/LS end a line without ending a paragraph. */
	FNTextUnitLine = 4
};

@interface FNTextBreaking : NSObject

/* EVERY UNIT IN A RANGE, IN ORDER, ONE BLOCK CALL PER UNIT. `stop` ends the walk early, which is what the callers
 * above it need as well. AN EMPTY RANGE CALLS NOTHING, and a range past the string's end is CLAMPED rather than
 * refused, because a caller asking about the tail of a string is asking a legitimate question. */
+ (void)fnEnumerate:(FNTextUnit)unit
	   inString:(NSString *)string
	      range:(NSRange)range
	 usingBlock:(void (^)(NSRange unitRange, BOOL *stop))block;

/* THE UNIT CONTAINING A LOCATION, which is a different question from enumerating them (a caller who walks to a
 * position and then asks "which sentence was that" should not have to walk twice). */
+ (NSRange)fnUnitContaining:(FNTextUnit)unit inString:(NSString *)string atIndex:(NSUInteger)index;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNTEXTBREAKING_H */
