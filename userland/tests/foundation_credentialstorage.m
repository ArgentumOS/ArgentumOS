/*
 * foundation_credentialstorage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The store: what it files, what it replaces, what it announces - and the property the previous class's
 * probe found the hard way, that a REBUILT protection space must find the entry a different instance filed.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-CREDENTIALSTORAGE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CREDENTIALSTORAGE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static NSURLProtectionSpace *fn_space(NSString *realm)
{
	return [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:realm authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
}

@interface FNCredWatcher : NSObject
{
	@public
	int changes;
}
@end

@implementation FNCredWatcher
- (void)credentialsChanged:(NSNotification *)notification
{
	(void)notification;
	changes++;
}
@end

int main(void)
{
	NSURLCredentialStorage *store;
	FNCredWatcher *watcher;
	NSURLProtectionSpace *space;
	NSURLCredential *a, *b;

	setvbuf(stdout, NULL, _IONBF, 0);

	store = [NSURLCredentialStorage sharedCredentialStorage];
	watcher = [[FNCredWatcher alloc] init];
	space = fn_space(@"restricted");
	a = [NSURLCredential credentialWithUser:@"kyle" password:@"one"
				   persistence:NSURLCredentialPersistenceForSession];
	b = [NSURLCredential credentialWithUser:@"other" password:@"two"
				   persistence:NSURLCredentialPersistenceForSession];

	check("the-shared-store-is-one-per-process",
	      store == [NSURLCredentialStorage sharedCredentialStorage],
	      @"two calls answer the same object");

	[[NSNotificationCenter defaultCenter] addObserver:watcher
						selector:@selector(credentialsChanged:)
						    name:NSURLCredentialStorageChangedNotification
						  object:store];

	/* --- FILING, AND THE KEY PROPERTY --------------------------------------------------------------- */
	[store setCredential:a forProtectionSpace:space];
	check("a-credential-is-filed-under-its-space",
	      [[store credentialsForProtectionSpace:space] count] == 1,
	      @"the store keeps what was set");
	{
		/* A REBUILT SPACE, which is the only form a caller ever has - and the form that aborted the
		 * previous class's probe when its copy/equality/hash were missing. */
		NSURLProtectionSpace *rebuilt = fn_space(@"restricted");

		check("a-rebuilt-space-finds-the-entry",
		      [[store credentialsForProtectionSpace:rebuilt] count] == 1,
		      @"which is why NSURLProtectionSpace implements -copy, -isEqual: and -hash");
	}

	[store setCredential:b forProtectionSpace:space];
	check("two-credentials-coexist-for-one-realm",
	      [[store credentialsForProtectionSpace:space] count] == 2,
	      @"one realm can accept more than one user");
	{
		int before = watcher->changes;

		[store setCredential:a forProtectionSpace:space];
		check("setting-the-same-credential-twice-does-not-duplicate",
		      [[store credentialsForProtectionSpace:space] count] == 2,
		      @"a repeat replaces rather than accumulating");
		check("and-a-repeat-is-still-a-change", watcher->changes > before,
		      @"the store can only say what it did; replacing IS doing something");
	}

	/* --- THE DEFAULT, AND REMOVAL ------------------------------------------------------------------- */
	check("no-default-means-none",
	      [store defaultCredentialForProtectionSpace:space] == nil,
	      @"the default is kept separately and starts absent");
	[store setDefaultCredential:a forProtectionSpace:space];
	check("the-default-is-kept",
	      [[[store defaultCredentialForProtectionSpace:space] user] isEqualToString:@"kyle"],
	      @"a default credential for a space is one per space");
	{
		int before = watcher->changes;
		NSURLCredential *never = [NSURLCredential credentialWithUser:@"nobody"
								   password:@"x"
								persistence:NSURLCredentialPersistenceNone];

		[store removeCredential:never forProtectionSpace:space];
		check("removing-what-is-not-there-is-silent", watcher->changes == before,
		      @"nothing changed, so nothing is announced");
	}
	[store removeCredential:b forProtectionSpace:space];
	check("removal-takes-the-credential-out",
	      [[store credentialsForProtectionSpace:space] count] == 1,
	      @"the one removed is gone and the other stays");
	check("all-credentials-answers-by-space",
	      [[store allCredentials] count] == 1,
	      @"the dictionary is keyed by protection space and the emptied one is dropped");
	check("the-store-announced-its-changes", watcher->changes >= 5,
	      @"every real change posts NSURLCredentialStorageChangedNotification");

	/* --- THE TASK-SCOPED DOORS ARE THE SAME ANSWER, AS THE HEADER SAYS ------------------------------ */
	

	/* --- WHAT IS REFUSED, ASSERTED ABSENT ----------------------------------------------------------- */
	check("the-removal-options-door-is-absent",
	      ![NSURLCredentialStorage instancesRespondToSelector:
			NSSelectorFromString(@"removeCredential:forProtectionSpace:options:")],
	      @"that dictionary exists for iCloud synchronisation, which this system does not have");
	check("and-so-is-its-task-form",
	      ![NSURLCredentialStorage instancesRespondToSelector:
			NSSelectorFromString(@"removeCredential:forProtectionSpace:options:task:")],
	      @"refused for the same reason");

	printf("FOUNDATION-CREDENTIALSTORAGE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CREDENTIALSTORAGE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CREDENTIALSTORAGE DONE\n");
	return failc ? 1 : 0;
}
