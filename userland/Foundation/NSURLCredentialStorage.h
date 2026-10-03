/*
 * NSURLCredentialStorage.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The credential store: which credentials exist for which realm, and which one is the default.
 *
 * IT IS KEYED BY PROTECTION SPACE, which is why that class implements -copy, -isEqual: and -hash: a store
 * that filed under this class without those three would lose every entry a caller looked up through a
 * protection space it rebuilt, which is the only way a caller ever has one.
 *
 * V1 IS IN-MEMORY AND THE KEYCHAIN IS THE NAMED PERSISTENCE SEAM (docs/design/foundation-plan.md §48).
 * Nothing here is written anywhere; the store lives as long as the process. A keychain-backed store would
 * substitute at exactly this boundary and would have to answer the same questions - which is why the seam is
 * named in one place rather than imitated.
 *
 * REFUSED BY NAME, and the checks assert they are ABSENT: the two `options:` removal doors, whose dictionary
 * exists for iCloud synchronisation - a thing this system does not have and this plan has never proposed.
 */

#ifndef _FNX_FOUNDATION_NSURLCREDENTIALSTORAGE_H
#define _FNX_FOUNDATION_NSURLCREDENTIALSTORAGE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLProtectionSpace.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSLock.h>

NS_ASSUME_NONNULL_BEGIN

@class NSURLCredential;

/*
 * The change notification, whose VALUE is ours: Apple publishes the name, and a notification name is an
 * opaque token, so it is this library's own string - the constant's own name.
 */
extern NSString * const NSURLCredentialStorageChangedNotification;

@interface NSURLCredentialStorage : NSObject
{
	NSMutableDictionary *_credentials;
	NSMutableDictionary *_defaultCredentials;
	NSLock *_lock;
}

+ (NSURLCredentialStorage *)sharedCredentialStorage;

/* THE DEFAULT CREDENTIAL is one per space - the one a caller reaches for when found nothing more specific,
 * which is why it is kept separately rather than as a flag on an entry. */
- (void)setDefaultCredential:(NSURLCredential *)credential
	  forProtectionSpace:(NSURLProtectionSpace *)space;
- (nullable NSURLCredential *)defaultCredentialForProtectionSpace:(NSURLProtectionSpace *)space;

/* EVERY CREDENTIAL FOR A SPACE COEXISTS, because one realm can accept more than one user - so these answers
 * are arrays and the same protection space may hold several. */
- (void)setCredential:(NSURLCredential *)credential
    forProtectionSpace:(NSURLProtectionSpace *)space;
- (void)removeCredential:(NSURLCredential *)credential
       forProtectionSpace:(NSURLProtectionSpace *)space;

- (nullable NSDictionary *)allCredentials;
- (NSArray *)credentialsForProtectionSpace:(NSURLProtectionSpace *)space;

/*
 * THE TASK-SCOPED DOORS ARE THE SAME ANSWER, and the header says so rather than leaving a caller to
 * discover it: a per-task credential scope would need a per-task lifetime this store does not have, and
 * inventing one would put Apple's name on behaviour it does not have. The task is accepted so that Apple
 * source compiles, exactly as the challenge's sender argument is (§48.1).
 */





@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLCREDENTIALSTORAGE_H */
