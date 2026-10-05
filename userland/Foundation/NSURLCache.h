/*
 * NSURLCache.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The response cache: what a request can be answered from without asking the network again.
 *
 * V1 IS IN-MEMORY AND NOTHING IS WRITTEN ANYWHERE, and that is the same decision the cookie store and the
 * credential store took (docs/design/foundation-plan.md §49): the initialisers accept what Apple's contract
 * says - a disk capacity, a directory - so that a caller compiling against these headers keeps compiling, but
 * `currentDiskUsage` answers 0 and nothing is written. WHEN the disk half is wanted its directory is an FSH
 * domain the way /System/Temporary Files/ is, and that is a decision for then rather than a layout invented
 * now.
 *
 * AN ENTRY IS KEYED BY THE REQUEST THAT MADE IT - its URL and its method - because a request has no value
 * equality in this library. That is a narrowing of Apple's contract, said here rather than hidden in the
 * implementation: two requests differing only in a header this cache does not look at are one entry.
 */

#ifndef _FNX_FOUNDATION_NSURLCACHE_H
#define _FNX_FOUNDATION_NSURLCACHE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCachedURLResponse.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSLock.h>

@class NSURLRequest;

NS_ASSUME_NONNULL_BEGIN

@interface NSURLCache : NSObject
{
	NSMutableDictionary *_entries;		/* key -> NSCachedURLResponse */
	NSMutableDictionary *_entryDates;	/* key -> NSDate, so a removal BY DATE can be answered */
	NSUInteger _memoryCapacity;
	NSUInteger _diskCapacity;
	NSLock *_lock;
}

/* THE SHARED CACHE is one per process, and it is what the bridge reaches for when a request has no cache of
 * its own. Apple's default value is not invented here: a cache with no capacity would store nothing and
 * look broken, so the shared one is built with the sizes Apple documents. */
+ (NSURLCache *)sharedURLCache;

/* AND IT CAN BE REPLACED - Apple's +setSharedURLCache: under this library's fn prefix, because a shared
 * cache nobody can replace is a shared cache a test cannot isolate. */
+ (void)fnSetSharedURLCache:(NSURLCache *)cache;

- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity
			  diskCapacity:(NSUInteger)diskCapacity
			  directoryURL:(nullable NSURL *)directoryURL;

/* THE OLDER FORM, still declared because Apple still declares it: the path is accepted and NOT used, since
 * v1 writes nothing (§49). */
- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity
			  diskCapacity:(NSUInteger)diskCapacity
			      diskPath:(nullable NSString *)path;

/* GETTING AND STORING. `-storeCachedResponse:forRequest:` REFUSES a response whose storage policy is
 * NSURLCacheStorageNotAllowed - a cache that returns what it was told not to keep is worse than no cache -
 * and the refusal is observable twice over: the lookup answers nil and the usage does not move. */
- (nullable NSCachedURLResponse *)cachedResponseForRequest:(NSURLRequest *)request;
- (void)storeCachedResponse:(NSCachedURLResponse *)cachedResponse forRequest:(NSURLRequest *)request;

/* The task-scoped pair answers through the task's own request, as every task-scoped door in this library
 * does: one index, and the task is how a caller happens to have the request. */



- (void)removeCachedResponseForRequest:(NSURLRequest *)request;

/* WHICH IS WHY EACH ENTRY REMEMBERS WHEN IT WAS CACHED: Apple's door cannot be answered any other way. */
- (void)removeCachedResponsesSinceDate:(NSDate *)date;
- (void)removeAllCachedResponses;

@property (readonly) NSUInteger currentMemoryUsage;
@property (readonly) NSUInteger memoryCapacity;
@property (readonly) NSUInteger currentDiskUsage;
@property (readonly) NSUInteger diskCapacity;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLCACHE_H */
