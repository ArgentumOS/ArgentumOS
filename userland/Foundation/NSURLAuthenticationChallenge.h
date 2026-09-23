/*
 * NSURLAuthenticationChallenge.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A challenge: what a server is asking for, who answered last time, and how it went.
 *
 * THE CLASS IS MECHANICS, NOT POLICY. It carries a protection space, the credential that was proposed
 * before, how many attempts have failed, and the response and error from the last one - and it decides
 * nothing. The decision belongs to whoever receives it (the delegate door on NSURLSession), and the store
 * that remembers credentials is a third class again.
 *
 * AND THE ONE PLACE THIS HEADER DEVIATES FROM APPLE'S, DELIBERATELY AND NARROWLY: the initialisers keep
 * their documented `sender:` argument so that Apple-source compatibility is not broken - `sender` is an `id`
 * that predates NSURLSession, and a caller compiling against these headers must still compile - but THE
 * `-sender` ACCESSOR IS NOT SHIPPED. Apple files it under "Legacy" in its own documentation (§48.1), and
 * this library's answer to "who asked" is the task, not an object of unknown type. The argument is
 * therefore accepted and NOT EXPOSED, and the probe asserts exactly that pair: the initialiser works, and
 * the accessor is absent.
 */

#ifndef _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H
#define _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLProtectionSpace.h>

@class NSURLCredential;
@class NSURLResponse;
@class NSError;

NS_ASSUME_NONNULL_BEGIN

@interface NSURLAuthenticationChallenge : NSObject
{
	NSURLProtectionSpace *_protectionSpace;
	NSURLCredential *_proposedCredential;
	NSURLResponse *_failureResponse;
	NSError *_error;
	NSInteger _previousFailureCount;
}

/* THE COPY DOOR, and it is a copy: everything but the sender comes from the challenge given. */
- (instancetype)initWithAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
					 sender:(nullable id)sender;

/* THE PRIMARY DOOR. A nil proposed credential, a count of 0 and a nil response and error are the FIRST
 * ATTEMPT, which is the state a challenge starts in rather than a broken one. */
- (instancetype)initWithProtectionSpace:(NSURLProtectionSpace *)space
		     proposedCredential:(nullable NSURLCredential *)credential
		   previousFailureCount:(NSInteger)previousFailureCount
			failureResponse:(nullable NSURLResponse *)response
				  error:(nullable NSError *)error
				 sender:(nullable id)sender;

@property (readonly, copy) NSURLProtectionSpace *protectionSpace;
@property (readonly, copy, nullable) NSURLCredential *proposedCredential;
/* HOW MANY HAVE FAILED - 0 means none have been tried, which is why the count is carried rather than
 * inferred from the presence of a failure response. */
@property (readonly) NSInteger previousFailureCount;
@property (readonly, copy, nullable) NSURLResponse *failureResponse;
@property (readonly, copy, nullable) NSError *error;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H */
