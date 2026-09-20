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
 * WHAT IS NOT, named: `+expressionForBlock:` and the conditional/block types (this library has no
 * blocks in its public headers), `NSSubqueryExpressionType`, and the two-argument functions
 * (`castObject:toType:`). `@anyKey` exists as a TYPE but evaluates to nil, as Apple's own
 * documentation says its value is undefined.
 */

#ifndef FOUNDATION_NSEXPRESSION_H
#define FOUNDATION_NSEXPRESSION_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSDictionary;
@class NSMutableDictionary;
@class NSString;

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
	NSSubqueryExpressionType = 8,
	NSAggregateExpressionType = 9,
	NSAnyKeyExpressionType = 10,
	NSBlockExpressionType = 11,
	NSConditionalExpressionType = 12
};

@interface NSExpression : NSObject <NSCopying>
{
	NSExpressionType _type;
	id _constant;		/* constant value, key path, variable name, left operand, collection */
	id _operand;		/* the right operand of a set operation; the arguments of a function */
	NSString *_function;
}

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

- (NSExpressionType)expressionType;
- (nullable id)constantValue;
- (nullable NSString *)keyPath;
- (nullable NSString *)variable;
- (nullable NSString *)function;
- (nullable NSArray *)arguments;
- (nullable id)operand;
- (nullable id)collection;

- (nullable id)evaluateWithObject:(nullable id)object;
- (nullable id)expressionValueWithObject:(nullable id)object
				 context:(nullable NSMutableDictionary *)context;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSEXPRESSION_H */
