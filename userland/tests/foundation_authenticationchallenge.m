/*
 * foundation_authenticationchallenge.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The challenge's mechanics: the first-attempt state, the copy door, and THE SENDER — which §62.27 changed
 * from "accepted and not kept" to "kept and handed back". §48.1 had refused the accessor as Apple-deprecated;
 * §62.24 retired that ground (the user's policy, 2026-09-26), and the accessor is the seam an older delegate
 * answers through. The check that asserted the accessor ABSENT is therefore FLIPPED rather than deleted —
 * §11.2's rule that an absence assertion is a fact about the tree, so a landing that changes the tree
 * revisits it, and this is the second time in this file's neighbourhood it has happened.
 *
 * WHAT THE ACCESSOR HANDS BACK IS A CONTRACT, NOT A CONVENTION: `id <NSURLAuthenticationChallengeSender>`,
 * with Apple's three required actions and its two optional ones - the protocol's shape is asserted here,
 * because a sender that cannot be answered is not a seam.
 */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>

/* AN OPTIONAL METHOD'S PRESENCE IS ASKED OF THE PROTOCOL OBJECT (the idiom the connection probe uses): an
 * `@optional` member is invisible to `-respondsToSelector:`, which answers about a CLASS. */
static BOOL fn_protocol_has(Protocol *proto, const char *sel, BOOL required)
{
	struct objc_method_description d;

	if(proto == NULL) {
		return NO;
	}
	d = protocol_getMethodDescription(proto, sel_registerName(sel), required, YES);
	return d.types != NULL;
}

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

		/* THE sender: ARGUMENT IS ACCEPTED AND NOW KEPT (§62.27). */
		{
			NSObject *fixtureSender = [[NSObject alloc] init];
			NSURLAuthenticationChallenge *withSender = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space
				     proposedCredential:nil
				   previousFailureCount:0
					failureResponse:nil
					  error:nil
					 sender:fixtureSender];
			NSURLAuthenticationChallenge *withoutSender = [[NSURLAuthenticationChallenge alloc]
				initWithProtectionSpace:space
				     proposedCredential:nil
				   previousFailureCount:0
					failureResponse:nil
					  error:nil
					 sender:nil];
			NSURLAuthenticationChallenge *copyWithout = [[NSURLAuthenticationChallenge alloc]
				initWithAuthenticationChallenge:withSender sender:nil];

			check("the-sender-argument-is-kept-and-handed-back",
			      withSender != nil && [[withSender protectionSpace] isEqual:space] &&
			      [withSender sender] == fixtureSender,
			      @"source compatibility holds AND the argument is retained: a challenge built with a "
			      @"sender answers it");
			check("a-challenge-with-no-sender-answers-nil",
			      withoutSender != nil && [withoutSender sender] == nil,
			      @"a caller that builds one without a sender gets nil, which is why the property is "
			      @"nullable - the LOADING SYSTEM is what supplies one");
			check("the-copy-door-takes-the-sender-it-was-given",
			      copyWithout != nil && [copyWithout sender] == nil &&
			      [[copyWithout protectionSpace] isEqual:space],
			      @"the copy door exists so a transport can hand out a challenge whose answers go "
			      @"somewhere of ITS choosing: the copy carries the sender passed to IT, not the "
			      @"original's");
			check("the-sender-protocol-shape",
			      fn_protocol_has(objc_getProtocol("NSURLAuthenticationChallengeSender"),
					      "useCredential:forAuthenticationChallenge:", YES) &&
			      fn_protocol_has(objc_getProtocol("NSURLAuthenticationChallengeSender"),
					      "continueWithoutCredentialForAuthenticationChallenge:", YES) &&
			      fn_protocol_has(objc_getProtocol("NSURLAuthenticationChallengeSender"),
					      "cancelAuthenticationChallenge:", YES) &&
			      fn_protocol_has(objc_getProtocol("NSURLAuthenticationChallengeSender"),
					      "performDefaultHandlingForAuthenticationChallenge:", NO) &&
			      fn_protocol_has(objc_getProtocol("NSURLAuthenticationChallengeSender"),
					      "rejectProtectionSpaceAndContinueWithChallenge:", NO),
			      @"Apple's three REQUIRED actions and its two OPTIONAL ones, at the names and the "
			      @"requiredness Apple publishes");
		}
	}

	printf("FOUNDATION-AUTHENTICATIONCHALLENGE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-AUTHENTICATIONCHALLENGE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-AUTHENTICATIONCHALLENGE DONE\n");
	return failc ? 1 : 0;
}
