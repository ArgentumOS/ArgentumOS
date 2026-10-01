/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSExpression — a value described as a TREE you can evaluate against an object. F13.10,
 * docs/design/foundation-plan.md §10.
 *
 * WHY IT IS NOT NSPREDICATE'S BUSINESS: F11 built a predicate as a tree of its own nodes, which is
 * all a predicate needs. An expression is the OTHER public half of that idea — a standalone thing a
 * caller can build, hand around, compare and evaluate (`[expression evaluateWithObject:row]`), and
 * `NSComparisonPredicate` is what puts two of them together. So the two coexist: this library's
 * `NSPredicate` keeps its own representation, and `NSExpression` is the API Apple documents.
 *
 * WHAT IS FAITHFUL, and it is the part callers use: every constructor whose evaluation this library
 * can actually perform — constant, evaluated object, variable, key path, aggregate, the three SET
 * operations, and the fold functions over a collection (sum, count, min, max, average) — plus the
 * doors that read a tree back (`-expressionType`, `-constantValue`, `-keyPath`, `-variable`,
 * `-function`, `-arguments`, `-operand`, `-collection`), `-evaluateWithObject:`, the
 * context-taking form that resolves VARIABLES, and equality that compares the tree.
 *
 * WHAT IS NOT, named: `+expressionForBlock:` (a block leaf whose block no coder here can carry),
 * and the `+expressionWithFormat:` family (an expression-format PARSER is a family of its own, and
 * the predicate grammar builds predicates rather than expression trees). The two-argument
 * functions (`castObject:toType:`) are likewise unwritten. `@anyKey` exists as a TYPE but
 * evaluates to nil, as Apple's own documentation says its value is undefined.
 *
 * THE CONDITIONAL AND SUBQUERY KINDS ARE THE PREDICATE FAMILY'S THIRD LEG — the two predicate
 * classes hold expressions, and these two expression kinds hold an `NSPredicate`. A CONDITIONAL
 * carries two branches and evaluates through the predicate's own `-evaluateWithObject:` (so it
 * WORKS on this substrate). A SUBQUERY carries a collection expression, an iterator-variable NAME
 * and a predicate, and its EVALUATION REFUSES BY NAME: the predicate evaluator threads no bindings
 * for the iterator variable, and a wrong collection is worse than a loud refusal.
 */

#ifndef FOUNDATION_NSEXPRESSION_H
#define FOUNDATION_NSEXPRESSION_H

#import <Foundation/NSObject.h>
/* FOR `NSCoding` AND THE `NSCoder` ITS TWO DOORS TAKE (§63.15). */
#import <Foundation/NSCoding.h>

@class NSArray;
@class NSDictionary;
@class NSMutableDictionary;
@class NSString;
@class NSPredicate;

NS_ASSUME_NONNULL_BEGIN

/* COCOA'S CONSTANTS, with Cocoa's values: a predicate serialized somewhere else must mean the same
 * thing here. */
typedef NSUInteger NSExpressionType;
enum {
	NSConstantValueExpressionType = 0,
	NSEvaluatedObjectExpressionType = 1,
	NSVariableExpressionType = 2,
	NSKeyPathExpressionType = 3,
	NSFunctionExpressionType = 4,
	NSUnionSetExpressionType = 5,
	NSIntersectSetExpressionType = 6,
	NSMinusSetExpressionType = 7,
	NSSubqueryExpressionType = 13,
	NSAggregateExpressionType = 14,
	NSAnyKeyExpressionType = 15,
	NSBlockExpressionType = 19,
	NSConditionalExpressionType = 20
};

@interface NSExpression : NSObject <NSCopying, NSCoding>
{
	NSExpressionType _type;
	id _constant;		/* constant value, key path, variable name, left operand, collection */
	id _operand;		/* the right operand of a set operation; the arguments of a function */
	NSString *_function;
	NSPredicate *_subpredicate;	/* a conditional's condition, or a subquery's predicate */
	NSString *_iterator;		/* a subquery's iterator-variable name */
}

/* THE NSCoding DOORS (§63.15). THE TYPE IS PART OF THE PAYLOAD, not a hint: this class's state is ONE TYPE AND
 * THREE SLOTS, and the slots' MEANING is the type (`_constant` alone is a constant, a key path, a variable
 * name, a left operand or an aggregate's members). Reconstructing the slots without the tag would produce an
 * expression nothing could evaluate — so the tag is written, and it is written as an INTEGER whose VALUES ARE
 * COCOA'S (the enum above says why: the constants are pinned so a predicate serialized elsewhere means the
 * same thing here).
 *
 * AND IT IS A PREREQUISITE RATHER THAN A PEER of the two predicate classes' doors: a comparison predicate
 * HOLDS two expressions, and a compound one holds subpredicates, so neither can round-trip until an expression
 * can. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;

+ (NSExpression *)expressionForConstantValue:(nullable id)object;
+ (NSExpression *)expressionForEvaluatedObject;
+ (NSExpression *)expressionForVariable:(NSString *)string;
+ (NSExpression *)expressionForKeyPath:(NSString *)keyPath;
+ (NSExpression *)expressionForFunction:(NSString *)name arguments:(NSArray *)arguments;
/* The older spelling, still in Cocoa's header: the target and selector are ignored, as they are
 * there, because what a function expression evaluates is decided by the NAME. */
+ (NSExpression *)expressionForFunction:(nullable id)target
			   selectorName:(NSString *)name
			      arguments:(nullable NSArray *)arguments;
+ (NSExpression *)expressionForAnyKey;
+ (NSExpression *)expressionForAggregate:(NSArray *)collection;
+ (NSExpression *)expressionForUnionSet:(NSExpression *)left with:(NSExpression *)right;
+ (NSExpression *)expressionForIntersectSet:(NSExpression *)left with:(NSExpression *)right;
+ (NSExpression *)expressionForMinusSet:(NSExpression *)left with:(NSExpression *)right;

/* THE CONDITIONAL AND SUBQUERY KINDS, in Apple's exact spelling. A conditional's condition, and a
 * subquery's filter, are an `NSPredicate` — the predicate family's own type. */
+ (NSExpression *)expressionForSubquery:(NSExpression *)expression
		  usingIteratorVariable:(NSString *)variable
			      predicate:(NSPredicate *)predicate;
+ (NSExpression *)expressionForConditional:(NSPredicate *)predicate
			   trueExpression:(NSExpression *)trueExpression
			  falseExpression:(NSExpression *)falseExpression;

/* Apple's designated initializer: a bare node of a chosen type, its slots empty. */
- (instancetype)initWithExpressionType:(NSExpressionType)type;

- (NSExpressionType)expressionType;
- (nullable id)constantValue;
- (nullable NSString *)keyPath;
- (nullable NSString *)variable;
- (nullable NSString *)function;
- (nullable NSArray *)arguments;
- (nullable id)operand;
- (nullable id)collection;

/* THE TWO SIDES OF A SET EXPRESSION (Apple's header says exactly that; a subquery's collection is
 * its left side too). */
- (nullable NSExpression *)leftExpression;
- (nullable NSExpression *)rightExpression;
/* A subquery's or a conditional's predicate. */
- (nullable NSPredicate *)predicate;
/* A conditional's two branches. */
- (nullable NSExpression *)trueExpression;
- (nullable NSExpression *)falseExpression;

- (nullable id)evaluateWithObject:(nullable id)object;
- (nullable id)expressionValueWithObject:(nullable id)object
				 context:(nullable NSMutableDictionary *)context;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSEXPRESSION_H */
