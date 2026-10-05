/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCacheDelegate — ONE door, told when an object leaves the cache. W13c.
 *
 * THE NAME SAYS "EVICT" AND THE DOOR IS ALSO USED FOR REMOVALS, which is Apple's own wording ("about to be
 * evicted or removed") and worth stating here because the two are different events at the call site: an
 * eviction is the cache deciding, a removal is the caller deciding, and a delegate that only expects the
 * first will be surprised by the second.
 *
 * IT IS CALLED BEFORE THE OBJECT GOES, and that is the useful half: the delegate still has the object, so a
 * cache of discardable contents can be told what is about to be let go.
 *
 * EVERY MEMBER IS OPTIONAL, and the cache asks `-respondsToSelector:` first, as the keyed-archiver delegates
 * do.
 */

#ifndef FOUNDATION_NSCACHEDELEGATE_H
#define FOUNDATION_NSCACHEDELEGATE_H

#import <Foundation/NSObject.h>

@class NSCache;

NS_ASSUME_NONNULL_BEGIN

@protocol NSCacheDelegate <NSObject>

@optional

/* The object is about to leave — evicted by a limit or removed by the caller. */
- (void)cache:(NSCache *)cache willEvictObject:(id)obj;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCACHEDELEGATE_H */
