/*
 * FNAuthenticationChallengeSender.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LOADING SYSTEM'S OWN SENDER: the object a challenge carries when the TRANSPORT built it, so that a
 * CLIENT can answer the way Apple's protocol answers — by sending `-useCredential:forAuthenticationChallenge:`
 * to the sender. It is the answer path of `NSURLProtocolClient`'s authentication door.
 *
 * ⚠⚠ AND SINCE §63.158 IT IS THE ONLY ANSWER PATH, WHICH IS WHY ITS VOCABULARY IS OURS (§48.6's registered
 * deviation is RETIRED). The seam used to carry a SECOND answer path — the completion handler that door took —
 * and this class existed to convert Apple's five sender actions into the five dispositions that handler
 * expected. The door is Apple's now (`-URLProtocol:didReceiveAuthenticationChallenge:`, no handler), so what is
 * left is the TRANSPORT'S OWN vocabulary: the five things a client can answer, named for this library rather
 * than borrowed from a session type. **THE SESSION'S OWN ENUM IS NOT DELETED BY THAT** — Apple declares
 * `NSURLSessionAuthChallengeDisposition` for its session doors, it is a `shipped` row in the ledger under
 * `NSURLSessionDelegate` and `NSURLSessionTaskDelegate`, and it lives in `NSURLSession.h` where Apple puts it.
 * This file simply stops borrowing it, which is also what stops the seam having a session-named type at all.
 *
 * WHY IT EXISTS (§62.27): `NSURLAuthenticationChallenge` always ACCEPTED a `sender:` and never kept one,
 * because §48.1 refused `-sender` as Apple-deprecated. §62.24 retired that ground, and the accessor is only
 * half the seam — the other half is a sender that ANSWERS SOMETHING. That something is the loading system's own
 * continuation: the transport is blocked while it waits, and this class is the object that releases it. NO
 * SECOND ANSWER PATH IS INVENTED: the sender is a THUNK over the transport's continuation, so a client that
 * answers moves the transfer and nothing else.
 *
 * IT IS NOT PUBLIC API AND IS NOT IN THE UMBRELLA: Apple's `NSURLAuthenticationChallengeSender` protocol is
 * the public contract (declared in NSURLAuthenticationChallenge.h), and WHICH object implements it is the
 * library's business. A first-party header is how this tree names an internal class it may still test.
 *
 * IT ANSWERS ONCE. Apple's rule is one answer per challenge, and a sender that forwarded a second answer would
 * resume a continuation that has already resumed; the second answer is ignored rather than reported, because
 * there is no caller for whom a report would mean anything.
 */

#ifndef FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H
#define FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLAuthenticationChallenge.h>

NS_ASSUME_NONNULL_BEGIN

@class NSURLCredential;

/* THE FIVE THINGS A CLIENT CAN ANSWER, which are Apple's five SENDER ACTIONS given names for the transport to
 * carry: the mapping from one to the other is the whole of what the sender does, and every door below says
 * which one it is. THE VALUES ARE OURS under §11.6.1 D2 (as every enum's in this library are): UseCredential
 * is 0 because it is the answer a client reaches for first, and a zeroed answer must not mean its opposite. */
typedef NS_ENUM(NSInteger, FNAuthenticationChallengeAnswer) {
	FNAuthenticationChallengeAnswerUseCredential = 0,
	FNAuthenticationChallengeAnswerWithoutCredential = 1,
	FNAuthenticationChallengeAnswerCancel = 2,
	FNAuthenticationChallengeAnswerDefaultHandling = 3,
	FNAuthenticationChallengeAnswerRejectProtectionSpace = 4
};

@interface FNAuthenticationChallengeSender : NSObject <NSURLAuthenticationChallengeSender>
{
	void (^_handler)(FNAuthenticationChallengeAnswer answer, NSURLCredential * _Nullable credential);
	BOOL _answered;
}

/* THE THUNK. The handler is the transport's continuation, and this object owns a copy of it. */
- (instancetype)fnInitWithContinuation:
	(void (^)(FNAuthenticationChallengeAnswer answer,
		  NSURLCredential * _Nullable credential))handler;

/* THE ONE PLACE THE MAPPING LIVES: every door of the protocol above is an answer, and this is the door they
 * all pass through. Exposed because the mapping IS the contract worth testing. */
- (void)fnAnswerWith:(FNAuthenticationChallengeAnswer)answer
	  credential:(nullable NSURLCredential *)credential;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H */
