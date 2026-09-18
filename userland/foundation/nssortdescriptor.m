/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nssortdescriptor.m — how to order, as a value. docs/design/foundation-plan.md, F10.
 *
 * MANUAL OWNERSHIP. The comparator block is the one thing it stores that needs ARC's help.
 *
 * THE ORDERING RULE IS IN ONE PLACE — `-compareObject:toObject:` — because a CHAIN and a single
 * descriptor must not be able to disagree about direction. It resolves the key on both objects
 * through KVC (F9), compares by whichever of the three kinds was asked for, and applies
 * `ascending` last. A descriptor with a nil key compares the OBJECTS THEMSELVES, which is the
 * one thing a nil key can mean.
 *
 * THE SELECTOR FORM IS CALLED THROUGH ITS OWN RETURN TYPE. `-performSelector:withObject:` is
 * typed as returning `id`, and a comparison selector returns `NSComparisonResult` — a SCALAR.
 * Reading that scalar as a pointer is exactly the bug F9's probe found by crashing on it, so
 * this file asks the runtime for the method's IMP and calls it as what it is.
 *
 * A NIL VALUE RAISES rather than being given a position. `nil` can arrive from an ivar that
 * holds nothing, and inventing an ordering for "nothing" would silently decide a question the
 * caller never asked; the message names the key so the answer is one line away.
 */

#import <foundation/NSSortDescriptor.h>
#import <foundation/NSKeyValueCoding.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>
#import <objc/runtime.h>	/* sel_getName, methodForSelector's IMP */

/*
 * The storage is here rather than in the header: v1 has no subclass to extend, and Cocoa keeps
 * its own ivars private too.
 */
@interface NSSortDescriptor ()
{
	NSString *_key;
	BOOL _ascending;
	SEL _selector;
	NSComparator _comparator;
}
@end

@implementation NSSortDescriptor

+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key ascending:(BOOL)ascending
{
	return [[self alloc] initWithKey:key ascending:ascending];
}

+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key
			    ascending:(BOOL)ascending
			     selector:(nullable SEL)selector
{
	return [[self alloc] initWithKey:key ascending:ascending selector:selector];
}

+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key
			    ascending:(BOOL)ascending
			   comparator:(NSComparator)comparator
{
	return [[self alloc] initWithKey:key ascending:ascending comparator:comparator];
}

- (instancetype)initWithKey:(nullable NSString *)key ascending:(BOOL)ascending
{
	return [self initWithKey:key ascending:ascending selector:NULL];
}

- (instancetype)initWithKey:(nullable NSString *)key
		  ascending:(BOOL)ascending
		   selector:(nullable SEL)selector
{
	if ((self = [super init]) != nil) {
		_key = [key copy];
		_ascending = ascending;
		_selector = selector;
	}
	return self;
}

- (instancetype)initWithKey:(nullable NSString *)key
		  ascending:(BOOL)ascending
		 comparator:(NSComparator)comparator
{
	if ((self = [super init]) != nil) {
		_key = [key copy];
		_ascending = ascending;
		_comparator = comparator;
	}
	return self;
}

- (nullable NSString *)key { return _key; }
- (BOOL)ascending { return _ascending; }
- (nullable SEL)selector { return _selector; }
- (nullable NSComparator)comparator { return _comparator; }

- (NSSortDescriptor *)reversedSortDescriptor
{
	/* The key and the comparison are untouched: only the DIRECTION is the descriptor's. */
	if (_comparator != nil) {
		return [[NSSortDescriptor alloc] initWithKey:_key
						 ascending:!_ascending
						comparator:_comparator];
	}
	return [[NSSortDescriptor alloc] initWithKey:_key
					 ascending:!_ascending
					  selector:_selector];
}

- (NSComparisonResult)compareObject:(id)object1 toObject:(id)object2
{
	id left;
	id right;
	NSComparisonResult order;

	if (object1 == nil || object2 == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-compareObject:toObject: needs two objects"];
	}
	left = _key == nil ? object1 : [object1 valueForKey:_key];
	right = _key == nil ? object2 : [object2 valueForKey:_key];
	if (left == nil || right == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"the key %@ is nil on one of the objects, and there is no "
				   "order between nothing and something",
				   _key == nil ? @"(none)" : _key];
	}

	if (_comparator != nil) {
		order = _comparator(left, right);
	} else if (_selector != NULL) {
		/* CALLED AS WHAT IT IS: a comparison selector answers
		 * NSComparisonResult, not `id`. */
		typedef NSComparisonResult (*fn_compare)(id, SEL, id);
		fn_compare call;

		if (![left respondsToSelector:_selector]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"%@ does not answer the comparison selector %s",
					   [left class], sel_getName(_selector)];
		}
		call = (fn_compare)[left methodForSelector:_selector];
		order = call(left, _selector, right);
	} else {
		order = [left compare:right];
	}

	/* THE DIRECTION IS APPLIED HERE, ONCE — a chain built on this cannot disagree with a
	 * single descriptor about which way it points. */
	if (!_ascending) {
		if (order == NSOrderedAscending) {
			return NSOrderedDescending;
		}
		if (order == NSOrderedDescending) {
			return NSOrderedAscending;
		}
	}
	return order;
}

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;			/* immutable */
}

- (BOOL)isEqual:(id)other
{
	if (![other isKindOfClass:[NSSortDescriptor class]]) {
		return NO;
	}
	{
		NSSortDescriptor *that = (NSSortDescriptor *)other;

		return (that->_key == _key || [that->_key isEqual:_key]) &&
		       that->_ascending == _ascending &&
		       that->_selector == _selector &&
		       that->_comparator == _comparator;
	}
}

- (NSUInteger)hash
{
	/* A SEL is a pointer, and casting one to an integer is deprecated — so the selector is
	 * hashed by NAME. Hashing is not a hot path here, and the alternative is a cast the
	 * compiler is right to complain about. */
	NSUInteger selectorHash = _selector == NULL ? 0
		: [[NSString stringWithUTF8String:sel_getName(_selector)] hash];

	return [_key hash] ^ (NSUInteger)_ascending ^ selectorHash;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: key = %@, %@, %@>",
		[self class],
		_key == nil ? @"(none)" : _key,
		_ascending ? @"ascending" : @"descending",
		_comparator != nil ? @"comparator"
		: (_selector != NULL ? @"selector" : @"compare:")];
}

@end
