/*
 * NSURLCache.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSURLCache.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSString.h>

/* APPLE'S OWN DEFAULT SIZES for the shared cache, so that a shared cache which stored nothing is not what a
 * caller gets by saying nothing. */
#define FN_SHARED_MEMORY_CAPACITY	(4 * 1024 * 1024)
#define FN_SHARED_DISK_CAPACITY		(20 * 1024 * 1024)

@implementation NSURLCache

/* THE KEY IS THE REQUEST'S IDENTITY FOR CACHING - its method and its URL - because a request has no value
 * equality here. The method comes first so that the two halves cannot run together ambiguously. */
static NSString *fn_keyForRequest(NSURLRequest *request)
{
	NSString *method = [request HTTPMethod];
	NSString *url = [[request URL] absoluteString];

	return [NSString stringWithFormat:@"%@ %@", (method != nil ? method : @"GET"),
			(url != nil ? url : @"")];
}

/* ONE SLOT FOR THE SHARED CACHE, read and written by both doors - the first version gave each its own
 * static, so the setter wrote a variable the getter never read, which is a replacement that replaces
 * nothing. A probe that installs a cache of its own is what makes that visible. */
static NSURLCache *fn_sharedCache = nil;

+ (NSURLCache *)sharedURLCache
{
	if(fn_sharedCache == nil) {
		fn_sharedCache = [[self alloc] initWithMemoryCapacity:FN_SHARED_MEMORY_CAPACITY
							     diskCapacity:FN_SHARED_DISK_CAPACITY
							     directoryURL:nil];
	}
	return fn_sharedCache;
}

/* THE SHARED CACHE CAN BE REPLACED, which Apple's +setSharedURLCache: does and which this class needs so a
 * program - or a probe - can install a cache of its own rather than sharing one it cannot isolate. The new
 * one is kept for the process's life, exactly as +sharedHTTPCookieStorage's is. */
+ (void)fnSetSharedURLCache:(NSURLCache *)cache
{
	if(cache == fn_sharedCache) {
		return;
	}
	[cache retain];
	[fn_sharedCache release];
	fn_sharedCache = cache;
}

- (instancetype)fnInitWithMemoryCapacity:(NSUInteger)memoryCapacity
			    diskCapacity:(NSUInteger)diskCapacity
{
	if(!(self = [super init])) {
		return nil;
	}
	_entries = [[NSMutableDictionary alloc] init];
	_entryDates = [[NSMutableDictionary alloc] init];
	_memoryCapacity = memoryCapacity;
	_diskCapacity = diskCapacity;
	_lock = [[NSLock alloc] init];
	return self;
}

- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity
			  diskCapacity:(NSUInteger)diskCapacity
			  directoryURL:(NSURL *)directoryURL
{
	/* THE DIRECTORY IS ACCEPTED AND NOT USED: v1 writes nothing (§49). */
	(void)directoryURL;
	return [self fnInitWithMemoryCapacity:memoryCapacity diskCapacity:diskCapacity];
}

- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity
			  diskCapacity:(NSUInteger)diskCapacity
			      diskPath:(NSString *)path
{
	(void)path;
	return [self fnInitWithMemoryCapacity:memoryCapacity diskCapacity:diskCapacity];
}

- (void)dealloc
{
	[_entries release];
	[_entryDates release];
	[_lock release];
	[super dealloc];
}

- (NSCachedURLResponse *)cachedResponseForRequest:(NSURLRequest *)request
{
	NSCachedURLResponse *cached;

	if(request == nil) {
		return nil;
	}
	[_lock lock];
	cached = [[[_entries objectForKey:fn_keyForRequest(request)] retain] autorelease];
	[_lock unlock];
	/* A RESPONSE STORED WITH NotAllowed IS NEVER IN THE DICTIONARY AT ALL, so there is no policy test to
	 * forget here - the refusal happened at the door that could still refuse it. */
	return cached;
}

- (void)storeCachedResponse:(NSCachedURLResponse *)cachedResponse forRequest:(NSURLRequest *)request
{
	NSString *key;

	if(cachedResponse == nil || request == nil) {
		return;
	}
	/* THE POLICY IS HONOURED AT THE DOOR, and this is the rule with teeth: a cache that returns what it was
	 * told not to keep is worse than no cache at all. */
	if([cachedResponse storagePolicy] == NSURLCacheStorageNotAllowed) {
		return;
	}
	key = fn_keyForRequest(request);
	[_lock lock];
	[_entries setObject:cachedResponse forKey:key];
	[_entryDates setObject:[NSDate date] forKey:key];
	[_lock unlock];
}

- (void)getCachedResponseForDataTask:(NSURLSessionTask *)dataTask
		   completionHandler:(void (^)(NSCachedURLResponse *))completionHandler
{
	if(completionHandler == nil) {
		return;
	}
	completionHandler([self cachedResponseForRequest:[dataTask originalRequest]]);
}

- (void)storeCachedResponse:(NSCachedURLResponse *)cachedResponse forDataTask:(NSURLSessionTask *)dataTask
{
	[self storeCachedResponse:cachedResponse forRequest:[dataTask originalRequest]];
}

- (void)removeCachedResponseForRequest:(NSURLRequest *)request
{
	NSString *key;

	if(request == nil) {
		return;
	}
	key = fn_keyForRequest(request);
	[_lock lock];
	[_entries removeObjectForKey:key];
	[_entryDates removeObjectForKey:key];
	[_lock unlock];
}

- (void)removeCachedResponseForDataTask:(NSURLSessionTask *)dataTask
{
	[self removeCachedResponseForRequest:[dataTask originalRequest]];
}

- (void)removeCachedResponsesSinceDate:(NSDate *)date
{
	NSArray *keys;
	NSUInteger i;

	if(date == nil) {
		return;
	}
	[_lock lock];
	keys = [[_entryDates allKeys] retain];
	for(i = 0; i < [keys count]; i++) {
		NSString *key = [keys objectAtIndex:i];
		NSDate *cached = [_entryDates objectForKey:key];

		/* A RESPONSE CACHED BEFORE THE DATE GOES: the comparison is against WHEN IT WAS CACHED, which is the
		 * only reading of Apple's door that can be implemented at all. */
		if(cached != nil && [cached compare:date] == NSOrderedAscending) {
			[_entries removeObjectForKey:key];
			[_entryDates removeObjectForKey:key];
		}
	}
	[keys release];
	[_lock unlock];
}

- (void)removeAllCachedResponses
{
	[_lock lock];
	[_entries removeAllObjects];
	[_entryDates removeAllObjects];
	[_lock unlock];
}

/* THE USAGE IS THE BYTES THE CACHE IS ACTUALLY HOLDING, summed from what it holds rather than tracked
 * alongside it - a counter maintained separately is a counter that can disagree with the store. */
- (NSUInteger)currentMemoryUsage
{
	NSUInteger total = 0;
	NSArray *keys;

	[_lock lock];
	keys = [_entries allKeys];
	{
		NSUInteger i;

		for(i = 0; i < [keys count]; i++) {
			total += [[[_entries objectForKey:[keys objectAtIndex:i]] data] length];
		}
	}
	[_lock unlock];
	return total;
}

- (NSUInteger)memoryCapacity { return _memoryCapacity; }
- (NSUInteger)diskCapacity { return _diskCapacity; }
/* NOTHING IS WRITTEN, so the disk usage is zero - reported rather than hidden, and the header says why. */
- (NSUInteger)currentDiskUsage { return 0; }

@end
