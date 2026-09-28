/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPredicate — a question about an object, as a VALUE. docs/design/foundation-plan.md, F11a.
 *
 * THE FAMILY HAS TWO HALVES AND THIS IS THE FIRST. A predicate OBJECT is a tree: leaves that
 * answer about one object, and nodes that combine their children. That tree needs no parser at
 * all — `+predicateWithValue:` and `+predicateWithBlock:` build the leaves and
 * `NSCompoundPredicate` builds the nodes — and it is what `-filteredArrayUsingPredicate:` waits
 * on. The SECOND half is the FORMAT GRAMMAR (`+predicateWithFormat:`, F11b), which parses a
 * string into exactly this tree — so a predicate can be BUILT or written, and both roads end at
 * the same nodes.
 *
 * THE BASE CLASS IS ABSTRACT, AND RAISES. A predicate with no rule of its own has no answer, and
 * a default of NO would be a LIE about what was asked: the caller would read "this object does
 * not match" where the truth is "nothing was asked". `NSString` raises from its unimplemented
 * primitives for the same reason.
 *
 * WHAT IS REFUSED BY NAME (F11's boundary, in the plan):
 *   - `MATCHES`: a regex, and a regex engine is a TABLE — none ships here. (`LIKE`, with its
 *     `*`/`?` wildcards, is the rule-based alternative, and it ships with the grammar half.)
 *   - the `[d]` diacritic-insensitive modifier: Unicode decomposition is a table too.
 *   - `NSExpression` and `NSComparisonPredicate`: an expression EVALUATOR is its own family, and
 *     `NSComparisonPredicate`'s whole API is expressed in one.
 *   - `IN`/`BETWEEN`, the `ANY`/`ALL`/`NONE`/`SOME` quantifiers and the aggregate key paths.
 *   - the `…WithFormat:arguments:` SUBSTITUTION forms: `%K` and `%@` are a second quoting rule
 *     laid over the grammar, and `+predicateWithFormat:` takes NO arguments because of it. A
 *     caller with a value to splice writes it into the string, quotes and all: the grammar IS
 *     the interface.
 */

#ifndef FOUNDATION_NSPREDICATE_H
#define FOUNDATION_NSPREDICATE_H

#import <Foundation/NSObject.h>

@class NSString;
@class NSArray;
@class NSDictionary;

NS_ASSUME_NONNULL_BEGIN

/* Not/And/Or, with Cocoa's values — a caller that switches on them gets the numbers it
 * expects. */
typedef enum {
	NSNotPredicateType = 0,
	NSAndPredicateType,
	NSOrPredicateType
} NSCompoundPredicateType;

@interface NSPredicate : NSObject <NSCopying>

/* THE TWO LEAVES THAT NEED NO PARSER: a constant, and a caller's own test. */
+ (instancetype)predicateWithValue:(BOOL)value;

/* `bindings` is nullable because NOTHING here substitutes: the parameter exists so the block has
 * Cocoa's shape, and it is always nil. The header says so rather than leaving a caller to
 * discover it. */
+ (instancetype)predicateWithBlock:(BOOL (^)(id _Nullable evaluatedObject,
					    NSDictionary * _Nullable bindings))block;

/*
 * THE GRAMMAR (F11b): the format string is parsed into exactly the tree these classes form —
 * NSCompoundPredicate for the connectives, a comparison leaf for the comparisons.
 *
 * IT TAKES NO ARGUMENTS. Cocoa's is variadic; this one is not, so `…WithFormat:@"x = %@", value`
 * is a COMPILE ERROR rather than a half-supported second quoting rule (the refusal list above).
 * A caller with a value to splice writes it into the string, escapes and all.
 *
 * A BAD FORMAT RAISES rather than answering nil, and the message names the construct — including
 * the ones this grammar REFUSES: `MATCHES`, `[d]`, `IN`, `BETWEEN`, the quantifiers and `$`
 * substitution. A refusal a caller cannot see would be worse than no parser at all.
 */
+ (instancetype)predicateWithFormat:(NSString *)format;
- (instancetype)initWithFormat:(NSString *)format;

/* THE ABSTRACT ONE. Raises in the base class (see the header note); every real predicate
 * answers. `object` is nullable because a predicate may legitimately be asked about nil. */
- (BOOL)evaluateWithObject:(nullable id)object;

/* The rendering — what the predicate says, as text. The grammar half (F11b) has to AGREE with
 * this, which is one reason it exists before the parser does. */
- (NSString *)predicateFormat;

@end

/* THE OPERATOR AND MODIFIER TABLES, with Cocoa's names and values: a predicate serialized
 * somewhere else has to mean the same thing here. */
typedef NSUInteger NSPredicateOperatorType;
enum {
	NSLessThanPredicateOperatorType = 0,
	NSLessThanOrEqualToPredicateOperatorType = 1,
	NSGreaterThanPredicateOperatorType = 2,
	NSGreaterThanOrEqualToPredicateOperatorType = 3,
	NSEqualToPredicateOperatorType = 4,
	NSNotEqualToPredicateOperatorType = 5,
	NSMatchesPredicateOperatorType = 6,
	NSLikePredicateOperatorType = 7,
	NSBeginsWithPredicateOperatorType = 8,
	NSEndsWithPredicateOperatorType = 9,
	NSInPredicateOperatorType = 10,
	NSCustomSelectorPredicateOperatorType = 11,
	NSContainsPredicateOperatorType = 12,
	NSBetweenPredicateOperatorType = 13
};

typedef NSUInteger NSComparisonPredicateModifier;
enum {
	NSDirectPredicateModifier = 0,
	NSAllPredicateModifier = 1,
	NSAnyPredicateModifier = 2
};

/* THE TYPE OF A COMPARISON PREDICATE'S OPTIONS (§62.104). `NSPredicateOptions` BELOW IS APPLE'S DEPRECATED NAME
 * for the same bits — this header had the deprecated spelling as its primary and the modern name missing,
 * which is the shape §62.93 found in the JSON options, reversed the same way. */
typedef NSUInteger NSComparisonPredicateOptions;
typedef NSUInteger NSPredicateOptions;
enum {
	NSCaseInsensitivePredicateOption = 0x01,
	NSDiacriticInsensitivePredicateOption = 0x02,
	NSNormalizedPredicateOption = 0x04,
	NSLocaleSensitivePredicateOption = 0x08
};

@class NSExpression;

/*
 * TWO EXPRESSIONS AND AN OPERATOR — the leaf Cocoa's world is built from, and the door that brings
 * `IN`, `BETWEEN` and the QUANTIFIERS (`ANY`/`ALL`) into this library, none of which the format
 * GRAMMAR accepts (it still refuses them by name, and that has not changed).
 *
 * IT SHARES ITS COMPARISON RULE with the leaf the grammar produces rather than restating it, so the
 * two cannot answer differently about the same pair of values.
 */
@interface NSComparisonPredicate : NSPredicate
{
	NSExpression *_left;
	NSExpression *_right;
	NSComparisonPredicateModifier _modifier;
	NSPredicateOperatorType _operatorType;
	NSPredicateOptions _options;
}

+ (instancetype)predicateWithLeftExpression:(NSExpression *)leftExpression
			     rightExpression:(NSExpression *)rightExpression
				    modifier:(NSComparisonPredicateModifier)modifier
					type:(NSPredicateOperatorType)type
				     options:(NSComparisonPredicateOptions)options;
- (instancetype)initWithLeftExpression:(NSExpression *)leftExpression
		       rightExpression:(NSExpression *)rightExpression
			      modifier:(NSComparisonPredicateModifier)modifier
				  type:(NSPredicateOperatorType)type
			       options:(NSComparisonPredicateOptions)options;

- (NSExpression *)leftExpression;
- (NSExpression *)rightExpression;
- (NSComparisonPredicateModifier)comparisonPredicateModifier;
- (NSPredicateOperatorType)predicateOperatorType;
- (NSPredicateOptions)options;
- (NSString *)predicateFormat;

@end

/* THE TREE NODE, and a rule in the plainest sense: ask the children, combine the booleans.
 * AND of nothing is YES and OR of nothing is NO — the identities, not special cases.
 */
@interface NSCompoundPredicate : NSPredicate

+ (instancetype)andPredicateWithSubpredicates:(NSArray *)subpredicates;
+ (instancetype)orPredicateWithSubpredicates:(NSArray *)subpredicates;
+ (instancetype)notPredicateWithSubpredicate:(NSPredicate *)predicate;
- (instancetype)initWithType:(NSCompoundPredicateType)type subpredicates:(NSArray *)subpredicates;

- (NSCompoundPredicateType)compoundPredicateType;
/* The children. `+notPredicateWithSubpredicate:` stores ONE, so this answers one element — the
 * same shape as AND and OR, which is what lets a walker have a single case. */
- (NSArray *)subpredicates;

@end

/*
 * THE PRIVATE HALF, folded in from fnpredicate.h: the declarations this library shares internally.
 * They are HERE because the public headers are now the only headers - what used to be a
 * private file two units imported is a section of the class's own header. The region is its
 * own one only when this point in the header is outside the header's own (a nested region
 * does not compile, and neither does an unclosed one).
 */
typedef enum {
	FNCompareEqual = 0,
	FNCompareNotEqual,
	FNCompareLess,
	FNCompareLessOrEqual,
	FNCompareGreater,
	FNCompareGreaterOrEqual,
	FNCompareContains,
	FNCompareBeginsWith,
	FNCompareEndsWith,
	FNCompareLike,
	FNCompareMatches,	/* F13.7d: regex, on the engine musl already ships inside libc */
	FNCompareIn,		/* F13.11: membership — reached through NSComparisonPredicate */
	FNCompareBetween	/* F13.11: an inclusive range, whose right side is two values */
} FNCompareOperator;

/*
 * THE GRAMMAR'S ENTRY POINT, as a C FUNCTION rather than a category method on NSPredicate. The
 * two methods that expose it belong to the PRIMARY implementation (a category implementing what
 * the class's own header declares is a warning, and rightly so), so the parser is reached
 * through this instead.
 *
 * It RAISES on a bad format — naming the construct, including the ones the grammar refuses — so
 * it never answers nil.
 */
NSPredicate *FNPredicateParse(NSString *format);

/*
 * THE COMPARISON RULE ITSELF, shared so that the grammar's LEAF and NSComparisonPredicate cannot
 * drift apart. NULL is a value here (nothing equals nothing), a string pair goes through the
 * case/`[d]` rules, and anything else must be able to compare itself. It RAISES on a comparison
 * that has no answer rather than guessing one.
 */
BOOL FNCompareValues(FNCompareOperator op, id _Nullable left, id _Nullable right,
		     BOOL caseInsensitive, BOOL diacriticInsensitive);

/*
 * One comparison. Each side is EITHER a key path (resolved through KVC, F9) OR a literal — a
 * string, a number, or nothing at all (`NULL`/`NIL`, for which BOTH the path and the literal are
 * nil). `SELF` is a path that names the object itself, and the leaf knows that spelling.
 */
@interface FNPredicateComparison : NSPredicate
{
	NSString *_leftPath;
	id _leftLiteral;
	NSString *_rightPath;
	id _rightLiteral;
	FNCompareOperator _op;
	BOOL _caseInsensitive;
	BOOL _diacriticInsensitive;	/* F13.7d: the `[d]` modifier, through ICU's collator */
}
- (instancetype)initWithLeftPath:(nullable NSString *)leftPath
		     leftLiteral:(nullable id)leftLiteral
			operator:(FNCompareOperator)op
		       rightPath:(nullable NSString *)rightPath
		    rightLiteral:(nullable id)rightLiteral
		 caseInsensitive:(BOOL)caseInsensitive
	   diacriticInsensitive:(BOOL)diacriticInsensitive;
@end


NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPREDICATE_H */
