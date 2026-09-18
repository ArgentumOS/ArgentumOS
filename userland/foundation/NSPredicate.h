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
 * on. The SECOND half is the FORMAT GRAMMAR (`+predicateWithFormat:`), which parses a string
 * into exactly this tree; until it lands, a predicate is BUILT rather than written.
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
 *     laid over the grammar, and the grammar itself is not written yet.
 */

#ifndef FOUNDATION_NSPREDICATE_H
#define FOUNDATION_NSPREDICATE_H

#import <foundation/NSObject.h>

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

/* THE ABSTRACT ONE. Raises in the base class (see the header note); every real predicate
 * answers. `object` is nullable because a predicate may legitimately be asked about nil. */
- (BOOL)evaluateWithObject:(nullable id)object;

/* The rendering — what the predicate says, as text. The grammar half (F11b) has to AGREE with
 * this, which is one reason it exists before the parser does. */
- (NSString *)predicateFormat;

@end

/*
 * THE TREE NODE, and a rule in the plainest sense: ask the children, combine the booleans.
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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPREDICATE_H */
