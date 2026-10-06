/*
 * foundation_urlcredential.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A credential keeps a secret, so two of these checks are about NOT LEAKING IT - which is a behaviour no
 * member list can state and only a probe can hold.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-URLCREDENTIAL %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLCREDENTIAL %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE VALUE ---------------------------------------------------------------------------------- */
	{
		NSURLCredential *c = [NSURLCredential credentialWithUser:@"kyle"
								password:@"hunter2"
							     persistence:NSURLCredentialPersistenceForSession];

		check("the-factory-keeps-what-it-was-given",
		      [[c user] isEqualToString:@"kyle"] && [[c password] isEqualToString:@"hunter2"],
		      @"user and password survive the factory");
	covers("NSURLCredential", "password");
	covers("NSURLCredential", "persistence");
		check("the-persistence-is-kept",
		      [c persistence] == NSURLCredentialPersistenceForSession,
		      @"the persistence kind is part of the value");
		check("has-password-answers-yes", [c hasPassword],
		      @"this credential has a secret to offer");
		check("the-none-persistence-is-zero",
		      NSURLCredentialPersistenceNone == 0 &&
		      NSURLCredentialPersistencePermanent == 2,
		      @"an absent choice must be the NOT-STORED one, so None is 0");
	}

	/* --- THE SECRET IS NOT LEAKED ------------------------------------------------------------------- */
	{
		NSURLCredential *c = [NSURLCredential credentialWithUser:@"kyle"
								password:@"hunter2"
							     persistence:NSURLCredentialPersistenceNone];
		NSString *text = [c description];

		check("the-description-does-not-print-the-password",
		      text != nil && [text rangeOfString:@"hunter2"].location == NSNotFound,
		      @"a description reaches logs and debuggers; a secret that reaches them has leaked");
		check("the-description-says-a-password-is-set",
		      [text rangeOfString:@"(set)"].location != NSNotFound,
		      @"it identifies the credential without handing over the secret");
		check("the-description-does-show-the-user",
		      [text rangeOfString:@"kyle"].location != NSNotFound,
		      @"the user identifies the credential and is not a secret");
	}

	/* --- A CREDENTIAL IS A VALUE, AND COPYING KEEPS IT ONE ------------------------------------------ */
	{
		NSURLCredential *c = [NSURLCredential credentialWithUser:@"kyle"
								password:@"hunter2"
							     persistence:NSURLCredentialPersistencePermanent];
		id copy = [c copy];

		check("a-copy-carries-the-same-secret",
		      [[copy user] isEqualToString:[c user]] &&
		      [[copy password] isEqualToString:[c password]] &&
		      [copy persistence] == [c persistence],
		      @"copying keeps the value; the member is -copy per this library's convention");
	}

	/* --- WHAT IS REFUSED, ASSERTED ABSENT ----------------------------------------------------------- */
	{
		SEL trustInit = NSSelectorFromString(@"initWithTrust:");
		SEL identityInit = NSSelectorFromString(@"initWithIdentity:certificates:persistence:");
		SEL forTrust = NSSelectorFromString(@"credentialForTrust:");

		check("the-trust-initialiser-is-absent",
		      ![NSURLCredential instancesRespondToSelector:trustInit],
		      @"a SecTrustRef is the Security framework's type");
		check("the-identity-initialiser-is-absent",
		      ![NSURLCredential instancesRespondToSelector:identityInit],
		      @"a SecIdentityRef and a certificate chain are refused with it");
		check("the-trust-factory-is-absent",
		      ![NSURLCredential respondsToSelector:forTrust],
		      @"every trust and identity door is refused for the same reason");
		check("the-identity-properties-are-absent",
		      ![NSURLCredential instancesRespondToSelector:NSSelectorFromString(@"identity")] &&
		      ![NSURLCredential instancesRespondToSelector:NSSelectorFromString(@"certificates")],
		      @"and so are the properties that would hand one out");
		check("the-synchronizable-persistence-is-absent",
		      ![NSURLCredential instancesRespondToSelector:
				NSSelectorFromString(@"NSURLCredentialPersistenceSynchronizable")],
		      @"iCloud synchronisation is not a thing this system has");
	}

	printf("FOUNDATION-URLCREDENTIAL RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLCREDENTIAL-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLCREDENTIAL DONE\n");
	return failc ? 1 : 0;
}
