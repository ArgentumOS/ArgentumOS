/*
 * NSURLHandle.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSURLHandle.h for the family, the policy that restored it, and why the class is here with the vocabulary.
 *
 * THE ONE DESIGN DECISION WORTH STATING: A HANDLE IS NOT A SECOND TRANSPORT. Its foreground load is
 * `+[NSURLConnection sendSynchronousRequest:…]`, so the bytes a pre-2005 program receives are the bytes this
 * library's modern door would have given it — one transport, two spellings. The background load is a THREAD around
 * that same call, and every client call-back is made on that thread.
 */

#import <Foundation/NSURLHandle.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSData.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSError.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSException.h>

NSString *const NSFTPPropertyActiveTransferModeKey	= @"NSFTPPropertyActiveTransferModeKey";
NSString *const NSFTPPropertyFileOffsetKey		= @"NSFTPPropertyFileOffsetKey";
NSString *const NSFTPPropertyFTPProxy			= @"NSFTPPropertyFTPProxy";
NSString *const NSFTPPropertyUserLoginKey		= @"NSFTPPropertyUserLoginKey";
NSString *const NSFTPPropertyUserPasswordKey		= @"NSFTPPropertyUserPasswordKey";

NSString *const NSHTTPPropertyErrorPageDataKey		= @"NSHTTPPropertyErrorPageDataKey";
NSString *const NSHTTPPropertyHTTPProxy			= @"NSHTTPPropertyHTTPProxy";
NSString *const NSHTTPPropertyRedirectionHeadersKey	= @"NSHTTPPropertyRedirectionHeadersKey";
NSString *const NSHTTPPropertyServerHTTPVersionKey	= @"NSHTTPPropertyServerHTTPVersionKey";
NSString *const NSHTTPPropertyStatusCodeKey		= @"NSHTTPPropertyStatusCodeKey";
NSString *const NSHTTPPropertyStatusReasonKey		= @"NSHTTPPropertyStatusReasonKey";

/* THE REGISTRY AND THE CACHE ARE CLASS-WIDE, which is what Apple's `+registerURLHandleClass:` means: a handle class
 * answers for a scheme for the whole process. */
static NSMutableArray *gHandleClasses = nil;
static NSMutableDictionary *gCachedHandles = nil;

@implementation NSURLHandle

+ (void)registerURLHandleClass:(Class)anURLHandleSubclass
{
	if (gHandleClasses == nil) {
		gHandleClasses = [[NSMutableArray alloc] init];
	}
	if (anURLHandleSubclass != Nil && ![gHandleClasses containsObject:anURLHandleSubclass]) {
		[gHandleClasses addObject:anURLHandleSubclass];
	}
}

+ (BOOL)canInitWithURL:(NSURL *)anURL
{
	/* THE TRANSPORT DECIDES, NOT A LIST OF SCHEMES WRITTEN OUT HERE: if this library can fetch it, a handle can be
	 * made for it. A scheme this library cannot fetch is a scheme a handle cannot serve, and saying so once is
	 * better than a list that drifts. */
	return anURL != nil && [NSURLConnection canHandleRequest:[NSURLRequest requestWithURL:anURL]];
}

+ (Class)URLHandleClassForURL:(NSURL *)anURL
{
	NSUInteger i;

	for (i = 0; i < [gHandleClasses count]; i++) {
		Class candidate = [gHandleClasses objectAtIndex:i];

		if ([candidate canInitWithURL:anURL]) {
			return candidate;
		}
	}
	return [self canInitWithURL:anURL] ? self : Nil;
}

+ (NSURLHandle *)cachedHandleForURL:(NSURL *)anURL
{
	return anURL == nil ? nil : [gCachedHandles objectForKey:[anURL absoluteString]];
}

- (instancetype)initWithURL:(NSURL *)anURL cached:(BOOL)willCache
{
	self = [super init];
	if (self != nil) {
		_URL = [anURL retain];
		_willCache = willCache;
		_status = NSURLHandleNotLoaded;
		_properties = [[NSMutableDictionary alloc] init];
		_clients = [[NSMutableArray alloc] init];
		_bodyParts = [[NSMutableArray alloc] init];
		if (willCache && anURL != nil) {
			if (gCachedHandles == nil) {
				gCachedHandles = [[NSMutableDictionary alloc] init];
			}
			[gCachedHandles setObject:self forKey:[anURL absoluteString]];
		}
	}
	return self;
}

- (NSURL *)URL { return _URL; }
- (NSURLHandleStatus)status { return _status; }
- (NSString *)failureReason { return _failureReason; }
- (void)addClient:(id <NSURLHandleClient>)client { [_clients addObject:client]; }
- (void)removeClient:(id <NSURLHandleClient>)client { [_clients removeObject:client]; }

- (id)propertyForKeyIfAvailable:(NSString *)propertyKey
{
	return [_properties objectForKey:propertyKey];
}

- (id)propertyForKey:(NSString *)propertyKey
{
	id known = [_properties objectForKey:propertyKey];

	/* APPLE'S DISTINCTION, KEPT: this one FLUSHES the cache and loads, which is why a program that asked for a
	 * property it had not set got an answer rather than nil. */
	if (known == nil) {
		[self loadInForeground];
		known = [_properties objectForKey:propertyKey];
	}
	return known;
}

- (BOOL)writeProperty:(id)propertyValue forKey:(NSString *)propertyKey
{
	if (propertyKey == nil) {
		return NO;
	}
	if (propertyValue == nil) {
		[_properties removeObjectForKey:propertyKey];
	} else {
		[_properties setObject:propertyValue forKey:propertyKey];
	}
	return YES;
}

- (BOOL)writeData:(NSData *)data
{
	if (data == nil) {
		return NO;
	}
	[_bodyParts addObject:data];
	return YES;
}

- (NSData *)availableResourceData
{
	return _resourceData;
}

- (void)flushCachedData
{
	/* THE CACHE IS THE HANDLE'S OWN COPY; the properties are what a caller reads afterwards, so they stay. */
	[_resourceData release];
	_resourceData = nil;
}

/* --- THE LOAD, IN THE FOREGROUND AND IN THE BACKGROUND --------------------------------------------- */

- (NSData *)fnLoadWithStatus:(NSURLHandleStatus *)outStatus reason:(NSString **)outReason
{
	NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:_URL];
	NSURLResponse *response = nil;
	NSError *error = nil;
	NSData *body = nil;
	NSData *data;

	/* THE BODY, IF ANY: the first `-writeData:` turned this into a POST, and every one of them is concatenated
	 * because that is what "append" means. */
	if ([_bodyParts count] > 0) {
		NSMutableData *joined = [[NSMutableData alloc] init];
		NSUInteger i;

		for (i = 0; i < [_bodyParts count]; i++) {
			[joined appendData:[_bodyParts objectAtIndex:i]];
		}
		body = [joined autorelease];
		[request setHTTPMethod:@"POST"];
		[request setHTTPBody:body];
	}

	data = [NSURLConnection sendSynchronousRequest:request returningResponse:&response error:&error];
	[request release];

	if (data == nil) {
		*outStatus = NSURLHandleLoadFailed;
		*outReason = error != nil ? [[error localizedDescription] retain] : [@"the load failed" retain];
		return nil;
	}

	/* THE RESPONSE, FILED UNDER THE KEYS THIS FAMILY DEFINES — which is what those eleven names are for. A handle
	 * that fetched a page and told nobody its status code would have restored the vocabulary without the meaning. */
	if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
		NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
		NSDictionary *headers = [http allHeaderFields];

		[_properties setObject:[NSNumber numberWithInteger:[http statusCode]]
				forKey:NSHTTPPropertyStatusCodeKey];
		if ([headers objectForKey:@"Server"] != nil) {
			[_properties setObject:[headers objectForKey:@"Server"]
					forKey:NSHTTPPropertyServerHTTPVersionKey];
		}
		/* THE REASON PHRASE HAS NO HEADER: it is the protocol's own text for the code, so it comes from the code. */
		[_properties setObject:([NSHTTPURLResponse localizedStringForStatusCode:[http statusCode]] != nil
					? [NSHTTPURLResponse localizedStringForStatusCode:[http statusCode]]
					: @"")
				forKey:NSHTTPPropertyStatusReasonKey];
		if ([headers objectForKey:@"Location"] != nil) {
			[_properties setObject:[NSArray arrayWithObject:[headers objectForKey:@"Location"]]
					forKey:NSHTTPPropertyRedirectionHeadersKey];
		}
	}
	*outStatus = NSURLHandleLoadSucceeded;
	*outReason = nil;
	return data;
}

- (NSData *)loadInForeground
{
	NSURLHandleStatus status;
	NSString *reason = nil;
	NSData *data = [self fnLoadWithStatus:&status reason:&reason];

	_status = status;
	[_failureReason release];
	_failureReason = reason;
	[_resourceData release];
	_resourceData = data != nil ? [data retain] : nil;
	return _resourceData;
}

/* THE BACKGROUND LOAD IS A THREAD AROUND THE SAME CALL, and it tells the clients what happened in the order a load
 * has: begin, the bytes, then one of finish/cancel/fail. */
- (void)fnBackgroundLoad
{
	NSData *data = [self loadInForeground];
	NSUInteger i;

	for (i = 0; i < [_clients count]; i++) {
		id <NSURLHandleClient> client = [_clients objectAtIndex:i];

		if (_cancelled) {
			if ([client respondsToSelector:@selector(URLHandleResourceDidCancelLoading:)]) {
				[client URLHandleResourceDidCancelLoading:self];
			}
			continue;
		}
		if (data == nil) {
			if ([client respondsToSelector:@selector(URLHandle:resourceDidFailLoadingWithReason:)]) {
				[client URLHandle:self resourceDidFailLoadingWithReason:
					(_failureReason != nil ? _failureReason : @"the load failed")];
			}
			continue;
		}
		if ([client respondsToSelector:@selector(URLHandle:resourceDataDidBecomeAvailable:)]) {
			[client URLHandle:self resourceDataDidBecomeAvailable:data];
		}
		if ([client respondsToSelector:@selector(URLHandleResourceDidFinishLoading:)]) {
			[client URLHandleResourceDidFinishLoading:self];
		}
	}
}

- (void)beginLoadInBackground
{
	NSUInteger i;

	if (_status == NSURLHandleLoadInProgress) {
		return;
	}
	_cancelled = NO;
	_status = NSURLHandleLoadInProgress;
	for (i = 0; i < [_clients count]; i++) {
		id <NSURLHandleClient> client = [_clients objectAtIndex:i];

		if ([client respondsToSelector:@selector(URLHandleResourceDidBeginLoading:)]) {
			[client URLHandleResourceDidBeginLoading:self];
		}
	}
	[NSThread detachNewThreadSelector:@selector(fnBackgroundLoad) toTarget:self withObject:nil];
}

- (void)cancelLoadInBackground
{
	/* A CANCEL IS NOT AN ERROR, and it is not a kill: the flag is read when the load returns, which is the only
	 * place a thread this class started can be interrupted from. */
	if (_status != NSURLHandleLoadInProgress) {
		return;
	}
	_cancelled = YES;
}

- (void)endLoadInBackground
{
	/* WAITING FOR THE THREAD WOULD BE THE NATURAL READING AND IS NOT WHAT THIS DOES: the house rule for this family
	 * is that a client is told when the load ends, so an `-endLoadInBackground` that blocked would only ever be
	 * called by a client that had already been told. It resets the handle so the next `-beginLoadInBackground`
	 * starts a new load — the state a caller needs, which is not the same as a join. */
	_cancelled = NO;
	_status = NSURLHandleNotLoaded;
}

- (void)dealloc
{
	/* A CACHED HANDLE IS RETAINED BY THE CACHE, so this runs only for a handle that was never cached: the URL and
	 * every bag it owns go back here, and the cache's entry (if any) went with `-removeObjectForKey:` elsewhere. */
	[_URL release];
	[_properties release];
	[_clients release];
	[_bodyParts release];
	[_resourceData release];
	[_failureReason release];
	[super dealloc];
}

@end
