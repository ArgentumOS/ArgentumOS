/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPredicate.m — the predicate object model. docs/design/foundation-plan.md, F11a.
 *
 * MANUAL OWNERSHIP. The block leaf is the one thing it stores that needs ARC's help.
 *
 * THE LEAVES ARE PRIVATE CLASSES, and they have to be classes rather than a kind tag on one:
 * `-evaluateWithObject:` is the whole of the protocol, so a leaf IS its answer, and the base's
 * behaviour (raise) is what a caller gets for a leaf that was never built. The comparison leaf
 * belongs to the GRAMMAR half, because its Cocoa API is expressed in NSExpression and the honest
 * version of it is whatever the parser produces.
 *
 * NOTHING HERE SUBSTITUTES. The block leaf is handed a nil `bindings` because there are no
 * variables to bind — that parameter is Cocoa's shape, not a feature of this half.
 */

#import <Foundation/NSPredicate.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSSet.h>	/* F13.11: IN takes a collection */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSKeyValueCoding.h>	/* the comparison leaf resolves key paths */
#import "NSPredicate.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>		/* F13.7d: MATCHES, on the engine musl already ships inside libc */
#include <unicode/ucol.h>	/* F13.7d: `[d]`, through ICU's collator */
#include <unicode/ustring.h>	/* ... and its UTF-8 <-> UTF-16 conversions */

/* --------------------------------------------------------------- the leaves */

@interface FNPredicateValue : NSPredicate
{
	BOOL _value;
}
- (instancetype)initWithValue:(BOOL)value;
@end

@interface FNPredicateBlock : NSPredicate
{
	BOOL (^_block)(id, NSDictionary *);
}
- (instancetype)initWithBlock:(BOOL (^)(id, NSDictionary *))block;
@end

/* The tree node's storage, here rather than in the header: v1 has no subclass to extend, and
 * Cocoa keeps its own ivars private too. */
@interface NSCompoundPredicate ()
{
	NSCompoundPredicateType _type;
	NSArray *_subpredicates;
}
@end

/* ------------------------------------------------------------ the base class */

@implementation NSPredicate

+ (instancetype)predicateWithValue:(BOOL)value
{
	return [[FNPredicateValue alloc] initWithValue:value];
}

+ (instancetype)predicateWithBlock:(BOOL (^)(id, NSDictionary *))block
{
	return [[FNPredicateBlock alloc] initWithBlock:block];
}

/* THE GRAMMAR'S TWO DOORS (F11b). The parser itself lives in NSPredicateFormat.m and is reached
 * through FNPredicateParse, so neither file owns the other: this one owns the CLASS, and that
 * one owns the LANGUAGE. */
+ (instancetype)predicateWithFormat:(NSString *)format
{
	return FNPredicateParse(format);
}

- (instancetype)initWithFormat:(NSString *)format
{
	/* Cocoa's -initWithFormat: ANSWERS THE PARSED PREDICATE, whatever it was sent to: a predicate
	 * is immutable, so there is nothing of the receiver to keep. */
	return FNPredicateParse(format);
}

- (BOOL)evaluateWithObject:(nullable id)object
{
	(void)object;
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: a predicate built from the base class has no rule to "
			   "evaluate — use +predicateWithValue:, +predicateWithBlock: or (F11b) "
			   "+predicateWithFormat:", [self class], @"evaluateWithObject:"];
	return NO;
}

- (NSString *)predicateFormat
{
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: a predicate built from the base class has nothing to render",
			   [self class], @"predicateFormat"];
	return nil;
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

@end

/* ------------------------------------------------------------ the value leaf */

@implementation FNPredicateValue

- (instancetype)initWithValue:(BOOL)value
{
	if ((self = [super init]) != nil) {
		_value = value;
	}
	return self;
}

- (BOOL)evaluateWithObject:(nullable id)object
{
	(void)object;			/* the answer does not depend on what was asked about */
	return _value;
}

- (NSString *)predicateFormat
{
	return _value ? @"TRUEPREDICATE" : @"FALSEPREDICATE";
}

@end

/* ------------------------------------------------------------ the block leaf */

@implementation FNPredicateBlock

- (instancetype)initWithBlock:(BOOL (^)(id, NSDictionary *))block
{
	if ((self = [super init]) != nil) {
		_block = block;
	}
	return self;
}

- (BOOL)evaluateWithObject:(nullable id)object
{
	if (_block == NULL) {
		return NO;
	}
	/* nil, not an empty dictionary: nothing here binds a variable, and an empty dictionary
	 * would suggest that something might. */
	return _block(object, nil);
}

- (NSString *)predicateFormat
{
	/* A block has no source form to render. Saying so beats inventing one that a parser would
	 * then have to refuse. */
	return @"BLOCKPREDICATE";
}

@end

/* ---------------------------------------------------------- the compound node */

@implementation NSCompoundPredicate

+ (instancetype)andPredicateWithSubpredicates:(NSArray *)subpredicates
{
	return [[NSCompoundPredicate alloc] initWithType:NSAndPredicateType
					    subpredicates:subpredicates];
}

+ (instancetype)orPredicateWithSubpredicates:(NSArray *)subpredicates
{
	return [[NSCompoundPredicate alloc] initWithType:NSOrPredicateType
					   subpredicates:subpredicates];
}

+ (instancetype)notPredicateWithSubpredicate:(NSPredicate *)predicate
{
	if (predicate == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+notPredicateWithSubpredicate: needs a predicate"];
	}
	return [[NSCompoundPredicate alloc] initWithType:NSNotPredicateType
					   subpredicates:@[predicate]];
}

- (instancetype)initWithType:(NSCompoundPredicateType)type subpredicates:(NSArray *)subpredicates
{
	if ((self = [super init]) != nil) {
		_type = type;
		_subpredicates = [subpredicates copy];
	}
	return self;
}

- (NSCompoundPredicateType)compoundPredicateType { return _type; }
- (NSArray *)subpredicates { return _subpredicates; }

- (BOOL)evaluateWithObject:(nullable id)object
{
	NSUInteger i;
	NSUInteger count = [_subpredicates count];

	/* IF/ELSE, NOT A switch. This toolchain has a recorded trap for switch statements (Kestrel
	 * S5.2a's jump-table trampoline), and a switch that covers every enumerator also makes the
	 * code after it UNREACHABLE — which is the shape that turns a bad value into an illegal
	 * instruction instead of a diagnosable complaint. Three branches and a fall-through cost
	 * nothing and remove the question. */
	if (_type == NSNotPredicateType) {
		/* One child, by construction (see the header). An empty NOT is vacuously true, which
		 * is the same identity AND uses. */
		return count == 0 ? YES
			: ![[_subpredicates objectAtIndex:0] evaluateWithObject:object];
	}
	if (_type == NSAndPredicateType) {
		for (i = 0; i < count; i++) {
			if (![[_subpredicates objectAtIndex:i] evaluateWithObject:object]) {
				return NO;	/* short-circuit: the first NO decides */
			}
		}
		return YES;		/* AND of nothing is YES */
	}
	if (_type == NSOrPredicateType) {
		for (i = 0; i < count; i++) {
			if ([[_subpredicates objectAtIndex:i] evaluateWithObject:object]) {
				return YES;	/* short-circuit: the first YES decides */
			}
		}
		return NO;		/* OR of nothing is NO */
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: %d is not a compound predicate type",
			   [self class], @"evaluateWithObject:", (int)_type];
	return NO;
}

- (NSString *)predicateFormat
{
	NSMutableString *out;
	NSUInteger i;

	if (_type == NSNotPredicateType) {
		if ([_subpredicates count] == 0) {
			return @"NOT (none)";
		}
		return [NSString stringWithFormat:@"NOT %@",
			[[_subpredicates objectAtIndex:0] predicateFormat]];
	}
	out = [[NSMutableString alloc] initWithString:@"("];
	for (i = 0; i < [_subpredicates count]; i++) {
		if (i > 0) {
			[out appendString:_type == NSAndPredicateType ? @" AND " : @" OR "];
		}
		[out appendString:[[_subpredicates objectAtIndex:i] predicateFormat]];
	}
	[out appendString:@")"];
	return out;
}

@end

/* ------------------------------------------------------- the comparison leaf */

/* ASCII case folding, which is what `[c]` means HERE: the library's case rules are its own (the
 * locale family says so), and a predicate must not pretend to more than it does. */
static char fn_fold_byte(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* LIKE: `*` is any run, `?` is any one byte, and `\` escapes the next byte — INCLUDING a `?` or
 * a `*` meant literally, which is why the escape is remembered in `literal` instead of being
 * applied and forgotten. Byte-wise on the UTF-8 spelling, which is exact for the patterns this
 * grammar writes. */
static BOOL fn_like_bytes(const char *text, const char *pattern, BOOL caseInsensitive)
{
	while (*pattern != '\0') {
		BOOL literal = NO;

		if (*pattern == '*') {
			pattern++;
			if (*pattern == '\0') {
				return YES;	/* a trailing star takes the rest */
			}
			while (*text != '\0') {
				if (fn_like_bytes(text, pattern, caseInsensitive)) {
					return YES;
				}
				text++;
			}
			return NO;
		}
		if (*text == '\0') {
			return NO;
		}
		if (*pattern == '\\' && *(pattern + 1) != '\0') {
			pattern++;
			literal = YES;
		}
		if (!literal && *pattern == '?') {
			pattern++;
			text++;
			continue;
		}
		if (caseInsensitive ? (fn_fold_byte(*pattern) != fn_fold_byte(*text))
				    : (*pattern != *text)) {
			return NO;
		}
		pattern++;
		text++;
	}
	return *text == '\0';
}

static BOOL fn_strings_equal(NSString *left, NSString *right, BOOL caseInsensitive)
{
	if (caseInsensitive) {
		return [[left lowercaseString] isEqualToString:[right lowercaseString]];
	}
	return [left isEqualToString:right];
}

/* A literal, as the GRAMMAR would write it — so that parsing a rendered predicate gives the same
 * tree back. A string is quoted (with the two escapes that would otherwise be ambiguous), a
 * number is its own description, and nothing is NULL. */
static NSString *fn_literal_format(id literal)
{
	if (literal == nil) {
		return @"NULL";
	}
	if ([literal isKindOfClass:[NSString class]]) {
		NSMutableString *out = [[NSMutableString alloc] initWithString:@"\""];
		const char *bytes = [(NSString *)literal UTF8String];
		size_t i;

		for (i = 0; bytes[i] != '\0'; i++) {
			char one[2];

			if (bytes[i] == '"' || bytes[i] == '\\') {
				[out appendString:@"\\"];
			}
			one[0] = bytes[i];
			one[1] = '\0';
			[out appendString:[NSString stringWithUTF8String:one]];
		}
		[out appendString:@"\""];
		return out;
	}
	return [literal description];
}

static NSString *fn_operator_name(FNCompareOperator op)
{
	if (op == FNCompareEqual) return @"=";
	if (op == FNCompareNotEqual) return @"!=";
	if (op == FNCompareLess) return @"<";
	if (op == FNCompareLessOrEqual) return @"<=";
	if (op == FNCompareGreater) return @">";
	if (op == FNCompareGreaterOrEqual) return @">=";
	if (op == FNCompareContains) return @"CONTAINS";
	if (op == FNCompareBeginsWith) return @"BEGINSWITH";
	if (op == FNCompareEndsWith) return @"ENDSWITH";
	if (op == FNCompareMatches) return @"MATCHES";
	if (op == FNCompareIn) return @"IN";
	if (op == FNCompareBetween) return @"BETWEEN";
	return @"LIKE";
}

/* ONE PLACE RESOLVES AN OPERAND: a path goes through KVC (so `inner.name` works and a SCALAR
 * comes back boxed, F9), a literal is itself, and SELF is the object. */
static id fn_operand_value(id object, NSString *path, id literal)
{
	if (path == nil) {
		return literal;
	}
	if ([path isEqualToString:@"SELF"]) {
		return object;
	}
	return [object valueForKey:path];
}

/*
 * THE TWO F13.7d HELPERS.
 *
 * fn_matches is musl's POSIX ERE engine, reached through <regex.h> — the engine lives INSIDE libc,
 * so MATCHES costs no new artifact (F12's zlib lesson with a shorter walk). ONE RULE GOES IN ABOVE
 * THE ENGINE: NSPredicate's MATCHES is a WHOLE-STRING match — "abc" MATCHES "b" is NO — while
 * regexec is unanchored, so the pattern is anchored here, once, rather than left to a caller's
 * memory of it.
 *
 * fn_collation is ICU's collator, and the STRENGTHS are exactly what the modifiers mean: SECONDARY
 * ignores case, PRIMARY ignores case AND accents, and PRIMARY with the case level turned on ignores
 * accents while KEEPING case — which is `[d]` on its own.
 */
static BOOL fn_matches(NSString *text, NSString *pattern, BOOL caseInsensitive)
{
	const char *patternText = [pattern UTF8String];
	char *anchored;
	size_t length;
	regex_t regex;
	int status;
	BOOL matched;

	/* THE ANCHOR IS A PLAIN GROUP, and that is a MEASURED constraint rather than a preference:
	 * POSIX ERE has no NON-CAPTURING group, so "^(?:...)$" is an invalid expression and regcomp
	 * refuses it — which is how this was found, as an abort in the probe rather than a wrong
	 * answer. A capturing group is fine here because the captures are never read. */
	length = strlen(patternText) + sizeof "^([])$";
	anchored = (char *)malloc(length);
	if (anchored == NULL) {
		return NO;
	}
	snprintf(anchored, length, "^(%s)$", patternText);
	status = regcomp(&regex, anchored, REG_EXTENDED | (caseInsensitive ? REG_ICASE : 0));
	free(anchored);
	if (status != 0) {
		char why[160];

		regerror(status, &regex, why, sizeof why);
		[NSException raise:NSInvalidArgumentException
			    format:@"MATCHES: \"%@\" is not a regular expression this engine accepts: %s",
				   pattern, why];
	}
	/* The pattern's borrowed bytes are done with, so the text's may be taken now. */
	matched = regexec(&regex, [text UTF8String], 0, NULL, 0) == 0;
	regfree(&regex);
	return matched;
}

static int fn_collation(NSString *left, NSString *right, BOOL caseInsensitive,
			BOOL diacriticInsensitive)
{
	UErrorCode status = U_ZERO_ERROR;
	UCollator *collator;
	UChar *a;
	UChar *b;
	int32_t alen = 0;
	int32_t blen = 0;
	UCollationResult result;

	collator = ucol_open("", &status);	/* the ROOT locale: a rule, not a taste */
	if (U_FAILURE(status) || collator == NULL) {
		return UCOL_EQUAL;
	}
	if (diacriticInsensitive && !caseInsensitive) {
		ucol_setStrength(collator, UCOL_PRIMARY);
		ucol_setAttribute(collator, UCOL_CASE_LEVEL, UCOL_ON, &status);
	} else if (diacriticInsensitive) {
		ucol_setStrength(collator, UCOL_PRIMARY);
	} else if (caseInsensitive) {
		ucol_setStrength(collator, UCOL_SECONDARY);
	}
	a = (UChar *)malloc(([left lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1) * sizeof(UChar));
	b = (UChar *)malloc(([right lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1) * sizeof(UChar));
	if (a == NULL || b == NULL) {
		free(a);
		free(b);
		ucol_close(collator);
		return UCOL_EQUAL;
	}
	status = U_ZERO_ERROR;
	u_strFromUTF8(a, (int32_t)([left lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1), &alen, [left UTF8String], -1, &status);
	u_strFromUTF8(b, (int32_t)([right lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1), &blen, [right UTF8String], -1, &status);
	if (U_FAILURE(status)) {
		free(a);
		free(b);
		ucol_close(collator);
		return UCOL_EQUAL;
	}
	result = ucol_strcoll(collator, a, alen, b, blen);
	free(a);
	free(b);
	ucol_close(collator);
	return (int)result;
}

/*
 * THE ONE COMPARISON RULE, reached from TWO doors. The grammar's leaves ask it directly — they ARE
 * its operands — and NSComparisonPredicate asks it with BOTH OPERANDS AS LITERALS, which
 * `fn_operand_value` answers as itself, so the rule is stated once and cannot answer one way when a
 * predicate was parsed and another when it was built from expressions.
 *
 * IN AND BETWEEN LIVE HERE rather than in the leaf's own switch because the GRAMMAR does not
 * produce them (its header still refuses both by name) while the expression door does.
 */
BOOL FNCompareValues(FNCompareOperator op, id _Nullable left, id _Nullable right,
		     BOOL caseInsensitive, BOOL diacriticInsensitive)
{
	if (op == FNCompareIn) {
		NSArray *members = nil;
		NSUInteger i;

		if ([right isKindOfClass:[NSSet class]]) {
			members = [(NSSet *)right allObjects];
		} else if ([right isKindOfClass:[NSArray class]]) {
			members = (NSArray *)right;
		}
		if (members == nil) {
			[NSException raise:NSInvalidArgumentException
				    format:@"IN needs a collection on the right, and this is %@", [right class]];
		}
		for (i = 0; i < [members count]; i++) {
			if (FNCompareValues(FNCompareEqual, left, [members objectAtIndex:i],
					    caseInsensitive, diacriticInsensitive)) {
				return YES;
			}
		}
		return NO;
	}
	if (op == FNCompareBetween) {
		NSArray *bounds = [right isKindOfClass:[NSArray class]] ? (NSArray *)right : nil;

		if (bounds == nil || [bounds count] != 2) {
			[NSException raise:NSInvalidArgumentException
				    format:@"BETWEEN needs TWO values on the right (low, high), and got %@",
				   right];
		}
		/* INCLUSIVE AT BOTH ENDS: `low <= value && value <= high`. */
		return FNCompareValues(FNCompareLessOrEqual, [bounds objectAtIndex:0], left,
				       caseInsensitive, diacriticInsensitive) &&
		       FNCompareValues(FNCompareLessOrEqual, left, [bounds objectAtIndex:1],
				       caseInsensitive, diacriticInsensitive);
	}
	{
		FNPredicateComparison *leaf = [[FNPredicateComparison alloc]
			initWithLeftPath:nil
			     leftLiteral:left
				operator:op
			       rightPath:nil
			    rightLiteral:right
			 caseInsensitive:caseInsensitive
		   diacriticInsensitive:diacriticInsensitive];

		return [leaf evaluateWithObject:nil];
	}
}

@implementation FNPredicateComparison

- (instancetype)initWithLeftPath:(nullable NSString *)leftPath
		     leftLiteral:(nullable id)leftLiteral
			operator:(FNCompareOperator)op
		       rightPath:(nullable NSString *)rightPath
		    rightLiteral:(nullable id)rightLiteral
		 caseInsensitive:(BOOL)caseInsensitive
	   diacriticInsensitive:(BOOL)diacriticInsensitive
{
	if ((self = [super init]) != nil) {
		_leftPath = leftPath;
		_leftLiteral = leftLiteral;
		_rightPath = rightPath;
		_rightLiteral = rightLiteral;
		_op = op;
		_caseInsensitive = caseInsensitive;
		_diacriticInsensitive = diacriticInsensitive;
	}
	return self;
}

- (BOOL)evaluateWithObject:(nullable id)object
{
	id left = fn_operand_value(object, _leftPath, _leftLiteral);
	id right = fn_operand_value(object, _rightPath, _rightLiteral);
	NSComparisonResult order;

	if (_op == FNCompareEqual || _op == FNCompareNotEqual) {
		BOOL equal;

		if (left == nil || right == nil) {
			/* Nothing equals anything except nothing: NULL = NULL is true, and NULL = 1
			 * is false rather than an error. */
			equal = (left == nil && right == nil);
		} else if ([left isKindOfClass:[NSString class]] &&
			   [right isKindOfClass:[NSString class]]) {
			/* `[d]` GOES THROUGH ICU'S COLLATOR (F13.7d), and ONLY then: the byte-wise path
			 * stays what a plain predicate uses, so adding the modifier cannot change any
			 * predicate that does not ask for it. */
			equal = _diacriticInsensitive
				? (fn_collation(left, right, _caseInsensitive, YES) == UCOL_EQUAL)
				: fn_strings_equal(left, right, _caseInsensitive);
		} else {
			equal = [left isEqual:right];
		}
		return _op == FNCompareEqual ? equal : !equal;
	}
	if (_op == FNCompareMatches) {
		if (![left isKindOfClass:[NSString class]] ||
		    ![right isKindOfClass:[NSString class]]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"MATCHES needs two strings, and these are not: %@ and %@",
					   [left class], [right class]];
		}
		if (_diacriticInsensitive) {
			/* NAMED, and narrow: the POSIX engine is byte-oriented and has no diacritic
			 * mode, so this COMBINATION is refused rather than answered approximately.
			 * `[c]` works, and `[d]` works on the comparison operators. */
			[NSException raise:NSInvalidArgumentException
				    format:@"MATCHES with the [d] modifier is refused: the POSIX engine has "
					   "no diacritic mode. Use [c], or [d] with =, !=, <, <=, > or >="];
		}
		return fn_matches((NSString *)left, (NSString *)right, _caseInsensitive);
	}
	if (_op == FNCompareContains || _op == FNCompareBeginsWith ||
	    _op == FNCompareEndsWith || _op == FNCompareLike) {
		if (![left isKindOfClass:[NSString class]] ||
		    ![right isKindOfClass:[NSString class]]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"%@ needs two strings, and these are not: %@ and %@",
					   fn_operator_name(_op), [left class], [right class]];
		}
		/* A FOLDED pair when `[c]`, so the folding rule is stated once instead of being
		 * trusted to a search option meaning the same thing a second time. */
		if (_caseInsensitive) {
			left = [left lowercaseString];
			right = [right lowercaseString];
		}
		{
			const char *haystack = [(NSString *)left UTF8String];
			const char *needle = [(NSString *)right UTF8String];

			if (_op == FNCompareLike) {
				return fn_like_bytes(haystack, needle, NO);
			}
			if (_op == FNCompareBeginsWith) {
				return strncmp(haystack, needle, strlen(needle)) == 0;
			}
			if (_op == FNCompareEndsWith) {
				size_t h = strlen(haystack);
				size_t n = strlen(needle);

				return n <= h && strcmp(haystack + (h - n), needle) == 0;
			}
			return strstr(haystack, needle) != NULL;
		}
	}
	/* THE ORDERING OPERATORS: both sides have to be able to compare themselves, and nothing
	 * has a place in an order. */
	if (left == nil || right == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"an ordering comparison cannot ask about nothing"];
	}
	if (![left respondsToSelector:@selector(compare:)]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"%@ cannot be compared with <, <=, > or >=", [left class]];
	}
	if (_diacriticInsensitive && [left isKindOfClass:[NSString class]] &&
	    [right isKindOfClass:[NSString class]]) {
		int collated = fn_collation(left, right, NO, YES);

		order = collated == UCOL_LESS ? NSOrderedAscending
		      : (collated == UCOL_GREATER ? NSOrderedDescending : NSOrderedSame);
	} else {
		order = [left compare:right];
	}
	if (_op == FNCompareLess) {
		return order == NSOrderedAscending;
	}
	if (_op == FNCompareLessOrEqual) {
		return order != NSOrderedDescending;
	}
	if (_op == FNCompareGreater) {
		return order == NSOrderedDescending;
	}
	return order != NSOrderedAscending;	/* FNCompareGreaterOrEqual */
}

- (NSString *)predicateFormat
{
	NSMutableString *out = [[NSMutableString alloc] init];

	[out appendString:_leftPath != nil ? _leftPath : fn_literal_format(_leftLiteral)];
	[out appendString:@" "];
	[out appendString:fn_operator_name(_op)];
	[out appendString:@" "];
	[out appendString:_rightPath != nil ? _rightPath : fn_literal_format(_rightLiteral)];
	if (_caseInsensitive || _diacriticInsensitive) {
		[out appendString:@"["];
		if (_caseInsensitive) {
			[out appendString:@"c"];
		}
		if (_diacriticInsensitive) {
			[out appendString:@"d"];
		}
		[out appendString:@"]"];
	}
	return out;
}

@end
