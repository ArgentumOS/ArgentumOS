/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCache — a MUTABLE COLLECTION THAT DECIDES FOR ITSELF WHAT TO DROP. docs/design/foundation-plan.md W13c.
 *
 * IT IS NOT AN NSMutableDictionary WITH LIMITS, and the difference is the direction of the decision: a
 * dictionary holds what it is given until it is told otherwise, while a cache MAY THROW AN ENTRY AWAY when
 * the entries it is already holding are worth more than the one arriving. That is why the cost door exists
 * (`-setObject:forKey:cost:`) and why eviction is not a failure — a caller asks for a key and may be told
 * nothing, which a dictionary never does.
 *
 * WHAT THE DELEGATE IS TOLD, EXACTLY: an eviction by a limit and a removal by the caller both call
 * `-cache:willEvictObject:`, and a REPLACEMENT DOES NOT — a key set again is neither, and a delegate counting
 * departures should not see a phantom for every write.
 *
 * THE TWO LIMITS ARE BOTH ENFORCED, on insertion, by removing the OLDEST INSERTED entry first: an entry that
 * is set again moves to the end, which makes the order a least-recently-INSERTED rule. No clock is consulted,
 * so this is not a true LRU — it is the order the cache was last written in, which is what can be honoured
 * without a timer.
 *
 * THE DISCARDABLE HANDSHAKE IS WHY THIS CLASS AND `NSDiscardableContent` ARRIVE TOGETHER:
 *
 *   * storing a value that conforms to `NSDiscardableContent` takes an ACCESS ON IT
 *     (`-beginContentAccess`), so the entry's own content cannot be purged while the cache is holding it;
 *   * evicting or removing it gives that access back (`-endContentAccess`);
 *   * an entry whose content WAS discarded — the owner purged it while it was not accessed, which a cache
 *     cannot prevent because a caller kept it — is evicted THE MOMENT IT IS ASKED FOR and the lookup answers
 *     nil, if `-evictsObjectsWithDiscardedContent` is set. The alternative would be handing the caller
 *     something whose content is known to be gone.
 *
 * THREAD SAFETY IS CLAIMED HERE, unlike this library's other collections: Apple documents NSCache as safe to
 * use from several threads, so every door below is taken under a lock.
 */

#ifndef FOUNDATION_NSCACHE_H
#define FOUNDATION_NSCACHE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCacheDelegate.h>

@class NSMutableDictionary;
@class NSMutableArray;
@class NSLock;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSCache : NSObject
{
	NSString *_name;
	NSUInteger _countLimit;			/* 0 means no limit */
	NSUInteger _totalCostLimit;		/* 0 means no limit */
	BOOL _evictsObjectsWithDiscardedContent;
	id <NSCacheDelegate> _delegate;		/* NOT retained */
	NSMutableDictionary *_entries;
	NSMutableDictionary *_costs;
	NSMutableArray *_order;			/* keys in least-recently-INSERTED order */
	NSUInteger _totalCost;
	NSLock *_lock;
}

/* A name for the cache, which is diagnostic only: nothing in this implementation reads it, and it is stored
 * because a caller sets it for a bug report or a debugger. */
- (NSString *)name;
- (void)setName:(nullable NSString *)value;

/* THE LIMITS. Zero means "no limit", for each of them independently. */
- (NSUInteger)countLimit;
- (void)setCountLimit:(NSUInteger)value;
- (NSUInteger)totalCostLimit;
- (void)setTotalCostLimit:(NSUInteger)value;

/* See the header's note on the discardable handshake. Defaults to YES, as Apple's does. */
- (BOOL)evictsObjectsWithDiscardedContent;
- (void)setEvictsObjectsWithDiscardedContent:(BOOL)value;

- (nullable id <NSCacheDelegate>)delegate;
- (void)setDelegate:(nullable id <NSCacheDelegate>)value;

- (nullable id)objectForKey:(id)key;
- (void)setObject:(id)obj forKey:(id)key;
- (void)setObject:(id)obj forKey:(id)key cost:(NSUInteger)cost;
- (void)removeObjectForKey:(id)key;
- (void)removeAllObjects;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCACHE_H */
