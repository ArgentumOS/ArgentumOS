/*
 * FNAuthenticationChallengeSender.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LOADING SYSTEM'S OWN SENDER: the object a challenge carries when the TRANSPORT built it, so that a
 * delegate can answer the way the older API answers — by sending `-useCredential:forAuthenticationChallenge:`
 * to the sender rather than by calling the completion handler it was handed.
 *
 * WHY IT EXISTS AT ALL (§62.27): `NSURLAuthenticationChallenge` always ACCEPTED a `sender:` and never kept
 * one, because §48.1 refused `-sender` as Apple-deprecated. §62.24 retired that ground, and the accessor is
 * only half the seam — the other half is a sender that ANSWERS SOMETHING. That something is the loading
 * system's own continuation: the transport is blocked waiting on the completion handler it passed to
 * `-fnAskClientForCredential:completionHandler:` (a synchronous wait, by design), and this class is the
 * object that calls it. NO SECOND ANSWER PATH IS INVENTED: the sender is a THUNK over the same handler, so
 * a delegate that answers either way moves the same transfer.
 *
 * IT IS NOT PUBLIC API AND IS NOT IN THE UMBRELLA: Apple's `NSURLAuthenticationChallengeSender` protocol is
 * the public contract (declared in NSURLAuthenticationChallenge.h), and WHICH object implements it is the
 * library's business. A first-party header is how this tree names an internal class it may still test.
 *
 * IT ANSWERS ONCE. Apple's rule is one answer per challenge, and a sender that forwarded a second answer
 * would resume a continuation that has already resumed; the second answer is ignored rather than reported,
 * because there is no caller for whom a report would mean anything.
 */

#ifndef FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H
#define FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLAuthenticationChallenge.h>
#import <Foundation/NSURLSession.h>

NS_ASSUME_NONNULL_BEGIN

@class NSURLCredential;

@interface FNAuthenticationChallengeSender : NSObject <NSURLAuthenticationChallengeSender>
{
	void (^_handler)(NSURLSessionAuthChallengeDisposition disposition,
			 NSURLCredential * _Nullable credential);
	BOOL _answered;
}

/* THE THUNK. The handler is the transport's continuation, and this object owns a copy of it. */
- (instancetype)fnInitWithCompletionHandler:
	(void (^)(NSURLSessionAuthChallengeDisposition disposition,
		  NSURLCredential * _Nullable credential))handler;

/* THE ONE PLACE THE MAPPING LIVES: every door of the protocol above is a disposition, and this is the door
 * they all pass through. Exposed because the mapping IS the contract worth testing. */
- (void)fnAnswerWithDisposition:(NSURLSessionAuthChallengeDisposition)disposition
		     credential:(nullable NSURLCredential *)credential;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNAUTHENTICATIONCHALLENGESENDER_H */
