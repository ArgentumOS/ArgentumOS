/*
 * FNAuthenticationChallengeSender.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The mapping from Apple's five sender actions onto the TRANSPORT'S five answers. It is a TABLE, and it is
 * worth writing out because every line of it is Apple's rather than this library's:
 *
 *   -useCredential:forAuthenticationChallenge:        -> UseCredential, with the credential given
 *   -continueWithoutCredentialForAuthenticationChallenge: -> WithoutCredential
 *   -cancelAuthenticationChallenge:                   -> Cancel
 *   -performDefaultHandlingForAuthenticationChallenge: -> DefaultHandling     (optional in the protocol)
 *   -rejectProtectionSpaceAndContinueWithChallenge:   -> RejectProtectionSpace (optional in the protocol)
 *
 * ⚠⚠ AND THE SECOND LINE IS WHY §63.158'S FIRST-PARTY VOCABULARY IS BETTER THAN THE ONE IT REPLACED. The old
 * table had to answer `-continueWithoutCredentialForAuthenticationChallenge:` with **UseCredential and a nil
 * credential**, because the session's enum it borrowed has no case for "continue without one" — Apple's own
 * disposition says "the specified credential, which may be nil", so a client saying "no credential" and a
 * client saying "here it is, and it is nil" were the SAME answer. Its own vocabulary has a case for it, so the
 * translation stops overloading a word: the transport now hears WHICH of the two a client meant.
 *
 * All five keep the continuation's own lifetime rule: this file owns a COPY (Block_copy) and releases it in
 * dealloc, which is the house rule for a stored block - the gate refuses the message form for a block-typed
 * name, and an MRC block is not an ordinary object to own.
 */

#import <Foundation/FNAuthenticationChallengeSender.h>
#import <Foundation/NSURLCredential.h>
#include <Block.h>	/* Block_copy/Block_release: the runtime entry points, which the house rule requires for a
			 * STORED block - tools/foundation-gate.py refuses the message form for a block-typed name. */

@implementation FNAuthenticationChallengeSender

- (instancetype)fnInitWithContinuation:
	(void (^)(FNAuthenticationChallengeAnswer answer,
		  NSURLCredential *credential))handler
{
	if (!(self = [super init])) {
		return nil;
	}
	_handler = Block_copy(handler);
	return self;
}

- (void)fnAnswerWith:(FNAuthenticationChallengeAnswer)answer
	  credential:(NSURLCredential *)credential
{
	/* ONE ANSWER, AND THE SECOND IS IGNORED: the handler resumes a continuation that has already resumed
	 * the moment it is called twice, and nothing here can tell the caller even if it wanted to. */
	if (_handler == NULL || _answered) {
		return;
	}
	_answered = YES;
	_handler(answer, credential);
}

- (void)useCredential:(NSURLCredential *)credential
forAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;	/* the sender knows which continuation it thunks; the challenge is the caller's
				 * context, and it is not needed to answer */

	[self fnAnswerWith:FNAuthenticationChallengeAnswerUseCredential credential:credential];
}

- (void)continueWithoutCredentialForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWith:FNAuthenticationChallengeAnswerWithoutCredential credential:nil];
}

- (void)cancelAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWith:FNAuthenticationChallengeAnswerCancel credential:nil];
}

- (void)performDefaultHandlingForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWith:FNAuthenticationChallengeAnswerDefaultHandling credential:nil];
}

- (void)rejectProtectionSpaceAndContinueWithChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWith:FNAuthenticationChallengeAnswerRejectProtectionSpace credential:nil];
}

- (void)dealloc
{
	if (_handler != NULL) {
		Block_release(_handler);
	}
	[super dealloc];
}

@end
