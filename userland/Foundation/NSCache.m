/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCache.m — W13c. See the header for the eviction rule and the discardable handshake.
 *
 * EVERY PUBLIC DOOR TAKES THE LOCK, and every internal step assumes it is already held: the private
 * `-fnRemoveKey:` is where eviction, removal and purging all meet, so the delegate call, the cost arithmetic
 * and the discardable release happen in ONE place rather than in three that could disagree.
 *
 * THE DELEGATE IS CALLED WITH THE LOCK HELD, which is worth naming: a delegate that re-enters this cache
 * would deadlock. Apple's documentation does not promise otherwise, and a non-recursive lock is what makes
 * the rule visible rather than accidental.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSCache.h>
#import <Foundation/NSDiscardableContent.h>
#import <Foundation/NSDictionary.h>	/* NSMutableDictionary lives here */
#import <Foundation/NSArray.h>		/* NSMutableArray too */
#import <Foundation/NSLock.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSEnumerator.h>

@implementation NSCache

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_entries = [[NSMutableDictionary alloc] init];
	_costs = [[NSMutableDictionary alloc] init];
	_order = [[NSMutableArray alloc] init];
	_lock = [[NSLock alloc] init];
	_countLimit = 0;
	_totalCostLimit = 0;
	_evictsObjectsWithDiscardedContent = YES;	/* Apple's default */
	_totalCost = 0;
	return self;
}

- (void)dealloc
{
	[_name release];
	[_entries release];
	[_costs release];
	[_order release];
	[_lock release];
	[super dealloc];
}

- (NSString *)name { return _name; }

- (void)setName:(NSString *)value
{
	id copy = [value copy];

	[_name release];
	_name = copy;
}

- (NSUInteger)countLimit
{
	NSUInteger value;

	[_lock lock];
	value = _countLimit;
	[_lock unlock];
	return value;
}

- (void)setCountLimit:(NSUInteger)value
{
	[_lock lock];
	_countLimit = value;
	[self fnEvictToFit];
	[_lock unlock];
}

- (NSUInteger)totalCostLimit
{
	NSUInteger value;

	[_lock lock];
	value = _totalCostLimit;
	[_lock unlock];
	return value;
}

- (void)setTotalCostLimit:(NSUInteger)value
{
	[_lock lock];
	_totalCostLimit = value;
	[self fnEvictToFit];
	[_lock unlock];
}

- (BOOL)evictsObjectsWithDiscardedContent
{
	BOOL value;

	[_lock lock];
	value = _evictsObjectsWithDiscardedContent;
	[_lock unlock];
	return value;
}

- (void)setEvictsObjectsWithDiscardedContent:(BOOL)value
{
	[_lock lock];
	_evictsObjectsWithDiscardedContent = value;
	[_lock unlock];
}

- (id <NSCacheDelegate>)delegate { return _delegate; }
- (void)setDelegate:(id <NSCacheDelegate>)value { _delegate = value; }	/* not retained */

/* ---- the private steps, all called with the lock held ---- */

/* THE ONE PLACE AN ENTRY LEAVES. `notify` IS THE DISTINCTION THAT MATTERS: an eviction or a removal tells
 * the delegate, and a REPLACEMENT DOES NOT — an entry whose value is overwritten was neither evicted by a
 * limit nor removed by the caller, so a delegate counting "how many entries have gone" would otherwise see a
 * phantom for every write to a key it already held. */
- (void)fnReleaseKey:(id)key notify:(BOOL)notify
{
	id value = [_entries objectForKey:key];
	NSNumber *cost = [_costs objectForKey:key];

	if (value == nil) {
		return;
	}
	if (notify && _delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(cache:willEvictObject:)]) {
		[(id)_delegate cache:self willEvictObject:value];
	}
	if (cost != nil) {
		_totalCost -= [cost unsignedIntegerValue];
	}
	if ([value conformsToProtocol:@protocol(NSDiscardableContent)]) {
		[(id<NSDiscardableContent>)value endContentAccess];
	}
	[_entries removeObjectForKey:key];
	[_costs removeObjectForKey:key];
	[_order removeObject:key];
}

- (void)fnRemoveKey:(id)key
{
	[self fnReleaseKey:key notify:YES];
}

/* THE OLDEST INSERTED ENTRY GOES FIRST, until both limits are satisfied. */
- (void)fnEvictToFit
{
	while ([_order count] > 0 &&
	       ((_countLimit > 0 && [_entries count] > _countLimit) ||
		(_totalCostLimit > 0 && _totalCost > _totalCostLimit))) {
		[self fnRemoveKey:[_order objectAtIndex:0]];
	}
}

- (id)objectForKey:(id)key
{
	id value;

	if (key == nil) {
		return nil;
	}
	[_lock lock];
	value = [_entries objectForKey:key];
	if (value != nil && _evictsObjectsWithDiscardedContent &&
	    [value conformsToProtocol:@protocol(NSDiscardableContent)] &&
	    [(id<NSDiscardableContent>)value isContentDiscarded]) {
		/* THE CONTENT IS KNOWN TO BE GONE, so the entry is not handed out: it leaves, and the answer is
		 * nil. That is the one eviction a cache performs on a LOOKUP rather than on an insertion. */
		[self fnRemoveKey:key];
		value = nil;
	}
	[_lock unlock];
	return value;
}

- (void)setObject:(id)obj forKey:(id)key
{
	[self setObject:obj forKey:key cost:0];
}

- (void)setObject:(id)obj forKey:(id)key cost:(NSUInteger)cost
{
	if (obj == nil || key == nil) {
		return;
	}
	[_lock lock];
	/* SETTING A KEY AGAIN REPLACES ITS VALUE, and the old value's cost and access go back before the new
	 * ones are taken — SILENTLY: see `-fnReleaseKey:notify:`. */
	[self fnReleaseKey:key notify:NO];
	[_entries setObject:obj forKey:key];
	[_costs setObject:[NSNumber numberWithUnsignedInteger:cost] forKey:key];
	[_order addObject:key];
	_totalCost += cost;
	/* THE CACHE TAKES AN ACCESS ON DISCARDABLE CONTENT, so the entry is not purged while it is held. */
	if ([obj conformsToProtocol:@protocol(NSDiscardableContent)]) {
		(void)[(id<NSDiscardableContent>)obj beginContentAccess];
	}
	[self fnEvictToFit];
	[_lock unlock];
}

- (void)removeObjectForKey:(id)key
{
	if (key == nil) {
		return;
	}
	[_lock lock];
	[self fnRemoveKey:key];
	[_lock unlock];
}

- (void)removeAllObjects
{
	[_lock lock];
	while ([_order count] > 0) {
		[self fnRemoveKey:[_order objectAtIndex:0]];
	}
	[_lock unlock];
}

@end
