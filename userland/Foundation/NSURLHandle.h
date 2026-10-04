/*
 * NSURLHandle.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSURLHandle`, ITS CLIENT PROTOCOL, AND THE PROPERTY KEYS (§62.47).
 *
 * Apple deprecated this whole family at 10.4 in favour of `NSURLConnection` and `NSURLDownload`, and §62.24's
 * policy put it back so a program written before then compiles. THE SIXTEEN ROWS THE LEDGER OWED WERE THE
 * VOCABULARY — eleven property keys, `NSURLHandleStatus` and its four cases, and the client protocol — and the
 * CLASS IS HERE FOR THE REASON THE VOCABULARY NEEDS ONE: a property key is a key INTO A HANDLE's property bag, and
 * a status is a status OF A HANDLE's load. Declaring the keys without the thing they key into would be a
 * vocabulary with nothing to say.
 *
 * ITS LOADING DOOR IS THE LIBRARY'S OWN: the foreground path is `+[NSURLConnection sendSynchronousRequest:…]`, so
 * a handle is a second spelling of an answer this library already gives, and the background path is a thread,
 * because that is what "in background" means.
 *
 * NOT DEPRECATED HERE, BY POLICY: these names carry no availability attribute even though Apple marked them. §62.24
 * exists precisely so that a pre-2005 program compiles and runs, and a warning at every use would be the policy's
 * opposite. Where this library genuinely cannot do what a name promises, THE DOOR REFUSES AND SAYS SO — see
 * `-beginLoadInBackground`'s note on which thread a client is called back on.
 */

#ifndef FOUNDATION_NSURLHANDLE_H
#define FOUNDATION_NSURLHANDLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>

@class NSURL, NSData, NSDictionary, NSMutableArray, NSMutableDictionary, NSURLRequest, NSURLResponse, NSThread;
/* THE CLASS ITSELF, FORWARD-DECLARED BECAUSE THE PROTOCOL NAMES IT AND COMES FIRST: a client protocol whose
 * methods take the type they client must be declared before that type exists. */
@class NSURLHandle;

NS_ASSUME_NONNULL_BEGIN

/* ===================================================================================================
 * THE PROPERTY KEYS, AND WHAT EACH ONE'S VALUE IS
 *
 * The keys are `NSString *const` because a property bag is keyed by strings; the VALUE TYPE is documented per key
 * and is the useful half of the name. The key STRINGS are ours under §11.6.1 D2 — Apple prints the names and not
 * their contents — and they are the names themselves, which is what makes a `-propertyForKey:` call readable in a
 * debugger and keeps two keys from colliding.
 * =================================================================================================== */

/* FTP. The transfer mode is an `NSNumber` read as a BOOL (passive when true), the file offset an `NSNumber` (a
 * resumable download starts there), the proxy an `NSDictionary`, and the credentials `NSString`s. */
extern NSString *const NSFTPPropertyActiveTransferModeKey;
extern NSString *const NSFTPPropertyFileOffsetKey;
extern NSString *const NSFTPPropertyFTPProxy;
extern NSString *const NSFTPPropertyUserLoginKey;
extern NSString *const NSFTPPropertyUserPasswordKey;

/* HTTP. The status code is an `NSNumber`, the reason phrase and the server's protocol version `NSString`s, the
 * redirection headers an `NSArray`, the error body an `NSData`, and the proxy an `NSDictionary`. */
extern NSString *const NSHTTPPropertyErrorPageDataKey;
extern NSString *const NSHTTPPropertyHTTPProxy;
extern NSString *const NSHTTPPropertyRedirectionHeadersKey;
extern NSString *const NSHTTPPropertyServerHTTPVersionKey;
extern NSString *const NSHTTPPropertyStatusCodeKey;
extern NSString *const NSHTTPPropertyStatusReasonKey;

/* ===================================================================================================
 * THE STATUS, AND THE FOUR CASES ARE THE FOUR THINGS A LOAD CAN BE
 * =================================================================================================== */
typedef enum {
	NSURLHandleNotLoaded		= 0,
	NSURLHandleLoadInProgress	= 1,
	NSURLHandleLoadSucceeded	= 2,
	NSURLHandleLoadFailed		= 3
} NSURLHandleStatus;

/* ===================================================================================================
 * THE CLIENT PROTOCOL: FIVE CALL-BACKS A HANDLE MAKES, AND EVERY ONE IS OPTIONAL
 *
 * Apple's protocol is a formalisation of what its page calls an informal protocol, and a client that implements one
 * of these and not the others is the normal case — so `@optional` is the faithful spelling rather than a
 * convenience. THEY ARE MADE ON THE LOADING THREAD, not on the main thread, because a background load is a thread
 * and pretending otherwise would be a promise this class cannot keep.
 * =================================================================================================== */
@protocol NSURLHandleClient <NSObject>
@optional
- (void)URLHandleResourceDidBeginLoading:(NSURLHandle *)sender;
- (void)URLHandleResourceDidFinishLoading:(NSURLHandle *)sender;
- (void)URLHandleResourceDidCancelLoading:(NSURLHandle *)sender;
- (void)URLHandle:(NSURLHandle *)sender resourceDataDidBecomeAvailable:(NSData *)newBytes;
- (void)URLHandle:(NSURLHandle *)sender resourceDidFailLoadingWithReason:(NSString *)reason;
@end

/* ===================================================================================================
 * THE HANDLE
 * =================================================================================================== */
@interface NSURLHandle : NSObject
{
	NSURL *_URL;
	NSMutableDictionary *_properties;
	NSMutableArray *_clients;
	NSMutableArray *_bodyParts;		/* what `-writeData:` accumulated, in order */
	NSData *_resourceData;			/* what has arrived, cached after `-flushCachedData` too */
	NSString *_failureReason;
	NSURLHandleStatus _status;
	BOOL _willCache;
	BOOL _cancelled;
}

/* THE REGISTRY. A handle class answers for a URL scheme; the first registered class that says YES serves it, and
 * the base class's answer (the transport's own capability) is the last resort rather than the first. */
+ (void)registerURLHandleClass:(Class)anURLHandleSubclass;
+ (nullable Class)URLHandleClassForURL:(NSURL *)anURL;
+ (BOOL)canInitWithURL:(NSURL *)anURL;

/* A CACHE OF HANDLES, keyed by the URL's absolute string: the reason the class has a designated initialiser with a
 * `cached` flag at all. A handle that was not asked to be cached is not here. */
+ (nullable NSURLHandle *)cachedHandleForURL:(NSURL *)anURL;

- (instancetype)initWithURL:(NSURL *)anURL cached:(BOOL)willCache;
- (NSURL *)URL;

/* THE PROPERTY BAG. `-propertyForKey:` FLUSHES THE CACHE and loads the resource if the property is not known yet,
 * which is the difference between it and `-propertyForKeyIfAvailable:` — a distinction Apple's documentation makes
 * and this implementation keeps, because it is the only thing the two names were for. */
- (nullable id)propertyForKey:(NSString *)propertyKey;
- (nullable id)propertyForKeyIfAvailable:(NSString *)propertyKey;
- (BOOL)writeProperty:(nullable id)propertyValue forKey:(NSString *)propertyKey;

/* THE REQUEST BODY. Each `-writeData:` appends; the first one turns the request into a POST, and the accumulated
 * bytes go out with the load. */
- (BOOL)writeData:(NSData *)data;

/* LOADING. `-loadInForeground` is synchronous and answers the whole resource; `-beginLoadInBackground` starts a
 * thread and tells the clients; `-availableResourceData` is whatever has arrived and does not start a load. */
- (nullable NSData *)loadInForeground;
- (void)beginLoadInBackground;
- (void)endLoadInBackground;
- (void)cancelLoadInBackground;
- (nullable NSData *)availableResourceData;

- (NSURLHandleStatus)status;
- (nullable NSString *)failureReason;
- (void)flushCachedData;

- (void)addClient:(id <NSURLHandleClient>)client;
- (void)removeClient:(id <NSURLHandleClient>)client;


/* §63.216: THE FIVE DOORS. This class already HAS the loader (`-fnLoadWithStatus:reason:`), the foreground door
 * and a background load, so each of these is a wrapper: `-resourceData` loads ON DEMAND (Apple's own contract),
 * `-loadInBackground` runs the loader the file already has on a thread, and the two SUBCLASS receipts notify the
 * clients the protocol declares. `-expectedResourceDataSize` answers -1 for unknown, Apple's own sentinel. */
- (nullable NSData *)resourceData;
- (void)loadInBackground;
- (long long)expectedResourceDataSize;
- (void)didLoadBytes:(NSData *)newBytes loadComplete:(BOOL)complete;
- (void)backgroundLoadDidFailWithReason:(nullable NSString *)reason;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLHANDLE_H */
