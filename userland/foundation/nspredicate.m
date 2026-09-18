/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nspredicate.m — the predicate object model. docs/design/foundation-plan.md, F11a.
 *
 * ARC file. The block leaf is the one thing it stores that needs ARC's help.
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

#import <foundation/NSPredicate.h>
#import <foundation/NSString.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSException.h>

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

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;			/* immutable */
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
