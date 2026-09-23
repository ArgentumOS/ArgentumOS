/*
 * NSURLCredential.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A credential: what a caller offers in answer to a challenge.
 *
 * V1 IS THE PASSWORD KIND, and that is the whole of it here — the identity and trust kinds are refused by
 * name (below), because each names a `SecIdentityRef`, a `SecTrustRef` or a certificate chain, and this
 * system has no certificate stack to give any of them a meaning. The keychain plan's own consumer list is
 * where that half arrives (docs/design/foundation-plan.md §48.1).
 *
 * IT IS A VALUE, AND IT KEEPS A SECRET — which is why two of its rules are about not leaking one:
 * `-hasPassword` answers WHETHER there is one without producing it, and `-description` never prints it.
 */

#ifndef _FNX_FOUNDATION_NSURLCREDENTIAL_H
#define _FNX_FOUNDATION_NSURLCREDENTIAL_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * The persistence cases are Apple's names; their values are ours, with `None` at 0 so that an
 * uninitialised or absent choice is the NOT-STORED one rather than the permanent one.
 *
 * THREE CASES, NOT FOUR: `NSURLCredentialPersistenceSynchronizable` is refused with the rest of the
 * synchronisation surface (§48.1) — it exists for iCloud, which this system does not have and this plan has
 * never proposed. A caller who passes that number gets a value this library does not recognise, which is
 * the honest outcome for a name that is deliberately absent.
 */
typedef NS_ENUM(NSUInteger, NSURLCredentialPersistence) {
	NSURLCredentialPersistenceNone = 0,
	NSURLCredentialPersistenceForSession = 1,
	NSURLCredentialPersistencePermanent = 2
};

/* NSCopying, because a credential is handed around and stored; the member is -copy per this library's own
 * copying convention (NSObject.h), not Cocoa's zone-taking form. */
@interface NSURLCredential : NSObject <NSCopying>
{
	NSString *_user;
	NSString *_password;
	NSURLCredentialPersistence _persistence;
}

+ (instancetype)credentialWithUser:(nullable NSString *)user
			  password:(nullable NSString *)password
		       persistence:(NSURLCredentialPersistence)persistence;

- (instancetype)initWithUser:(nullable NSString *)user
		    password:(nullable NSString *)password
		 persistence:(NSURLCredentialPersistence)persistence;

@property (readonly, copy, nullable) NSString *user;
@property (readonly, copy, nullable) NSString *password;
/* ASK WHETHER A SECRET EXISTS WITHOUT ASKING FOR IT - the property a caller should reach for first. */
@property (readonly) BOOL hasPassword;
@property (readonly) NSURLCredentialPersistence persistence;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLCREDENTIAL_H */
