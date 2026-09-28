/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAttributedStringMarkdown.m — the markdown family (§62.64 shipped the two value objects; §62.107 added the
 * importer). MANUAL OWNERSHIP.
 *
 * THE TWO VALUE OBJECTS STILL PARSE NOTHING, and they were shipped first on purpose: they are what an importer
 * is CONFIGURED with and what it RECORDS about where text came from. §62.107 is the importer itself — the three
 * doors Apple pairs with those options, and the parser behind them — and its own contract, its conventions and
 * its one refusal are stated in the block above the parser at the end of this file.
 *
 * THE ONE PIECE OF REAL ARITHMETIC IS -rangeInString:, and it is real because the two coordinate systems do not
 * match: Apple's source positions are 1-based LINE and COLUMN numbers where a column is a UTF-8 BYTE offset
 * ("columns represent UTF-8 indices; for multi-byte characters, the column indicates the first byte"), while
 * every range in this library is in UTF-16 units. So the conversion walks the string's own bytes — the same
 * bytes -UTF8String answers with — counting UTF-16 units as it goes (a 4-byte sequence is a SURROGATE PAIR and
 * therefore TWO units), which is the only way to get this right for text outside the BMP.
 */

#import <Foundation/NSAttributedStringMarkdown.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSAttributedString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSPresentationIntent.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

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


/* ================================================================================================
 * THE IMPORTER (§62.107).
 *
 * WHAT IT IS: the three doors Apple pairs with the options this file already shipped, and the parser behind
 * them. The result is SEMANTIC, not stylistic — Apple's own statement of the design, quoted because it is the
 * whole reason this is not a font-setting routine: "The system doesn't add style attributes to match the
 * Markdown elements." So a run carries an INLINE intent (NSInlinePresentationIntentAttributeName) and a block
 * carries an NSPresentationIntent (NSPresentationIntentAttributeName), and nothing here writes a font or a
 * colour.
 *
 * THE CONVENTIONS THIS LIBRARY HAD TO CHOOSE, because Apple publishes the vocabulary and not the algorithm (the
 * same shape §11.6.1 D2 already records for enum values). Each is named here, asserted by the probe, and stated
 * in the plan:
 *   - A block ends with "\n", so a document's blocks are its lines.
 *   - A TABLE IS NOT PARSED. The twelve kinds exist — NSPresentationIntent ships every one of them — but this
 *     importer leaves a table as paragraph text. That is a REFUSAL with a reason: Apple publishes neither the
 *     character content of a row nor the home of the alignment enum, so emitting cells would be inventing the
 *     wire, and this library refuses rather than invents.
 *   - An image contributes its alt text, carrying NSImageURLAttributeName (Apple's attribute for exactly this)
 *     and NSAlternateDescriptionAttributeName. A link's title is read and dropped: Apple maps it to no
 *     attribute this library can cite.
 *   - A run of four or more emphasis characters is literal; an intraword `_` is literal.
 *   - Extended attributes (`^[text](key: value)`) are parsed only when allowsExtendedAttributes is set; a
 *     numeric or true/false value becomes an NSNumber and everything else an NSString.
 *   - `languageCode` is applied as NSLanguageIdentifierAttributeName to every run — Apple's own words for the
 *     default are "applies no attributes", which is what nil does.
 *   - THE TWO FAILURES are an UNTERMINATED FENCE and a MALFORMED EXTENDED ATTRIBUTE, and the error domain is
 *     this library's (Apple publishes no domain for this importer); its userInfo names the line.
 */

static BOOL fn_md_is_space_or_tab(unsigned int c)
{
	return c == ' ' || c == '\t';
}

static BOOL fn_md_is_digit(unsigned int c)
{
	return c >= '0' && c <= '9';
}

static BOOL fn_md_is_alnum(unsigned int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* ASCII punctuation, which is exactly the set a backslash escapes. */
static BOOL fn_md_is_punct(unsigned int c)
{
	if (c >= '!' && c <= '/') {
		return YES;
	}
	if (c >= ':' && c <= '@') {
		return YES;
	}
	if (c >= '[' && c <= '`') {
		return YES;
	}
	return c >= '{' && c <= '~';
}

static BOOL fn_md_blank(NSString *line)
{
	NSUInteger i, n = [line length];

	for (i = 0; i < n; i++) {
		if (!fn_md_is_space_or_tab([line characterAtIndex:i])) {
			return NO;
		}
	}
	return YES;
}

/* The leading whitespace width (a tab counts four) and the UTF-16 index where the content starts. */
static NSUInteger fn_md_indent(NSString *line, NSUInteger *width)
{
	NSUInteger i = 0, n = [line length], w = 0;

	while (i < n && fn_md_is_space_or_tab([line characterAtIndex:i])) {
		w += ([line characterAtIndex:i] == '\t') ? 4 : 1;
		i++;
	}
	if (width != NULL) {
		*width = w;
	}
	return i;
}

static NSString *fn_md_rtrim(NSString *line)
{
	NSUInteger n = [line length];

	while (n > 0 && fn_md_is_space_or_tab([line characterAtIndex:n - 1])) {
		n--;
	}
	return [line substringToIndex:n];
}

static NSUInteger fn_md_index_of(NSString *s, unsigned int ch, NSUInteger from)
{
	NSUInteger i, n = [s length];

	for (i = from; i < n; i++) {
		if ([s characterAtIndex:i] == ch) {
			return i;
		}
	}
	return NSNotFound;
}

/* The length of the run of `ch` that starts at `i`. */
static NSUInteger fn_md_run_len(NSString *s, NSUInteger i, unsigned int ch)
{
	NSUInteger j = i, n = [s length];

	while (j < n && [s characterAtIndex:j] == ch) {
		j++;
	}
	return j - i;
}

/* The first run of `ch` at or after `from` whose length is EXACTLY `want`. */
static NSUInteger fn_md_exact_run(NSString *s, NSUInteger from, unsigned int ch, NSUInteger want)
{
	NSUInteger i = from, n = [s length];

	while (i < n) {
		if ([s characterAtIndex:i] == ch) {
			NSUInteger len = fn_md_run_len(s, i, ch);
			if (len == want) {
				return i;
			}
			i += len;
		} else {
			i++;
		}
	}
	return NSNotFound;
}

/* The index of the ']' that closes the '[' before `from`, counting nesting. */
static NSUInteger fn_md_match_bracket(NSString *s, NSUInteger from)
{
	NSUInteger i, n = [s length];
	NSInteger depth = 0;

	for (i = from; i < n; i++) {
		unsigned int c = [s characterAtIndex:i];
		if (c == '[') {
			depth++;
		} else if (c == ']') {
			if (depth == 0) {
				return i;
			}
			depth--;
		}
	}
	return NSNotFound;
}

/* The index of the ')' that closes the '(' before `from`, counting nesting. */
static NSUInteger fn_md_match_paren(NSString *s, NSUInteger from)
{
	NSUInteger i, n = [s length];
	NSInteger depth = 0;

	for (i = from; i < n; i++) {
		unsigned int c = [s characterAtIndex:i];
		if (c == '(') {
			depth++;
		} else if (c == ')') {
			if (depth == 0) {
				return i;
			}
			depth--;
		}
	}
	return NSNotFound;
}

/* A code span's content: one leading and one trailing space are removed when both are there and the content is
 * not itself all spaces — CommonMark's rule, kept because it is the one that makes `` ` a ` `` mean "a". */
static NSString *fn_md_code_span_trim(NSString *content)
{
	NSUInteger n = [content length];

	if (n >= 2 && [content characterAtIndex:0] == ' ' && [content characterAtIndex:n - 1] == ' ') {
		NSUInteger i;
		for (i = 1; i < n - 1; i++) {
			if ([content characterAtIndex:i] != ' ') {
				return [content substringWithRange:NSMakeRange(1, n - 2)];
			}
		}
	}
	return content;
}

/* The named entities this library decodes (a stated subset, not the HTML5 table) plus decimal and hex
 * numeric references. Returns nil when the text at `i` (an '&') is not an entity, and the characters it
 * consumed otherwise. */
static NSString *fn_md_entity(NSString *s, NSUInteger i, NSUInteger *consumed)
{
	NSUInteger semi = fn_md_index_of(s, ';', i + 1);
	NSString *name;
	unichar scalar = 0;
	BOOL have = NO;

	if (semi == NSNotFound || semi - i > 32) {
		return nil;
	}
	name = [s substringWithRange:NSMakeRange(i + 1, semi - (i + 1))];
	if ([name length] > 1 && [name characterAtIndex:0] == '#') {
		NSUInteger k, n = [name length];
		unsigned long value = 0;
		BOOL hex = ([name characterAtIndex:1] == 'x' || [name characterAtIndex:1] == 'X');

		for (k = hex ? 2 : 1; k < n; k++) {
			unsigned int c = [name characterAtIndex:k];
			unsigned int digit;
			if (c >= '0' && c <= '9') {
				digit = c - '0';
			} else if (hex && c >= 'a' && c <= 'f') {
				digit = 10 + (c - 'a');
			} else if (hex && c >= 'A' && c <= 'F') {
				digit = 10 + (c - 'A');
			} else {
				return nil;
			}
			value = value * (hex ? 16 : 10) + digit;
			if (value > 0x10FFFF) {
				return nil;
			}
		}
		if (value == 0) {
			return nil;
		}
		if (consumed != NULL) {
			*consumed = semi - i + 1;
		}
		{
			unichar built = (unichar)value;

			return [NSString stringWithCharacters:&built length:1];
		}
	}
	if ([name isEqualToString:@"amp"]) { scalar = '&'; have = YES; }
	else if ([name isEqualToString:@"lt"]) { scalar = '<'; have = YES; }
	else if ([name isEqualToString:@"gt"]) { scalar = '>'; have = YES; }
	else if ([name isEqualToString:@"quot"]) { scalar = '"'; have = YES; }
	else if ([name isEqualToString:@"apos"]) { scalar = '\''; have = YES; }
	else if ([name isEqualToString:@"nbsp"]) { scalar = 0x00A0; have = YES; }
	else if ([name isEqualToString:@"copy"]) { scalar = 0x00A9; have = YES; }
	else if ([name isEqualToString:@"reg"]) { scalar = 0x00AE; have = YES; }
	else if ([name isEqualToString:@"mdash"]) { scalar = 0x2014; have = YES; }
	else if ([name isEqualToString:@"ndash"]) { scalar = 0x2013; have = YES; }
	else if ([name isEqualToString:@"hellip"]) { scalar = 0x2026; have = YES; }
	else if ([name isEqualToString:@"laquo"]) { scalar = 0x00AB; have = YES; }
	else if ([name isEqualToString:@"raquo"]) { scalar = 0x00BB; have = YES; }
	if (!have) {
		return nil;
	}
	if (consumed != NULL) {
		*consumed = semi - i + 1;
	}
	return [NSString stringWithCharacters:&scalar length:1];
}

/* A link destination: the first whitespace-separated token, with <angle brackets> and backslash escapes
 * removed. A title after it is read and dropped (see the contract above). */
static NSString *fn_md_destination(NSString *inside)
{
	NSUInteger i = 0, n = [inside length], start = 0, end = 0;
	NSMutableString *out;
	while (i < n && fn_md_is_space_or_tab([inside characterAtIndex:i])) {
		i++;
	}
	start = i;
	if (i < n && [inside characterAtIndex:i] == '<') {
		start = i + 1;
		i++;
		while (i < n && [inside characterAtIndex:i] != '>') {
			i++;
		}
		end = i;
	} else {
		while (i < n && !fn_md_is_space_or_tab([inside characterAtIndex:i])) {
			i++;
		}
		end = i;
	}
	if (end <= start) {
		return nil;
	}
	out = [NSMutableString stringWithString:[inside substringWithRange:NSMakeRange(start, end - start)]];
	[out replaceOccurrencesOfString:@"\\" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	return out;
}

/* The markup characters a plain-text rendering drops, for an image's alternate description. */
static NSString *fn_md_plain_text(NSString *label)
{
	NSMutableString *out = [NSMutableString stringWithString:label];

	[out replaceOccurrencesOfString:@"**" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"~~" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"`" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"*" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"_" withString:@"" options:0 range:NSMakeRange(0, [out length])];
	return out;
}

static BOOL fn_md_is_number(NSString *value);

/* The `key: value` pairs of an extended attribute. */
static NSDictionary *fn_md_attribute_list(NSString *inside)
{
	NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
	NSArray *pairs = [inside componentsSeparatedByString:@","];
	NSUInteger i;

	for (i = 0; i < [pairs count]; i++) {
		NSString *pair = fn_md_rtrim([pairs objectAtIndex:i]);
		NSRange colon = [pair rangeOfString:@":"];
		NSString *key, *value;

		if (colon.location == NSNotFound) {
			continue;
		}
		key = [fn_md_rtrim([pair substringFromIndex:fn_md_indent(pair, NULL)])
						substringToIndex:colon.location - fn_md_indent(pair, NULL)];
		value = [pair substringFromIndex:colon.location + 1];
		while ([value length] > 0 && fn_md_is_space_or_tab([value characterAtIndex:0])) {
			value = [value substringFromIndex:1];
		}
		value = fn_md_rtrim(value);
		if ([key length] == 0 || [value length] == 0) {
			continue;
		}
		if ([value isEqualToString:@"true"] || [value isEqualToString:@"false"]) {
			[attrs setObject:[NSNumber numberWithBool:[value isEqualToString:@"true"]] forKey:key];
		} else if (fn_md_is_number(value)) {
			[attrs setObject:[NSNumber numberWithLongLong:[value longLongValue]] forKey:key];
		} else {
			[attrs setObject:value forKey:key];
		}
	}
	return attrs;
}

/* A whole number, optionally signed — the extended-attribute values that become NSNumbers. */
static BOOL fn_md_is_number(NSString *value)
{
	NSUInteger i = 0, n = [value length];

	if (n == 0) {
		return NO;
	}
	if ([value characterAtIndex:0] == '-' || [value characterAtIndex:0] == '+') {
		i = 1;
	}
	if (i >= n) {
		return NO;
	}
	for (; i < n; i++) {
		if (!fn_md_is_digit([value characterAtIndex:i])) {
			return NO;
		}
	}
	return YES;
}

typedef struct {
	NSMutableAttributedString *out;
	NSURL *baseURL;
	NSAttributedStringMarkdownParsingOptions *opts;
	NSInteger nextIdentity;
	NSUInteger currentLine;
	BOOL failed;
	NSUInteger failLine;
} fn_md_ctx;

static NSInteger fn_md_new_identity(fn_md_ctx *c)
{
	c->nextIdentity += 1;
	return c->nextIdentity;
}

static void fn_md_emit(fn_md_ctx *c, NSString *text, unsigned int bits, NSDictionary *extra)
{
	NSMutableDictionary *attrs;
	NSAttributedString *piece;

	if ([text length] == 0) {
		return;
	}
	attrs = [NSMutableDictionary dictionary];
	if (extra != nil) {
		[attrs addEntriesFromDictionary:extra];
	}
	if ([c->opts languageCode] != nil) {
		[attrs setObject:[c->opts languageCode] forKey:NSLanguageIdentifierAttributeName];
	}
	if (bits != 0) {
		[attrs setObject:[NSNumber numberWithUnsignedInt:bits] forKey:NSInlinePresentationIntentAttributeName];
	}
	piece = [[NSAttributedString alloc] initWithString:text
						attributes:([attrs count] > 0) ? attrs : nil];
	[c->out appendAttributedString:piece];
	[piece release];
}

static void fn_md_intent(fn_md_ctx *c, NSPresentationIntent *intent, NSUInteger start, NSUInteger end)
{
	if (intent == nil || end <= start) {
		return;
	}
	[c->out addAttribute:NSPresentationIntentAttributeName
		       value:intent
		       range:NSMakeRange(start, end - start)];
}

static void fn_md_source_position(fn_md_ctx *c, NSUInteger start, NSUInteger end,
				  NSUInteger startLine, NSUInteger startColumn,
				  NSUInteger endLine, NSUInteger endColumn)
{
	NSAttributedStringMarkdownSourcePosition *pos;

	if (![c->opts appliesSourcePositionAttributes] || end <= start) {
		return;
	}
	pos = [[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:(NSInteger)startLine
								     startColumn:(NSInteger)startColumn
									 endLine:(NSInteger)endLine
								       endColumn:(NSInteger)endColumn];
	[c->out addAttribute:NSMarkdownSourcePositionAttributeName
		       value:pos
		       range:NSMakeRange(start, end - start)];
	[pos release];
}

/* Is `i` a closing emphasis run for a run of `runLen` that opened at `open`? */
static NSUInteger fn_md_emphasis_close(NSString *s, NSUInteger open, unsigned int ch, NSUInteger runLen)
{
	NSUInteger at = fn_md_exact_run(s, open + runLen, ch, runLen);

	if (at == NSNotFound) {
		return NSNotFound;
	}
	if (ch == '_') {
		/* an intraword underscore does not close either: `a_b_c` is not emphasis */
		NSUInteger after = at + runLen;
		if (after < [s length] && fn_md_is_alnum([s characterAtIndex:after])) {
			return NSNotFound;
		}
	}
	return at;
}

static void fn_md_inline(fn_md_ctx *c, NSString *src, unsigned int baseBits, NSDictionary *extra)
{
	NSMutableString *buf = [NSMutableString string];
	NSUInteger i = 0, n = [src length];

	while (i < n) {
		unsigned int ch = [src characterAtIndex:i];

		if (ch == '\n') {
			NSUInteger spaces = 0, k = i;
			unsigned int bits;

			while (k > 0 && [src characterAtIndex:k - 1] == ' ') {
				spaces++;
				k--;
			}
			while ([buf length] > 0 && [buf characterAtIndex:[buf length] - 1] == ' ') {
				[buf deleteCharactersInRange:NSMakeRange([buf length] - 1, 1)];
			}
			fn_md_emit(c, buf, baseBits, extra);
			[buf setString:@""];
			bits = (spaces >= 2) ? (baseBits | NSInlinePresentationIntentLineBreak)
					     : (baseBits | NSInlinePresentationIntentSoftBreak);
			fn_md_emit(c, @"\n", bits, extra);
			i++;
			continue;
		}

		if (ch == '\\' && i + 1 < n) {
			unsigned int next = [src characterAtIndex:i + 1];

			if (next == '\n') {
				while ([buf length] > 0 && [buf characterAtIndex:[buf length] - 1] == ' ') {
					[buf deleteCharactersInRange:NSMakeRange([buf length] - 1, 1)];
				}
				fn_md_emit(c, buf, baseBits, extra);
				[buf setString:@""];
				fn_md_emit(c, @"\n", baseBits | NSInlinePresentationIntentLineBreak, extra);
				i += 2;
				continue;
			}
			if (fn_md_is_punct(next)) {
				[buf appendString:[src substringWithRange:NSMakeRange(i + 1, 1)]];
				i += 2;
				continue;
			}
			[buf appendString:@"\\"];
			i++;
			continue;
		}

		if (ch == '`') {
			NSUInteger runLen = fn_md_run_len(src, i, '`');
			NSUInteger close = fn_md_exact_run(src, i + runLen, '`', runLen);

			if (close != NSNotFound) {
				NSString *content = [src substringWithRange:NSMakeRange(i + runLen, close - (i + runLen))];

				fn_md_emit(c, buf, baseBits, extra);
				[buf setString:@""];
				fn_md_emit(c, fn_md_code_span_trim(content),
					   baseBits | NSInlinePresentationIntentCode, extra);
				i = close + runLen;
				continue;
			}
			[buf appendString:[src substringWithRange:NSMakeRange(i, runLen)]];
			i += runLen;
			continue;
		}

		if (ch == '*' || ch == '_') {
			NSUInteger runLen = fn_md_run_len(src, i, ch);

			if (runLen <= 3 && !(ch == '_' && i > 0 && fn_md_is_alnum([src characterAtIndex:i - 1]))) {
				NSUInteger close = fn_md_emphasis_close(src, i, ch, runLen);

				if (close != NSNotFound) {
					NSString *inner = [src substringWithRange:NSMakeRange(i + runLen, close - (i + runLen))];
					unsigned int bits = baseBits;

					fn_md_emit(c, buf, baseBits, extra);
					[buf setString:@""];
					if (runLen >= 2) {
						bits |= NSInlinePresentationIntentStronglyEmphasized;
					}
					if (runLen != 2) {
						bits |= NSInlinePresentationIntentEmphasized;
					}
					fn_md_inline(c, inner, bits, extra);
					i = close + runLen;
					continue;
				}
			}
			[buf appendString:[src substringWithRange:NSMakeRange(i, runLen)]];
			i += runLen;
			continue;
		}

		if (ch == '~' && i + 1 < n && [src characterAtIndex:i + 1] == '~') {
			NSUInteger close = fn_md_exact_run(src, i + 2, '~', 2);

			if (close != NSNotFound) {
				NSString *inner = [src substringWithRange:NSMakeRange(i + 2, close - (i + 2))];

				fn_md_emit(c, buf, baseBits, extra);
				[buf setString:@""];
				fn_md_inline(c, inner, baseBits | NSInlinePresentationIntentStrikethrough, extra);
				i = close + 2;
				continue;
			}
		}

		if (ch == '^' && [c->opts allowsExtendedAttributes] && i + 1 < n &&
		    [src characterAtIndex:i + 1] == '[') {
			NSUInteger labelEnd = fn_md_match_bracket(src, i + 2);

			if (labelEnd != NSNotFound && labelEnd + 1 < n &&
			    [src characterAtIndex:labelEnd + 1] == '(') {
				NSUInteger close = fn_md_match_paren(src, labelEnd + 2);

				if (close == NSNotFound) {
					c->failed = YES;
					c->failLine = c->currentLine;
				} else {
					NSString *label = [src substringWithRange:NSMakeRange(i + 2, labelEnd - (i + 2))];
					NSString *inside = [src substringWithRange:NSMakeRange(labelEnd + 2,
										close - (labelEnd + 2))];
					NSMutableDictionary *more = [NSMutableDictionary dictionary];

					if (extra != nil) {
						[more addEntriesFromDictionary:extra];
					}
					[more addEntriesFromDictionary:fn_md_attribute_list(inside)];
					fn_md_emit(c, buf, baseBits, extra);
					[buf setString:@""];
					fn_md_inline(c, label, baseBits, more);
					i = close + 1;
					continue;
				}
			}
		}

		if (ch == '[' || (ch == '!' && i + 1 < n && [src characterAtIndex:i + 1] == '[')) {
			NSUInteger open = (ch == '!') ? i + 1 : i;
			NSUInteger labelEnd = fn_md_match_bracket(src, open + 1);

			if (labelEnd != NSNotFound && labelEnd + 1 < n &&
			    [src characterAtIndex:labelEnd + 1] == '(') {
				NSUInteger destEnd = fn_md_match_paren(src, labelEnd + 2);

				if (destEnd != NSNotFound) {
					NSString *label = [src substringWithRange:NSMakeRange(open + 1,
										 labelEnd - (open + 1))];
					NSString *inside = [src substringWithRange:NSMakeRange(labelEnd + 2,
										destEnd - (labelEnd + 2))];
					NSString *dest = fn_md_destination(inside);
					NSMutableDictionary *more = [NSMutableDictionary dictionary];

					if (extra != nil) {
						[more addEntriesFromDictionary:extra];
					}
					if (dest != nil) {
						NSURL *url = [NSURL URLWithString:dest relativeToURL:c->baseURL];

						if (url != nil) {
							[more setObject:url forKey:(ch == '!') ? NSImageURLAttributeName
											       : NSLinkAttributeName];
						}
					}
					if (ch == '!') {
						[more setObject:fn_md_plain_text(label)
							 forKey:NSAlternateDescriptionAttributeName];
					}
					fn_md_emit(c, buf, baseBits, extra);
					[buf setString:@""];
					fn_md_inline(c, label, baseBits, more);
					i = destEnd + 1;
					continue;
				}
			}
			[buf appendString:[src substringWithRange:NSMakeRange(i, (ch == '!') ? 2 : 1)]];
			i += (ch == '!') ? 2 : 1;
			continue;
		}

		if (ch == '<') {
			NSUInteger close = fn_md_index_of(src, '>', i + 1);

			if (close != NSNotFound && close - i <= 512) {
				NSString *inside = [src substringWithRange:NSMakeRange(i + 1, close - (i + 1))];
				BOOL spaces = ([inside rangeOfString:@" "].location != NSNotFound);

				if ([inside length] > 0 && !spaces &&
				    ([inside rangeOfString:@":"].location != NSNotFound ||
				     [inside rangeOfString:@"@"].location != NSNotFound)) {
					NSString *destination = ([inside rangeOfString:@":"].location != NSNotFound)
								? inside
								: [@"mailto:" stringByAppendingString:inside];
					NSURL *url = [NSURL URLWithString:destination relativeToURL:c->baseURL];
					NSMutableDictionary *more = [NSMutableDictionary dictionary];

					if (extra != nil) {
						[more addEntriesFromDictionary:extra];
					}
					if (url != nil) {
						[more setObject:url forKey:NSLinkAttributeName];
					}
					fn_md_emit(c, buf, baseBits, extra);
					[buf setString:@""];
					fn_md_emit(c, inside, baseBits, more);
					i = close + 1;
					continue;
				}
				if ([inside length] > 0 &&
				    (fn_md_is_alnum([inside characterAtIndex:0]) ||
				     [inside characterAtIndex:0] == '/' || [inside characterAtIndex:0] == '!')) {
					fn_md_emit(c, buf, baseBits, extra);
					[buf setString:@""];
					fn_md_emit(c, [src substringWithRange:NSMakeRange(i, close - i + 1)],
						   baseBits | NSInlinePresentationIntentInlineHTML, extra);
					i = close + 1;
					continue;
				}
			}
		}

		if (ch == '&') {
			NSUInteger used = 0;
			NSString *decoded = fn_md_entity(src, i, &used);

			if (decoded != nil) {
				[buf appendString:decoded];
				i += used;
				continue;
			}
		}

		[buf appendString:[src substringWithRange:NSMakeRange(i, 1)]];
		i++;
	}
	fn_md_emit(c, buf, baseBits, extra);
}

/* ------------------------------------------------------------------------------------------------
 * THE BLOCK LEVEL.
 */

static BOOL fn_md_fence(NSString *body, unsigned int *fenceChar, NSUInteger *fenceLen, NSString **info)
{
	NSUInteger i = 0, len;

	if ([body length] == 0) {
		return NO;
	}
	if ([body characterAtIndex:0] != '`' && [body characterAtIndex:0] != '~') {
		return NO;
	}
	*fenceChar = [body characterAtIndex:0];
	len = fn_md_run_len(body, 0, *fenceChar);
	if (len < 3) {
		return NO;
	}
	*fenceLen = len;
	i = len;
	*info = fn_md_rtrim([body substringFromIndex:i]);
	/* a backtick fence's info string may not contain a backtick */
	return !(*fenceChar == '`' && [*info rangeOfString:@"`"].location != NSNotFound);
}

static BOOL fn_md_closes_fence(NSString *line, unsigned int fenceChar, NSUInteger fenceLen)
{
	NSUInteger indent = fn_md_indent(line, NULL);
	NSString *body = [line substringFromIndex:indent];

	if ([body length] < fenceLen) {
		return NO;
	}
	if ([body characterAtIndex:0] != fenceChar) {
		return NO;
	}
	if (fn_md_run_len(body, 0, fenceChar) < fenceLen) {
		return NO;
	}
	return fn_md_blank([body substringFromIndex:fn_md_run_len(body, 0, fenceChar)]);
}

/* An ATX header's level and its content, without the closing #s. */
static BOOL fn_md_atx(NSString *body, NSInteger *level, NSString **content)
{
	NSUInteger hashes = fn_md_run_len(body, 0, '#');
	NSUInteger i = hashes;
	NSString *rest;

	if (hashes < 1 || hashes > 6) {
		return NO;
	}
	if (i < [body length] && !fn_md_is_space_or_tab([body characterAtIndex:i])) {
		return NO;
	}
	while (i < [body length] && fn_md_is_space_or_tab([body characterAtIndex:i])) {
		i++;
	}
	rest = fn_md_rtrim([body substringFromIndex:i]);
	while ([rest length] > 0 && [rest characterAtIndex:[rest length] - 1] == '#') {
		rest = [rest substringToIndex:[rest length] - 1];
	}
	*level = (NSInteger)hashes;
	*content = fn_md_rtrim(rest);
	return YES;
}

static BOOL fn_md_thematic(NSString *body)
{
	unsigned int ch;
	NSUInteger i = 0, n = [body length], count = 0;

	if (n == 0) {
		return NO;
	}
	ch = [body characterAtIndex:0];
	if (ch != '-' && ch != '*' && ch != '_') {
		return NO;
	}
	for (i = 0; i < n; i++) {
		unsigned int c = [body characterAtIndex:i];

		if (c == ch) {
			count++;
		} else if (!fn_md_is_space_or_tab(c)) {
			return NO;
		}
	}
	return count >= 3;
}

/* `- `, `* `, `+ `, or `1.` / `1)`: the marker, its width and (for ordered lists) its number. */
static BOOL fn_md_list_marker(NSString *body, BOOL *ordered, NSInteger *ordinal, NSUInteger *width,
			      NSString **delimiter)
{
	NSUInteger i, n = [body length];

	if (n == 0) {
		return NO;
	}
	if ([body characterAtIndex:0] == '-' || [body characterAtIndex:0] == '*' ||
	    [body characterAtIndex:0] == '+') {
		if (n == 1 || fn_md_is_space_or_tab([body characterAtIndex:1])) {
			*ordered = NO;
			*ordinal = 1;
			*width = 1;
			*delimiter = [body substringWithRange:NSMakeRange(0, 1)];
			return YES;
		}
		return NO;
	}
	for (i = 0; i < n && fn_md_is_digit([body characterAtIndex:i]) && i < 9; i++) {
	}
	if (i == 0 || i >= n) {
		return NO;
	}
	if ([body characterAtIndex:i] != '.' && [body characterAtIndex:i] != ')') {
		return NO;
	}
	if (i + 1 < n && !fn_md_is_space_or_tab([body characterAtIndex:i + 1])) {
		return NO;
	}
	*ordered = YES;
	*ordinal = (NSInteger)[[body substringToIndex:i] integerValue];
	*width = i + 1;
	*delimiter = [body substringWithRange:NSMakeRange(i, 1)];
	return YES;
}

static BOOL fn_md_setext(NSString *line, NSInteger *level)
{
	NSString *body = [line substringFromIndex:fn_md_indent(line, NULL)];
	unsigned int ch;
	NSUInteger i, n;

	if ([body length] == 0) {
		return NO;
	}
	ch = [body characterAtIndex:0];
	if (ch != '=' && ch != '-') {
		return NO;
	}
	n = [body length];
	for (i = 0; i < n; i++) {
		unsigned int c = [body characterAtIndex:i];

		if (c != ch && !fn_md_is_space_or_tab(c)) {
			return NO;
		}
	}
	*level = (ch == '=') ? 1 : 2;
	return YES;
}

static BOOL fn_md_starts_block(NSString *line)
{
	NSUInteger indent = fn_md_indent(line, NULL);
	NSString *body = [line substringFromIndex:indent];
	unsigned int fenceChar;
	NSUInteger fenceLen;
	NSString *info;
	NSInteger level;
	NSString *content;
	BOOL ordered;
	NSInteger ordinal;
	NSUInteger width;
	NSString *delimiter;

	if ([body length] == 0) {
		return NO;
	}
	if ([body characterAtIndex:0] == '>') {
		return YES;
	}
	if (fn_md_fence(body, &fenceChar, &fenceLen, &info)) {
		return YES;
	}
	if (fn_md_atx(body, &level, &content)) {
		return YES;
	}
	if (fn_md_thematic(body)) {
		return YES;
	}
	return fn_md_list_marker(body, &ordered, &ordinal, &width, &delimiter);
}

static void fn_md_sequence(fn_md_ctx *c, NSArray *lines, NSPresentationIntent *parent, NSDictionary *extra);

static void fn_md_list_item(fn_md_ctx *c, NSArray *itemLines, NSPresentationIntent *listIntent,
			    BOOL ordered, NSInteger ordinal, NSString *delimiter)
{
	NSUInteger start = [c->out length];
	NSMutableDictionary *itemExtra = [NSMutableDictionary dictionary];
	NSPresentationIntent *item;

	item = [NSPresentationIntent listItemIntentWithIdentity:fn_md_new_identity(c)
							ordinal:ordinal
					     nestedInsideIntent:listIntent];
	if (delimiter != nil) {
		[itemExtra setObject:delimiter forKey:NSListItemDelimiterAttributeName];
	}
	fn_md_sequence(c, itemLines, item, itemExtra);
}

static void fn_md_sequence(fn_md_ctx *c, NSArray *lines, NSPresentationIntent *parent, NSDictionary *extra)
{
	NSUInteger i = 0, n = [lines count];

	while (i < n) {
		NSString *line = [lines objectAtIndex:i];
		NSUInteger indentBytes = 0;
		NSUInteger indent = fn_md_indent(line, &indentBytes);
		NSString *body = fn_md_rtrim([line substringFromIndex:indent]);
		unsigned int fenceChar;
		NSUInteger fenceLen;
		NSString *info;
		NSInteger level;
		NSString *content;
		BOOL ordered;
		NSInteger ordinal;
		NSUInteger width;
		NSString *delimiter;
		NSUInteger start, end;

		c->currentLine = i + 1;
		if ([body length] == 0) {
			i++;
			continue;
		}

		/* a fenced code block */
		if (fn_md_fence(body, &fenceChar, &fenceLen, &info)) {
			NSMutableArray *contentLines = [NSMutableArray array];
			NSMutableString *text = [NSMutableString string];
			NSString *language = nil;
			NSUInteger j = i + 1;
			BOOL closed = NO;
			NSPresentationIntent *intent;

			while (j < n) {
				NSString *l = [lines objectAtIndex:j];

				if (fn_md_closes_fence(l, fenceChar, fenceLen)) {
					closed = YES;
					j++;
					break;
				}
				[contentLines addObject:[lines objectAtIndex:j]];
				j++;
			}
			if (!closed) {
				c->failed = YES;
				c->failLine = i + 1;
			}
			if ([info length] > 0) {
				NSRange space = [info rangeOfString:@" "];
				language = (space.location == NSNotFound) ? info : [info substringToIndex:space.location];
			}
			if ([contentLines count] > 0) {
				[text appendString:[contentLines componentsJoinedByString:@"\n"]];
				[text appendString:@"\n"];
			}
			start = [c->out length];
			fn_md_emit(c, text, 0, extra);
			end = [c->out length];
			intent = [NSPresentationIntent codeBlockIntentWithIdentity:fn_md_new_identity(c)
								     languageHint:language
								nestedInsideIntent:parent];
			fn_md_intent(c, intent, start, end);
			fn_md_source_position(c, start, end, i + 1, indentBytes + 1, j, 1);
			i = j;
			continue;
		}

		/* an ATX header */
		if (fn_md_atx(body, &level, &content)) {
			NSPresentationIntent *intent;

			start = [c->out length];
			fn_md_inline(c, content, 0, extra);
			fn_md_emit(c, @"\n", 0, extra);
			end = [c->out length];
			intent = [NSPresentationIntent headerIntentWithIdentity:fn_md_new_identity(c)
									 level:level
							      nestedInsideIntent:parent];
			fn_md_intent(c, intent, start, end);
			fn_md_source_position(c, start, end, i + 1, indentBytes + 1, i + 1,
					      (NSUInteger)(indentBytes + [line length] - indent + 1));
			i++;
			continue;
		}

		/* a thematic break */
		if (fn_md_thematic(body)) {
			NSPresentationIntent *intent;

			start = [c->out length];
			fn_md_emit(c, @"\n", 0, extra);
			end = [c->out length];
			intent = [NSPresentationIntent thematicBreakIntentWithIdentity:fn_md_new_identity(c)
								     nestedInsideIntent:parent];
			fn_md_intent(c, intent, start, end);
			i++;
			continue;
		}

		/* a block quote: the inner lines, with the marker and one space removed */
		if ([body characterAtIndex:0] == '>') {
			NSMutableArray *inner = [NSMutableArray array];
			NSPresentationIntent *intent;
			NSUInteger j = i;

			while (j < n) {
				NSString *l = [lines objectAtIndex:j];
				NSUInteger li = fn_md_indent(l, NULL);
				NSString *lb = [l substringFromIndex:li];

				if ([lb length] == 0 || [lb characterAtIndex:0] != '>') {
					break;
				}
				lb = [lb substringFromIndex:1];
				if ([lb length] > 0 && [lb characterAtIndex:0] == ' ') {
					lb = [lb substringFromIndex:1];
				}
				[inner addObject:lb];
				j++;
			}
			start = [c->out length];
			intent = [NSPresentationIntent blockQuoteIntentWithIdentity:fn_md_new_identity(c)
									  nestedInsideIntent:parent];
			fn_md_sequence(c, inner, intent, extra);
			i = j;
			continue;
		}

		/* a list: consecutive items of one kind */
		if (fn_md_list_marker(body, &ordered, &ordinal, &width, &delimiter)) {
			NSMutableArray *items = [NSMutableArray array];
			NSMutableArray *ordinals = [NSMutableArray array];
			NSMutableArray *delimiters = [NSMutableArray array];
			NSPresentationIntent *listIntent;
			NSUInteger j = i;

			while (j < n) {
				NSString *l = [lines objectAtIndex:j];
				NSUInteger li = fn_md_indent(l, NULL);
				NSString *lb = fn_md_rtrim([l substringFromIndex:li]);
				BOOL itemOrdered;
				NSInteger itemOrdinal;
				NSUInteger itemWidth;
				NSString *itemDelimiter;
				NSMutableArray *itemLines = [NSMutableArray array];
				NSUInteger k;

				if (!fn_md_list_marker(lb, &itemOrdered, &itemOrdinal, &itemWidth, &itemDelimiter)) {
					break;
				}
				if (itemOrdered != ordered) {
					break;
				}
				{
					NSUInteger after = itemWidth;

					if (after < [lb length] &&
					    fn_md_is_space_or_tab([lb characterAtIndex:after])) {
						after++;
					}
					[itemLines addObject:[lb substringFromIndex:after]];
				}
				k = j + 1;
				while (k < n) {
					NSString *next = [lines objectAtIndex:k];
					NSUInteger ni = fn_md_indent(next, NULL);
					NSString *nb = [next substringFromIndex:ni];

					if ([nb length] == 0) {
						/* a blank line ends the item unless the next line is indented into it */
						if (k + 1 < n) {
							NSUInteger nni = fn_md_indent([lines objectAtIndex:k + 1], NULL);
							if (nni > 0) {
								[itemLines addObject:@""];
								k++;
								continue;
							}
						}
						break;
					}
					if (ni == 0) {
						BOOL nextOrdered;
						NSInteger nextOrdinal;
						NSUInteger nextWidth;
						NSString *nextDelimiter;

						if (fn_md_list_marker(nb, &nextOrdered, &nextOrdinal, &nextWidth,
								      &nextDelimiter)) {
							break;
						}
						if (fn_md_starts_block(next)) {
							break;
						}
						[itemLines addObject:nb];
						k++;
						continue;
					}
					/* an indented continuation: dedent by the marker width and one space */
					{
						NSUInteger drop = itemWidth + 1;
						NSUInteger t = 0;

						while (t < [next length] && t < drop &&
						       fn_md_is_space_or_tab([next characterAtIndex:t])) {
							t++;
						}
						[itemLines addObject:[next substringFromIndex:t]];
					}
					k++;
				}
				[items addObject:itemLines];
				[ordinals addObject:[NSNumber numberWithInteger:itemOrdinal]];
				[delimiters addObject:itemDelimiter];
				j = k;
			}
			start = [c->out length];
			listIntent = ordered
				? [NSPresentationIntent orderedListIntentWithIdentity:fn_md_new_identity(c)
								 nestedInsideIntent:parent]
				: [NSPresentationIntent unorderedListIntentWithIdentity:fn_md_new_identity(c)
								   nestedInsideIntent:parent];
			{
				NSUInteger m;

				for (m = 0; m < [items count]; m++) {
					fn_md_list_item(c, [items objectAtIndex:m], listIntent, ordered,
							[[ordinals objectAtIndex:m] integerValue],
							[delimiters objectAtIndex:m]);
				}
			}
			end = [c->out length];
			i = j;
			continue;
		}

		/* a paragraph, or a setext header when the next line underlines it */
		{
			NSMutableArray *para = [NSMutableArray array];
			NSString *text;
			NSPresentationIntent *intent;
			NSUInteger j = i;
			NSInteger setextLevel = 0;

			while (j < n) {
				NSString *l = [lines objectAtIndex:j];

				if (fn_md_blank(l)) {
					break;
				}
				if (j > i && fn_md_starts_block(l)) {
					break;
				}
				if (j > i && fn_md_setext(l, &setextLevel)) {
					break;
				}
				[para addObject:l];
				j++;
			}
			if ([para count] == 0) {
				i++;
				continue;
			}
			start = [c->out length];
			text = [para componentsJoinedByString:@"\n"];
			text = fn_md_rtrim(text);
			fn_md_inline(c, text, 0, extra);
			end = [c->out length];
			if (setextLevel > 0 && [para count] == 1) {
				intent = [NSPresentationIntent headerIntentWithIdentity:fn_md_new_identity(c)
										 level:setextLevel
									 nestedInsideIntent:parent];
			} else {
				intent = [NSPresentationIntent paragraphIntentWithIdentity:fn_md_new_identity(c)
									 nestedInsideIntent:parent];
			}
			fn_md_intent(c, intent, start, end);
			fn_md_source_position(c, start, end, i + 1, 1, j, 1);
			fn_md_emit(c, @"\n", 0, extra);
			i = j + ((setextLevel > 0) ? 1 : 0);
			continue;
		}
	}
}

/* Every "\r\n" and every lone "\r" becomes a "\n", so the line split is the document. */
static NSString *fn_md_normalize_newlines(NSString *source)
{
	NSMutableString *out = [NSMutableString stringWithString:source];

	[out replaceOccurrencesOfString:@"\r\n" withString:@"\n" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"\r" withString:@"\n" options:0 range:NSMakeRange(0, [out length])];
	return out;
}

/* InlineOnly's whitespace rule: every run of spaces, tabs and newlines becomes one space. */
static NSString *fn_md_collapse_whitespace(NSString *text)
{
	NSMutableString *out = [NSMutableString string];
	NSUInteger i, n = [text length];
	BOOL pending = NO;

	for (i = 0; i < n; i++) {
		unsigned int c = [text characterAtIndex:i];

		if (c == ' ' || c == '\t' || c == '\n') {
			pending = YES;
			continue;
		}
		if (pending && [out length] > 0) {
			[out appendString:@" "];
		}
		pending = NO;
		[out appendString:[text substringWithRange:NSMakeRange(i, 1)]];
	}
	return out;
}

static NSError *fn_md_error(NSUInteger line, NSString *reason)
{
	NSMutableDictionary *info = [NSMutableDictionary dictionary];

	[info setObject:reason forKey:NSLocalizedDescriptionKey];
	[info setObject:[NSNumber numberWithUnsignedInteger:line] forKey:@"FNMarkdownErrorLine"];
	return [NSError errorWithDomain:@"FNMarkdownErrorDomain" code:1 userInfo:info];
}

/* THE ONE DOOR THE THREE INITIALISERS SHARE. */
static NSAttributedString *fn_md_import(NSString *source,
					NSAttributedStringMarkdownParsingOptions *options,
					NSURL *baseURL,
					NSError **error)
{
	fn_md_ctx ctx;
	NSMutableAttributedString *out;
	NSAttributedString *result;
	NSAttributedStringMarkdownParsingOptions *opts = options;

	memset(&ctx, 0, sizeof(ctx));
	if (opts == nil) {
		opts = [[[NSAttributedStringMarkdownParsingOptions alloc] init] autorelease];
	}
	out = [[NSMutableAttributedString alloc] initWithString:@""];
	ctx.out = out;
	ctx.baseURL = baseURL;
	ctx.opts = opts;
	ctx.currentLine = 1;
	{
		NSString *text = fn_md_normalize_newlines(source);
		NSAttributedStringMarkdownInterpretedSyntax syntax = [opts interpretedSyntax];

		if (syntax == NSAttributedStringMarkdownInterpretedSyntaxInlineOnly ||
		    syntax == NSAttributedStringMarkdownInterpretedSyntaxInlineOnlyPreservingWhitespace) {
			if (syntax == NSAttributedStringMarkdownInterpretedSyntaxInlineOnly) {
				text = fn_md_collapse_whitespace(text);
			}
			fn_md_inline(&ctx, text, 0, nil);
		} else {
			{
				NSMutableArray *lines = [NSMutableArray arrayWithArray:
							[text componentsSeparatedByString:@"\n"]];

				if ([lines count] > 0 && [[lines lastObject] length] == 0) {
					[lines removeLastObject];
				}
				fn_md_sequence(&ctx, lines, nil, nil);
			}
		}
	}
	if (ctx.failed &&
	    [opts failurePolicy] == NSAttributedStringMarkdownParsingFailureReturnError) {
		if (error != NULL) {
			*error = fn_md_error(ctx.failLine, @"the markdown source is not valid");
		}
		result = nil;
	} else {
		result = [[out copy] autorelease];
	}
	[out release];
	return result;
}



/* ================================================================================================
 * THE THREE DOORS (§62.107). Each parses and hands the result to -initWithAttributedString:, then RELEASES the
 * object the caller started with — the standard shape for an initialiser that returns a substitute, and the
 * only correct one here: an attributed string cannot be re-seated in place.
 */
@implementation NSAttributedString (NSAttributedStringMarkdown)

- (nullable instancetype)initWithMarkdownString:(NSString *)markdownString
					options:(nullable NSAttributedStringMarkdownParsingOptions *)options
					baseURL:(nullable NSURL *)baseURL
					  error:(NSError * _Nullable * _Nullable)error
{
	NSAttributedString *parsed = fn_md_import(markdownString, options, baseURL, error);
	NSAttributedString *result;

	if (parsed == nil) {
		[self release];
		return nil;
	}
	result = [[NSAttributedString alloc] initWithAttributedString:parsed];
	[self release];
	return result;
}

- (nullable instancetype)initWithMarkdown:(NSData *)markdown
				  options:(nullable NSAttributedStringMarkdownParsingOptions *)options
				  baseURL:(nullable NSURL *)baseURL
				    error:(NSError * _Nullable * _Nullable)error
{
	NSString *source = [[[NSString alloc] initWithData:markdown encoding:NSUTF8StringEncoding] autorelease];

	if (source == nil) {
		if (error != NULL) {
			*error = fn_md_error(0, @"the markdown data is not valid UTF-8");
		}
		[self release];
		return nil;
	}
	return [self initWithMarkdownString:source options:options baseURL:baseURL error:error];
}

- (nullable instancetype)initWithContentsOfMarkdownFileAtURL:(NSURL *)url
						     options:(nullable NSAttributedStringMarkdownParsingOptions *)options
						       error:(NSError * _Nullable * _Nullable)error
{
	NSData *data = [NSData dataWithContentsOfURL:url options:0 error:error];

	if (data == nil) {
		[self release];
		return nil;
	}
	/* the file's own URL is the base, which is what a relative link inside it means */
	return [self initWithMarkdown:data options:options baseURL:url error:error];
}

@end
