/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSExpression.m — the expression tree (F13.10). MANUAL OWNERSHIP.
 *
 * ONE NODE HOLDS A TYPE AND THREE SLOTS, and each type uses a different subset: `_constant` is the
 * constant, the key path, the variable NAME, the left operand or the aggregate's members; `_operand`
 * is the right operand of a set operation or the arguments of a function; `_function` is the
 * function's name. That is why the doors read the same slots through differently-named accessors —
 * `-keyPath` and `-variable` and `-constantValue` are one field asked three questions.
 *
 * THE FOLD FUNCTIONS ARE THE FIVE THE LIBRARY CAN ACTUALLY COMPUTE (sum, count, min, max, average)
 * over the collection its argument evaluates to; anything else raises rather than answering nil,
 * because a silently wrong total is worse than a loud refusal.
 */

#import <Foundation/NSExpression.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNull.h>
/* NEITHER A KEY PATH NOR A FUNCTION EXPRESSION MEANS ANYTHING WITHOUT KVC: -valueForKeyPath: is what
 * a key path expression evaluates through, so the header for it is a dependency of this file rather
 * than a convenience. */
#import <Foundation/NSKeyValueCoding.h>
#import <Foundation/NSException.h>

@interface NSExpression (FNPrivate)
+ (NSExpression *)fnWithType:(NSExpressionType)type
		    constant:(nullable id)constant
		     operand:(nullable id)operand
		    function:(nullable NSString *)name;
@end

/* WHAT A COLLECTION MEANS HERE: either kind of collection, as a list of members. */
static NSArray *fn_members(id collection)
{
	if (collection == nil) {
		return nil;
	}
	if ([collection isKindOfClass:[NSSet class]]) {
		return [(NSSet *)collection allObjects];
	}
	if ([collection isKindOfClass:[NSArray class]]) {
		return (NSArray *)collection;
	}
	return nil;
}

static BOOL fn_contains(id collection, id object)
{
	NSArray *members = fn_members(collection);
	NSUInteger i;

	if (members == nil) {
		return NO;
	}
	for (i = 0; i < [members count]; i++) {
		if ([[members objectAtIndex:i] isEqual:object]) {
			return YES;
		}
	}
	return NO;
}

static id fn_fold(NSString *name, id collection)
{
	NSArray *members = fn_members(collection);
	NSUInteger i, count;

	if (members == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExpression: the argument of \"%@\" is not a collection", name];
	}
	count = [members count];
	if ([name isEqualToString:@"count:"]) {
		return [NSNumber numberWithUnsignedInteger:count];
	}
	if ([name isEqualToString:@"sum:"] || [name isEqualToString:@"average:"]) {
		double total = 0;

		for (i = 0; i < count; i++) {
			total += [[members objectAtIndex:i] doubleValue];
		}
		if ([name isEqualToString:@"average:"]) {
			if (count == 0) {
				[NSException raise:NSInvalidArgumentException
					    format:@"NSExpression: the average of nothing has no value"];
			}
			return [NSNumber numberWithDouble:total / (double)count];
		}
		return [NSNumber numberWithDouble:total];
	}
	if ([name isEqualToString:@"min:"] || [name isEqualToString:@"max:"]) {
		id best;

		if (count == 0) {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSExpression: the %@ of nothing has no value",
					   [name isEqualToString:@"min:"] ? @"minimum" : @"maximum"];
		}
		best = [members objectAtIndex:0];
		for (i = 1; i < count; i++) {
			id candidate = [members objectAtIndex:i];
			NSComparisonResult order = [candidate compare:best];

			if ([name isEqualToString:@"max:"] ? order == NSOrderedDescending
							   : order == NSOrderedAscending) {
				best = candidate;
			}
		}
		return best;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"NSExpression: \"%@\" is not a function this library implements "
			   "(sum:, count:, min:, max:, average:)", name];
	return nil;
}

/* THE THREE SET OPERATIONS, with the RESULT'S KIND following the operands: two sets answer a set,
 * anything else answers an array. That is Cocoa's rule and it matters, because a caller that gets an
 * array back from a union expects array order rather than set membership. */
static id fn_set_operation(NSExpressionType type, id left, id right)
{
	NSArray *leftMembers = fn_members(left);
	NSArray *rightMembers = fn_members(right);
	NSMutableArray *result;
	NSUInteger i;
	BOOL asSets;

	if (leftMembers == nil || rightMembers == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExpression: a set operation needs two collections"];
	}
	asSets = [left isKindOfClass:[NSSet class]] && [right isKindOfClass:[NSSet class]];
	result = [NSMutableArray arrayWithCapacity:[leftMembers count] + [rightMembers count]];
	for (i = 0; i < [leftMembers count]; i++) {
		id member = [leftMembers objectAtIndex:i];

		if (type == NSMinusSetExpressionType) {
			if (!fn_contains(right, member)) {
				[result addObject:member];
			}
		} else if (type == NSIntersectSetExpressionType) {
			if (fn_contains(right, member)) {
				[result addObject:member];
			}
		} else {
			[result addObject:member];
		}
	}
	if (type == NSUnionSetExpressionType) {
		for (i = 0; i < [rightMembers count]; i++) {
			id member = [rightMembers objectAtIndex:i];

			if (!fn_contains(left, member)) {
				[result addObject:member];
			}
		}
	}
	return asSets ? (id)[NSSet setWithArray:result] : (id)result;
}

@implementation NSExpression

+ (NSExpression *)fnWithType:(NSExpressionType)type
		    constant:(nullable id)constant
		     operand:(nullable id)operand
		    function:(nullable NSString *)name
{
	NSExpression *expression = [[self alloc] init];

	/* THE EXPRESSION OWNS WHAT IT HOLDS (§63.20). These three assignments used to BORROW their arguments - no
	 * retain here and no -dealloc anywhere in the file - so every value an expression handed back
	 * (`-constantValue`, `-operand`, `-function`) was valid only as long as the CALLER's reference happened to
	 * live. An expression is a VALUE: `[NSExpression expressionForConstantValue:[NSMutableString stringWithString:@"x"]]`
	 * built an expression whose only constant died with the pool - the defect class that hides on the guest
	 * (musl leaves freed memory readable) and is fatal on the host (glibc reuses it).
	 *
	 * THE PAIR IS THE FIX: retain here, release in the -dealloc below. THIS FUNNEL IS THE ONLY PLACE ANY SLOT IS
	 * ASSIGNED - `-initWithCoder:` comes through it too - which is what makes one retain and one dealloc an
	 * exact mirror rather than a place a leak can hide. */
	expression->_type = type;
	expression->_constant = [constant retain];
	expression->_operand = [operand retain];
	expression->_function = [name retain];
	return expression;
}

- (void)dealloc
{
	/* THE MIRROR OF `+fnWithType:` AND NOTHING ELSE, for the reason written there: the funnel is the only
	 * writer of these three slots, so these releases are the whole of the ownership this class has. */
	[_constant release];
	[_operand release];
	[_function release];
	[super dealloc];
}

+ (NSExpression *)expressionForConstantValue:(nullable id)object
{
	return [self fnWithType:NSConstantValueExpressionType constant:object operand:nil function:nil];
}

/* ===================================================================================================
 * THE NSCoding DOORS (§63.15). THE KEYS ARE OURS (Apple publishes no name for them, §11.6.1 D2's ground) and
 * they are defined beside their only writer and reader, as NSNotification's are (§63.14) and for the same
 * reason: nothing else in the library writes them, so putting them in a wire header would share them with
 * nobody.
 * =================================================================================================== */
static NSString *const kTypeKey = @"NS.expressionType";
static NSString *const kConstantKey = @"NS.constant";
static NSString *const kOperandKey = @"NS.operand";
static NSString *const kFunctionKey = @"NS.function";

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* ALL FOUR, because the type decides what the slots MEAN — and all three slots are written even when the
	 * type uses only one, so the decoder's shape is the encoder's shape rather than a per-type puzzle. */
	[coder encodeInteger:(NSInteger)_type forKey:kTypeKey];
	[coder encodeObject:_constant forKey:kConstantKey];
	[coder encodeObject:_operand forKey:kOperandKey];
	[coder encodeObject:_function forKey:kFunctionKey];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	/* THROUGH THE CLASS'S OWN CONSTRUCTOR, so this door follows EXACTLY the ownership regime every factory
	 * above follows — it neither adds a retain nor quietly fixes one — and an expression built by a decoder is
	 * indistinguishable from one built by `+expressionForConstantValue:`. That constructor is a CLASS method
	 * that allocates its own instance, so the allocated receiver is released and the built one is the answer:
	 * the same idiom the collections' shared-empty case uses (§C.3 item 1), and the only honest one when the
	 * class's own door is the thing being reused. */
	NSExpression *built = [[self class] fnWithType:(NSExpressionType)[coder decodeIntegerForKey:kTypeKey]
					      constant:[coder decodeObjectForKey:kConstantKey]
					       operand:[coder decodeObjectForKey:kOperandKey]
					      function:[coder decodeObjectForKey:kFunctionKey]];

	[self release];		/* never initialized: `built` is the answer */
	return built;
}

+ (NSExpression *)expressionForEvaluatedObject
{
	return [self fnWithType:NSEvaluatedObjectExpressionType constant:nil operand:nil function:nil];
}

+ (NSExpression *)expressionForVariable:(NSString *)string
{
	if (string == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExpression: a variable expression needs a name"];
	}
	return [self fnWithType:NSVariableExpressionType constant:string operand:nil function:nil];
}

+ (NSExpression *)expressionForKeyPath:(NSString *)keyPath
{
	if (keyPath == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExpression: a key path expression needs a key path"];
	}
	return [self fnWithType:NSKeyPathExpressionType constant:keyPath operand:nil function:nil];
}

+ (NSExpression *)expressionForFunction:(NSString *)name arguments:(NSArray *)arguments
{
	if (name == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExpression: a function expression needs a name"];
	}
	return [self fnWithType:NSFunctionExpressionType
		       constant:nil
			operand:arguments
		       function:name];
}

+ (NSExpression *)expressionForFunction:(nullable id)target
			   selectorName:(NSString *)name
			      arguments:(nullable NSArray *)arguments
{
	(void)target;		/* ignored, as in Cocoa: the NAME decides what is evaluated */
	return [self expressionForFunction:name arguments:arguments ?: [NSArray array]];
}

+ (NSExpression *)expressionForAnyKey
{
	return [self fnWithType:NSAnyKeyExpressionType constant:nil operand:nil function:nil];
}

+ (NSExpression *)expressionForAggregate:(NSArray *)collection
{
	return [self fnWithType:NSAggregateExpressionType constant:collection operand:nil function:nil];
}

+ (NSExpression *)expressionForUnionSet:(NSExpression *)left with:(NSExpression *)right
{
	return [self fnWithType:NSUnionSetExpressionType constant:left operand:right function:nil];
}

+ (NSExpression *)expressionForIntersectSet:(NSExpression *)left with:(NSExpression *)right
{
	return [self fnWithType:NSIntersectSetExpressionType constant:left operand:right function:nil];
}

+ (NSExpression *)expressionForMinusSet:(NSExpression *)left with:(NSExpression *)right
{
	return [self fnWithType:NSMinusSetExpressionType constant:left operand:right function:nil];
}

- (NSExpressionType)expressionType
{
	return _type;
}

- (nullable id)constantValue
{
	return _type == NSConstantValueExpressionType ? _constant : nil;
}

- (nullable NSString *)keyPath
{
	return _type == NSKeyPathExpressionType ? _constant : nil;
}

- (nullable NSString *)variable
{
	return _type == NSVariableExpressionType ? _constant : nil;
}

- (nullable NSString *)function
{
	return _function;
}

- (nullable NSArray *)arguments
{
	return _type == NSFunctionExpressionType ? _operand : nil;
}

- (nullable id)operand
{
	switch (_type) {
	case NSUnionSetExpressionType:
	case NSIntersectSetExpressionType:
	case NSMinusSetExpressionType:
		return _constant;
	default:
		return nil;
	}
}

- (nullable id)collection
{
	return _type == NSAggregateExpressionType ? _constant : nil;
}

- (nullable id)evaluateWithObject:(nullable id)object
{
	return [self expressionValueWithObject:object context:nil];
}

- (nullable id)expressionValueWithObject:(nullable id)object
				 context:(nullable NSMutableDictionary *)context
{
	switch (_type) {
	case NSConstantValueExpressionType:
		return _constant;
	case NSEvaluatedObjectExpressionType:
		return object;
	case NSVariableExpressionType:
		return context != nil ? [context objectForKey:_constant] : nil;
	case NSKeyPathExpressionType:
		return object != nil ? [object valueForKeyPath:_constant] : nil;
	case NSFunctionExpressionType: {
		id first = nil;

		if ([_operand count] > 0) {
			first = [[_operand objectAtIndex:0] expressionValueWithObject:object
									      context:context];
		}
		return fn_fold(_function, first);
	}
	case NSAggregateExpressionType: {
		NSMutableArray *evaluated = [NSMutableArray arrayWithCapacity:[(NSArray *)_constant count]];
		NSUInteger i;

		for (i = 0; i < [(NSArray *)_constant count]; i++) {
			id member = [(NSArray *)_constant objectAtIndex:i];

			if ([member isKindOfClass:[NSExpression class]]) {
				id value = [member expressionValueWithObject:object context:context];

				[evaluated addObject:(value != nil ? value : (id)[NSNull null])];
			} else {
				[evaluated addObject:member];
			}
		}
		return evaluated;
	}
	case NSUnionSetExpressionType:
	case NSIntersectSetExpressionType:
	case NSMinusSetExpressionType: {
		NSExpression *left = _constant;
		NSExpression *right = _operand;

		return fn_set_operation(_type,
					[left expressionValueWithObject:object context:context],
					[right expressionValueWithObject:object context:context]);
	}
	case NSAnyKeyExpressionType:
		/* COCOA'S OWN ANSWER IS UNDEFINED for a value; nil is this library's. */
		return nil;
	default:
		return nil;
	}
}

- (BOOL)isEqual:(nullable id)other
{
	NSExpression *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSExpression class]]) {
		return NO;
	}
	them = (NSExpression *)other;
	if (them->_type != _type) {
		return NO;
	}
	if (_constant != them->_constant &&
	    !(_constant != nil && [(id)_constant isEqual:them->_constant])) {
		return NO;
	}
	if (_operand != them->_operand &&
	    !(_operand != nil && [(id)_operand isEqual:them->_operand])) {
		return NO;
	}
	return _function == them->_function ||
	       (_function != nil && [_function isEqualToString:them->_function]);
}

- (NSUInteger)hash
{
	NSUInteger hash = _type;

	if (_constant != nil) {
		hash ^= [_constant hash];
	}
	if (_operand != nil) {
		hash ^= [_operand hash];
	}
	if (_function != nil) {
		hash ^= [_function hash];
	}
	return hash;
}

- (NSString *)description
{
	switch (_type) {
	case NSConstantValueExpressionType:
		return [NSString stringWithFormat:@"%@", _constant];
	case NSEvaluatedObjectExpressionType:
		return @"SELF";
	case NSVariableExpressionType:
		return [NSString stringWithFormat:@"$%@", _constant];
	case NSKeyPathExpressionType:
		return _constant;
	case NSFunctionExpressionType:
		return [NSString stringWithFormat:@"%@(%@)", _function, _operand];
	case NSAggregateExpressionType:
		return [NSString stringWithFormat:@"{%@}", _constant];
	case NSAnyKeyExpressionType:
		return @"@anyKey";
	case NSUnionSetExpressionType:
		return [NSString stringWithFormat:@"%@ UNION %@", _constant, _operand];
	case NSIntersectSetExpressionType:
		return [NSString stringWithFormat:@"%@ INTERSECT %@", _constant, _operand];
	case NSMinusSetExpressionType:
		return [NSString stringWithFormat:@"%@ MINUS %@", _constant, _operand];
	default:
		return @"<expression>";
	}
}

- (id)copy
{
	/* IMMUTABLE, so a copy is itself — the same rule the collections follow. */
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

@end
