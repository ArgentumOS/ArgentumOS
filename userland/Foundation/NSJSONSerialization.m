/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSJSONSerialization.m — a recursive-descent parser and a writer (W2h).
 *
 * THE PARSER READS UTF-8 and BUILDS UTF-16 STRINGS, which is what this library's strings ARE: a \u
 * escape arrives as one or two UTF-16 units and goes straight into -initWithCharacters:length:, with
 * no re-encoding in between. That is the one place where our storage choice makes a parser simpler
 * rather than harder, and it is worth noticing.
 */
#import <Foundation/NSJSONSerialization.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSException.h>
#include <stdlib.h>	/* malloc/free/strtod */
#include <string.h>
#include <math.h>
#include <errno.h>	/* ERANGE, for an integer too large to be one */

typedef struct {
	const unsigned char *bytes;
	unsigned long length;
	unsigned long at;
	NSJSONReadingOptions options;
	const char *failure;		/* set by the first thing that goes wrong */
} fn_json_reader;

static id fn_json_value(fn_json_reader *reader);

/* WHICH GRAMMAR THE READER IS SPEAKING. json5Allowed is not a second parser: JSON5 is a SUPERSET of
 * JSON, so this is one recursive-descent reader whose every deviation is behind this question - and
 * the question is asked in one place per deviation rather than once at the top, so a reader that
 * forgot one would be caught by the probe rather than by a coincidence. */
static BOOL fn_json_speaks_json5(fn_json_reader *reader)
{
	return (reader->options & NSJSONReadingJSON5Allowed) != 0;
}

/* BLANK SPACE, AND - IN JSON5 - THE TWO COMMENT FORMS.
 *
 * THE UNICODE BLANK SPACE IS NOT DECORATION: JSON5 counts the byte order mark, the line separators,
 * the no-break space and the Unicode space separators as whitespace, and a reader that skipped only
 * ASCII blank space would refuse documents its own grammar calls legal. The set below is JSON5's
 * (ES5's Whitespace plus LineTerminator), which is reachable from the ASCII and BMP bytes listed.
 */
static void fn_json_skip_whitespace(fn_json_reader *reader)
{
	BOOL json5 = fn_json_speaks_json5(reader);

	for (;;) {
		while (reader->at < reader->length) {
			unsigned char c = reader->bytes[reader->at];

			if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
				reader->at++;
				continue;
			}
			if (json5 && c == 0xC2 && reader->at + 1 < reader->length &&
			    (reader->bytes[reader->at + 1] == 0xA0 || reader->bytes[reader->at + 1] == 0x85)) {
				reader->at += 2;	/* U+00A0 no-break space, U+0085 next line */
				continue;
			}
			if (json5 && c == 0xE2 && reader->at + 2 < reader->length &&
			    reader->bytes[reader->at + 1] == 0x80 &&
			    (reader->bytes[reader->at + 2] == 0x80 || reader->bytes[reader->at + 2] == 0x81 ||
			     reader->bytes[reader->at + 2] == 0x82 || reader->bytes[reader->at + 2] == 0x83 ||
			     reader->bytes[reader->at + 2] == 0x84 || reader->bytes[reader->at + 2] == 0x85 ||
			     reader->bytes[reader->at + 2] == 0x86 || reader->bytes[reader->at + 2] == 0x87 ||
			     reader->bytes[reader->at + 2] == 0x89 || reader->bytes[reader->at + 2] == 0x8A ||
			     reader->bytes[reader->at + 2] == 0xA8 || reader->bytes[reader->at + 2] == 0xA9 ||
			     reader->bytes[reader->at + 2] == 0xAF)) {
				reader->at += 3;	/* U+2000-200A, U+2028, U+2029, U+202F */
				continue;
			}
			if (json5 && c == 0xEF && reader->at + 2 < reader->length &&
			    reader->bytes[reader->at + 1] == 0xBB && reader->bytes[reader->at + 2] == 0xBF) {
				reader->at += 3;	/* U+FEFF, the byte order mark */
				continue;
			}
			break;
		}
		if (!json5 || reader->at + 1 >= reader->length || reader->bytes[reader->at] != '/') {
			return;
		}
		/* A LINE COMMENT ENDS AT A LINE TERMINATOR, and JSON5 counts U+2028 and U+2029 as one, so
		 * the byte scan stops at their first byte as well as at the two ASCII ones. */
		if (reader->bytes[reader->at + 1] == '/') {
			reader->at += 2;
			while (reader->at < reader->length && reader->bytes[reader->at] != '\n') {
				if (reader->bytes[reader->at] == 0xE2 && reader->at + 2 < reader->length &&
				    reader->bytes[reader->at + 1] == 0x80 &&
				    (reader->bytes[reader->at + 2] == 0xA8 ||
				     reader->bytes[reader->at + 2] == 0xA9)) {
					break;
				}
				reader->at++;
			}
			continue;
		}
		if (reader->bytes[reader->at + 1] == '*') {
			reader->at += 2;
			for (;;) {
				if (reader->at + 1 >= reader->length) {
					reader->failure = "a block comment was never closed";
					return;
				}
				if (reader->bytes[reader->at] == '*' && reader->bytes[reader->at + 1] == '/') {
					reader->at += 2;
					break;
				}
				reader->at++;
			}
			continue;
		}
		/* A SOLITARY `/` IS NOT BLANK SPACE, and returning here is what keeps the failure a
		 * parser's ("expected a value") rather than a silently skipped character. */
		return;
	}
}

static int fn_json_hex(unsigned char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* A STRING IS READ INTO UTF-16 UNITS, so a surrogate PAIR from \u escapes is two units that this
 * library's NSString already understands - the same representation the constant-string door uses.
 *
 * THE QUOTE IS A PARAMETER, because JSON5 has two of them. It is also the only way this routine keeps
 * its promise about JSON: a caller may only pass `'` when the reader speaks JSON5, and the `\'` escape
 * is refused outright in strict mode rather than being a no-op, so a document that swaps its quotes
 * fails at the first character it swapped.
 */
static NSString *fn_json_string(fn_json_reader *reader, unsigned char quote)
{
	NSMutableArray *units = [[[NSMutableArray alloc] init] autorelease];
	NSMutableString *out;
	unichar *buffer;
	unsigned long count;
	BOOL json5 = fn_json_speaks_json5(reader);

	if (reader->at >= reader->length || reader->bytes[reader->at] != quote) {
		reader->failure = "expected a string";
		return nil;
	}
	reader->at++;
	while (reader->at < reader->length && reader->bytes[reader->at] != quote) {
		unsigned char c = reader->bytes[reader->at++];

		if (c == '\\') {
			unsigned char escape;

			if (reader->at >= reader->length) {
				reader->failure = "a string ended inside an escape";
				return nil;
			}
			escape = reader->bytes[reader->at++];
			switch (escape) {
			case '"': case '\\': case '/':
				[units addObject:[NSNumber numberWithInt:(int)escape]];
				break;
			case '\'':
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				[units addObject:[NSNumber numberWithInt:'\'']];
				break;
			case 'b': [units addObject:[NSNumber numberWithInt:8]]; break;
			case 'f': [units addObject:[NSNumber numberWithInt:12]]; break;
			case 'n': [units addObject:[NSNumber numberWithInt:10]]; break;
			case 'r': [units addObject:[NSNumber numberWithInt:13]]; break;
			case 't': [units addObject:[NSNumber numberWithInt:9]]; break;
			/* JSON5'S THREE EXTRA SIMPLE ESCAPES: the vertical tab, a NUL, and a two-digit
			 * hexadecimal character - the last of which is the one a program uses when it wants
			 * a character it cannot type. */
			case 'v':
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				[units addObject:[NSNumber numberWithInt:11]];
				break;
			case '0':
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				[units addObject:[NSNumber numberWithInt:0]];
				break;
			case 'x': {
				int value = 0;
				int i;

				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				if (reader->at + 2 > reader->length) {
					reader->failure = "a \\x escape ran off the end";
					return nil;
				}
				for (i = 0; i < 2; i++) {
					int digit = fn_json_hex(reader->bytes[reader->at + (unsigned long)i]);

					if (digit < 0) {
						reader->failure = "a \\x escape is not two hex digits";
						return nil;
					}
					value = (value << 4) | digit;
				}
				reader->at += 2;
				[units addObject:[NSNumber numberWithInt:value]];
				break;
			}
			case 'u': {
				int value = 0;
				int i;

				if (reader->at + 4 > reader->length) {
					reader->failure = "a \\u escape ran off the end";
					return nil;
				}
				for (i = 0; i < 4; i++) {
					int digit = fn_json_hex(reader->bytes[reader->at + (unsigned long)i]);

					if (digit < 0) {
						reader->failure = "a \\u escape is not four hex digits";
						return nil;
					}
					value = (value << 4) | digit;
				}
				reader->at += 4;
				[units addObject:[NSNumber numberWithInt:value]];
				break;
			}
			/* A LINE CONTINUATION: the backslash, the line terminator, and NOTHING. It gets a
			 * case of its own so that it cannot fall to the any-character rule below, where it
			 * would put a newline into the string instead of removing one. */
			case '\n':
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				break;
			case '\r':
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				if (reader->at < reader->length && reader->bytes[reader->at] == '\n') {
					reader->at++;
				}
				break;
			default:
				/* "\" BEFORE ANY OTHER CHARACTER IS THAT CHARACTER (JSON5), so `"\q"` is the
				 * letter q - where strict JSON refuses the escape. */
				if (!json5) {
					reader->failure = "unknown string escape";
					return nil;
				}
				[units addObject:[NSNumber numberWithInt:(int)escape]];
				break;
			}
			continue;
		}
		if (c < 0x20) {
			reader->failure = "a control character was not escaped";
			return nil;
		}
		/* UTF-8: the leading byte says how many follow, and the units are assembled here rather
		 * than re-encoded later. */
		if (c < 0x80) {
			[units addObject:[NSNumber numberWithInt:(int)c]];
		} else if ((c & 0xE0) == 0xC0 && reader->at < reader->length) {
			int value = ((c & 0x1F) << 6) | (reader->bytes[reader->at++] & 0x3F);

			[units addObject:[NSNumber numberWithInt:value]];
		} else if ((c & 0xF0) == 0xE0 && reader->at + 1 < reader->length) {
			int value = ((c & 0x0F) << 12) | ((reader->bytes[reader->at] & 0x3F) << 6) |
				    (reader->bytes[reader->at + 1] & 0x3F);

			reader->at += 2;
			[units addObject:[NSNumber numberWithInt:value]];
		} else if ((c & 0xF8) == 0xF0 && reader->at + 2 < reader->length) {
			unsigned long point = ((unsigned long)(c & 0x07) << 18) |
					      ((unsigned long)(reader->bytes[reader->at] & 0x3F) << 12) |
					      ((unsigned long)(reader->bytes[reader->at + 1] & 0x3F) << 6) |
					      (unsigned long)(reader->bytes[reader->at + 2] & 0x3F);

			reader->at += 3;
			/* AN ASTRAL CODE POINT IS A SURROGATE PAIR, which is what the string's units are. */
			point -= 0x10000;
			[units addObject:[NSNumber numberWithInt:(int)(0xD800 + (point >> 10))]];
			[units addObject:[NSNumber numberWithInt:(int)(0xDC00 + (point & 0x3FF))]];
		} else {
			reader->failure = "the string is not valid UTF-8";
			return nil;
		}
	}
	if (reader->at >= reader->length) {
		reader->failure = "a string was never closed";
		return nil;
	}
	reader->at++;	/* the closing quote */
	count = [units count];
	buffer = (unichar *)malloc(count * sizeof(unichar));
	for (unsigned long i = 0; i < count; i++) {
		buffer[i] = (unichar)[[units objectAtIndex:i] intValue];
	}
	out = [[[NSMutableString alloc] initWithCharacters:buffer length:count] autorelease];
	free(buffer);
	if ((reader->options & NSJSONReadingMutableLeaves) != 0) {
		return out;
	}
	return [NSString stringWithString:out];
}

/* AN UNQUOTED KEY IS AN ECMASCRIPT IDENTIFIER NAME - and this is the one place where JSON5 is
 * supported IN PART, so the boundary is stated here rather than left to be discovered:
 *
 * ES5's IdentifierStart is "any Unicode letter, `$` or `_`", and this library has no Unicode
 * identifier table (ICU can classify a code point, but the ES5 identifier classes are not ICU's).
 * So the class implemented is ASCII's - a letter, `$` or `_` to start, digits after that - PLUS
 * JSON5's \u escapes, whose decoded code point is checked against the same ASCII class. A key written
 * with a non-ASCII letter is REFUSED with a reason, and the probe asserts that refusal by name.
 * Guessing which code points ES5 admits would be inventing a grammar, which is the one thing this
 * library refuses to do more strongly than it refuses an API.
 */
static NSString *fn_json_identifier(fn_json_reader *reader)
{
	NSMutableArray *units = [[[NSMutableArray alloc] init] autorelease];
	unichar *buffer;
	unsigned long count;
	BOOL first = YES;

	for (;;) {
		unsigned char c;
		int value = -1;

		if (reader->at >= reader->length) {
			break;
		}
		c = reader->bytes[reader->at];
		if (c == '\\') {
			int i;

			if (reader->at + 1 >= reader->length || reader->bytes[reader->at + 1] != 'u') {
				reader->failure = "an identifier escape is not a \\u escape";
				return nil;
			}
			if (reader->at + 6 > reader->length) {
				reader->failure = "an identifier escape ran off the end";
				return nil;
			}
			value = 0;
			for (i = 0; i < 4; i++) {
				int digit = fn_json_hex(reader->bytes[reader->at + 2 + (unsigned long)i]);

				if (digit < 0) {
					reader->failure = "an identifier escape is not four hex digits";
					return nil;
				}
				value = (value << 4) | digit;
			}
			reader->at += 6;
		} else if (c == '$' || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			   (!first && c >= '0' && c <= '9')) {
			value = (int)c;
			reader->at++;
		} else {
			break;
		}
		if (value >= 0x80 || (value != '$' && value != '_' &&
		    !(value >= 'a' && value <= 'z') && !(value >= 'A' && value <= 'Z') &&
		    !(!first && value >= '0' && value <= '9'))) {
			reader->failure = "a non-ASCII identifier character is not in this library's table";
			return nil;
		}
		[units addObject:[NSNumber numberWithInt:value]];
		first = NO;
	}
	if ([units count] == 0) {
		reader->failure = "expected an object key";
		return nil;
	}
	count = [units count];
	buffer = (unichar *)malloc(count * sizeof(unichar));
	for (unsigned long i = 0; i < count; i++) {
		buffer[i] = (unichar)[[units objectAtIndex:i] intValue];
	}
	{
		NSString *out = [NSString stringWithCharacters:buffer length:count];

		free(buffer);
		return out;
	}
}

/* A NUMBER IS SCANNED BY ITS GRAMMAR, not handed to strtod's taste.
 *
 * THE STRICT PATH CHANGED HERE, and the file that does it should say why: strtod - and the character
 * class that fed it - accepted "+1", "01", ".5" and "5.", so the two grammars were NOT
 * distinguishable and JSON5's number additions could not have been observed at all. Now:
 *
 *   strict JSON (RFC 8259)  -? (0 | [1-9][0-9]*) ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?
 *   JSON5                   the same, plus a leading '+', a leading or trailing '.',
 *                           hexadecimal (0x...), Infinity, -Infinity and NaN
 *
 * A LEADING ZERO IS REFUSED IN BOTH. ES5's DecimalIntegerLiteral - which JSON5's number rule is
 * written in terms of - has no room for one either, so admitting "01" would have been our invention
 * rather than JSON5's.
 */
static id fn_json_number(fn_json_reader *reader)
{
	unsigned long start = reader->at;
	BOOL json5 = fn_json_speaks_json5(reader);
	BOOL negative = NO;
	BOOL floating = NO;
	unsigned long digits = 0;

	if (reader->at < reader->length &&
	    (reader->bytes[reader->at] == '-' || reader->bytes[reader->at] == '+')) {
		if (reader->bytes[reader->at] == '+') {
			if (!json5) {
				reader->failure = "a leading '+' is JSON5, not JSON";
				return nil;
			}
		} else {
			negative = YES;
		}
		reader->at++;
	}

	/* HEXADECIMAL (JSON5), read exactly before it becomes a double like every other number. */
	if (json5 && reader->at + 1 < reader->length && reader->bytes[reader->at] == '0' &&
	    (reader->bytes[reader->at + 1] == 'x' || reader->bytes[reader->at + 1] == 'X')) {
		unsigned long long value = 0;
		unsigned long hexStart;

		reader->at += 2;
		hexStart = reader->at;
		while (reader->at < reader->length && fn_json_hex(reader->bytes[reader->at]) >= 0) {
			value = (value << 4) | (unsigned long long)fn_json_hex(reader->bytes[reader->at]);
			reader->at++;
		}
		if (reader->at == hexStart) {
			reader->failure = "a hexadecimal number with no digits";
			return nil;
		}
		return [NSNumber numberWithDouble:(negative ? -(double)value : (double)value)];
	}

	/* Infinity AND NaN (JSON5), the two numbers JSON has no spelling for. NaN carries no sign in
	 * IEEE, so the sign accepted here is accepted and dropped. */
	if (json5 && reader->at + 8 <= reader->length &&
	    memcmp(reader->bytes + reader->at, "Infinity", 8) == 0) {
		reader->at += 8;
		return [NSNumber numberWithDouble:(negative ? -HUGE_VAL : HUGE_VAL)];
	}
	if (json5 && reader->at + 3 <= reader->length &&
	    memcmp(reader->bytes + reader->at, "NaN", 3) == 0) {
		reader->at += 3;
		return [NSNumber numberWithDouble:NAN];
	}

	while (reader->at < reader->length && reader->bytes[reader->at] >= '0' &&
	       reader->bytes[reader->at] <= '9') {
		if (digits == 0 && reader->bytes[reader->at] == '0' && reader->at + 1 < reader->length &&
		    reader->bytes[reader->at + 1] >= '0' && reader->bytes[reader->at + 1] <= '9') {
			reader->failure = "a number has a leading zero";
			return nil;
		}
		digits++;
		reader->at++;
	}

	/* THE FRACTIONAL PART: a leading '.' is JSON5's, a TRAILING one is too ("5." is a legal JSON5
	 * number), so the digits after the point are optional and only a BARE '.' is not a number. */
	if (reader->at < reader->length && reader->bytes[reader->at] == '.') {
		unsigned long fractionDigits = 0;

		if (digits == 0 && !json5) {
			reader->failure = "a leading '.' is JSON5, not JSON";
			return nil;
		}
		floating = YES;
		reader->at++;
		while (reader->at < reader->length && reader->bytes[reader->at] >= '0' &&
		       reader->bytes[reader->at] <= '9') {
			digits++;
			fractionDigits++;
			reader->at++;
		}
		/* RFC 8259 WANTS A DIGIT AFTER THE POINT - "1." is not a JSON number - while JSON5
		 * accepts a trailing point. The probe found this one: "[5.]" was parsed as 5 in strict
		 * mode, because the leading-dot rule above only covers the OTHER end of the same token. */
		if (fractionDigits == 0 && !json5) {
			reader->failure = "a '.' must be followed by a digit in JSON";
			return nil;
		}
	}
	if (digits == 0) {
		reader->failure = "expected a number";
		return nil;
	}

	/* THE EXPONENT, whose digits are NOT optional in either grammar. */
	if (reader->at < reader->length &&
	    (reader->bytes[reader->at] == 'e' || reader->bytes[reader->at] == 'E')) {
		BOOL exponentDigits = NO;

		floating = YES;
		reader->at++;
		if (reader->at < reader->length &&
		    (reader->bytes[reader->at] == '+' || reader->bytes[reader->at] == '-')) {
			reader->at++;
		}
		while (reader->at < reader->length && reader->bytes[reader->at] >= '0' &&
		       reader->bytes[reader->at] <= '9') {
			exponentDigits = YES;
			reader->at++;
		}
		if (!exponentDigits) {
			reader->failure = "an exponent with no digits";
			return nil;
		}
	}

	{
		char *text = (char *)malloc(reader->at - start + 1);
		char *end = NULL;
		double value;
		long long integer;

		memcpy(text, reader->bytes + start, reader->at - start);
		text[reader->at - start] = '\0';
		if (floating) {
			value = strtod(text, &end);
			free(text);
			return [NSNumber numberWithDouble:value];
		}
		/* AN INTEGER THAT DOES NOT FIT IS A DOUBLE rather than a wrapped one: strtoll says so with
		 * ERANGE, and answering the wrapped value would be a difference a program could see. */
		errno = 0;
		integer = strtoll(text, &end, 10);
		if (errno == ERANGE) {
			value = strtod(text, &end);
			free(text);
			return [NSNumber numberWithDouble:value];
		}
		free(text);
		return [NSNumber numberWithLongLong:integer];
	}
}

/* AN OBJECT BODY, WITH OR WITHOUT ITS BRACES: `terminator` is '}' for the ordinary form and 0 for
 * Apple's topLevelDictionaryAssumed, where the DOCUMENT is the body. One routine serves both so the
 * two forms cannot drift apart in their comma rules, their key rule or their mutability rule.
 *
 * AND THIS IS WHERE THE STRICT PATH CHANGED, in the same breath as JSON5's trailing comma: a MISSING
 * COMMA used to be accepted, because the old loop fell back to "read another member" whenever it saw
 * neither a comma nor the closing brace - so `[1 2]` and `{"a":1"b":2}` parsed in a reader that claims
 * to speak JSON. Now a member that follows another without a comma FAILS IN BOTH GRAMMARS, and a
 * TRAILING comma fails in strict JSON while being legal under json5Allowed. The two rules are one
 * change: JSON5's trailing comma is unobservable against a parser that accepts commas nowhere, which
 * is exactly how this was found.
 */
static id fn_json_object_body(fn_json_reader *reader, unsigned char terminator)
{
	NSMutableDictionary *out = [[[NSMutableDictionary alloc] init] autorelease];
	BOOL json5 = fn_json_speaks_json5(reader);
	BOOL afterComma = NO;
	BOOL haveMember = NO;

	for (;;) {
		NSString *key;
		id value;

		fn_json_skip_whitespace(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		if (terminator != 0) {
			if (reader->at < reader->length && reader->bytes[reader->at] == terminator) {
				if (afterComma && !json5) {
					reader->failure = "a trailing comma is not JSON";
					return nil;
				}
				reader->at++;
				break;
			}
		} else if (reader->at >= reader->length) {
			if (afterComma && !json5) {
				reader->failure = "a trailing comma is not JSON";
				return nil;
			}
			break;
		}
		if (haveMember && !afterComma) {
			reader->failure = "expected a comma between object members";
			return nil;
		}
		/* AN UNQUOTED KEY IS ASKED FOR BY NAME, so that a strict reader answers "expected a
		 * string" for `{a: 1}` rather than reading `a` as a number and failing later at a colon
		 * it never reaches. */
		if (reader->bytes[reader->at] == '"' ||
		    (json5 && reader->bytes[reader->at] == '\'')) {
			key = fn_json_string(reader, reader->bytes[reader->at]);
		} else if (json5) {
			key = fn_json_identifier(reader);
		} else {
			reader->failure = "expected a string";
			return nil;
		}
		if (key == nil) {
			return nil;
		}
		fn_json_skip_whitespace(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		if (reader->at >= reader->length || reader->bytes[reader->at] != ':') {
			reader->failure = "expected a colon after an object key";
			return nil;
		}
		reader->at++;
		value = fn_json_value(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		[out setObject:value forKey:key];
		haveMember = YES;
		afterComma = NO;
		fn_json_skip_whitespace(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		if (reader->at < reader->length && reader->bytes[reader->at] == ',') {
			reader->at++;
			afterComma = YES;
		}
	}
	return ((reader->options & NSJSONReadingMutableContainers) != 0) ? out
		: (id)[NSDictionary dictionaryWithDictionary:out];
}

/* AN ARRAY BODY, with the same two comma rules and for the same reasons. */
static id fn_json_array_body(fn_json_reader *reader)
{
	NSMutableArray *out = [[[NSMutableArray alloc] init] autorelease];
	BOOL json5 = fn_json_speaks_json5(reader);
	BOOL afterComma = NO;
	BOOL haveElement = NO;

	for (;;) {
		id value;

		fn_json_skip_whitespace(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		if (reader->at < reader->length && reader->bytes[reader->at] == ']') {
			if (afterComma && !json5) {
				reader->failure = "a trailing comma is not JSON";
				return nil;
			}
			reader->at++;
			break;
		}
		if (reader->at >= reader->length) {
			reader->failure = "an array was never closed";
			return nil;
		}
		if (haveElement && !afterComma) {
			reader->failure = "expected a comma between array elements";
			return nil;
		}
		value = fn_json_value(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		[out addObject:value];
		haveElement = YES;
		afterComma = NO;
		fn_json_skip_whitespace(reader);
		if (reader->failure != NULL) {
			return nil;
		}
		if (reader->at < reader->length && reader->bytes[reader->at] == ',') {
			reader->at++;
			afterComma = YES;
		}
	}
	return ((reader->options & NSJSONReadingMutableContainers) != 0) ? out
		: (id)[NSArray arrayWithArray:out];
}

static id fn_json_value(fn_json_reader *reader)
{
	fn_json_skip_whitespace(reader);
	if (reader->failure != NULL) {
		return nil;
	}
	if (reader->at >= reader->length) {
		reader->failure = "the JSON ended where a value was expected";
		return nil;
	}
	switch (reader->bytes[reader->at]) {
	case '{':
		reader->at++;
		return fn_json_object_body(reader, '}');
	case '[':
		reader->at++;
		return fn_json_array_body(reader);
	case '"':
		return fn_json_string(reader, '"');
	case '\'':
		if (!fn_json_speaks_json5(reader)) {
			reader->failure = "a single-quoted string is JSON5, not JSON";
			return nil;
		}
		return fn_json_string(reader, '\'');
	case 't':
		if (reader->at + 4 <= reader->length && memcmp(reader->bytes + reader->at, "true", 4) == 0) {
			reader->at += 4;
			return [NSNumber numberWithBool:YES];
		}
		break;
	case 'f':
		if (reader->at + 5 <= reader->length && memcmp(reader->bytes + reader->at, "false", 5) == 0) {
			reader->at += 5;
			return [NSNumber numberWithBool:NO];
		}
		break;
	case 'n':
		if (reader->at + 4 <= reader->length && memcmp(reader->bytes + reader->at, "null", 4) == 0) {
			reader->at += 4;
			return [NSNull null];
		}
		break;
	default:
		/* EVERYTHING ELSE IS A NUMBER, which is where the JSON5 literals Infinity and NaN are
		 * read too - they begin with letters this switch has no case for. */
		return fn_json_number(reader);
	}
	reader->failure = "not a JSON value";
	return nil;
}

/* ---------------------------------------------------------------- the writer */

typedef struct {
	NSMutableString *out;
	NSJSONWritingOptions options;
	unsigned long depth;
} fn_json_writer;

static void fn_json_write(fn_json_writer *writer, id object);

static BOOL fn_json_is_boolean(NSNumber *number)
{
	const char *type = [number objCType];

	return (type != NULL && (type[0] == 'c' || type[0] == 'C'));
}

static void fn_json_indent(fn_json_writer *writer)
{
	if ((writer->options & NSJSONWritingPrettyPrinted) == 0) {
		return;
	}
	[writer->out appendString:@"\n"];
	for (unsigned long i = 0; i < writer->depth; i++) {
		[writer->out appendString:@"  "];
	}
}

static void fn_json_write_string(fn_json_writer *writer, NSString *string)
{
	unsigned long i;
	unsigned long length = [string length];

	[writer->out appendString:@"\""];
	for (i = 0; i < length; i++) {
		unichar c = [string characterAtIndex:i];

		switch (c) {
		case '"': [writer->out appendString:@"\\\""]; continue;
		case '\\': [writer->out appendString:@"\\\\"]; continue;
		case '/': [writer->out appendString:@"\\/"]; continue;
		case 8: [writer->out appendString:@"\\b"]; continue;
		case 12: [writer->out appendString:@"\\f"]; continue;
		case 10: [writer->out appendString:@"\\n"]; continue;
		case 13: [writer->out appendString:@"\\r"]; continue;
		case 9: [writer->out appendString:@"\\t"]; continue;
		default: break;
		}
		if (c < 0x20) {
			[writer->out appendFormat:@"\\u%04x", (unsigned)c];
		} else {
			[writer->out appendFormat:@"%C", c];
		}
	}
	[writer->out appendString:@"\""];
}

static void fn_json_write_object(fn_json_writer *writer, NSDictionary *dictionary)
{
	NSArray *keys = [dictionary allKeys];
	unsigned long count;

	if ((writer->options & NSJSONWritingSortedKeys) != 0) {
		keys = [keys sortedArrayUsingSelector:@selector(compare:)];
	}
	count = [keys count];
	[writer->out appendString:@"{"];
	writer->depth++;
	for (unsigned long i = 0; i < count; i++) {
		id key = [keys objectAtIndex:i];

		if (i > 0) {
			[writer->out appendString:@","];
		}
		/* fn_json_indent IS THE PRETTY PATH AND IT WAS NEVER CALLED: a bare space stood in for it,
		 * so PrettyPrinted produced { "a": "a"} with no newline anywhere. It no-ops when the option
		 * is absent, which is what makes it safe to call unconditionally. */
		fn_json_indent(writer);
		fn_json_write_string(writer, key);
		[writer->out appendString:((writer->options & NSJSONWritingPrettyPrinted) != 0) ? @" : " : @":"];
		fn_json_write(writer, [dictionary objectForKey:key]);
	}
	writer->depth--;
	if (count > 0) {
		fn_json_indent(writer);
	}
	[writer->out appendString:@"}"];
}

static void fn_json_write_array(fn_json_writer *writer, NSArray *array)
{
	unsigned long count = [array count];

	[writer->out appendString:@"["];
	writer->depth++;
	for (unsigned long i = 0; i < count; i++) {
		if (i > 0) {
			[writer->out appendString:@","];
		}
		fn_json_indent(writer);
		fn_json_write(writer, [array objectAtIndex:i]);
	}
	writer->depth--;
	if (count > 0) {
		fn_json_indent(writer);
	}
	[writer->out appendString:@"]"];
}

static void fn_json_write(fn_json_writer *writer, id object)
{
	if (object == nil || object == [NSNull null]) {
		[writer->out appendString:@"null"];
		return;
	}
	if ([object isKindOfClass:[NSString class]]) {
		fn_json_write_string(writer, object);
		return;
	}
	if ([object isKindOfClass:[NSNumber class]]) {
		if (fn_json_is_boolean(object)) {
			[writer->out appendString:[object boolValue] ? @"true" : @"false"];
			return;
		}
		{
			double value = [object doubleValue];

			if (isnan(value) || isinf(value)) {
				/* THE RULES EXCLUDE THESE, and -isValidJSONObject: is what catches them first. */
				[writer->out appendString:@"null"];
				return;
			}
		}
		[writer->out appendString:[object description]];
		return;
	}
	if ([object isKindOfClass:[NSDictionary class]]) {
		fn_json_write_object(writer, object);
		return;
	}
	if ([object isKindOfClass:[NSArray class]]) {
		fn_json_write_array(writer, object);
		return;
	}
	[writer->out appendString:@"null"];
}

@implementation NSJSONSerialization

+ (id)JSONObjectWithData:(NSData *)data options:(NSJSONReadingOptions)options error:(NSError **)errorPtr
{
	fn_json_reader reader;
	id result = nil;

	if (data == nil) {
		if (errorPtr != NULL) {
			*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
		}
		return nil;
	}
	reader.bytes = (const unsigned char *)[data bytes];
	reader.length = [data length];
	reader.at = 0;
	reader.options = options;
	reader.failure = NULL;
	/* A UTF-8 BOM IS SKIPPED RATHER THAN REJECTED, which is what Apple's parser does: the byte order
	 * marks are legal in the five encodings the documentation names. */
	if (reader.length >= 3 && reader.bytes[0] == 0xEF && reader.bytes[1] == 0xBB && reader.bytes[2] == 0xBF) {
		reader.at = 3;
	}
	/* APPLE'S topLevelDictionaryAssumed: A DOCUMENT WITH NO ENCLOSING BRACES IS AN OBJECT BODY. The
	 * option is applied to the FIRST TOKEN rather than blindly, so it can only ADD a form: a document
	 * that begins with `{` or `[` parses exactly as it did with or without the option, which is the
	 * boundary the probe pins rather than assumes. */
	if ((options & NSJSONReadingTopLevelDictionaryAssumed) != 0) {
		unsigned long before = reader.at;

		fn_json_skip_whitespace(&reader);
		if (reader.failure == NULL && reader.at < reader.length &&
		    reader.bytes[reader.at] != '{' && reader.bytes[reader.at] != '[') {
			result = fn_json_object_body(&reader, 0);
		} else {
			reader.at = before;
		}
	}
	if (result == nil && reader.failure == NULL) {
		result = fn_json_value(&reader);
	}
	if (result == nil) {
		if (errorPtr != NULL) {
			*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
		}
		return nil;
	}
	fn_json_skip_whitespace(&reader);
	if (reader.at != reader.length) {
		if (errorPtr != NULL) {
			*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
		}
		return nil;
	}
	/* THE TOP LEVEL MUST BE A COLLECTION unless the fragments option allows otherwise, and that rule
	 * is the caller's to relax rather than ours to assume. */
	if ((options & NSJSONReadingFragmentsAllowed) == 0 &&
	    ![result isKindOfClass:[NSArray class]] && ![result isKindOfClass:[NSDictionary class]]) {
		if (errorPtr != NULL) {
			*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
		}
		return nil;
	}
	return result;
}

+ (NSData *)dataWithJSONObject:(id)object options:(NSJSONWritingOptions)options error:(NSError **)errorPtr
{
	fn_json_writer writer;

	if (![self isValidJSONObject:object]) {
		/* APPLE THROWS HERE RATHER THAN ANSWERING AN ERROR, and this library follows: the object was
		 * never representable, which is a programming error, and +isValidJSONObject: is the door a
		 * caller uses to ask first. */
		[NSException raise:NSInvalidArgumentException
			    format:@"+[NSJSONSerialization dataWithJSONObject:options:error:]: the object is not "
				   "valid JSON - ask +isValidJSONObject: first"];
		return nil;
	}
	writer.out = [[[NSMutableString alloc] init] autorelease];
	writer.options = options;
	writer.depth = 0;
	fn_json_write(&writer, object);
	return [writer.out dataUsingEncoding:NSUTF8StringEncoding];
}

/* THE SAME RULES THE WRITER RELIES ON, asked as a question: a collection at the top, leaves from the
 * five classes, keys all strings, and no NaN or infinity where a number appears. */
/*
 * THE RULES FOR A NESTED VALUE, which are not the rules for the top level. JSON HAS A NULL, so
 * [NSNull null] is a legal VALUE - the old single routine refused it and therefore called a dictionary
 * full of perfectly ordinary JSON invalid, which made +dataWithJSONObject: throw. A date is still out:
 * it is not a JSON type at all.
 */
static BOOL fn_json_value_is_valid(id object)
{
	if (object == nil) {
		return NO;
	}
	if ([object isKindOfClass:[NSNull class]]) {
		return YES;
	}
	if ([object isKindOfClass:[NSString class]]) {
		return YES;
	}
	if ([object isKindOfClass:[NSNumber class]]) {
		double value = [object doubleValue];

		return !(isnan(value) || isinf(value));
	}
	if ([object isKindOfClass:[NSArray class]]) {
		unsigned long i;

		for (i = 0; i < [object count]; i++) {
			if (!fn_json_value_is_valid([object objectAtIndex:i])) {
				return NO;
			}
		}
		return YES;
	}
	if ([object isKindOfClass:[NSDictionary class]]) {
		NSArray *keys = [object allKeys];
		unsigned long i;

		for (i = 0; i < [keys count]; i++) {
			id key = [keys objectAtIndex:i];

			if (![key isKindOfClass:[NSString class]]) {
				return NO;
			}
			if (!fn_json_value_is_valid([object objectForKey:key])) {
				return NO;
			}
		}
		return YES;
	}
	return NO;
}

+ (BOOL)isValidJSONObject:(id)object
{
	/*
	 * THE TOP LEVEL MUST BE AN ARRAY OR A DICTIONARY: a bare string, number or null only becomes legal
	 * with the fragment options, which is exactly why this is a separate question from "can I encode
	 * it". Everything below the top level is fn_json_value_is_valid's business.
	 */
	if (![object isKindOfClass:[NSArray class]] && ![object isKindOfClass:[NSDictionary class]]) {
		return NO;
	}
	return fn_json_value_is_valid(object);
}

@end
