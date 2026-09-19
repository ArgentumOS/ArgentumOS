/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsjsonserialization.m — a recursive-descent parser and a writer (W2h).
 *
 * THE PARSER READS UTF-8 and BUILDS UTF-16 STRINGS, which is what this library's strings ARE: a \u
 * escape arrives as one or two UTF-16 units and goes straight into -initWithCharacters:length:, with
 * no re-encoding in between. That is the one place where our storage choice makes a parser simpler
 * rather than harder, and it is worth noticing.
 */
#import <foundation/NSJSONSerialization.h>
#import <foundation/NSString.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSNull.h>
#import <foundation/NSNumber.h>
#import <foundation/NSException.h>
#include <stdlib.h>	/* malloc/free/strtod */
#include <string.h>
#include <math.h>

typedef struct {
	const unsigned char *bytes;
	unsigned long length;
	unsigned long at;
	NSJSONReadingOptions options;
	const char *failure;		/* set by the first thing that goes wrong */
} fn_json_reader;

static id fn_json_value(fn_json_reader *reader);

static void fn_json_skip_whitespace(fn_json_reader *reader)
{
	while (reader->at < reader->length) {
		unsigned char c = reader->bytes[reader->at];

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			reader->at++;
			continue;
		}
		break;
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
 * library's NSString already understands - the same representation the constant-string door uses. */
static NSString *fn_json_string(fn_json_reader *reader)
{
	NSMutableArray *units = [[[NSMutableArray alloc] init] autorelease];
	NSMutableString *out;
	unichar *buffer;
	unsigned long count;

	if (reader->at >= reader->length || reader->bytes[reader->at] != '"') {
		reader->failure = "expected a string";
		return nil;
	}
	reader->at++;
	while (reader->at < reader->length && reader->bytes[reader->at] != '"') {
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
			case 'b': [units addObject:[NSNumber numberWithInt:8]]; break;
			case 'f': [units addObject:[NSNumber numberWithInt:12]]; break;
			case 'n': [units addObject:[NSNumber numberWithInt:10]]; break;
			case 'r': [units addObject:[NSNumber numberWithInt:13]]; break;
			case 't': [units addObject:[NSNumber numberWithInt:9]]; break;
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
			default:
				reader->failure = "unknown string escape";
				return nil;
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

static id fn_json_number(fn_json_reader *reader)
{
	unsigned long start = reader->at;
	BOOL isDouble = NO;

	if (reader->at < reader->length &&
	    (reader->bytes[reader->at] == '-' || reader->bytes[reader->at] == '+')) {
		reader->at++;
	}
	while (reader->at < reader->length) {
		unsigned char c = reader->bytes[reader->at];

		if (c >= '0' && c <= '9') {
			reader->at++;
			continue;
		}
		if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
			isDouble = (c == '.' || c == 'e' || c == 'E');
			reader->at++;
			continue;
		}
		break;
	}
	if (reader->at == start) {
		reader->failure = "expected a value";
		return nil;
	}
	{
		char *text = (char *)malloc(reader->at - start + 1);
		double value;

		memcpy(text, reader->bytes + start, reader->at - start);
		text[reader->at - start] = '\0';
		value = strtod(text, NULL);
		free(text);
		if (isDouble) {
			return [NSNumber numberWithDouble:value];
		}
		return [NSNumber numberWithLongLong:(long long)value];
	}
}

static id fn_json_value(fn_json_reader *reader)
{
	fn_json_skip_whitespace(reader);
	if (reader->at >= reader->length) {
		reader->failure = "the JSON ended where a value was expected";
		return nil;
	}
	switch (reader->bytes[reader->at]) {
	case '{': {
		NSMutableDictionary *out = [[[NSMutableDictionary alloc] init] autorelease];

		reader->at++;
		for (;;) {
			NSString *key;

			fn_json_skip_whitespace(reader);
			if (reader->at < reader->length && reader->bytes[reader->at] == '}') {
				reader->at++;
				break;
			}
			key = fn_json_string(reader);
			if (key == nil) {
				return nil;
			}
			fn_json_skip_whitespace(reader);
			if (reader->at >= reader->length || reader->bytes[reader->at] != ':') {
				reader->failure = "expected a colon after an object key";
				return nil;
			}
			reader->at++;
			{
				id value = fn_json_value(reader);

				if (reader->failure != NULL) {
					return nil;
				}
				[out setObject:value forKey:key];
			}
			fn_json_skip_whitespace(reader);
			if (reader->at < reader->length && reader->bytes[reader->at] == ',') {
				reader->at++;
				continue;
			}
		}
		return ((reader->options & NSJSONReadingMutableContainers) != 0) ? out
			: (id)[NSDictionary dictionaryWithDictionary:out];
	}
	case '[': {
		NSMutableArray *out = [[[NSMutableArray alloc] init] autorelease];

		reader->at++;
		for (;;) {
			fn_json_skip_whitespace(reader);
			if (reader->at < reader->length && reader->bytes[reader->at] == ']') {
				reader->at++;
				break;
			}
			{
				id value = fn_json_value(reader);

				if (reader->failure != NULL) {
					return nil;
				}
				[out addObject:value];
			}
			fn_json_skip_whitespace(reader);
			if (reader->at < reader->length && reader->bytes[reader->at] == ',') {
				reader->at++;
				continue;
			}
		}
		return ((reader->options & NSJSONReadingMutableContainers) != 0) ? out
			: (id)[NSArray arrayWithArray:out];
	}
	case '"':
		return fn_json_string(reader);
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
	for (unsigned long i = 0; i < count; i++) {
		id key = [keys objectAtIndex:i];

		if (i > 0) {
			[writer->out appendString:@","];
		}
		if ((writer->options & NSJSONWritingPrettyPrinted) != 0) {
			[writer->out appendString:@" "];
		}
		fn_json_write_string(writer, key);
		[writer->out appendString:@":"];
		if ((writer->options & NSJSONWritingPrettyPrinted) != 0) {
			[writer->out appendString:@" "];
		}
		fn_json_write(writer, [dictionary objectForKey:key]);
	}
	[writer->out appendString:@"}"];
}

static void fn_json_write_array(fn_json_writer *writer, NSArray *array)
{
	unsigned long count = [array count];

	[writer->out appendString:@"["];
	for (unsigned long i = 0; i < count; i++) {
		if (i > 0) {
			[writer->out appendString:@","];
		}
		if ((writer->options & NSJSONWritingPrettyPrinted) != 0) {
			[writer->out appendString:@" "];
		}
		fn_json_write(writer, [array objectAtIndex:i]);
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
	result = fn_json_value(&reader);
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
	if ((options & NSJSONReadingAllowFragments) == 0 &&
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
+ (BOOL)isValidJSONObject:(id)object
{
	if (object == nil || object == [NSNull null]) {
		return NO;
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
			if (![self isValidJSONObject:[object objectAtIndex:i]]) {
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
			if (![self isValidJSONObject:[object objectForKey:key]]) {
				return NO;
			}
		}
		return YES;
	}
	return NO;
}

@end
