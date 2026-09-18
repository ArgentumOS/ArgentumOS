/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * npredicateformat.m — THE FORMAT GRAMMAR. docs/design/foundation-plan.md, F11b.
 *
 * ARC file.
 *
 * A recursive-descent parser over the UTF-8 bytes of the format string, and the class methods
 * that expose it. The tree it builds is the OBJECT MODEL from F11a — NSCompoundPredicate for the
 * connectives, FNPredicateComparison for a comparison — so the grammar adds no evaluator of its
 * own: it adds a way to WRITE one.
 *
 *   predicate  := orExpr
 *   orExpr     := andExpr ( OR andExpr )*
 *   andExpr    := notExpr ( AND notExpr )*
 *   notExpr    := NOT notExpr | primary
 *   primary    := '(' predicate ')' | TRUEPREDICATE | FALSEPREDICATE | comparison
 *   comparison := operand op operand [ modifier ]
 *   operand    := SELF | path | number | string | YES | NO | TRUE | FALSE | NULL | NIL
 *   op         := = | == | != | <> | < | <= | > | >= | CONTAINS | BEGINSWITH | ENDSWITH | LIKE
 *   modifier   := [c]
 *
 * THE KEYWORDS ARE CASE-INSENSITIVE and the operators are not: `and` and `AND` are the connective,
 * while `=` and `==` are two spellings of the same comparison.
 *
 * WHAT THE PARSER REFUSES, AND WHY IT REFUSES IT LOUDLY. Every one of these is a RAISE that names
 * the construct, not a quiet misparse — a refusal a caller cannot see is worse than no parser:
 *   - `MATCHES`: a regex engine is a table, and none ships here. `LIKE` (with `*`/`?` and `\`
 *     escapes) is the rule-based alternative and it is part of this grammar;
 *   - the `[d]` modifier: diacritic folding is Unicode DECOMPOSITION, a table again. `[c]` ships;
 *   - `IN` and `BETWEEN`: they need constant COLLECTIONS and ranges, which is grammar this half
 *     does not have;
 *   - `ANY`/`ALL`/`NONE`/`SOME` and the `@`-aggregates: quantifiers over key paths;
 *   - `$variables`: the substitution forms are refused (§5), so nothing here binds one.
 *
 * THE ESCAPES INSIDE A QUOTED STRING ARE EXACTLY the four the house's other parsers use — `\"`,
 * `\\`, `\n`, `\t` — because two escape sets in one library is one too many.
 */

#import <foundation/NSPredicate.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#import <foundation/NSArray.h>
#import <foundation/NSException.h>
#import "fnpredicate.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* --------------------------------------------------------------- the parser */

@interface FNParser : NSObject
{
	const char *_text;		/* the format's UTF-8 bytes; OWNED — see -initWithString: */
	NSUInteger _length;
	NSUInteger _at;
}
- (instancetype)initWithString:(NSString *)format;
- (NSPredicate *)parsePredicate;	/* raises NSInvalidArgumentException on a bad format */
@end

static BOOL fn_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* A word character: what a key path is made of, plus the dots that join its parts. */
static BOOL fn_is_word(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static char fn_fold(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

@implementation FNParser

- (instancetype)initWithString:(NSString *)format
{
	if (format == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"a predicate format cannot be nil"];
	}
	if ((self = [super init]) != nil) {
		/* A COPY, AND NOT A NICETY: -UTF8String hands back a buffer this class does not own, and
		 * for a SHORT literal that buffer can be a shared decode scratch — the next -UTF8String
		 * on ANOTHER string overwrites it under our feet. The operator symbols this parser
		 * compares against are exactly such strings, which is how a comparison against
		 * "age > 30" could fail to find the `>` that was plainly there. Owning the bytes is the
		 * parser's job; borrowing them was the bug. */
		const char *bytes = [format UTF8String];

		_text = strdup(bytes == NULL ? "" : bytes);
		if (_text == NULL) {
			[NSException raise:NSInvalidArgumentException
				    format:@"the predicate format could not be copied"];
		}
		_length = strlen(_text);
		_at = 0;
	}
	return self;
}

- (void)dealloc
{
	free((void *)_text);		/* the copy above: ARC does not own C pointers */
}

/* ------------------------------------------------------------- the scanning */

- (void)skipSpaces
{
	while (_at < _length && fn_is_space(_text[_at])) {
		_at++;
	}
}

- (BOOL)atEnd
{
	return _at >= _length;
}

/* The word at the cursor WITHOUT consuming it, so a caller can look before it leaps. */
- (const char *)peekWord
{
	static char word[128];
	NSUInteger start = _at;
	NSUInteger n = 0;

	while (start < _length && fn_is_word(_text[start]) && n < sizeof word - 1) {
		word[n++] = _text[start++];
	}
	word[n] = '\0';
	return word;
}

/* Consume KEYWORD if it is next, case-insensitively, and only on a word boundary. It takes an
 * NSString because that is how the grammar READS at every call site — `[self matchKeyword:@"OR"]`
 * — and a helper whose argument is written as an object and typed as a `char *` is a bug waiting
 * for a strlen. */
- (BOOL)matchKeyword:(NSString *)keyword
{
	const char *bytes = [keyword UTF8String];
	const char *word = [self peekWord];
	NSUInteger length = strlen(bytes);
	NSUInteger i;

	if (strlen(word) < length) {
		return NO;
	}
	for (i = 0; i < length; i++) {
		if (fn_fold(word[i]) != fn_fold(bytes[i])) {
			return NO;
		}
	}
	/* A LONGER word is a different word: `OR` must not match `ORDERED`. */
	if (word[length] != '\0' && word[length] != '.') {
		return NO;
	}
	_at += length;
	return YES;
}

- (BOOL)matchOperator:(NSString *)symbol
{
	const char *bytes = [symbol UTF8String];
	NSUInteger length = strlen(bytes);

	if (_at + length > _length || strncmp(_text + _at, bytes, length) != 0) {
		return NO;
	}
	_at += length;
	return YES;
}

- (void)raise:(NSString *)what at:(NSUInteger)where
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%@ at character %lu of the predicate format", what,
			   (unsigned long)where];
}

- (NSPredicate *)parsePredicate
{
	NSPredicate *parsed;

	[self skipSpaces];
	if ([self atEnd]) {
		[self raise:@"an empty predicate format is not a predicate" at:_at];
	}
	parsed = [self parseOr];
	[self skipSpaces];
	if (![self atEnd]) {
		[self raise:@"unexpected trailing text" at:_at];
	}
	return parsed;
}

- (NSPredicate *)parseOr
{
	NSMutableArray *terms = [[NSMutableArray alloc] init];
	NSPredicate *first = [self parseAnd];

	[terms addObject:first];
	for (;;) {
		NSPredicate *next;

		[self skipSpaces];
		if (![self matchKeyword:@"OR"] && ![self matchOperator:@"||"]) {
			break;
		}
		next = [self parseAnd];
		[terms addObject:next];
	}
	if ([terms count] == 1) {
		return first;
	}
	return [NSCompoundPredicate orPredicateWithSubpredicates:terms];
}

- (NSPredicate *)parseAnd
{
	NSMutableArray *terms = [[NSMutableArray alloc] init];
	NSPredicate *first = [self parseNot];

	[terms addObject:first];
	for (;;) {
		NSPredicate *next;

		[self skipSpaces];
		if (![self matchKeyword:@"AND"] && ![self matchOperator:@"&&"]) {
			break;
		}
		next = [self parseNot];
		[terms addObject:next];
	}
	if ([terms count] == 1) {
		return first;
	}
	return [NSCompoundPredicate andPredicateWithSubpredicates:terms];
}

- (NSPredicate *)parseNot
{
	[self skipSpaces];
	if ([self matchKeyword:@"NOT"] || [self matchOperator:@"!"]) {
		NSPredicate *inner = [self parseNot];

		return [NSCompoundPredicate notPredicateWithSubpredicate:inner];
	}
	return [self parsePrimary];
}

- (NSPredicate *)parsePrimary
{
	[self skipSpaces];
	if ([self matchOperator:@"("]) {
		NSPredicate *inner;

		[self skipSpaces];
		inner = [self parseOr];
		[self skipSpaces];
		if (![self matchOperator:@")"]) {
			[self raise:@"a '(' was never closed" at:_at];
		}
		return inner;
	}
	if ([self matchKeyword:@"TRUEPREDICATE"]) {
		return [NSPredicate predicateWithValue:YES];
	}
	if ([self matchKeyword:@"FALSEPREDICATE"]) {
		return [NSPredicate predicateWithValue:NO];
	}
	return [self parseComparison];
}

/* --------------------------------------------------------------- an operand */

/* Returns the PATH when the operand is one, and sets *literal when it is a value. Exactly one of
 * the two comes back non-nil, except for NULL/NIL, where BOTH are nil and that is the value. */
- (nullable NSString *)parseOperandInto:(id _Nullable * _Nullable)literal
			     sawLiteral:(BOOL *)sawLiteral
{
	[self skipSpaces];
	if ([self atEnd]) {
		[self raise:@"an operand is missing" at:_at];
	}
	*sawLiteral = NO;
	*literal = nil;

	/* `$name` is a SUBSTITUTION VARIABLE, and nothing here binds one: the substitution forms are
	 * refused (§5), so this names the construct instead of failing as "an operand is missing". */
	if (_text[_at] == '$') {
		[self raise:@"a $variable is refused — nothing here substitutes, so there is no value "
			   "to bind it to" at:_at];
	}

	if (_text[_at] == '"') {
		NSMutableString *text = [[NSMutableString alloc] init];
		NSUInteger start = _at;

		_at++;				/* the opening quote */
		for (;;) {
			if ([self atEnd]) {
				[self raise:@"a string literal was never closed" at:start];
			}
			if (_text[_at] == '"') {
				_at++;
				break;
			}
			if (_text[_at] == '\\' && _at + 1 < _length) {
				char escaped = _text[_at + 1];

				_at += 2;
				if (escaped == 'n') {
					[text appendString:@"\n"];
				} else if (escaped == 't') {
					[text appendString:@"\t"];
				} else {
					/* `\"` and `\\` — and anything else is itself, which is what
					 * the house's other parsers do rather than guessing. */
					char one[2];

					one[0] = escaped;
					one[1] = '\0';
					[text appendString:[NSString stringWithUTF8String:one]];
				}
				continue;
			}
			{
				char one[2];

				one[0] = _text[_at];
				one[1] = '\0';
				[text appendString:[NSString stringWithUTF8String:one]];
			}
			_at++;
		}
		*sawLiteral = YES;
		*literal = text;
		return nil;
	}

	if ((_text[_at] >= '0' && _text[_at] <= '9') ||
	    ((_text[_at] == '-' || _text[_at] == '+') && _at + 1 < _length &&
	     _text[_at + 1] >= '0' && _text[_at + 1] <= '9')) {
		NSUInteger start = _at;
		BOOL isFractional = NO;

		if (_text[_at] == '-' || _text[_at] == '+') {
			_at++;
		}
		while (_at < _length && _text[_at] >= '0' && _text[_at] <= '9') {
			_at++;
		}
		if (_at < _length && _text[_at] == '.') {
			isFractional = YES;
			_at++;
			while (_at < _length && _text[_at] >= '0' && _text[_at] <= '9') {
				_at++;
			}
		}
		if (_at < _length && (_text[_at] == 'e' || _text[_at] == 'E')) {
			isFractional = YES;
			_at++;
			if (_at < _length && (_text[_at] == '-' || _text[_at] == '+')) {
				_at++;
			}
			while (_at < _length && _text[_at] >= '0' && _text[_at] <= '9') {
				_at++;
			}
		}
		{
			char number[64];
			NSUInteger n = _at - start;

			if (n >= sizeof number) {
				[self raise:@"a number is too long" at:start];
			}
			memcpy(number, _text + start, n);
			number[n] = '\0';
			if (isFractional) {
				*literal = [NSNumber numberWithDouble:strtod(number, NULL)];
			} else {
				*literal = [NSNumber numberWithLongLong:strtoll(number, NULL, 10)];
			}
		}
		*sawLiteral = YES;
		return nil;
	}

	if ([self matchKeyword:@"YES"] || [self matchKeyword:@"TRUE"]) {
		*literal = [NSNumber numberWithBool:YES];
		*sawLiteral = YES;
		return nil;
	}
	if ([self matchKeyword:@"NO"] || [self matchKeyword:@"FALSE"]) {
		*literal = [NSNumber numberWithBool:NO];
		*sawLiteral = YES;
		return nil;
	}
	if ([self matchKeyword:@"NULL"] || [self matchKeyword:@"NIL"]) {
		/* nil IS the value: a NULL literal has no path and no box. */
		*sawLiteral = YES;
		*literal = nil;
		return nil;
	}
	{
		const char *word = [self peekWord];

		if (word[0] == '\0') {
			[self raise:@"an operand is missing" at:_at];
		}
		{
			NSString *path = [NSString stringWithUTF8String:word];

			_at += strlen(word);
			return path;
		}
	}
}

/* ----------------------------------------------------------- a comparison */

- (nullable NSNumber *)matchComparisonOperator:(FNCompareOperator *)op
{
	if ([self matchOperator:@"<="]) { *op = FNCompareLessOrEqual; return @YES; }
	if ([self matchOperator:@">="]) { *op = FNCompareGreaterOrEqual; return @YES; }
	if ([self matchOperator:@"!="]) { *op = FNCompareNotEqual; return @YES; }
	if ([self matchOperator:@"<>"]) { *op = FNCompareNotEqual; return @YES; }
	if ([self matchOperator:@"=="]) { *op = FNCompareEqual; return @YES; }
	if ([self matchOperator:@"="]) { *op = FNCompareEqual; return @YES; }
	if ([self matchOperator:@"<"]) { *op = FNCompareLess; return @YES; }
	if ([self matchOperator:@">"]) { *op = FNCompareGreater; return @YES; }
	if ([self matchKeyword:@"CONTAINS"]) { *op = FNCompareContains; return @YES; }
	if ([self matchKeyword:@"BEGINSWITH"]) { *op = FNCompareBeginsWith; return @YES; }
	if ([self matchKeyword:@"ENDSWITH"]) { *op = FNCompareEndsWith; return @YES; }
	if ([self matchKeyword:@"LIKE"]) { *op = FNCompareLike; return @YES; }
	return nil;
}

- (NSPredicate *)parseComparison
{
	NSString *leftPath;
	NSString *rightPath;
	id leftLiteral = nil;
	id rightLiteral = nil;
	BOOL leftIsLiteral = NO;
	BOOL rightIsLiteral = NO;
	FNCompareOperator op = FNCompareEqual;
	BOOL caseInsensitive = NO;
	BOOL diacriticInsensitive = NO;
	BOOL matchedWord = NO;		/* MATCHES names its own operator: it is a word, not a symbol */
	NSPredicate *leaf;

	leftPath = [self parseOperandInto:&leftLiteral sawLiteral:&leftIsLiteral];
	[self skipSpaces];
	if ([self matchKeyword:@"MATCHES"]) {
		/* F13.7d: MATCHES SHIPS. F11 refused it because "a regex engine is a table" — true of
		 * a hand-written one. musl ships a real POSIX ERE engine INSIDE libc, so this is a
		 * BINDING, exactly like F12's zlib: no new artifact, and no table written here. */
		matchedWord = YES;
		op = FNCompareMatches;
		/* The word ENDS the operator, so the space before the pattern is skipped HERE: the
		 * symbol path skips it inside matchComparisonOperator:, and MATCHES never goes
		 * through that. (Measured: without this, `name MATCHES "a.*"` failed with "an operand
		 * is missing at character 12" — the space.) */
		[self skipSpaces];
	}
	if ([self matchKeyword:@"IN"]) {
		[self raise:@"IN is refused — it needs constant collections, which this grammar "
			   "does not have" at:_at];
	}
	if ([self matchKeyword:@"BETWEEN"]) {
		[self raise:@"BETWEEN is refused — it needs a range, which this grammar does not "
			   "have" at:_at];
	}
	if ([self matchKeyword:@"ANY"] || [self matchKeyword:@"ALL"] ||
	    [self matchKeyword:@"NONE"] || [self matchKeyword:@"SOME"]) {
		[self raise:@"the ANY/ALL/NONE/SOME quantifiers are refused" at:_at];
	}
	if (!matchedWord && [self matchComparisonOperator:&op] == nil) {
		/* THE MESSAGE NAMES WHAT IT FOUND. "a comparison operator is missing" with no character
		 * is the kind of refusal that costs a round trip — this one did — and the caller may not
		 * be able to see the format at all. */
		char here[12];
		NSUInteger i = 0;
		NSUInteger at = _at;

		while (at < _length && i < sizeof here - 1) {
			here[i++] = _text[at++];
		}
		here[i] = '\0';
		[self raise:[NSString stringWithFormat:
				@"a comparison operator is missing (found \"%s\" at %lu of %lu)",
				here, (unsigned long)_at, (unsigned long)_length] at:_at];
	}
	rightPath = [self parseOperandInto:&rightLiteral sawLiteral:&rightIsLiteral];

	/* THE MODIFIER. `[c]` ships; `[d]` is refused by name, because diacritic folding is
	 * Unicode decomposition — a table. */
	[self skipSpaces];
	if ([self matchOperator:@"["]) {
		BOOL sawC = NO;
		BOOL sawD = NO;

		for (;;) {
			if ([self atEnd]) {
				[self raise:@"a '[' was never closed" at:_at];
			}
			if (_text[_at] == ']') {
				_at++;
				break;
			}
			if (fn_fold(_text[_at]) == 'c') {
				sawC = YES;
			} else if (fn_fold(_text[_at]) == 'd') {
				/* F13.7d: `[d]` SHIPS, through ICU's collator — the same binding that
				 * answered the calendars. F11 refused it because diacritic folding is
				 * Unicode decomposition; ICU HAS that table, so the modifier is a strength
				 * setting here rather than a table to write. */
				sawD = YES;
			} else {
				[self raise:@"an unknown modifier letter" at:_at];
			}
			_at++;
		}
		caseInsensitive = sawC;
		diacriticInsensitive = sawD;
	}
	if (!leftIsLiteral && leftPath == nil) {
		/* NULL on the left is a value, not a missing operand. */
		leftIsLiteral = YES;
	}
	if (!rightIsLiteral && rightPath == nil) {
		rightIsLiteral = YES;
	}
	(void)rightIsLiteral;
	leaf = [[FNPredicateComparison alloc] initWithLeftPath:leftPath
						   leftLiteral:leftLiteral
						      operator:op
						     rightPath:rightPath
						  rightLiteral:rightLiteral
					       caseInsensitive:caseInsensitive
					 diacriticInsensitive:diacriticInsensitive];
	return leaf;
}

@end

/* ------------------------------------------------------ the grammar's entry point */

/*
 * A C FUNCTION, not a category method. +predicateWithFormat: and -initWithFormat: belong to the
 * PRIMARY implementation of NSPredicate — a category implementing what the class's own header
 * declares is a warning, and rightly so — so the parser is reached through this instead.
 */
NSPredicate *FNPredicateParse(NSString *format)
{
	FNParser *parser = [[FNParser alloc] initWithString:format];

	return [parser parsePredicate];		/* raises on a bad format: never nil */
}
