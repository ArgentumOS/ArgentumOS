/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_expression, unit of 1 — F13.10's acceptance for NSExpression.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>. The objects it evaluates against are private
 * to this file.
 *
 * WHAT IT MEASURES, with the numbers in every detail:
 *   expr-constant          the type, the value and the evaluation of a constant;
 *   expr-keypath           an accessor reached BY NAME, and the same through a DOTTED path;
 *   expr-evaluated-object  @SELF, which is the object it is evaluated against;
 *   expr-variable-context  a VARIABLE resolved from the context — and nil when there is no context,
 *                          which is the honest answer rather than a guess;
 *   expr-aggregate         a collection whose NESTED expressions are evaluated as it is;
 *   expr-set-operations    union/intersect/minus, including the rule that TWO SETS answer a SET and
 *                          anything else answers an ARRAY;
 *   expr-fold-functions    sum/count/min/max over a key path's collection;
 *   expr-equality          two separately-built trees that are equal, and their descriptions.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

/* THE OBJECT EXPRESSIONS ARE EVALUATED AGAINST. */
@interface ExprThing : NSObject
{
	NSInteger _amount;
	NSArray *_values;
	ExprThing *_child;
}
- (NSInteger)amount;
- (void)setAmount:(NSInteger)amount;
- (NSArray *)values;
- (ExprThing *)child;
- (void)setChild:(ExprThing *)child;
@end

@implementation ExprThing

- (NSInteger)amount { return _amount; }
- (void)setAmount:(NSInteger)amount { _amount = amount; }
- (NSArray *)values { return _values; }
- (ExprThing *)child { return _child; }
- (void)setChild:(ExprThing *)child { _child = child; }

- (instancetype)initWithAmount:(NSInteger)amount values:(NSArray *)values
{
	if ((self = [super init]) != nil) {
		_amount = amount;
		_values = values;
	}
	return self;
}

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-EXPRESSION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-EXPRESSION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		NSExpression *constant = [NSExpression expressionForConstantValue:@42];

		check("expr-constant",
		      constant != nil &&
		      [constant expressionType] == NSConstantValueExpressionType &&
		      [[constant constantValue] integerValue] == 42 &&
		      [[constant evaluateWithObject:nil] integerValue] == 42 &&
		      [constant keyPath] == nil && [constant variable] == nil,
		      [NSString stringWithFormat:@"type=%lu value=%@ eval=%@",
			(unsigned long)[constant expressionType], [constant constantValue],
			[constant evaluateWithObject:nil]]);
	}

	{
		ExprThing *child = [[ExprThing alloc] initWithAmount:5 values:@[]];
		ExprThing *thing = [[ExprThing alloc] initWithAmount:7 values:@[@1, @2, @3]];
		NSExpression *flat = [NSExpression expressionForKeyPath:@"amount"];
		NSExpression *dotted = [NSExpression expressionForKeyPath:@"child.amount"];

		[thing setChild:child];
		check("expr-keypath",
		      flat != nil && [[flat keyPath] isEqualToString:@"amount"] &&
		      [[flat evaluateWithObject:thing] integerValue] == 7 &&
		      [[dotted evaluateWithObject:thing] integerValue] == 5 &&
		      [flat evaluateWithObject:nil] == nil,
		      [NSString stringWithFormat:@"flat=%@ dotted=%@ nullObject=%@",
			[flat evaluateWithObject:thing], [dotted evaluateWithObject:thing],
			[flat evaluateWithObject:nil]]);
	}

	{
		ExprThing *thing = [[ExprThing alloc] initWithAmount:7 values:@[]];
		NSExpression *self_ = [NSExpression expressionForEvaluatedObject];

		check("expr-evaluated-object",
		      self_ != nil &&
		      [self_ expressionType] == NSEvaluatedObjectExpressionType &&
		      [self_ evaluateWithObject:thing] == thing &&
		      [self_ evaluateWithObject:nil] == nil,
		      [NSString stringWithFormat:@"type=%lu same=%d",
			(unsigned long)[self_ expressionType],
			(int)([self_ evaluateWithObject:thing] == thing)]);
	}

	{
		NSExpression *variable = [NSExpression expressionForVariable:@"limit"];
		NSMutableDictionary *context = [NSMutableDictionary dictionary];

		[context setObject:@250 forKey:@"limit"];
		check("expr-variable-context",
		      variable != nil && [[variable variable] isEqualToString:@"limit"] &&
		      [[variable expressionValueWithObject:nil context:context] integerValue] == 250 &&
		      [variable expressionValueWithObject:nil context:nil] == nil &&
		      [variable evaluateWithObject:nil] == nil,
		      [NSString stringWithFormat:@"with=%@ without=%@",
			[variable expressionValueWithObject:nil context:context],
			[variable evaluateWithObject:nil]]);
	}

	{
		ExprThing *thing = [[ExprThing alloc] initWithAmount:7 values:@[]];
		NSExpression *aggregate = [NSExpression expressionForAggregate:
			@[@"fixed", [NSExpression expressionForKeyPath:@"amount"]]];
		id evaluated = [aggregate evaluateWithObject:thing];

		check("expr-aggregate",
		      aggregate != nil &&
		      [aggregate expressionType] == NSAggregateExpressionType &&
		      [aggregate collection] != nil &&
		      [evaluated isKindOfClass:[NSArray class]] && [evaluated count] == 2 &&
		      [[evaluated objectAtIndex:0] isEqualToString:@"fixed"] &&
		      [[evaluated objectAtIndex:1] integerValue] == 7,
		      [NSString stringWithFormat:@"evaluated=%@", evaluated]);
	}

	{
		NSExpression *leftSet = [NSExpression expressionForConstantValue:
					[NSSet setWithArray:@[@1, @2]]];
		NSExpression *rightSet = [NSExpression expressionForConstantValue:
					 [NSSet setWithArray:@[@2, @3]]];
		NSExpression *leftArray = [NSExpression expressionForConstantValue:@[@1, @2]];
		NSExpression *rightArray = [NSExpression expressionForConstantValue:@[@2, @3]];
		id unioned = [[NSExpression expressionForUnionSet:leftSet with:rightSet]
				evaluateWithObject:nil];
		id intersected = [[NSExpression expressionForIntersectSet:leftSet with:rightSet]
				evaluateWithObject:nil];
		id minused = [[NSExpression expressionForMinusSet:leftSet with:rightSet]
				evaluateWithObject:nil];
		id arrayUnion = [[NSExpression expressionForUnionSet:leftArray with:rightArray]
				evaluateWithObject:nil];

		check("expr-set-operations",
		      unioned != nil && [unioned isKindOfClass:[NSSet class]] && [unioned count] == 3 &&
		      intersected != nil && [intersected count] == 1 &&
		      [intersected containsObject:@2] &&
		      minused != nil && [minused count] == 1 && [minused containsObject:@1] &&
		      /* TWO ARRAYS ANSWER AN ARRAY, because the result's KIND follows the operands. */
		      arrayUnion != nil && [arrayUnion isKindOfClass:[NSArray class]] &&
		      [arrayUnion count] == 3,
		      [NSString stringWithFormat:@"counts=union %lu intersect %lu minus %lu array %lu"
						" isSet=%d/%d/%d isArray=%d",
			(unsigned long)(unioned != nil ? [unioned count] : 0),
			(unsigned long)(intersected != nil ? [intersected count] : 0),
			(unsigned long)(minused != nil ? [minused count] : 0),
			(unsigned long)(arrayUnion != nil ? [arrayUnion count] : 0),
			(int)(unioned != nil && [unioned isKindOfClass:[NSSet class]]),
			(int)(intersected != nil && [intersected isKindOfClass:[NSSet class]]),
			(int)(minused != nil && [minused isKindOfClass:[NSSet class]]),
			(int)(arrayUnion != nil && [arrayUnion isKindOfClass:[NSArray class]])]);
	}

	{
		ExprThing *thing = [[ExprThing alloc] initWithAmount:7 values:@[@1, @2, @3]];
		NSArray *arguments = @[[NSExpression expressionForKeyPath:@"values"]];
		NSExpression *sum = [NSExpression expressionForFunction:@"sum:" arguments:arguments];
		NSExpression *count = [NSExpression expressionForFunction:@"count:" arguments:arguments];
		NSExpression *max = [NSExpression expressionForFunction:@"max:" arguments:arguments];

		check("expr-fold-functions",
		      sum != nil && [sum expressionType] == NSFunctionExpressionType &&
		      [[sum function] isEqualToString:@"sum:"] &&
		      [[sum evaluateWithObject:thing] doubleValue] == 6.0 &&
		      [[count evaluateWithObject:thing] integerValue] == 3 &&
		      [[max evaluateWithObject:thing] integerValue] == 3,
		      [NSString stringWithFormat:@"sum=%@ count=%@ max=%@",
			[sum evaluateWithObject:thing], [count evaluateWithObject:thing],
			[max evaluateWithObject:thing]]);
	}

	{
		NSExpression *one = [NSExpression expressionForKeyPath:@"amount"];
		NSExpression *two = [NSExpression expressionForKeyPath:@"amount"];
		NSExpression *different = [NSExpression expressionForConstantValue:@1];

		check("expr-equality-and-description",
		      [one isEqual:two] && [one hash] == [two hash] &&
		      ![one isEqual:different] &&
		      [one copy] == one &&
		      [[one description] isEqualToString:@"amount"],
		      [NSString stringWithFormat:@"equal=%d description=%@",
			(int)[one isEqual:two], [one description]]);
	}

	printf("FOUNDATION-EXPRESSION RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-EXPRESSION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-EXPRESSION DONE\n");
	return failc ? 1 : 0;
}
