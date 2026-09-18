/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsregularexpression.m — regular expressions as objects (F13.16). MANUAL OWNERSHIP.
 *
 * THE ONE PLACE THE TWO WORLDS MEET: Cocoa measures ranges in UTF-16 units and the engine counts
 * BYTES, so this file builds a map from UTF-16 indices to byte offsets once per call and converts
 * every range through it. For ASCII the two are the same, which is why a probe that only used ASCII
 * would prove nothing about it — the probe has a non-ASCII case for exactly that reason.
 *
 * THE LOOP THAT MATCHES ADVANCES BY ONE CHARACTER WHEN A MATCH IS EMPTY, because a pattern that can
 * match nothing ("" or "a*") matches at every position and a loop that did not advance would never
 * end. It advances by a CHARACTER rather than a byte so that it cannot land inside a UTF-8 sequence.
 */

#import <foundation/NSRegularExpression.h>
#import <foundation/NSArray.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#import <foundation/NSError.h>
/* A DICTIONARY LITERAL NEEDS NSDictionary DECLARED, not merely available — the same trap F12's codec
 * file recorded, hit again here at the regerror door. */
#import <foundation/NSDictionary.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

/* THE BYTE LENGTH OF ONE UTF-16 INDEX, which is what makes the map possible at all: a character
 * outside the basic plane is two UTF-16 units and four bytes, and both facts live on one row. */
static NSUInteger fn_utf8_length_of(NSString *string, NSUInteger index)
{
	NSString *one;
	const char *utf8;

	if (index >= [string length]) {
		return 0;
	}
	one = [string substringWithRange:NSMakeRange(index, 1)];
	utf8 = [one UTF8String];
	return utf8 != NULL ? strlen(utf8) : 0;
}

/* UTF-16 INDEX -> BYTE OFFSET, with one extra entry for the end of the string. */
static NSUInteger *fn_build_map(NSString *string, NSUInteger *outLength)
{
	NSUInteger count = [string length];
	NSUInteger *map = malloc((count + 1) * sizeof(NSUInteger));
	NSUInteger i;
	NSUInteger offset = 0;

	if (map == NULL) {
		*outLength = 0;
		return NULL;
	}
	for (i = 0; i < count; i++) {
		map[i] = offset;
		offset += fn_utf8_length_of(string, i);
	}
	map[count] = offset;
	*outLength = count;
	return map;
}

/* BYTE OFFSET -> UTF-16 INDEX: the last entry that starts at or before `byteOffset`. */
static NSUInteger fn_utf16_index(const NSUInteger *map, NSUInteger length, NSUInteger byteOffset)
{
	NSUInteger i;

	for (i = 0; i <= length; i++) {
		if (map[i] >= byteOffset) {
			return i;
		}
	}
	return length;
}

@implementation NSTextCheckingResult

+ (instancetype)resultWithRanges:(const NSRange *)ranges count:(NSUInteger)count
{
	NSTextCheckingResult *result = [[self alloc] init];

	if (result == nil) {
		return nil;
	}
	if (count > 0) {
		result->_ranges = malloc(count * sizeof(NSRange));
		if (result->_ranges == NULL) {
			return nil;
		}
		memcpy(result->_ranges, ranges, count * sizeof(NSRange));
	}
	result->_count = count;
	return result;
}

- (NSUInteger)numberOfRanges
{
	return _count;
}

- (NSRange)range
{
	return [self rangeAtIndex:0];
}

- (NSRange)rangeAtIndex:(NSUInteger)index
{
	if (index >= _count) {
		return NSMakeRange(NSNotFound, 0);
	}
	return _ranges[index];
}

- (void)dealloc
{
	free(_ranges);
}

- (BOOL)isEqual:(id)other
{
	NSTextCheckingResult *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSTextCheckingResult class]]) {
		return NO;
	}
	them = (NSTextCheckingResult *)other;
	if (them->_count != _count) {
		return NO;
	}
	return _count == 0 || memcmp(_ranges, them->_ranges, _count * sizeof(NSRange)) == 0;
}

- (NSUInteger)hash
{
	return _count;
}

- (NSString *)description
{
	NSMutableString *out = [NSMutableString stringWithFormat:@"<match %lu ranges", (unsigned long)_count];
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		[out appendFormat:@" (%lu,%lu)", (unsigned long)_ranges[i].location,
				   (unsigned long)_ranges[i].length];
	}
	[out appendString:@">"];
	return out;
}

- (id)copyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [NSTextCheckingResult resultWithRanges:_ranges count:_count];
}

@end

@implementation NSRegularExpression

+ (nullable instancetype)regularExpressionWithPattern:(NSString *)pattern
					      options:(NSRegularExpressionOptions)options
						error:(NSError ** _Nullable)error
{
	return [[self alloc] initWithPattern:pattern options:options error:error];
}

- (nullable instancetype)initWithPattern:(NSString *)pattern
				 options:(NSRegularExpressionOptions)options
				   error:(NSError ** _Nullable)error
{
	regex_t *compiled;
	int flags = REG_EXTENDED;
	int result;
	const char *text;

	self = [super init];
	if (self == nil || pattern == nil) {
		return nil;
	}
	compiled = malloc(sizeof(regex_t));
	if (compiled == NULL) {
		return nil;
	}
	if ((options & NSRegularExpressionCaseInsensitive) != 0) {
		flags |= REG_ICASE;
	}
	if ((options & NSRegularExpressionAnchorsMatchLines) != 0) {
		flags |= REG_NEWLINE;
	}
	text = [pattern UTF8String];
	result = regcomp(compiled, text != NULL ? text : "", flags);
	if (result != 0) {
		char message[256];

		regerror(result, compiled, message, sizeof(message));
		if (error != NULL) {
			*error = [NSError errorWithDomain:@"NSRegularExpressionErrorDomain"
						     code:result
						 userInfo:@{ NSLocalizedDescriptionKey :
							     [NSString stringWithUTF8String:message] }];
		}
		free(compiled);
		return nil;
	}
	_compiled = compiled;
	_pattern = pattern;
	/* THE OPTIONS THAT ACTUALLY TOOK EFFECT: the four POSIX cannot express are cleared, so a
	 * caller reading -options is told the truth about the pattern in hand. */
	_options = options & (NSRegularExpressionCaseInsensitive |
			      NSRegularExpressionAnchorsMatchLines |
			      NSRegularExpressionDotMatchesLineSeparators);
	_captures = compiled->re_nsub;
	return self;
}

- (void)dealloc
{
	if (_compiled != NULL) {
		regfree((regex_t *)_compiled);
		free(_compiled);
	}
}

- (NSString *)pattern
{
	return _pattern;
}

- (NSRegularExpressionOptions)options
{
	return _options;
}

- (NSUInteger)numberOfCaptureGroups
{
	return _captures;
}

- (NSArray *)fnMatchesIn:(NSString *)string range:(NSRange)range
{
	const char *text;
	NSUInteger *map;
	NSUInteger length = 0;
	NSUInteger start;
	NSUInteger end;
	NSMutableArray *results = [NSMutableArray array];
	regmatch_t *matches;

	text = [string UTF8String];
	if (text == NULL) {
		return results;
	}
	map = fn_build_map(string, &length);
	if (map == NULL) {
		return results;
	}
	matches = malloc((_captures + 1) * sizeof(regmatch_t));
	if (matches == NULL) {
		free(map);
		return results;
	}
	if (range.location > length) {
		range.location = length;
	}
	if (range.location + range.length > length) {
		range.length = length - range.location;
	}
	start = map[range.location];
	end = map[range.location + range.length];
	while (start <= end) {
		if (regexec((regex_t *)_compiled, text + start, _captures + 1, matches, 0) != 0) {
			break;
		}
		{
			NSRange *ranges = malloc((_captures + 1) * sizeof(NSRange));
			NSUInteger i;

			if (ranges == NULL) {
				break;
			}
			for (i = 0; i <= _captures; i++) {
				if (matches[i].rm_so < 0) {
					ranges[i] = NSMakeRange(NSNotFound, 0);
				} else {
					NSUInteger from = fn_utf16_index(map, length,
									 start + (NSUInteger)matches[i].rm_so);
					NSUInteger to = fn_utf16_index(map, length,
								       start + (NSUInteger)matches[i].rm_eo);

					ranges[i] = NSMakeRange(from, to - from);
				}
			}
			[results addObject:[NSTextCheckingResult resultWithRanges:ranges
									   count:_captures + 1]];
			free(ranges);
		}
		if (matches[0].rm_eo > 0) {
			start += (NSUInteger)matches[0].rm_eo;
		} else {
			/* AN EMPTY MATCH: move to the NEXT CHARACTER, never into the middle of one. */
			start++;
			while (start <= end && fn_utf16_index(map, length, start) ==
					      fn_utf16_index(map, length, start - 1)) {
				start++;
			}
		}
	}
	free(matches);
	free(map);
	return results;
}

- (NSArray *)matchesInString:(NSString *)string
		     options:(NSMatchingOptions)options
		       range:(NSRange)range
{
	(void)options;
	if (string == nil) {
		return [NSArray array];
	}
	return [self fnMatchesIn:string range:range];
}

- (NSUInteger)numberOfMatchesInString:(NSString *)string
			      options:(NSMatchingOptions)options
				range:(NSRange)range
{
	return [[self matchesInString:string options:options range:range] count];
}

- (nullable NSTextCheckingResult *)firstMatchInString:(NSString *)string
					      options:(NSMatchingOptions)options
						range:(NSRange)range
{
	NSArray *matches = [self matchesInString:string options:options range:range];

	return [matches count] > 0 ? [matches objectAtIndex:0] : nil;
}

- (NSRange)rangeOfFirstMatchInString:(NSString *)string
			     options:(NSMatchingOptions)options
			       range:(NSRange)range
{
	NSTextCheckingResult *first = [self firstMatchInString:string options:options range:range];

	return first != nil ? [first range] : NSMakeRange(NSNotFound, 0);
}

/* THE TEMPLATE: "$0" to "$9" are the ranges, "\\$" is a literal dollar, and anything else is itself.
 * A group that did not participate contributes nothing, which is what Cocoa does. */
- (NSString *)stringByReplacingMatchesInString:(NSString *)string
				       options:(NSMatchingOptions)options
					 range:(NSRange)range
				  withTemplate:(NSString *)template
{
	NSArray *matches = [self matchesInString:string options:options range:range];
	NSMutableString *out = [NSMutableString string];
	NSUInteger cursor = range.location;
	NSUInteger i;

	for (i = 0; i < [matches count]; i++) {
		NSTextCheckingResult *match = [matches objectAtIndex:i];
		NSRange whole = [match range];

		if (whole.location == NSNotFound || whole.location < cursor) {
			continue;
		}
		[out appendString:[string substringWithRange:NSMakeRange(cursor, whole.location - cursor)]];
		{
			NSUInteger j;

			for (j = 0; j < [template length]; j++) {
				unichar c = [template characterAtIndex:j];

				if (c == '\\' && j + 1 < [template length]) {
					[out appendString:[template substringWithRange:NSMakeRange(j + 1, 1)]];
					j++;
					continue;
				}
				if (c == '$' && j + 1 < [template length]) {
					unichar digit = [template characterAtIndex:j + 1];

					if (digit >= '0' && digit <= '9') {
						NSRange group = [match rangeAtIndex:(NSUInteger)(digit - '0')];

						if (group.location != NSNotFound &&
						    group.location + group.length <= [string length]) {
							[out appendString:[string substringWithRange:group]];
						}
						j++;
						continue;
					}
				}
				[out appendString:[template substringWithRange:NSMakeRange(j, 1)]];
			}
		}
		cursor = whole.location + whole.length;
	}
	if (cursor < range.location + range.length && cursor <= [string length]) {
		[out appendString:[string substringWithRange:
				NSMakeRange(cursor, range.location + range.length - cursor)]];
	}
	return out;
}

- (NSUInteger)replaceMatchesInString:(NSMutableString *)string
			     options:(NSMatchingOptions)options
			       range:(NSRange)range
			withTemplate:(NSString *)template
{
	NSArray *matches = [self matchesInString:string options:options range:range];
	NSString *replaced = [self stringByReplacingMatchesInString:string
							   options:options
							     range:range
						      withTemplate:template];

	[string setString:replaced];
	return [matches count];
}

- (BOOL)isEqual:(id)other
{
	NSRegularExpression *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSRegularExpression class]]) {
		return NO;
	}
	them = (NSRegularExpression *)other;
	return [_pattern isEqualToString:them->_pattern] &&
	       (_options & (NSRegularExpressionCaseInsensitive |
			    NSRegularExpressionAnchorsMatchLines)) ==
	       (them->_options & (NSRegularExpressionCaseInsensitive |
				  NSRegularExpressionAnchorsMatchLines));
}

- (NSUInteger)hash
{
	return [_pattern hash];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ pattern=%@ captures=%lu>", [self class], _pattern,
				  (unsigned long)_captures];
}

- (id)copyWithZone:(nullable NSZone *)zone
{
	NSRegularExpression *copy;

	(void)zone;
	copy = [[NSRegularExpression alloc] initWithPattern:_pattern options:_options error:NULL];
	return copy != nil ? copy : self;
}

@end
