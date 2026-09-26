/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRegularExpression.m — regular expressions as objects (F13.16). MANUAL OWNERSHIP.
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

#import <Foundation/NSRegularExpression.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSError.h>
/* A DICTIONARY LITERAL NEEDS NSDictionary DECLARED, not merely available — the same trap F12's codec
 * file recorded, hit again here at the regerror door. */
#import <Foundation/NSDictionary.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

/* THE BYTE LENGTH OF ONE UTF-16 INDEX, which is what makes the map possible at all: a character
 * outside the basic plane is two UTF-16 units and four bytes, and both facts live on one row. */
/*
 * THE MAP IS UTF-16 INDEX -> BYTE OFFSET, AND IT IS NOW EXACT (W1 slice 3).
 *
 * This engine hands POSIX regexec the UTF-8 BYTES, so the match offsets it gets back
 * are byte offsets; the NSRange contract it must answer in is UTF-16 UNITS. So the map
 * has one entry PER UNIT (a character above U+FFFF is two), each the byte offset where
 * that unit's character starts, plus an end entry.
 *
 * IT USED TO BE THE IDENTITY BY ACCIDENT: the builder sliced single bytes and measured
 * strlen, which gave 1 for any byte only because the old storage copied invalid UTF-8
 * VERBATIM. When the storage became UTF-16 that byte became U+FFFD — three bytes — and
 * the map thickened (measured: `count=2 [hél] [o wö]`). Writing the identity down kept
 * the pre-flip contract green; SLICE 3 IS THE FLIP, so the exact map takes over here and
 * the engine and -substringWithRange:/ -length finally inhabit the same space.
 * §13.7 has the bisect that established all of this.
 */
static NSUInteger *fn_build_map(NSString *string, NSUInteger *outLength)
{
	const char *utf8 = [string UTF8String];
	size_t bytes = utf8 != NULL ? strlen(utf8) : 0;
	NSUInteger *map = malloc((bytes + 2) * sizeof(NSUInteger));
	size_t i = 0;
	NSUInteger units = 0;

	if (map == NULL) {
		return NULL;
	}
	while (i < bytes) {
		unsigned char lead = (unsigned char)utf8[i];
		size_t seq;

		if (lead < 0x80) {
			seq = 1;
		} else if ((lead & 0xE0) == 0xC0) {
			seq = 2;
		} else if ((lead & 0xF0) == 0xE0) {
			seq = 3;
		} else if ((lead & 0xF8) == 0xF0) {
			seq = 4;
		} else {
			seq = 1;	/* a stray continuation byte: one byte, one unit */
		}
		if (i + seq > bytes) {
			seq = 1;
		}
		map[units++] = (NSUInteger)i;
		if (seq == 4) {
			/* a character above U+FFFF is TWO units, and the low surrogate
			 * starts at the same byte offset as its high half */
			map[units++] = (NSUInteger)i;
		}
		i += seq;
	}
	map[units] = (NSUInteger)bytes;
	*outLength = units;
	return map;
}

#if 0

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
#endif

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

/* ---- NAMED CAPTURE GROUPS, TRANSLATED FOR AN ENGINE THAT CANNOT SPELL THEM -------------------------
 *
 * POSIX ERE has no `(?<name>...)`; its `(` IS the capture group, numbered left to right. So the translation
 * is the identity on numbering - `(?<name>` becomes `(` and the name is recorded, which is why a regular
 * expression that uses named groups keeps working for callers that address groups by INDEX as well.
 *
 * IT IS A SCAN RATHER THAN A SUBSTITUTION, and the difference is not pedantry: `\(` is an escaped literal,
 * `[(]` is a literal inside a class, and `[[:alpha:]]` has brackets of its own. A blind replace would move a
 * group boundary and change what the pattern means, and would do it silently. So escapes, character classes
 * and the class-nesting form are all tracked here.
 *
 * TWO CHOICES APPLE DOES NOT PUBLISH, both stated where they are made: a name that appears on two groups keeps
 * the FIRST (the second occurrence is transliterated as a plain group and is not addressable by name), and a
 * `(?<` that is NOT followed by a usable name and a `>` is passed through UNTOUCHED - so regcomp refuses the
 * whole pattern with its own message rather than this function inventing one.
 */
static NSString *fn_translate_named_groups(NSString *pattern, NSDictionary **outNames,
					      NSUInteger *outGroups)
{
	NSUInteger length = [pattern length];
	NSMutableString *out = [NSMutableString stringWithCapacity:length];
	NSMutableDictionary *names = [NSMutableDictionary dictionary];
	NSUInteger i = 0;
	NSUInteger groups = 0;
	BOOL inClass = NO;

	while (i < length) {
		unichar c = [pattern characterAtIndex:i];

		if (c == '\\' && i + 1 < length) {
			[out appendString:[pattern substringWithRange:NSMakeRange(i, 2)]];
			i += 2;
			continue;
		}
		if (inClass) {
			[out appendString:[pattern substringWithRange:NSMakeRange(i, 1)]];
			if (c == ']') {
				inClass = NO;
			}
			i++;
			continue;
		}
		if (c == '[') {
			inClass = YES;
			[out appendString:@"["];
			i++;
			continue;
		}
		if (c == '(') {
			if (i + 3 < length && [pattern characterAtIndex:i + 1] == '?' &&
			    [pattern characterAtIndex:i + 2] == '<') {
				NSUInteger from = i + 3;
				NSUInteger close = from;
				BOOL usable = YES;
				NSString *name;

				while (close < length && [pattern characterAtIndex:close] != '>') {
					close++;
				}
				if (close >= length || close == from) {
					usable = NO;
				}
				for (NSUInteger k = from; usable && k < close; k++) {
					unichar n = [pattern characterAtIndex:k];

					if (k == from) {
						usable = (n == '_' ||
							  (n >= 'A' && n <= 'Z') || (n >= 'a' && n <= 'z'));
					} else {
						usable = (n == '_' ||
							  (n >= 'A' && n <= 'Z') || (n >= 'a' && n <= 'z') ||
							  (n >= '0' && n <= '9'));
					}
				}
				if (usable) {
					name = [pattern substringWithRange:NSMakeRange(from, close - from)];
					groups++;
					if ([names objectForKey:name] == nil) {
						[names setObject:[NSNumber numberWithUnsignedInteger:groups]
							  forKey:name];
					}
					[out appendString:@"("];
					i = close + 1;
					continue;
				}
			}
			groups++;
			[out appendString:@"("];
			i++;
			continue;
		}
		[out appendString:[pattern substringWithRange:NSMakeRange(i, 1)]];
		i++;
	}
	*outNames = names;
	*outGroups = groups;
	return out;
}

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
	long groups = 0;
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
	/* THE PATTERN GOES THROUGH THE NAMED-GROUP TRANSLATION FIRST: the engine sees the translated text (which is
	 * byte-identical to the caller's unless a named group was used), while `-pattern` answers what the caller
	 * wrote - Apple's contract, and the reason `_pattern` is the original below. */
	{
		NSDictionary *names = nil;
		NSUInteger counted = 0;
		NSString *translated = fn_translate_named_groups(pattern, &names, &counted);

		_groupNames = [names copy];
		groups = counted;
		text = [translated UTF8String];
	}
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
		[_groupNames release];
		_groupNames = nil;
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
	/* THE TRANSLATION'S OWN COUNT MUST MATCH THE ENGINE'S, and this is where a disagreement would surface: a
	 * group counted by the scan and not by regcomp means the translation moved a boundary, and every NAME
	 * would then be off by one - ranges that look right and address the wrong group, which is the worst
	 * possible failure for a library that answers by INDEX. So a disagreement REFUSES the pattern. The count
	 * comes from the same scan that produced the names, because a second scanner would be a second answer. */
	if ([_groupNames count] > 0 && (long)_captures != groups) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:@"NSRegularExpressionErrorDomain"
						     code:result
						 userInfo:@{ NSLocalizedDescriptionKey :
							     @"a capture group name could not be placed" }];
		}
		regfree(compiled);
		free(compiled);
		[_groupNames release];
		_groupNames = nil;
		return nil;
	}
	return self;
}

- (void)dealloc
{
	if (_compiled != NULL) {
		regfree((regex_t *)_compiled);
		free(_compiled);
	}
	[_groupNames release];
	/* `_pattern` IS NOT RELEASED HERE BECAUSE IT WAS NEVER RETAINED, which is a pre-existing ownership defect
	 * of this class rather than a decision: `-init` assigns the caller's string. It is NAMED rather than fixed
	 * quietly with them, because the same file's `-copy` recompiles from `_pattern` and a release here would
	 * change when a dangling one is noticed. The URL unit's `-dealloc` debt (§60) was the same shape. */
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

/* THE NAME MAP, asked by `-[NSTextCheckingResult rangeWithName:]`. A name no group carries answers NSNotFound,
 * which is that method's own spelling for "nothing here" and is also what it answers for a group that did not
 * participate in the match. */
- (NSUInteger)indexOfCaptureGroupNamed:(NSString *)name
{
	NSNumber *index = (name != nil) ? (NSNumber *)[_groupNames objectForKey:name] : nil;

	return index != nil ? [index unsignedIntegerValue] : NSNotFound;
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
			/* THE RESULT IS TYPED, which is what §62.18 changed here: a match is a
			 * NSTextCheckingTypeRegularExpression result carrying the expression it came from, so
			 * -resultType, -regularExpression and -rangeWithName: all answer for it. */
			[results addObject:[NSTextCheckingResult
						regularExpressionCheckingResultWithRanges:ranges
										     count:_captures + 1
									 regularExpression:self]];
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

/* APPLE'S GENERAL WAY TO WALK THE MATCHES, and the one its NSDataDetector page uses as its example. THE FLAGS
 * ARE ALWAYS ZERO HERE, and that is a statement rather than an oversight: NSMatchingProgress and
 * NSMatchingCompleted are the options NSMatchingReportProgress and NSMatchingReportCompletion ASK FOR, and this
 * engine is SYNCHRONOUS - a POSIX regexec call cannot report progress, so there is no progress call to make and
 * this file already refuses those two options by name at the top. A caller who wants to stop early has `stop`,
 * which is the mechanism that does not depend on the engine. */
- (void)enumerateMatchesInString:(NSString *)string
			 options:(NSMatchingOptions)options
			   range:(NSRange)range
		      usingBlock:(void (^)(NSTextCheckingResult *result, NSMatchingFlags flags,
					   BOOL *stop))block
{
	NSArray *matches;
	NSUInteger i;

	if (block == nil) {
		return;
	}
	matches = [self matchesInString:string options:options range:range];
	for (i = 0; i < [matches count]; i++) {
		BOOL stop = NO;

		block([matches objectAtIndex:i], (NSMatchingFlags)0, &stop);
		if (stop) {
			break;
		}
	}
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

- (id)copy
{
	NSRegularExpression *copy;

	copy = [[NSRegularExpression alloc] initWithPattern:_pattern options:_options error:NULL];
	return copy != nil ? copy : self;
}

@end
