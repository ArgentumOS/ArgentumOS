/*
 * FNAuthenticationChallengeSender.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The mapping from Apple's five sender actions onto the loading system's five dispositions. It is a
 * TABLE, and it is worth writing out because every line of it is Apple's rather than this library's:
 *
 *   -useCredential:forAuthenticationChallenge:        -> UseCredential, with the credential given
 *   -continueWithoutCredentialForAuthenticationChallenge: -> UseCredential, and the credential is NIL
 *                                                        (Apple's disposition says "the specified
 *                                                        credential, which may be nil", so "continue
 *                                                        without one" IS that)
 *   -cancelAuthenticationChallenge:                   -> CancelAuthenticationChallenge
 *   -performDefaultHandlingForAuthenticationChallenge: -> PerformDefaultHandling   (optional in the protocol)
 *   -rejectProtectionSpaceAndContinueWithChallenge:   -> RejectProtectionSpace     (optional in the protocol)
 *
 * All five keep the completion block's own lifetime rule: this file owns a COPY (Block_copy) and releases it
 * in dealloc, which is the house rule for a stored block - the gate refuses the message form for a
 * block-typed name, and an MRC block is not an ordinary object to own.
 */

#import <Foundation/FNAuthenticationChallengeSender.h>
#import <Foundation/NSURLCredential.h>
#include <Block.h>	/* Block_copy/Block_release: the runtime entry points, which the house rule requires for a
			 * STORED block - tools/foundation-gate.py refuses the message form for a block-typed name. */

@implementation FNAuthenticationChallengeSender

- (instancetype)fnInitWithCompletionHandler:
	(void (^)(NSURLSessionAuthChallengeDisposition disposition,
		  NSURLCredential *credential))handler
{
	if (!(self = [super init])) {
		return nil;
	}
	_handler = Block_copy(handler);
	return self;
}

- (void)fnAnswerWithDisposition:(NSURLSessionAuthChallengeDisposition)disposition
		     credential:(NSURLCredential *)credential
{
	/* ONE ANSWER, AND THE SECOND IS IGNORED: the handler resumes a continuation that has already resumed
	 * the moment it is called twice, and nothing here can tell the caller even if it wanted to. */
	if (_handler == NULL || _answered) {
		return;
	}
	_answered = YES;
	_handler(disposition, credential);
}

- (void)useCredential:(NSURLCredential *)credential
forAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;	/* the sender knows which continuation it thunks; the challenge is the caller's
				 * context, and it is not needed to answer */

	[self fnAnswerWithDisposition:NSURLSessionAuthChallengeUseCredential credential:credential];
}

- (void)continueWithoutCredentialForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWithDisposition:NSURLSessionAuthChallengeUseCredential credential:nil];
}

- (void)cancelAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWithDisposition:NSURLSessionAuthChallengeCancelAuthenticationChallenge credential:nil];
}

- (void)performDefaultHandlingForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWithDisposition:NSURLSessionAuthChallengePerformDefaultHandling credential:nil];
}

- (void)rejectProtectionSpaceAndContinueWithChallenge:(NSURLAuthenticationChallenge *)challenge
{
	(void)challenge;
	[self fnAnswerWithDisposition:NSURLSessionAuthChallengeRejectProtectionSpace credential:nil];
}

- (void)dealloc
{
	if (_handler != NULL) {
		Block_release(_handler);
	}
	[super dealloc];
}

@end
