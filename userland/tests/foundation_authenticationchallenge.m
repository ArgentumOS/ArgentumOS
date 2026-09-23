/*
 * foundation_authenticationchallenge.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The challenge's mechanics: the first-attempt state, the copy door, and the one place this library
 * deviates from Apple's header - the initialisers keep the documented sender: argument while the accessor
 * that would hand it back is NOT shipped. Both halves of that are asserted, because either alone would be
 * wrong: an initialiser that dropped the argument would break source compatibility, and an accessor would
 * ship a Legacy API.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-AUTHENTICATIONCHALLENGE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-AUTHENTICATIONCHALLENGE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	{
		NSURLProtectionSpace *space = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:@"restricted" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSURLCredential *proposed = [NSURLCredential credentialWithUser:@"kyle"
								       password:@"hunter2"
								    persistence:NSURLCredentialPersistenceForSession];
		NSURLAuthenticationChallenge *first = [[NSURLAuthenticationChallenge alloc]
			initWithProtectionSpace:space
			     proposedCredential:nil
			   previousFailureCount:0
				failureResponse:nil
					  error:nil
					 sender:nil];
		NSURLAuthenticationChallenge *retry = [[NSURLAuthenticationChallenge alloc]
			initWithProtectionSpace:space
			     proposedCredential:proposed
			   previousFailureCount:2
				failureResponse:nil
					  error:nil
					 sender:nil];

		check("the-challenge-carries-its-space",
		      [[first protectionSpace] isEqual:space] &&
		      [[[first protectionSpace] host] isEqualToString:@"example.com"],
		      @"the protection space is the challenge's subject and survives as a value");
		check("a-first-attempt-is-counted-zero",
		      [first previousFailureCount] == 0,
		      @"0 means none have been tried, which is why the count is carried");
		check("a-first-attempt-has-no-proposed-credential",
		      [first proposedCredential] == nil,
		      @"nothing has been offered yet");
		check("a-first-attempt-has-no-error",
		      [first error] == nil && [first failureResponse] == nil,
		      @"nothing has failed yet");
		check("a-retry-carries-what-was-proposed",
		      [[[retry proposedCredential] user] isEqualToString:@"kyle"] &&
		      [[[retry proposedCredential] password] isEqualToString:@"hunter2"],
		      @"the credential offered last time is part of the challenge");
		check("and-how-many-times-it-has-failed", [retry previousFailureCount] == 2,
		      @"the count is kept, not inferred");

		/* THE COPY DOOR KEEPS EVERY FIELD. */
		{
			NSURLAuthenticationChallenge *copy = [[NSURLAuthenticationChallenge alloc]
				initWithAuthenticationChallenge:retry sender:nil];

			check("the-copy-door-keeps-the-space",
			      [[copy protectionSpace] isEqual:[retry protectionSpace]],
			      @"a rebuilt challenge is the same challenge");
			check("the-copy-door-keeps-the-count",
			      [copy previousFailureCount] == [retry previousFailureCount],
			      @"including the failure count");
			check("the-copy-door-keeps-the-credential",
			      [[copy proposedCredential] isEqual:[retry proposedCredential]] &&
			      [[[copy proposedCredential] user] isEqualToString:@"kyle"],
			      @"and the proposed credential");
		}

		/* THE DOCUMENTED sender: ARGUMENT COMPILES AND IS NOT EXPOSED. */
		{
			NSURLAuthenticationChallenge *withSender = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space
				     proposedCredential:nil
				   previousFailureCount:0
					failureResponse:nil
					  error:nil
					 sender:space];

			check("the-documented-sender-argument-still-compiles",
			      withSender != nil &&
			      [[withSender protectionSpace] isEqual:space],
			      @"source compatibility holds: the argument is accepted");
			check("but-the-sender-accessor-is-absent",
			      ![NSURLAuthenticationChallenge instancesRespondToSelector:
					NSSelectorFromString(@"sender")],
			      @"Apple files -sender as Legacy; this library answers 'who asked' with the task");
		}
	}

	printf("FOUNDATION-AUTHENTICATIONCHALLENGE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-AUTHENTICATIONCHALLENGE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-AUTHENTICATIONCHALLENGE DONE\n");
	return failc ? 1 : 0;
}
