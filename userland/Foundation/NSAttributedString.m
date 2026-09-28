/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAttributedString / NSMutableAttributedString — W10 slice 1. See the header for the contract, the chosen
 * rules and the boundaries; this file is the run store and the splice operations over it.
 *
 * EVERY EDIT GOES THROUGH TWO HELPERS, AND THAT IS THE WHOLE DESIGN: -fnSplitAt: makes an index a RUN
 * BOUNDARY, so a range can then be covered by whole runs, and -fnReplaceRuns: swaps a run span for another.
 * Insertion, deletion, replacement, attribute setting and the attributed-string splice are all the same two
 * operations in different orders, which is why there is one coalescing rule to get right instead of six.
 */

#import <Foundation/NSAttributedString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSData.h>
/* NSCoder, NOT A FORWARD DECLARATION OF IT: with only `@class NSCoder;` (which the header carries) in scope,
 * clang reports an unknown selector as a WARNING (-Wobjc-method-access) and assumes the return type is `id` —
 * so a call with the wrong argument label compiles and fails at runtime. That is exactly how this file reached
 * §62.69 with `-decodeBytesForKey:returningLength:` (the tree and Apple both spell it `returnedLength:`): a
 * typo that a forward declaration turned into a warning instead of an error. */
#import <Foundation/NSCoder.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSNumber.h>

#include <stdint.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct fn_run {
	NSRange range;			/* UTF-16 units, in the CURRENT string */
	NSDictionary *attrs;		/* owned; nil means "no attributes" */
} fn_run;

@interface NSAttributedString (FNPrivate)
- (fn_run *)fnRunAt:(NSUInteger)index;
- (fn_run *)fnRunContaining:(NSUInteger)location;
- (void)fnReserve:(NSUInteger)needed;
- (void)fnInsertRun:(fn_run)run at:(NSUInteger)index;
- (void)fnRemoveRunsFrom:(NSUInteger)from count:(NSUInteger)count;
- (void)fnSortRuns;
- (void)fnCoalesce;
- (void)fnSplitAt:(NSUInteger)index;
- (NSRange)fnRunSpanCovering:(NSRange)range;
- (void)fnReplaceRuns:(NSRange)span with:(const fn_run *)runs count:(NSUInteger)count;
- (void)fnShiftRunsAfter:(NSUInteger)index by:(NSInteger)delta;
@end

/* WHICH VALUE, NOT MERELY THAT ONE: a refusal that cannot say what it refused sends the reader back to
 * guess. This walks the payload and answers the first value that is not a property list type - or nil when
 * every type is fine and the failure was structural. */
static NSString *fn_plist_offender(id object)
{
	if (object == nil) {
		return nil;
	}
	if ([object isKindOfClass:[NSString class]] || [object isKindOfClass:[NSNumber class]] ||
	    [object isKindOfClass:[NSDate class]] || [object isKindOfClass:[NSData class]]) {
		return nil;
	}
	if ([object isKindOfClass:[NSArray class]]) {
		NSUInteger i;

		for (i = 0; i < [(NSArray *)object count]; i++) {
			NSString *found = fn_plist_offender([(NSArray *)object objectAtIndex:i]);

			if (found != nil) {
				return found;
			}
		}
		return nil;
	}
	if ([object isKindOfClass:[NSDictionary class]]) {
		NSArray *keys = [(NSDictionary *)object allKeys];
		NSUInteger i;

		for (i = 0; i < [keys count]; i++) {
			NSString *found = fn_plist_offender([(NSDictionary *)object objectForKey:
								[keys objectAtIndex:i]]);

			if (found != nil) {
				return found;
			}
		}
		return nil;
	}
	return (NSString *)[object class];
}

/* ONE REFUSAL, BUILT THE SAME WAY EVERYWHERE: an NSError whose domain and description NAME the format and
 * the reason. Apple's own shape for a door that cannot do its work - and the reason a silent nil would be
 * worse is the reason the dictionary has one: a caller that loses a value without being told cannot tell a
 * failure from an empty answer. */
static void fn_format_refusal_log(NSString *selector, NSString *what)
{
	NSString *line = [NSString stringWithFormat:@"Foundation: %@ refused: %@\n", selector, what];
	const char *bytes = [line UTF8String];

	if (bytes != NULL) {
		(void)write(2, bytes, strlen(bytes));
	}
}

static NSError *fn_format_refusal(NSString *selector, NSString *what)
{
	NSDictionary *info = [NSDictionary dictionaryWithObject:
		[NSString stringWithFormat:@"%@ is not implemented: %@", selector, what]
							     forKey:NSLocalizedDescriptionKey];

	return [NSError errorWithDomain:@"NSAttributedStringDocumentErrorDomain" code:1 userInfo:info];
}

/* ---- RTF: THE ONE DOCUMENT FORMAT THIS SYSTEM CAN WRITE (§62.58) ------------------------------------
 *
 * RTF'S WIRE IS 7-BIT ASCII AND IT RESERVES THREE CHARACTERS: `\` introduces a control word and `{` `}`
 * open and close a group. Everything else is literal, so a writer is exactly two questions - what to
 * escape, and which runs carry which control words.
 *
 * THE ESCAPE IS THE FORMAT'S OWN AND NOT A CHOICE: `\u<SIGNED 16-BIT DECIMAL>?` is how RTF carries a
 * character outside the ANSI code page, and the trailing `?` is the one-character fallback every reader
 * accepts. THE VALUE IS SIGNED, which is the whole reason it is computed through a `short`: U+00E9 goes
 * out as `\u233?`, U+2014 as `\u8212?`, and a character in the top half of the BMP - U+FFFD, say - goes out
 * NEGATIVE as `\u-3?` rather than as 65533.
 *
 * THE FORMATTING VOCABULARY IS THIS LIBRARY'S OWN. `NSInlinePresentationIntent` is the one character-
 * formatting vocabulary Foundation declares here (emphasis, strong emphasis, strikethrough, code), and each
 * of its bits has a direct RTF control word, so the mapping is a rule and not a table. The AppKit attributes
 * a reader might expect - font, colour, paragraph style, underline - have NO TYPE IN THIS SYSTEM (there is
 * no NSFont, NSColor or NSParagraphStyle anywhere in the tree), so no caller can construct one and there is
 * nothing to map. That is the boundary, and it is stated in the header. */

/* Append `text` to `out` with RTF's own escaping. A line feed ends a paragraph (`\par`); a carriage return
 * is treated as the first half of a CRLF pair rather than as a second break; a tab is `\tab`. */
static void fn_rtf_append_escaped(NSMutableString *out, NSString *text)
{
	NSUInteger i, length = [text length];

	for (i = 0; i < length; i++) {
		unichar c = [text characterAtIndex:i];

		switch (c) {
		case '\\':
			[out appendString:@"\\\\"];
			break;
		case '{':
			[out appendString:@"\\{"];
			break;
		case '}':
			[out appendString:@"\\}"];
			break;
		case '\r':
			/* CR LF is ONE break: the LF that follows is consumed rather than emitted twice. */
			if (i + 1 < length && [text characterAtIndex:i + 1] == '\n') {
				i++;
			}
			[out appendString:@"\\par\n"];
			break;
		case '\n':
			[out appendString:@"\\par\n"];
			break;
		case '\t':
			[out appendString:@"\\tab "];
			break;
		default:
			if (c >= 0x20 && c < 0x7F) {
				[out appendString:[NSString stringWithCharacters:&c length:1]];
			} else {
				[out appendString:@"\\u"];
				[out appendString:[[NSNumber numberWithInt:(int)(short)c] stringValue]];
				[out appendString:@"?"];
			}
			break;
		}
	}
}

/* THE INLINE-INTENT BITS AS RTF CONTROL WORDS. The opens go out in a fixed order and the closes in the
 * reverse so the nesting is visibly balanced, and every control word carries its trailing delimiting space -
 * a word that ran straight into a letter would otherwise be read as a longer name (`\bhello` is one word). */
static void fn_rtf_append_intent(NSMutableString *out, NSDictionary *attrs, BOOL opening)
{
	id value = attrs != nil ? [attrs objectForKey:NSInlinePresentationIntentAttributeName] : nil;
	int bits = [value isKindOfClass:[NSNumber class]] ? [value intValue] : 0;

	if (!opening) {
		if ((bits & NSInlinePresentationIntentCode) != 0) {
			[out appendString:@"\\f0 "];
		}
		if ((bits & NSInlinePresentationIntentStrikethrough) != 0) {
			[out appendString:@"\\strike0 "];
		}
		if ((bits & NSInlinePresentationIntentEmphasized) != 0) {
			[out appendString:@"\\i0 "];
		}
		if ((bits & NSInlinePresentationIntentStronglyEmphasized) != 0) {
			[out appendString:@"\\b0 "];
		}
		return;
	}
	if ((bits & NSInlinePresentationIntentStronglyEmphasized) != 0) {
		[out appendString:@"\\b "];
	}
	if ((bits & NSInlinePresentationIntentEmphasized) != 0) {
		[out appendString:@"\\i "];
	}
	if ((bits & NSInlinePresentationIntentStrikethrough) != 0) {
		[out appendString:@"\\strike "];
	}
	if ((bits & NSInlinePresentationIntentCode) != 0) {
		[out appendString:@"\\f1 "];
	}
}

/* THE DOCUMENT SHELL. `\ansi\ansicpg1252` names the code page the fallback characters belong to, `\deff0`
 * names the default font, and the font table carries the two faces this writer can refer to - the default
 * `\f0` and the monospace `\f1` that `NSInlinePresentationIntentCode` selects. THE FONT NAMES ARE
 * PLACEHOLDERS AND SAY SO: RTF names a font so that a reader can SUBSTITUTE one, and this system's own font
 * resolution lives in the file system's font directories, not in a writer. The size (12 pt, `\fs24`) is our
 * default too - Apple publishes no size attribute and this library has none (§11.6.1 D2) - and the colour
 * table's single entry is the automatic colour, which is what an uncoloured run uses. */
static void fn_rtf_append_document_head(NSMutableString *out)
{
	[out appendString:@"{\\rtf1\\ansi\\ansicpg1252\\deff0\n"];
	[out appendString:@"{\\fonttbl{\\f0\\fnil\\fcharset0 Helvetica;}{\\f1\\fmodern\\fcharset0 Courier;}}\n"];
	[out appendString:@"{\\colortbl;\\red0\\green0\\blue0;}\n"];
	[out appendString:@"\\f0\\fs24 "];
}

/* §62.105: the one formatting-context key this library declares, valued as its own name — which is what a
 * caller's context dictionary and any log of it spell. */
NSAttributedStringFormattingContextKey const NSInflectionConceptsKey = @"NSInflectionConceptsKey";

@implementation NSAttributedString

/* ---- THE MODERN FAMILIES' CONSTANTS (W10 slice 4): names Apple publishes, values ours (§11.6.1 D2). */
NSAttributedStringKey const NSAlternateDescriptionAttributeName = @"NSAlternateDescriptionAttributeName";
NSAttributedStringKey const NSImageURLAttributeName = @"NSImageURLAttributeName";
NSAttributedStringKey const NSInflectionAgreementArgumentAttributeName = @"NSInflectionAgreementArgumentAttributeName";
NSAttributedStringKey const NSInflectionAgreementConceptAttributeName = @"NSInflectionAgreementConceptAttributeName";
NSAttributedStringKey const NSInflectionAlternativeAttributeName = @"NSInflectionAlternativeAttributeName";
NSAttributedStringKey const NSInflectionReferentConceptAttributeName = @"NSInflectionReferentConceptAttributeName";
NSAttributedStringKey const NSInflectionRuleAttributeName = @"NSInflectionRuleAttributeName";
NSAttributedStringKey const NSInlinePresentationIntentAttributeName = @"NSInlinePresentationIntentAttributeName";
NSAttributedStringKey const NSLanguageIdentifierAttributeName = @"NSLanguageIdentifierAttributeName";
NSAttributedStringKey const NSListItemDelimiterAttributeName = @"NSListItemDelimiterAttributeName";
NSAttributedStringKey const NSLocalizedNumberFormatAttributeName = @"NSLocalizedNumberFormatAttributeName";
NSAttributedStringKey const NSMarkdownSourcePositionAttributeName = @"NSMarkdownSourcePositionAttributeName";
NSAttributedStringKey const NSMorphologyAttributeName = @"NSMorphologyAttributeName";
NSAttributedStringKey const NSPresentationIntentAttributeName = @"NSPresentationIntentAttributeName";
NSAttributedStringKey const NSReplacementIndexAttributeName = @"NSReplacementIndexAttributeName";


- (instancetype)initWithString:(NSString *)str
{
	return [self initWithString:str attributes:nil];
}

- (instancetype)initWithString:(NSString *)str attributes:(NSDictionary *)attrs
{
	self = [super init];
	if (self != nil) {
		/* NOT -copy: THIS LIBRARY'S -copy IS NOT RELIABLE - "-copy is not implemented" is what
		 * the guest printed before it aborted, and -copy on an immutable dictionary returns self without
		 * the +1 ownership needs (both measured here). A store of text has to own its string, so it takes
		 * a real copy through the constructor. */
		_string = [[NSString alloc] initWithString:(str != nil ? str : @"")];
		if ([_string length] > 0) {
			/* ONE RUN COVERING THE WHOLE STRING, ALWAYS - with a nil dictionary when there are no
			 * attributes. A RANGE WITH NO RUN IS THE "no attributes here" STATE, and an edit that lands in
			 * one has nothing to merge into, which is how an -addAttributes: over a fresh string came back
			 * with no attributes at all. */
			fn_run run;

			run.range = NSMakeRange(0, [_string length]);
			/* A REAL COPY, AND NOT -copy: the store must OWN its dictionary, and this library's -copy
			 * returns SELF for an immutable dictionary (measured: copy-same-object=1) without the +1 that
			 * ownership needs - so every store was releasing a reference it did not hold, freeing the
			 * caller's dictionary under it. That is the corruption (a run reading {B} where {A} was built,
			 * i.e. freed memory reused) and the crash (the final release of a shared constant). */
			run.attrs = attrs != nil && [attrs count] > 0
				? [[NSDictionary alloc] initWithDictionary:attrs] : nil;
			[self fnInsertRun:run at:0];
		}
	}
	return self;
}

- (instancetype)initWithAttributedString:(NSAttributedString *)attrStr
{
	self = [super init];
	if (self != nil) {
		NSString *source = [attrStr string];
		NSUInteger i;

		_string = [[NSString alloc] initWithString:(source != nil ? source : @"")];
		for (i = 0; i < [attrStr length]; ) {
			fn_run *run = [attrStr fnRunContaining:i];
			fn_run copy;

			if (run == NULL) {
				break;
			}
			copy.range = run->range;
			copy.attrs = run->attrs != nil
				? [[NSDictionary alloc] initWithDictionary:run->attrs] : nil;
			[self fnInsertRun:copy at:_runCount];
			i = run->range.location + run->range.length;
		}
	}
	return self;
}

- (void)dealloc
{
	NSUInteger i;


	for (i = 0; i < _runCount; i++) {
		[((fn_run *)_runs)[i].attrs release];
	}
	free(_runs);
	[_string release];
	[super dealloc];
}

- (NSString *)string
{
	return _string;
}

- (NSUInteger)length
{
	return [_string length];
}

/* ---- THE RUN STORE, AS PRIVATE METHODS --------------------------------------------------------------- */

- (fn_run *)fnRunAt:(NSUInteger)index
{
	return ((fn_run *)_runs) + index;
}

- (fn_run *)fnRunContaining:(NSUInteger)location
{
	NSUInteger i;

	for (i = 0; i < _runCount; i++) {
		fn_run *run = [self fnRunAt:i];

		if (location >= run->range.location &&
		    location < run->range.location + run->range.length) {
			return run;
		}
	}
	return NULL;
}

- (void)fnReserve:(NSUInteger)needed
{
	if (needed > _runCapacity) {
		NSUInteger grown = _runCapacity > 0 ? _runCapacity : 4;
		fn_run *moved;

		while (grown < needed) {
			grown *= 2;
		}
		moved = realloc(_runs, grown * sizeof(fn_run));
		if (moved == NULL) {
			[NSException raise:NSMallocException format:@"out of room for %lu runs",
				(unsigned long)needed];
		}
		_runs = moved;
		_runCapacity = grown;
	}
}

- (void)fnInsertRun:(fn_run)run at:(NSUInteger)index
{
	[self fnReserve:_runCount + 1];
	if (index < _runCount) {
		memmove([self fnRunAt:index + 1], [self fnRunAt:index],
			(_runCount - index) * sizeof(fn_run));
	}
	*[self fnRunAt:index] = run;
	_runCount++;
}

- (void)fnRemoveRunsFrom:(NSUInteger)from count:(NSUInteger)count
{
	NSUInteger i;

	if (count == 0 || from >= _runCount) {
		return;
	}
	if (from + count > _runCount) {
		count = _runCount - from;
	}
	for (i = from; i < from + count; i++) {
		[((fn_run *)_runs)[i].attrs release];
	}
	if (from + count < _runCount) {
		memmove([self fnRunAt:from], [self fnRunAt:from + count],
			(_runCount - from - count) * sizeof(fn_run));
	}
	_runCount -= count;
}

/* "the longest range over which the attributes are the same" - so adjacent runs whose DICTIONARIES ARE EQUAL
 * are ONE run, whatever order they were added in. */
/* THE STORE'S INVARIANT IS "RUNS IN RANGE ORDER", AND IT IS ENFORCED HERE because this is the one method that
 * DEPENDS on it: coalescing merges ADJACENT runs, so an array that is out of order (which the trace caught
 * as [(2,2), (0,2)] after a second append) cannot be merged at all and the equal runs survive as two. A
 * stable insertion sort is right for a list this size and keeps equal keys in their existing order. */
- (void)fnSortRuns
{
	NSUInteger i;

	for (i = 1; i < _runCount; i++) {
		fn_run key = *[self fnRunAt:i];
		NSUInteger j = i;

		while (j > 0 && [self fnRunAt:j - 1]->range.location > key.range.location) {
			*[self fnRunAt:j] = *[self fnRunAt:j - 1];
			j--;
		}
		*[self fnRunAt:j] = key;
	}
}

- (void)fnCoalesce
{
	NSUInteger i = 0;

	[self fnSortRuns];

	while (i + 1 < _runCount) {
		fn_run *a = [self fnRunAt:i];
		fn_run *b = [self fnRunAt:i + 1];
		BOOL adjacent = a->range.location + a->range.length == b->range.location;

		BOOL same = adjacent && ((a->attrs == b->attrs) ||
			    (a->attrs != nil && b->attrs != nil && [a->attrs isEqualToDictionary:b->attrs]));

		if (same) {
			a->range.length += b->range.length;
			[self fnRemoveRunsFrom:i + 1 count:1];
		} else {
			i++;
		}
	}
}

/* AN INDEX BECOMES A RUN BOUNDARY: an edit that starts or ends inside a run splits it, which is what lets
 * every operation below work on WHOLE runs. */
- (void)fnSplitAt:(NSUInteger)index
{
	fn_run *run = [self fnRunContaining:index];
	NSUInteger at;

	if (run == NULL || index == run->range.location) {
		return;
	}
	at = (NSUInteger)(run - (fn_run *)_runs);
	{
		fn_run tail;

		tail.range = NSMakeRange(index, run->range.location + run->range.length - index);
		/* A COPY AND NOT A RETAIN: two runs that share one dictionary make a merge on one run observable
		 * through the other, which is the corruption the DLIB store dump caught as a run reading
		 * {B} where {A} was built. */
		tail.attrs = run->attrs != nil
			? [[NSDictionary alloc] initWithDictionary:run->attrs] : nil;
		run = [self fnRunAt:at];			/* realloc moved nothing, but be explicit */
		run->range.length = index - run->range.location;
		[self fnInsertRun:tail at:at + 1];
	}
}

/* THE RUNS A RANGE COVERS, as a half-open span in the run array. Callers split first. */
- (NSRange)fnRunSpanCovering:(NSRange)range
{
	NSUInteger i, first = NSNotFound, last = 0;

	for (i = 0; i < _runCount; i++) {
		fn_run *run = [self fnRunAt:i];

		if (run->range.location + run->range.length <= range.location ||
		    run->range.location >= range.location + range.length) {
			continue;
		}
		if (first == NSNotFound) {
			first = i;
		}
		last = i;
	}
	if (first == NSNotFound) {
		return NSMakeRange(0, 0);
	}
	return NSMakeRange(first, last - first + 1);
}

- (void)fnReplaceRuns:(NSRange)span with:(const fn_run *)runs count:(NSUInteger)count
{
	NSUInteger i;
	fn_run *tail = NULL;

	if (span.length > 0) {
		[self fnRemoveRunsFrom:span.location count:span.length];
	}
	if (count == 0) {
		return;
	}
	for (i = 0; i < count; i++) {
		[self fnInsertRun:runs[i] at:span.location + i];
	}
}

/* ---- THE ACCESSORS ----------------------------------------------------------------------------------- */

- (NSDictionary *)attributesAtIndex:(NSUInteger)location effectiveRange:(NSRangePointer)range
{
	fn_run *run;

	if (location >= [_string length]) {
		[NSException raise:NSRangeException format:@"index %lu is out of range for a string of length %lu",
			(unsigned long)location, (unsigned long)[_string length]];
	}
	run = [self fnRunContaining:location];
	if (run == NULL) {
		if (range != NULL) {
			*range = NSMakeRange(location, 0);
		}
		return [NSDictionary dictionary];
	}
	if (range != NULL) {
		*range = run->range;
	}
	return run->attrs != nil ? run->attrs : [NSDictionary dictionary];
}

/* ONE ATTRIBUTE'S OWN EFFECTIVE RANGE: the longest stretch over which THAT attribute holds this value, which
 * can be longer than the run the index sits in. */
- (nullable id)attribute:(NSAttributedStringKey)attrName
		  atIndex:(NSUInteger)location
	   effectiveRange:(NSRangePointer)range
{
	id value;
	NSRange runRange;
	NSUInteger i, first, last;

	value = [[self attributesAtIndex:location effectiveRange:&runRange] objectForKey:attrName];
	first = last = NSNotFound;
	for (i = 0; i < _runCount; i++) {
		fn_run *run = [self fnRunAt:i];
		id here = run->attrs != nil ? [run->attrs objectForKey:attrName] : nil;

		if ((here == value) || (here != nil && value != nil && [here isEqual:value])) {
			if (first == NSNotFound) {
				first = run->range.location;
			}
			last = run->range.location + run->range.length;
		} else if (first != NSNotFound) {
			break;
		}
	}
	if (range != NULL) {
		*range = first != NSNotFound ? NSMakeRange(first, last - first) : runRange;
	}
	return value;
}

- (NSDictionary *)attributesAtIndex:(NSUInteger)location
		longestEffectiveRange:(NSRangePointer)range
			      inRange:(NSRange)rangeLimit
{
	NSRange effective;
	NSDictionary *attrs = [self attributesAtIndex:location effectiveRange:&effective];

	if (range != NULL) {
		NSUInteger start = effective.location > rangeLimit.location ? effective.location
									    : rangeLimit.location;
		NSUInteger end = effective.location + effective.length;
		NSUInteger limitEnd = rangeLimit.location + rangeLimit.length;

		if (end > limitEnd) {
			end = limitEnd;
		}
		*range = end > start ? NSMakeRange(start, end - start) : NSMakeRange(start, 0);
	}
	return attrs;
}

- (nullable id)attribute:(NSAttributedStringKey)attrName
		  atIndex:(NSUInteger)location
    longestEffectiveRange:(NSRangePointer)range
		  inRange:(NSRange)rangeLimit
{
	NSRange effective;
	id value = [self attribute:attrName atIndex:location effectiveRange:&effective];

	if (range != NULL) {
		NSUInteger start = effective.location > rangeLimit.location ? effective.location
									    : rangeLimit.location;
		NSUInteger end = effective.location + effective.length;
		NSUInteger limitEnd = rangeLimit.location + rangeLimit.length;

		if (end > limitEnd) {
			end = limitEnd;
		}
		*range = end > start ? NSMakeRange(start, end - start) : NSMakeRange(start, 0);
	}
	return value;
}

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range
{
	NSAttributedString *answer;

	if (range.location + range.length > [_string length]) {
		[NSException raise:NSRangeException format:@"range (%lu,%lu) exceeds a string of length %lu",
			(unsigned long)range.location, (unsigned long)range.length, (unsigned long)[_string length]];
	}
	/* A MUTABLE ANSWER: the runs are copied onto it with -addAttributes:, and sending that to an
	 * NSAttributedString instance (which is what a cast to the mutable class would have been) is an
	 * UNRECOGNIZED SELECTOR - the abort this probe localized to this method. */
	answer = [[NSMutableAttributedString alloc] initWithString:
			[_string substringWithRange:range] attributes:nil];
	{
		NSUInteger i;

		for (i = 0; i < _runCount; i++) {
			fn_run *run = [self fnRunAt:i];
			NSUInteger runEnd = run->range.location + run->range.length;
			NSUInteger start = run->range.location > range.location ? run->range.location
									       : range.location;
			NSUInteger end = runEnd < range.location + range.length ? runEnd
									       : range.location + range.length;

			if (end > start) {
				[(NSMutableAttributedString *)answer addAttributes:run->attrs
									     range:NSMakeRange(start - range.location,
											       end - start)];
			}
		}
	}
	return [answer autorelease];
}

- (BOOL)isEqualToAttributedString:(NSAttributedString *)other
{
	NSUInteger i, j;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSAttributedString class]]) {
		return NO;
	}
	if (![[other string] isEqualToString:_string]) {
		return NO;
	}
	/* RUN-FOR-RUN, which is the same question as "the same attributes over the same ranges" ONLY because both
	 * strings coalesce: without that, two spellings of one string would compare unequal. Both sides walk their
	 * own runs, so the comparison does not depend on the two stores agreeing about run boundaries - only on
	 * what an index would report. */
	i = 0;
	while (i < [_string length]) {
		NSRange mine = NSMakeRange(0, 0), theirs = NSMakeRange(0, 0);
		NSDictionary *da = [self attributesAtIndex:i effectiveRange:&mine];
		NSDictionary *db = [other attributesAtIndex:i effectiveRange:&theirs];

		if (mine.location != theirs.location || mine.length != theirs.length) {
			return NO;
		}
		if (da != db && ![da isEqualToDictionary:db]) {
			return NO;
		}
		i = mine.location + mine.length;
	}
	return YES;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSAttributedString class]]) {
		return NO;
	}
	return [self isEqualToAttributedString:other];
}

- (NSUInteger)hash
{
	return [_string hash];
}

- (void)enumerateAttributesInRange:(NSRange)enumerationRange
			   options:(NSAttributedStringEnumerationOptions)opts
			usingBlock:(void (^)(NSDictionary *attrs, NSRange range, BOOL *stop))block
{
	NSRange span;
	NSUInteger step, i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	[self fnSplitAt:enumerationRange.location];
	[self fnSplitAt:enumerationRange.location + enumerationRange.length];
	span = [self fnRunSpanCovering:enumerationRange];
	step = span.length > 0 ? span.length : 0;
	for (i = 0; i < step && !stop; i++) {
		NSUInteger index = (opts & NSAttributedStringEnumerationReverse) ? span.length - 1 - i : i;
		fn_run *run = [self fnRunAt:span.location + index];

		block(run->attrs != nil ? run->attrs : [NSDictionary dictionary], run->range, &stop);
	}
}

- (void)enumerateAttribute:(NSAttributedStringKey)attrName
		   inRange:(NSRange)enumerationRange
		   options:(NSAttributedStringEnumerationOptions)opts
		usingBlock:(void (^)(_Nullable id value, NSRange range, BOOL *stop))block
{
	/* APPLE'S OVERVIEW SAYS THE BLOCK IS INVOKED FOR THE RANGES THE ATTRIBUTE COVERS; THE STRETCHES WHERE IT
	 * IS ABSENT ARE REPORTED WITH nil, WHICH IS WHAT A CALLER MOST NEEDS AND IS RECORDED HERE AS THE CHOICE
	 * (§11.6.1 D2) rather than left to be inferred from the probe. */
	NSRange at = enumerationRange;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	while (at.length > 0 && !stop) {
		NSRange effective = NSMakeRange(0, 0);
		id value = [self attribute:attrName atIndex:at.location effectiveRange:&effective];

		if (effective.length == 0) {
			effective = NSMakeRange(at.location, at.length);
		}
		if (effective.location < at.location) {
			effective.length -= (at.location - effective.location);
			effective.location = at.location;
		}
		if (effective.location + effective.length > at.location + at.length) {
			effective.length = at.location + at.length - effective.location;
		}
		block(value, effective, &stop);
		at.location = effective.location + effective.length;
		at.length = enumerationRange.location + enumerationRange.length - at.location;
	}
}

/* ---- COPYING ---------------------------------------------------------------------------------------- */

/* THE ZONE API IS REMOVED IN THIS TREE BY DECISION (NSObject.h states it: NSCopying's members here are
 * -copy and -mutableCopy), so these are the two doors a caller uses rather than the zone-taking forms. */
- (id)copy
{
	return [[NSAttributedString alloc] initWithAttributedString:self];
}

- (id)mutableCopy
{
	return [[NSMutableAttributedString alloc] initWithAttributedString:self];
}

/* ---- CODING (W10 slice 3) ---------------------------------------------------------------------------
 *
 * THE PAYLOAD IS A PROPERTY LIST, AND THAT IS THE DECISION §61 RECORDED RATHER THAN DISCOVERED: this tree's
 * coder has no -decodeObjectOfClass: (NSCoding.h says so itself) and NSArray/NSDictionary carry no coding at
 * all, so a run store cannot be handed to the archiver piece by piece. A plist CAN carry exactly what a run
 * store holds - a string, integer ranges, and attribute dictionaries of plist values - and the plist door
 * REFUSES anything else, which is the named refusal this slice owes. The payload rides in an NSData, which
 * this library already codes through -encodeBytes:length:forKey:. */
- (void)encodeWithCoder:(NSCoder *)coder
{
	NSMutableArray *runs = [NSMutableArray arrayWithCapacity:_runCount];
	NSMutableDictionary *plist = [NSMutableDictionary dictionary];
	NSData *payload;
	NSUInteger i;

	for (i = 0; i < _runCount; i++) {
		fn_run *run = [self fnRunAt:i];
		NSMutableDictionary *entry = [NSMutableDictionary dictionary];

		[entry setObject:[NSNumber numberWithUnsignedInteger:run->range.location] forKey:@"location"];
		[entry setObject:[NSNumber numberWithUnsignedInteger:run->range.length] forKey:@"length"];
		if (run->attrs != nil) {
			[entry setObject:run->attrs forKey:@"attributes"];
		}
		[runs addObject:entry];
	}
	[plist setObject:_string forKey:@"string"];
	[plist setObject:runs forKey:@"runs"];
	/* XML, WHICH IS THE ONLY FORMAT THIS LIBRARY'S PROPERTY-LIST CORE SPEAKS: OpenStep and BINARY are NAMED and
	 * REJECTED there by design (NSPropertyListSerialization.h says why — "a config file read with the wrong
	 * reader is worse than one that refuses to open"). Asking for the binary format was a REAL DEFECT, not a
	 * style point: it made this method raise for EVERY attributed string, so the class Apple documents as
	 * NSSecureCoding could not be archived at all — and the raise blamed the VALUES, which were innocent. Found
	 * by §62.69's probe, which archives an NSExtensionItem holding an attributed string. */
	payload = [NSPropertyListSerialization dataWithPropertyList:plist
							    format:NSPropertyListXMLFormat_v1_0
							   options:0
							      error:NULL];
	if (payload == nil) {
		/* THE NAMED REFUSAL: with the format settled, a nil payload means a run store holds something a
		 * property list cannot carry, and the honest answer is to say so rather than write a half-archive. */
		{
			NSString *offender = fn_plist_offender(plist);

			[NSException raise:NSInvalidArgumentException
				    format:@"NSAttributedString cannot be archived: %@ is not a property list value "
					   @"(the runs are carried as a property list)",
					   offender != nil ? offender : @"(every type is known - the failure was "
									  @"structural)"];
		}
	}
	/* THE PAYLOAD'S BYTES, NOT A NESTED OBJECT REFERENCE: this tree's archiver records a root object's own
	 * primitive calls and does NOT carry a nested -encodeObject: (measured: an NSData root archives to 424
	 * bytes while this class's nested reference archived to zero), so the carrier goes out through
	 * -encodeBytes:length:forKey: - the same primitive NSData's own -encodeWithCoder: uses. */
	[coder encodeBytes:[payload bytes] length:[payload length] forKey:@"NSAttributedString"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSUInteger byteCount = 0;
	const void *bytes = [coder decodeBytesForKey:@"NSAttributedString" returnedLength:&byteCount];
	id payload = bytes != NULL && byteCount > 0
		? [NSData dataWithBytes:bytes length:byteCount] : nil;
	id plist;
	NSArray *runs;
	NSUInteger i;

	if (payload == nil) {
		[self release];
		return nil;
	}
	plist = [NSPropertyListSerialization propertyListWithData:payload options:0 format:NULL error:NULL];
	if (![plist isKindOfClass:[NSDictionary class]]) {
		[self release];
		return nil;
	}
	self = [self initWithString:[plist objectForKey:@"string"] attributes:nil];
	if (self == nil) {
		return nil;
	}
	runs = [plist objectForKey:@"runs"];
	for (i = 0; i < [runs count]; i++) {
		NSDictionary *entry = [runs objectAtIndex:i];
		NSDictionary *attrs = [entry objectForKey:@"attributes"];
		fn_run run;

		run.range = NSMakeRange([[entry objectForKey:@"location"] unsignedIntegerValue],
					[[entry objectForKey:@"length"] unsignedIntegerValue]);
		run.attrs = attrs != nil ? [[NSDictionary alloc] initWithDictionary:attrs] : nil;
		[self fnInsertRun:run at:_runCount];
	}
	/* THE SEEDED RUN IS DROPPED, AND THIS ONE LINE IS THE WHOLE DIFFERENCE BETWEEN A ROUND TRIP THAT KEEPS THE
	 * ATTRIBUTES AND ONE THAT APPEARS TO LOSE THEM. -initWithString:attributes: seeds a run covering the WHOLE
	 * string (with no attributes), and the payload's runs were then appended AFTER it: two runs covering the same
	 * range, the seeded one first, so -attributesAtIndex: answered from the empty one for every index. The
	 * payload was right on BOTH sides - measured, not inferred: the encoder wrote `{attributes = {A = 1}}` and
	 * the decoder read it back unchanged - which is what pointed at the reader's own store rather than at the
	 * wire. Found by §62.69's probe. */
	[self fnRemoveRunsFrom:0 count:1];
	[self fnCoalesce];
	return self;
}

+ (BOOL)supportsSecureCoding
{
	/* APPLE'S ANSWER, WITH THE GAP NAMED WHERE IT LIVES: NSCoding.h records that this tree's archiver does
	 * not yet ENFORCE secure coding (it has no -decodeObjectOfClass:), so the conformance is honest about
	 * what it means here - the class supports secure coding; the coder does not yet check. */
	return YES;
}



/* ---- THE FILE-FORMAT DOORS (W10 slice 4) -----------------------------------------------------------
 *
 * RTF WRITES AND THE REST REFUSE, EACH BY NAME: -RTFFromRange:documentAttributes: is a real RTF writer
 * (§62.58) and the doors beside it answer nil with a reason. What each still-refusing door's reason IS,
 * said here where the code says it too: RTFD needs the ATTACHMENT that no class in this library defines;
 * the doc format is Microsoft Word's BINARY container, whose specification this system does not carry; HTML
 * import needs a parser and a fetch (and Apple itself discourages the synchronous form); and the polymorphic
 * doors pick their format from a document-type attribute whose value vocabulary belongs to the AppKit half
 * that is not here. A refusal that cannot report through an NSError says so in the one channel it has - the
 * log - and answers nil. */
- (nullable NSData *)dataFromRange:(NSRange)range
	       documentAttributes:(nullable NSDictionary *)dict
			    error:(NSError **)error
{
	(void)range;
	(void)dict;
	if (error != NULL) {
		*error = fn_format_refusal(@"-dataFromRange:documentAttributes:error:",
					   @"no document format is implemented in this system");
	}
	return nil;
}

/* THE RTF WRITER: a real document, not a stub. The range is CLAMPED to the string rather than raising
 * (Apple publishes no behaviour for an out-of-range write, §11.6.1 D2), the runs covering the range are
 * walked in order, and each run contributes its text escaped and bracketed by the control words its
 * NSInlinePresentationIntent bits select. `documentAttributes:` is an INPUT and is unused: Apple's modern
 * declaration has no out-parameter (the `**` of the older API is gone), and the only attributes an
 * RTF-specific writer could consume - a default font, a paper size - name AppKit objects this system does
 * not have. */
- (nullable NSData *)RTFFromRange:(NSRange)range documentAttributes:(nullable NSDictionary *)dict
{
	NSMutableString *out = [NSMutableString string];
	NSUInteger length = [_string length];
	NSUInteger cursor, stop;

	(void)dict;

	if (range.location > length) {
		range.location = length;
	}
	if (range.length > length - range.location) {
		range.length = length - range.location;
	}
	cursor = range.location;
	stop = range.location + range.length;

	fn_rtf_append_document_head(out);
	while (cursor < stop) {
		fn_run *run = [self fnRunContaining:cursor];
		NSUInteger runEnd, limit;
		NSRange slice;

		if (run == NULL) {
			break;
		}
		runEnd = run->range.location + run->range.length;
		limit = runEnd < stop ? runEnd : stop;
		if (limit <= cursor) {
			break;
		}
		slice = NSMakeRange(cursor, limit - cursor);
		fn_rtf_append_intent(out, run->attrs, YES);
		fn_rtf_append_escaped(out, [_string substringWithRange:slice]);
		fn_rtf_append_intent(out, run->attrs, NO);
		cursor = limit;
	}
	[out appendString:@"\n}\n"];

	return [out dataUsingEncoding:NSUTF8StringEncoding];
}

- (nullable NSData *)RTFDFromRange:(NSRange)range documentAttributes:(nullable NSDictionary *)dict
{
	(void)range;
	(void)dict;
	fn_format_refusal_log(@"-RTFDFromRange:documentAttributes:", @"RTFD needs an attachment this library "
			      @"has no class for");
	return nil;
}

- (nullable NSFileWrapper *)RTFDFileWrapperFromRange:(NSRange)range
				documentAttributes:(nullable NSDictionary *)dict
{
	(void)range;
	(void)dict;
	fn_format_refusal_log(@"-RTFDFileWrapperFromRange:documentAttributes:", @"RTFD needs an attachment this "
			      @"library has no class for");
	return nil;
}

- (nullable NSDictionary *)fileWrapperFromRange:(NSRange)range
			   documentAttributes:(nullable NSDictionary *)dict
					error:(NSError **)error
{
	(void)range;
	(void)dict;
	if (error != NULL) {
		*error = fn_format_refusal(@"-fileWrapperFromRange:documentAttributes:error:",
					   @"the format it would wrap is named by a document-type attribute this "
					   @"library does not declare");
	}
	return nil;
}

- (nullable NSData *)docFormatFromRange:(NSRange)range documentAttributes:(nullable NSDictionary *)dict
{
	(void)range;
	(void)dict;
	fn_format_refusal_log(@"-docFormatFromRange:documentAttributes:", @"the doc format is Microsoft Word's "
			      @"binary container, whose specification this system does not carry");
	return nil;
}

+ (void)loadFromHTMLWithRequest:(NSURLRequest *)request
			options:(nullable NSDictionary *)options
	      completionHandler:(void (^)(NSAttributedString *, NSDictionary *, NSError *))completionHandler
{
	/* THE ONE DOOR WITH AN ERROR CHANNEL AND A BLOCK: the handler is called ONCE with the refusal, which is
	 * the shape Apple documents (the completion handler always runs) and the honest half of a load that
	 * cannot happen. */
	(void)request;
	(void)options;
	if (completionHandler != NULL) {
		completionHandler(nil, nil,
			fn_format_refusal(@"+loadFromHTMLWithRequest:options:completionHandler:",
					  @"HTML import needs a parser and a network fetch, neither of which exists here"));
	}
}

@end


@implementation NSMutableAttributedString

- (instancetype)initWithString:(NSString *)str
{
	self = [super initWithString:str attributes:nil];
	return self;
}

- (instancetype)initWithString:(NSString *)str attributes:(NSDictionary *)attrs
{
	self = [super initWithString:str attributes:attrs];
	return self;
}

- (instancetype)initWithAttributedString:(NSAttributedString *)attrStr
{
	self = [super initWithAttributedString:attrStr];
	return self;
}

/* THE ONE PLACE THE SHIFTING HAPPENS: every run after a changed range moves by the delta, and the changed
 * range is then covered by the runs the caller supplies. */
- (void)fnShiftRunsAfter:(NSUInteger)index by:(NSInteger)delta
{
	NSUInteger i;

	for (i = 0; i < _runCount; i++) {
		fn_run *run = [self fnRunAt:i];

		if (run->range.location >= index) {
			run->range.location += delta;
		}
	}
}

- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)str
{
	NSString *replacement = str != nil ? str : @"";
	NSInteger delta = (NSInteger)[replacement length] - (NSInteger)range.length;
	NSRange span;
	fn_run *template = [self fnRunContaining:range.location];
	/* THE ATTRIBUTES IN FORCE ARE READ *BEFORE* ANY STRUCTURAL CHANGE, AND THAT IS THE FIX: the splits
	 * below INSERT runs, an insert can REALLOC the runs array, and `template` is a pointer into it - so
	 * reading template->attrs afterwards read freed memory whose bytes were whatever was allocated next
	 * (usually a plain object, which is why the failure surfaced as -[NSObject copy] and why it came and
	 * went). One dictionary read, taken while the pointer is still valid. */
	NSDictionary *templateAttrs = template != NULL && template->attrs != nil
		? [[NSDictionary alloc] initWithDictionary:template->attrs] : nil;
	fn_run run;

	[self fnSplitAt:range.location];
	[self fnSplitAt:range.location + range.length];
	span = [self fnRunSpanCovering:range];
	/* THE REPLACEMENT'S ATTRIBUTES ARE THE ONES IN FORCE AT THE START OF THE RANGE (a chosen rule, see the
	 * header); at the very end of the string the last run's attributes are the ones in force. */
	if (template == NULL && _runCount > 0) {
		template = [self fnRunAt:_runCount - 1];
	}
	run.range = NSMakeRange(range.location, [replacement length]);
	run.attrs = templateAttrs;
	[self fnShiftRunsAfter:range.location + range.length by:delta];
	/* THE COPY IS TRANSFERRED, NOT SHARED: the run array owns it from here, and releasing it as well is the
	 * double free that made this probe abort with a corrupted heap. */
	[self fnReplaceRuns:span with:&run count:([replacement length] > 0) ? 1 : 0];
	{
		/* CONCATENATED, NOT FORMATTED: the formatter's path reaches -copy on a bare NSObject, which the root
		 * class reports as unimplemented - and that call used to ABORT THE GUEST (it raises now). Three
		 * appends need no formatter, and this is the only place in the store that built a string from
		 * others. THE FORMATTER BUG IS STILL THERE AND IS NAMED where it can be found: something inside
		 * -initWithFormat: copies a plain object, and it is now a CATCHABLE exception rather than a dead
		 * process, which is what makes it cheap to hunt. */
		NSString *head = [_string substringToIndex:range.location];
		NSString *tail = [_string substringFromIndex:range.location + range.length];
		/* THE CLASS FORM OF THE FORMATTER, and the difference is measured: -initWithFormat: reached -copy on
		 * a bare object and died, and the mutable-string route reached a SECOND unimplemented mutator
		 * (-[NSOwnedString appendString:], recorded in NSString.m's own comment). +stringWithFormat: is
		 * used successfully throughout this tree, including every probe's diagnostics. */
		NSString *joined = [NSString stringWithFormat:@"%@%@%@", head, replacement, tail];

		[_string release];
		_string = [joined retain];
	}
	/* IT COALESCES HERE, AND THE ATTRIBUTED SPLICE COALESCES AGAIN AT ITS END - both, not one. This method
	 * is a complete operation when it is called directly (a replacement whose attributes equal its
	 * neighbours' must read back as ONE range), and -replaceCharactersInRange:withAttributedString: calls it
	 * and then RE-SPLITS at its own boundaries before replacing the runs its range covers, so a merge here
	 * is undone by that split rather than relied on. */
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
}

- (void)replaceCharactersInRange:(NSRange)range withAttributedString:(NSAttributedString *)attrString
{
	NSString *replacement = attrString != nil ? [attrString string] : @"";
	fn_run *copied;
	NSUInteger count = 0, i, at;
	NSInteger delta = (NSInteger)[replacement length] - (NSInteger)range.length;

	[self replaceCharactersInRange:range withString:replacement];
	if (attrString == nil || [replacement length] == 0) {
		return;
	}
	copied = malloc(([attrString length] > 0 ? [attrString length] : 1) * sizeof(fn_run));
	for (i = 0; i < [attrString length]; ) {
		fn_run *run = [attrString fnRunContaining:i];

		if (run == NULL) {
			break;
		}
		copied[count].range = NSMakeRange(run->range.location + range.location, run->range.length);
		/* A REAL COPY, NOT A RETAIN - THE LAST PLACE THE STORE SHARED A DICTIONARY. A retained source
		 * dictionary is freed by whoever else owns it (the splice's own span removal releases the runs it
		 * replaces), and the run array is then left holding a dangling pointer: that is how a run's attrs
		 * came to read as a bare NSObject with nothing under it after a dictionary copy walked it. */
		copied[count].attrs = run->attrs != nil
			? [[NSDictionary alloc] initWithDictionary:run->attrs] : nil;
		count++;
		i = run->range.location + run->range.length;
	}
	{
		NSRange span;

		[self fnSplitAt:range.location];
		[self fnSplitAt:range.location + [replacement length]];
		span = [self fnRunSpanCovering:NSMakeRange(range.location, [replacement length])];
		[self fnReplaceRuns:span with:copied count:count];
	}
	/* THE OWNERSHIP WAS TRANSFERRED BY -fnReplaceRuns: - RELEASING HERE IS A DOUBLE FREE, AND IT WAS THE
	 * WHOLE BUG: every run this splice inserted held an attributes dictionary with one release too many, so
	 * the dictionary died while the run still pointed at it. That is the use-after-free, the {A}-reads-as-{B}
	 * corruption and the intermittency, all of them, and it took `__builtin_return_address(2)` inside
	 * -[NSDictionary dealloc] resolved with addr2line to name this line. */
	free(copied);
	/* AND THIS PATH COALESCES TOO: it splices a fresh run in AFTER the string path already merged, so
	 * without this guard two equal runs that had just become one become two again - which is exactly what
	 * the probe's first coalescing check measured as a (0,2) where a (0,4) was promised. */
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
	(void)delta;
	(void)at;
}

- (void)deleteCharactersInRange:(NSRange)range
{
	[self replaceCharactersInRange:range withString:@""];
}

- (void)insertAttributedString:(NSAttributedString *)attrString atIndex:(NSUInteger)loc
{
	[self replaceCharactersInRange:NSMakeRange(loc, 0) withAttributedString:attrString];
}

- (void)appendAttributedString:(NSAttributedString *)attrString
{
	[self replaceCharactersInRange:NSMakeRange([self length], 0) withAttributedString:attrString];
}

- (void)setAttributedString:(NSAttributedString *)attrString
{
	[self replaceCharactersInRange:NSMakeRange(0, [self length]) withAttributedString:attrString];
}

- (void)addAttribute:(NSAttributedStringKey)name value:(id)value range:(NSRange)range
{
	if (value == nil) {
		[self removeAttribute:name range:range];
		return;
	}
	[self addAttributes:[NSDictionary dictionaryWithObject:value forKey:name] range:range];
}

- (void)addAttributes:(NSDictionary *)attrs range:(NSRange)range
{
	NSUInteger i, n;

	if (attrs == nil || [attrs count] == 0 || range.length == 0) {
		return;
	}
	[self fnSplitAt:range.location];
	[self fnSplitAt:range.location + range.length];
	n = [self fnRunSpanCovering:range].length;
	for (i = 0; i < n; i++) {
		fn_run *run = [self fnRunAt:[self fnRunSpanCovering:range].location + i];
		NSMutableDictionary *merged = [[NSMutableDictionary alloc] initWithDictionary:
						run->attrs != nil ? run->attrs : [NSDictionary dictionary]];

		NSDictionary *replacement;

		[merged addEntriesFromDictionary:attrs];
		replacement = [[NSDictionary alloc] initWithDictionary:merged];
		[merged release];
		[run->attrs release];
		run->attrs = replacement;
	}
	{
		NSUInteger k;

		for (k = 0; k < _runCount; k++) {
			fn_run *r = [self fnRunAt:k];

		}
	}
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
}

- (void)removeAttribute:(NSAttributedStringKey)name range:(NSRange)range
{
	NSUInteger i, n;
	NSRange span;

	if (range.length == 0) {
		return;
	}
	[self fnSplitAt:range.location];
	[self fnSplitAt:range.location + range.length];
	span = [self fnRunSpanCovering:range];
	n = span.length;
	for (i = 0; i < n; i++) {
		fn_run *run = [self fnRunAt:span.location + i];

		if (run->attrs != nil && [run->attrs objectForKey:name] != nil) {
			NSMutableDictionary *left = [[NSMutableDictionary alloc] initWithDictionary:run->attrs];

			NSDictionary *replacement;

			[left removeObjectForKey:name];
			replacement = [left count] > 0
				? [[NSDictionary alloc] initWithDictionary:left] : nil;
			[left release];
			[run->attrs release];
			run->attrs = replacement;
		}
	}
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
}

- (void)setAttributes:(NSDictionary *)attrs range:(NSRange)range
{
	NSUInteger i, n;
	NSRange span;

	if (range.length == 0) {
		return;
	}
	[self fnSplitAt:range.location];
	[self fnSplitAt:range.location + range.length];
	span = [self fnRunSpanCovering:range];
	n = span.length;
	for (i = 0; i < n; i++) {
		fn_run *run = [self fnRunAt:span.location + i];

		[run->attrs release];
		run->attrs = attrs != nil && [attrs count] > 0 ? [[NSDictionary alloc] initWithDictionary:attrs] : nil;
	}
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
}

- (void)beginEditing
{
	_editDepth++;
}

- (void)endEditing
{
	if (_editDepth > 0) {
		_editDepth--;
	}
	if (_editDepth == 0) {
		[self fnCoalesce];
	}
}

- (void)fixAttributesInRange:(NSRange)range
{
	NSUInteger i = 0;

	/* DROP WHAT CANNOT BE TRUE - a run that reaches past the string - and then coalesce what is left. There
	 * are no attribute DEFAULTS to install: no attribute in this library has a rendering default (a font is
	 * the drawing layer's business, not the store's). */
	while (i < _runCount) {
		fn_run *run = [self fnRunAt:i];
		NSUInteger limit = [_string length];

		if (run->range.location >= limit) {
			[self fnRemoveRunsFrom:i count:1];
			continue;
		}
		if (run->range.location + run->range.length > limit) {
			run->range.length = limit - run->range.location;
		}
		i++;
	}
	[self fnCoalesce];
	(void)range;
}

@end
