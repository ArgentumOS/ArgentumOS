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
 * nothing. The decision belongs to whoever receives it — the delegate door on `NSURLConnection` or on
 * `NSURLDownload`, which is what the transport asks — and the store
 * that remembers credentials is a third class again.
 *
 * AND THE ACCESSOR THAT WAS REFUSED IS NOW SHIPPED, BECAUSE THE GROUND IT RESTED ON WAS RETIRED. The
 * initialisers always kept their documented `sender:` argument (Apple-source compatibility, since `sender`
 * is an `id` that predates NSURLSession); what §48.1 refused was `-sender`, on the ground that Apple files
 * it under "Legacy". §62.24 RETIRED THE DEPRECATION GROUND (the user's policy, 2026-09-26 - deprecated API
 * is a porting target), and this accessor is exactly the kind of seam that policy was FOR: a delegate
 * answers a challenge by sending `-useCredential:forAuthenticationChallenge:` TO THE SENDER, which is how
 * the whole older authentication API works and therefore how an older application's networking code works.
 *
 * WHAT §48.1 WAS PROTECTING AGAINST IS KEPT, IN A DIFFERENT FORM: the sender is an
 * `id <NSURLAuthenticationChallengeSender>` rather than an object of unknown type, so a caller compiles
 * against a contract instead of a convention, and the library's own sender answers THE LOADING SYSTEM'S OWN
 * CONTINUATION (the completion handler the transport is waiting on) rather than inventing a second answer
 * path. A challenge built by a caller may carry whatever sender it was given, or none.
 */

#ifndef _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H
#define _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLProtectionSpace.h>

@class NSURLCredential;
@class NSURLResponse;
@class NSError;

NS_ASSUME_NONNULL_BEGIN

/* THE PROTOCOL BELOW NAMES THE CLASS, AND THE CLASS IS DECLARED UNDER IT: the forward declaration is what
 * makes that legal, and it is the reason it sits beside the other three rather than being left out. */
@class NSURLAuthenticationChallenge;

/*
 * WHO IS ASKING, AND HOW AN ANSWER GOES BACK (Apple's protocol, and the door an older delegate uses).
 * THE THREE REQUIRED ACTIONS ARE APPLE'S THREE - answer with a credential, continue with none, or cancel -
 * and the two optional ones are the finer outcomes (Apple's own documentation marks them optional). A
 * sender is expected to be answered ONCE per challenge; what a second answer does is the sender's business,
 * and this library's sender ignores it.
 */
@protocol NSURLAuthenticationChallengeSender <NSObject>

@required

- (void)useCredential:(NSURLCredential *)credential
forAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

- (void)continueWithoutCredentialForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

- (void)cancelAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

@optional

- (void)performDefaultHandlingForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

- (void)rejectProtectionSpaceAndContinueWithChallenge:(NSURLAuthenticationChallenge *)challenge;

@end

@interface NSURLAuthenticationChallenge : NSObject
{
	NSURLProtectionSpace *_protectionSpace;
	NSURLCredential *_proposedCredential;
	NSURLResponse *_failureResponse;
	NSError *_error;
	NSInteger _previousFailureCount;
	id <NSURLAuthenticationChallengeSender> _sender;
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
/* WHO ASKED, AND THE DOOR AN ANSWER GOES BACK THROUGH. Nil when a caller built the challenge without one,
 * which is why the property is nullable; non-nil for a challenge the loading system handed to a delegate. */
@property (nullable, readonly, retain) id <NSURLAuthenticationChallengeSender> sender;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLAUTHENTICATIONCHALLENGE_H */
