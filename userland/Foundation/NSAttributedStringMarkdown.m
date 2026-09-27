/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAttributedStringMarkdown.m — the markdown family's two value objects (§62.64). MANUAL OWNERSHIP.
 *
 * NEITHER CLASS PARSES ANYTHING, and that is the point of shipping them now: they are what an importer would be
 * CONFIGURED with and what it would RECORD about where text came from, and the importer itself is the dependency
 * §12.6 still lists. A caller can build, compare and copy both objects today, and the day the parser lands it
 * has its options object waiting.
 *
 * THE ONE PIECE OF REAL ARITHMETIC IS -rangeInString:, and it is real because the two coordinate systems do not
 * match: Apple's source positions are 1-based LINE and COLUMN numbers where a column is a UTF-8 BYTE offset
 * ("columns represent UTF-8 indices; for multi-byte characters, the column indicates the first byte"), while
 * every range in this library is in UTF-16 units. So the conversion walks the string's own bytes — the same
 * bytes -UTF8String answers with — counting UTF-16 units as it goes (a 4-byte sequence is a SURROGATE PAIR and
 * therefore TWO units), which is the only way to get this right for text outside the BMP.
 */

#import <Foundation/NSAttributedStringMarkdown.h>
#import <Foundation/NSString.h>

#include <string.h>

/* THE LINE'S FIRST BYTE: line 1 starts at byte 0, and every '\n' ends one line and begins the next. A line
 * number past the end answers the end of the string, which is the clamping -rangeInString: documents. */
static NSUInteger fn_line_start(const unsigned char *bytes, NSUInteger line)
{
	NSUInteger current = 1;
	NSUInteger i = 0;

	if (line <= 1) {
		return 0;
	}
	while (bytes[i] != '\0') {
		if (bytes[i] == '\n') {
			current++;
			if (current == line) {
				return i + 1;
			}
		}
		i++;
	}
	return i;
}

/* HOW MANY UTF-16 UNITS THE FIRST `byteOffset` BYTES OF `string` HOLD. A byte in the middle of a sequence is
 * counted as the sequence it belongs to and the walk stops there, which is the honest answer for a column that
 * names a byte no character starts at. An invalid lead byte is counted as one unit rather than looping. */
static NSUInteger fn_units_for_bytes(NSString *string, NSUInteger byteOffset)
{
	const unsigned char *bytes = (const unsigned char *)[string UTF8String];
	NSUInteger i = 0;
	NSUInteger units = 0;

	if (bytes == NULL) {
		return 0;
	}
	while (i < byteOffset && bytes[i] != '\0') {
		unsigned char c = bytes[i];

		if (c < 0x80) {
			i += 1;
			units += 1;
		} else if ((c & 0xE0) == 0xC0) {
			i += 2;
			units += 1;
		} else if ((c & 0xF0) == 0xE0) {
			i += 3;
			units += 1;
		} else if ((c & 0xF8) == 0xF0) {
			i += 4;
			units += 2;		/* outside the BMP: ONE character, TWO UTF-16 units */
		} else {
			i += 1;
			units += 1;
		}
	}
	return units;
}

/* A COLUMN AS A UTF-16 INDEX. The column is 1-based and names a byte, so column 1 is the line's first byte and
 * column c is byte offset (c - 1); a column of 0 or less clamps to the line's start (Apple publishes no rule for
 * one, and a negative offset is meaningless). */
static NSUInteger fn_index_in_line(NSString *string, const unsigned char *bytes, NSInteger line, NSInteger column)
{
	NSUInteger lineStart = fn_line_start(bytes, (NSUInteger)(line > 0 ? line : 1));
	NSUInteger offset = lineStart;

	if (column > 1) {
		offset = lineStart + (NSUInteger)(column - 1);
	}
	return fn_units_for_bytes(string, offset);
}

@implementation NSAttributedStringMarkdownParsingOptions

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		/* THE STARTING VALUES, AND ONLY TWO OF THEM ARE APPLE'S. Apple states them in as many words for
		 * `allowsExtendedAttributes` ("The default is `NO`") and `languageCode` ("The default is `nil`"), and
		 * for the other three the pages publish the property and no starting value — so those are OURS and
		 * stated here (§11.6.1 D2): the FULL syntax, the FORGIVING failure policy, and no source-position
		 * attributes. */
		_allowsExtendedAttributes = NO;
		_appliesSourcePositionAttributes = NO;
		_interpretedSyntax = NSAttributedStringMarkdownInterpretedSyntaxFull;
		_failurePolicy = NSAttributedStringMarkdownParsingFailureReturnPartiallyParsedIfPossible;
	}
	return self;
}

- (void)dealloc
{
	[_languageCode release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

- (BOOL)allowsExtendedAttributes
{
	return _allowsExtendedAttributes;
}

- (void)setAllowsExtendedAttributes:(BOOL)value
{
	_allowsExtendedAttributes = value;
}

- (BOOL)appliesSourcePositionAttributes
{
	return _appliesSourcePositionAttributes;
}

- (void)setAppliesSourcePositionAttributes:(BOOL)value
{
	_appliesSourcePositionAttributes = value;
}

- (NSAttributedStringMarkdownInterpretedSyntax)interpretedSyntax
{
	return _interpretedSyntax;
}

- (void)setInterpretedSyntax:(NSAttributedStringMarkdownInterpretedSyntax)value
{
	_interpretedSyntax = value;
}

- (NSAttributedStringMarkdownParsingFailurePolicy)failurePolicy
{
	return _failurePolicy;
}

- (void)setFailurePolicy:(NSAttributedStringMarkdownParsingFailurePolicy)value
{
	_failurePolicy = value;
}

- (nullable NSString *)languageCode
{
	return _languageCode;
}

- (void)setLanguageCode:(nullable NSString *)value
{
	/* APPLE DECLARES THIS PROPERTY `copy`, so the object holds a SNAPSHOT — through -initWithString: and not
	 * -copy, for the reason the string store and the locking family state: this library's -copy is not a value
	 * copy. */
	NSString *snapshot = value != nil ? [[NSString alloc] initWithString:value] : nil;

	[_languageCode release];
	_languageCode = snapshot;
}

/* NSCopying, THE HOUSE WAY: the protocol declares -copy (the -copyWithZone: members are the D1 deviation), and
 * a value object copies its values. */
- (id)copy
{
	NSAttributedStringMarkdownParsingOptions *copy = [[NSAttributedStringMarkdownParsingOptions alloc] init];

	copy->_allowsExtendedAttributes = _allowsExtendedAttributes;
	copy->_appliesSourcePositionAttributes = _appliesSourcePositionAttributes;
	copy->_interpretedSyntax = _interpretedSyntax;
	copy->_failurePolicy = _failurePolicy;
	[copy setLanguageCode:_languageCode];
	return copy;
}

@end

@implementation NSAttributedStringMarkdownSourcePosition

- (instancetype)initWithStartLine:(NSInteger)startLine
		      startColumn:(NSInteger)startColumn
			  endLine:(NSInteger)endLine
			endColumn:(NSInteger)endColumn
{
	self = [super init];
	if (self != nil) {
		_startLine = startLine;
		_startColumn = startColumn;
		_endLine = endLine;
		_endColumn = endColumn;
	}
	return self;
}

- (NSInteger)startLine { return _startLine; }
- (NSInteger)startColumn { return _startColumn; }
- (NSInteger)endLine { return _endLine; }
- (NSInteger)endColumn { return _endColumn; }

- (NSRange)rangeInString:(NSString *)string
{
	const unsigned char *bytes = string != nil ? (const unsigned char *)[string UTF8String] : NULL;
	NSUInteger start, end;

	if (bytes == NULL) {
		return NSMakeRange(0, 0);
	}
	start = fn_index_in_line(string, bytes, _startLine, _startColumn);
	end = fn_index_in_line(string, bytes, _endLine, _endColumn);
	/* THE END IS EXCLUSIVE AND AN END BEFORE ITS START IS EMPTY: Apple publishes the method's PURPOSE and not
	 * its inclusivity, so both readings are stated here rather than assumed (§11.6.1 D2). A position past the
	 * end of the string was clamped by the line walk, so `end` can only be short of `start` by construction. */
	if (end < start) {
		end = start;
	}
	return NSMakeRange(start, end - start);
}

- (id)copy
{
	return [[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:_startLine
								      startColumn:_startColumn
									  endLine:_endLine
									endColumn:_endColumn];
}

@end
