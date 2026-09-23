/*
 * NSHTTPCookieStorage.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The cookie store: which cookies exist, which of them a URL may see, and what the policy allows.
 *
 * V1 IS IN-MEMORY AND PERSISTS NOTHING, and that is a decision taken deliberately rather than a limitation
 * dressed as one (docs/design/foundation-plan.md §47.1): it is reversible, it needs no FSH domain and no
 * file format, and it lets this class be completed and probed without inventing a file layout ahead of the
 * code that would read it. When persistence is wanted, the store names an FSH domain the way the
 * configuration domains and /System/Temporary Files/ do.
 *
 * REFUSED BY NAME, and the checks assert they are ABSENT:
 *   +sharedCookieStorageForGroupContainerIdentifier: - a GROUP CONTAINER is an app-group concept this
 *   system has no notion of, and inventing one would put Apple's name on a feature it does not have;
 *   any persistence door - see above.
 */

#ifndef _FNX_FOUNDATION_NSHTTPCOOKIESTORAGE_H
#define _FNX_FOUNDATION_NSHTTPCOOKIESTORAGE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSHTTPCookie.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSNotification.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * The two change notifications. Their VALUES are ours under the same rule the cookie keys follow (§47.2):
 * Apple publishes the names, and a notification name is an opaque token no wire can carry, so it is this
 * library's own string - the constant's own name, which is the least surprising choice for a token nothing
 * else observes.
 */
extern NSString * const NSHTTPCookieManagerCookiesChangedNotification;

/* ONE NOTIFICATION, NOT TWO, AND THE MISSING ONE IS A DECISION RATHER THAN AN OVERSIGHT:
 * `NSHTTPCookieManagerAcceptPolicyChangedNotification` is DEPRECATED in Apple's own documentation - the
 * ledger carries it as `struck`, and section 11.5 strikes deprecated API - so it is not shipped. Setting
 * the policy therefore changes the policy and posts nothing, and the probe asserts the name is ABSENT.
 * Shipping it would have put a name in the headers that Apple itself has withdrawn. */

/* Forward, not imported: this header names the task type but has no reason to drag the class in. */
@class NSURLSessionTask;

@interface NSHTTPCookieStorage : NSObject
{
	NSMutableArray *_cookies;
	NSLock *_lock;
	NSHTTPCookieAcceptPolicy _policy;
}

/*
 * THE SHARED STORE is one per process and it is what a caller reaches for by default; the initialiser is
 * public because Apple's is (`-init` makes a store of one's own).
 */
+ (NSHTTPCookieStorage *)sharedHTTPCookieStorage;

@property NSHTTPCookieAcceptPolicy cookieAcceptPolicy;

- (void)setCookie:(NSHTTPCookie *)cookie;
- (void)setCookies:(NSArray *)cookies
	    forURL:(NSURL *)URL
  mainDocumentURL:(nullable NSURL *)mainDocumentURL;
- (void)deleteCookie:(NSHTTPCookie *)cookie;
- (void)removeCookiesSinceDate:(NSDate *)date;

@property (readonly, copy) NSArray *cookies;
- (NSArray *)cookiesForURL:(NSURL *)URL;
- (NSArray *)sortedCookiesUsingDescriptors:(NSArray *)sortDescriptors;

/* The task-scoped pair. A task names a request, so both answer through its URL rather than keeping a
 * second table: this store has ONE index and it is the cookie list. */
- (void)storeCookies:(NSArray *)cookies forTask:(NSURLSessionTask *)task;
- (void)getCookiesForTask:(NSURLSessionTask *)task
	completionHandler:(void (^)(NSArray *cookies))completionHandler;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSHTTPCOOKIESTORAGE_H */
