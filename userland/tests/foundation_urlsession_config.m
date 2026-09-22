/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlsession_config, unit of 1 — W7 slice 2c's session half, first row:
 * NSURLSessionConfiguration. docs/design/foundation-plan.md W7; the transport plan's §4, slice 2c.
 *
 * ONE unit, importing only <Foundation/Foundation.h>, and NO SESSION IS CREATED HERE: this class is what
 * a session is BUILT FROM, and it is a value with documented defaults — so the probe is a value probe,
 * exactly as slice 1's was for the request/response pair.
 *
 * THE CHECKS THAT EARN THEIR PLACE:
 *   ephemeral-configuration-differs-only-by-identity  Apple documents the ephemeral door as keeping no
 *        persistent caches, cookies or credentials — AND THIS LIBRARY SHIPS NONE OF THOSE YET — so the
 *        honest assertion is that in this tree the two built-in doors are equal in every value and
 *        distinct as objects. Pinning it is what stops the door from reading as "it does something";
 *   configuration-copy-is-a-snapshot  every property here is READWRITE, so -copy must be a REAL copy:
 *        mutating the copy must leave the original alone, which is the one thing that distinguishes it
 *        from the -retain the immutable classes in this library answer with;
 *   configuration-api-inventory  the audited inventory: every owed selector exists and every one this
 *        slice REFUSES (the storage-valued properties, whose classes are their own ledger rows, and the
 *        coder doors) is absent.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLSESSION-CONFIG %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLSESSION-CONFIG %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	/* --- THE DOCUMENTED DEFAULTS ----------------------------------------------------------------- */
	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];

		check("session-configuration-defaults",
		      configuration != nil &&
		      [configuration identifier] == nil &&
		      [configuration requestCachePolicy] == NSURLRequestUseProtocolCachePolicy &&
		      [configuration timeoutIntervalForRequest] == 60.0 &&
		      [configuration timeoutIntervalForResource] == 604800.0 &&
		      [configuration networkServiceType] == NSURLNetworkServiceTypeDefault &&
		      [configuration allowsCellularAccess] == YES &&
		      [configuration HTTPShouldUsePipelining] == NO &&
		      [configuration HTTPShouldSetCookies] == YES &&
		      [configuration HTTPMaximumConnectionsPerHost] == 6 &&
		      [configuration discretionary] == NO &&
		      [configuration protocolClasses] == nil,
		      @"the default door answers Apple's documented values, in one place");

		/* --- AND THE TWO BUILT-IN DOORS: EQUAL VALUES, DISTINCT OBJECTS ------------------------- */
		{
			NSURLSessionConfiguration *ephemeral = [NSURLSessionConfiguration ephemeralSessionConfiguration];
			int sameValues = ephemeral != nil &&
					 [ephemeral requestCachePolicy] == [configuration requestCachePolicy] &&
					 [ephemeral timeoutIntervalForRequest] == [configuration timeoutIntervalForRequest] &&
					 [ephemeral timeoutIntervalForResource] == [configuration timeoutIntervalForResource] &&
					 [ephemeral networkServiceType] == [configuration networkServiceType] &&
					 [ephemeral HTTPMaximumConnectionsPerHost] ==
						 [configuration HTTPMaximumConnectionsPerHost] &&
					 [ephemeral identifier] == nil &&
					 [ephemeral protocolClasses] == nil;

			check("ephemeral-configuration-differs-only-by-identity",
			      sameValues && ephemeral != configuration,
			      @"with no cache, cookie or credential class in this tree, the ephemeral door can only "
			      @"differ by identity - asserted rather than assumed");
		}
	}

	/* --- THE BACKGROUND DOOR CARRIES ITS IDENTIFIER ----------------------------------------------- */
	{
		NSURLSessionConfiguration *background =
			[NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier:@"com.example.bg"];

		check("background-configuration-carries-its-identifier",
		      background != nil &&
		      [[background identifier] isEqual:@"com.example.bg"] &&
		      [background timeoutIntervalForRequest] == 60.0,
		      @"the background door stores the identifier and starts from the same defaults");
	}

	/* --- EVERY SETTER ROUND-TRIPS ---------------------------------------------------------------- */
	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSArray *classes = [NSArray arrayWithObject:[NSURLProtocol class]];

		[configuration setRequestCachePolicy:NSURLRequestReloadIgnoringLocalCacheData];
		[configuration setTimeoutIntervalForRequest:12.5];
		[configuration setTimeoutIntervalForResource:900.0];
		[configuration setNetworkServiceType:NSURLNetworkServiceTypeBackground];
		[configuration setAllowsCellularAccess:NO];
		[configuration setAllowsExpensiveNetworkAccess:NO];
		[configuration setAllowsConstrainedNetworkAccess:NO];
		[configuration setWaitsForConnectivity:YES];
		[configuration setHTTPShouldUsePipelining:YES];
		[configuration setHTTPShouldSetCookies:NO];
		[configuration setHTTPMaximumConnectionsPerHost:2];
		[configuration setDiscretionary:YES];
		[configuration setProtocolClasses:classes];

		check("configuration-setters-round-trip",
		      [configuration requestCachePolicy] == NSURLRequestReloadIgnoringLocalCacheData &&
		      [configuration timeoutIntervalForRequest] == 12.5 &&
		      [configuration timeoutIntervalForResource] == 900.0 &&
		      [configuration networkServiceType] == NSURLNetworkServiceTypeBackground &&
		      [configuration allowsCellularAccess] == NO &&
		      [configuration allowsExpensiveNetworkAccess] == NO &&
		      [configuration allowsConstrainedNetworkAccess] == NO &&
		      [configuration waitsForConnectivity] == YES &&
		      [configuration HTTPShouldUsePipelining] == YES &&
		      [configuration HTTPShouldSetCookies] == NO &&
		      [configuration HTTPMaximumConnectionsPerHost] == 2 &&
		      [configuration discretionary] == YES &&
		      [[configuration protocolClasses] count] == 1,
		      @"every property is readwrite and reads back what was set");

		/* A SNAPSHOT: the array a caller keeps is copied in, so mutating it cannot reach the config. */
		{
			NSMutableArray *mutable = [NSMutableArray arrayWithObject:[NSURLProtocol class]];

			[configuration setProtocolClasses:mutable];
			[mutable addObject:[NSURL class]];
			check("protocol-classes-are-snapshotted",
			      [[configuration protocolClasses] count] == 1,
			      @"-setProtocolClasses: copies, so the caller's array cannot change under it");
		}

		/* AND A REAL COPY: mutating the copy leaves the original alone. */
		{
			NSURLSessionConfiguration *copy = [configuration copy];

			[copy setTimeoutIntervalForRequest:1.0];
			[copy setHTTPMaximumConnectionsPerHost:99];
			check("configuration-copy-is-a-snapshot",
			      [configuration timeoutIntervalForRequest] == 12.5 &&
			      [configuration HTTPMaximumConnectionsPerHost] == 2 &&
			      [copy timeoutIntervalForRequest] == 1.0 &&
			      [copy HTTPMaximumConnectionsPerHost] == 99,
			      @"a configuration's -copy is a real copy, because every property is readwrite");
		}
	}

	/* --- THE AUDITED INVENTORY -------------------------------------------------------------------- */
	{
		static const char *classSelectors[] = {
			"defaultSessionConfiguration", "ephemeralSessionConfiguration",
			"backgroundSessionConfigurationWithIdentifier:", NULL
		};
		static const char *instanceSelectors[] = {
			"identifier", "requestCachePolicy", "setRequestCachePolicy:",
			"timeoutIntervalForRequest", "setTimeoutIntervalForRequest:",
			"timeoutIntervalForResource", "setTimeoutIntervalForResource:",
			"networkServiceType", "setNetworkServiceType:",
			"allowsCellularAccess", "setAllowsCellularAccess:",
			"allowsExpensiveNetworkAccess", "setAllowsExpensiveNetworkAccess:",
			"allowsConstrainedNetworkAccess", "setAllowsConstrainedNetworkAccess:",
			"waitsForConnectivity", "setWaitsForConnectivity:",
			"HTTPShouldUsePipelining", "setHTTPShouldUsePipelining:",
			"HTTPShouldSetCookies", "setHTTPShouldSetCookies:",
			"HTTPMaximumConnectionsPerHost", "setHTTPMaximumConnectionsPerHost:",
			"discretionary", "setDiscretionary:",
			"protocolClasses", "setProtocolClasses:", NULL
		};
		/* REFUSED, EACH BECAUSE THE CLASS BEHIND IT IS ITS OWN LEDGER ROW AND NOT SHIPPED: the store,
		 * the cookie storage and the credential storage, plus the cookie family's own enum and the coder
		 * doors that every class in this library refuses. */
		static const char *excluded[] = {
			"URLCache", "setURLCache:", "HTTPCookieStorage", "setHTTPCookieStorage:",
			"URLCredentialStorage", "setURLCredentialStorage:",
			"HTTPCookieAcceptPolicy", "setHTTPCookieAcceptPolicy:",
			"initWithCoder:", "encodeWithCoder:", NULL
		};
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSURLSessionConfiguration respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION-CONFIG missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![configuration respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION-CONFIG missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([configuration respondsToSelector:sel_registerName(excluded[i])] ||
			    [NSURLSessionConfiguration respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION-CONFIG present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("configuration-api-inventory", complete,
		      @"the audited inventory: every owed selector exists, and nothing listed as excluded does");
	}

	printf("FOUNDATION-URLSESSION-CONFIG RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLSESSION-CONFIG-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLSESSION-CONFIG DONE\n");
	return failc ? 1 : 0;
}
