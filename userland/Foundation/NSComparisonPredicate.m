/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSComparisonPredicate.m — two expressions and an operator (F13.11). MANUAL OWNERSHIP.
 *
 * THE DOOR THIS OPENS, and why it is not the grammar's job: `NSPredicate`'s FORMAT GRAMMAR still
 * refuses `IN`, `BETWEEN` and the quantifiers by name — that has not changed, and its header says
 * so. What changes is that the OBJECT MODEL now provides them, exactly as Cocoa does, because a
 * caller with expressions in hand never needed a parser.
 *
 * THE COMPARISON RULE IS NOT RESTATED HERE. `FNCompareValues` is the same function the grammar's
 * leaf calls, so a comparison cannot mean one thing when it was parsed and another when it was
 * built — which is the failure that would be hardest to see.
 *
 * THE QUANTIFIERS ARE THE MODIFIERS: `NSAllPredicateModifier` asks whether EVERY member of the left
 * collection satisfies the comparison and `NSAnyPredicateModifier` whether SOME member does, with
 * the right side evaluated once.
 */

#import <Foundation/NSPredicate.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSExpression.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSSet.h>	/* the ANY/ALL modifiers ask whether the left side is a collection */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#include "NSPredicate.h"

/* APPLE'S OPERATOR TYPES ONTO THE ONE ENUM the grammar and the renderer already share. */
static FNCompareOperator fn_operator_for(NSPredicateOperatorType type)
{
	switch (type) {
	case NSLessThanPredicateOperatorType:
		return FNCompareLess;
	case NSLessThanOrEqualToPredicateOperatorType:
		return FNCompareLessOrEqual;
	case NSGreaterThanPredicateOperatorType:
		return FNCompareGreater;
	case NSGreaterThanOrEqualToPredicateOperatorType:
		return FNCompareGreaterOrEqual;
	case NSEqualToPredicateOperatorType:
		return FNCompareEqual;
	case NSNotEqualToPredicateOperatorType:
		return FNCompareNotEqual;
	case NSMatchesPredicateOperatorType:
		return FNCompareMatches;
	case NSLikePredicateOperatorType:
		return FNCompareLike;
	case NSBeginsWithPredicateOperatorType:
		return FNCompareBeginsWith;
	case NSEndsWithPredicateOperatorType:
		return FNCompareEndsWith;
	case NSContainsPredicateOperatorType:
		return FNCompareContains;
	case NSInPredicateOperatorType:
		return FNCompareIn;
	case NSBetweenPredicateOperatorType:
		return FNCompareBetween;
	default:
		[NSException raise:NSInvalidArgumentException
			    format:@"NSComparisonPredicate: no comparison here for the operator type %lu "
				   "(a custom selector is not an expression comparison)",
				   (unsigned long)type];
		return FNCompareEqual;	/* unreachable */
	}
}

/* COCOA'S SPELLING for a comparison, so a caller that prints a predicate reads the same word the
 * grammar would have used. */
static NSString *fn_operator_text(NSPredicateOperatorType type)
{
	switch (type) {
	case NSLessThanPredicateOperatorType: return @"<";
	case NSLessThanOrEqualToPredicateOperatorType: return @"<=";
	case NSGreaterThanPredicateOperatorType: return @">";
	case NSGreaterThanOrEqualToPredicateOperatorType: return @">=";
	case NSEqualToPredicateOperatorType: return @"==";
	case NSNotEqualToPredicateOperatorType: return @"!=";
	case NSMatchesPredicateOperatorType: return @"MATCHES";
	case NSLikePredicateOperatorType: return @"LIKE";
	case NSBeginsWithPredicateOperatorType: return @"BEGINSWITH";
	case NSEndsWithPredicateOperatorType: return @"ENDSWITH";
	case NSInPredicateOperatorType: return @"IN";
	case NSContainsPredicateOperatorType: return @"CONTAINS";
	case NSBetweenPredicateOperatorType: return @"BETWEEN";
	default: return @"CUSTOM";
	}
}

@implementation NSComparisonPredicate

+ (instancetype)predicateWithLeftExpression:(NSExpression *)leftExpression
			     rightExpression:(NSExpression *)rightExpression
				    modifier:(NSComparisonPredicateModifier)modifier
					type:(NSPredicateOperatorType)type
				     options:(NSPredicateOptions)options
{
	return [[self alloc] initWithLeftExpression:leftExpression
				    rightExpression:rightExpression
					   modifier:modifier
					       type:type
					    options:options];
}

- (instancetype)initWithLeftExpression:(NSExpression *)leftExpression
		       rightExpression:(NSExpression *)rightExpression
			      modifier:(NSComparisonPredicateModifier)modifier
				  type:(NSPredicateOperatorType)type
			       options:(NSPredicateOptions)options
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (leftExpression == nil || rightExpression == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSComparisonPredicate: a comparison needs TWO expressions"];
	}
	_left = leftExpression;
	_right = rightExpression;
	_modifier = modifier;
	_operatorType = type;
	_options = options;
	return self;
}

/* ===================================================================================================
 * THE NSCoding DOORS (§63.16). KEYS OURS (Apple publishes no name for them, §11.6.1 D2's ground), defined
 * beside their only writer and reader as §63.14's and §63.15's are.
 *
 * THE TWO ENUMS RIDE AS INTEGERS, and their VALUES ARE COCOA'S (NSPredicate.h pins them), so this is the
 * interoperable spelling rather than a private one — and it is WHY they are payload: the same two expressions
 * with a different OPERATOR are a different predicate, and `ANY`/`ALL`/`NONE` ask a different question of the
 * same values.
 * =================================================================================================== */
static NSString *const kLeftKey = @"NS.left";
static NSString *const kRightKey = @"NS.right";
static NSString *const kModifierKey = @"NS.modifier";
static NSString *const kOperatorKey = @"NS.operatorType";
static NSString *const kOptionsKey = @"NS.options";

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_left forKey:kLeftKey];
	[coder encodeObject:_right forKey:kRightKey];
	[coder encodeInteger:(NSInteger)_modifier forKey:kModifierKey];
	[coder encodeInteger:(NSInteger)_operatorType forKey:kOperatorKey];
	[coder encodeInteger:(NSInteger)_options forKey:kOptionsKey];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSExpression *left = [coder decodeObjectForKey:kLeftKey];
	NSExpression *right = [coder decodeObjectForKey:kRightKey];

	/* THROUGH THE DESIGNATED DOOR, which is also where the class's own rule lives: it RAISES for a comparison
	 * missing one of its two expressions, so a corrupt archive is refused by the same line that refuses a
	 * caller's half-built comparison rather than by a second check here. */
	return [self initWithLeftExpression:left
			    rightExpression:right
				   modifier:(NSComparisonPredicateModifier)[coder decodeIntegerForKey:kModifierKey]
				       type:(NSPredicateOperatorType)[coder decodeIntegerForKey:kOperatorKey]
				    options:(NSPredicateOptions)[coder decodeIntegerForKey:kOptionsKey]];
}

- (NSExpression *)leftExpression
{
	return _left;
}

- (NSExpression *)rightExpression
{
	return _right;
}

- (NSComparisonPredicateModifier)comparisonPredicateModifier
{
	return _modifier;
}

- (NSPredicateOperatorType)predicateOperatorType
{
	return _operatorType;
}

- (NSPredicateOptions)options
{
	return _options;
}

- (BOOL)evaluateWithObject:(nullable id)object
{
	FNCompareOperator op = fn_operator_for(_operatorType);
	BOOL caseInsensitive = (_options & NSCaseInsensitivePredicateOption) != 0;
	BOOL diacriticInsensitive = (_options & NSDiacriticInsensitivePredicateOption) != 0;
	id left = [_left expressionValueWithObject:object
					   context:nil];
	id right = [_right expressionValueWithObject:object
					     context:nil];

	if (_modifier == NSDirectPredicateModifier) {
		return FNCompareValues(op, left, right, caseInsensitive, diacriticInsensitive);
	}
	/* ANY AND ALL: the LEFT side is the collection, and the right side was evaluated ONCE above
	 * because it does not depend on the member being asked about. */
	if (![left isKindOfClass:[NSArray class]] && ![left isKindOfClass:[NSSet class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSComparisonPredicate: an ANY/ALL comparison needs a collection on "
				   "the left, and this is %@", [left class]];
	}
	{
		NSArray *members = [left isKindOfClass:[NSSet class]]
				 ? [(NSSet *)left allObjects] : (NSArray *)left;
		NSUInteger i;

		for (i = 0; i < [members count]; i++) {
			BOOL satisfied = FNCompareValues(op, [members objectAtIndex:i], right,
							 caseInsensitive, diacriticInsensitive);

			if (_modifier == NSAnyPredicateModifier && satisfied) {
				return YES;
			}
			if (_modifier == NSAllPredicateModifier && !satisfied) {
				return NO;
			}
		}
		/* ALL of nothing is YES and ANY of nothing is NO — the identities, not special cases. */
		return _modifier == NSAllPredicateModifier;
	}
}

- (NSString *)predicateFormat
{
	NSMutableString *out = [NSMutableString string];

	if (_modifier == NSAllPredicateModifier) {
		[out appendString:@"ALL "];
	} else if (_modifier == NSAnyPredicateModifier) {
		[out appendString:@"ANY "];
	}
	[out appendString:[_left description]];
	[out appendString:@" "];
	[out appendString:fn_operator_text(_operatorType)];
	[out appendString:@" "];
	[out appendString:[_right description]];
	if ((_options & NSCaseInsensitivePredicateOption) != 0) {
		[out appendString:@"[c]"];
	}
	if ((_options & NSDiacriticInsensitivePredicateOption) != 0) {
		[out appendString:@"[d]"];
	}
	return out;
}

/* NSObject's `isEqual:` takes a NONNULL object, so this re-declaration may not say otherwise —
 * the same rule that shaped NSNull's. */
- (BOOL)isEqual:(id)other
{
	NSComparisonPredicate *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSComparisonPredicate class]]) {
		return NO;
	}
	them = (NSComparisonPredicate *)other;
	return [_left isEqual:them->_left] && [_right isEqual:them->_right] &&
	       _modifier == them->_modifier && _operatorType == them->_operatorType &&
	       _options == them->_options;
}

- (NSUInteger)hash
{
	return [_left hash] ^ [_right hash] ^ _operatorType ^ (_modifier << 8) ^ (_options << 16);
}

@end
