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
 *   configuration-api-inventory  the audited inventory: every owed selector exists — including the four
 *        storage/header doors whose classes are shipped today (see the class header) — and every one this
 *        slice still REFUSES (the coder doors) is absent.
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

	{
		/* §63.128: THE EIGHT DOORS §63.127 ADDED. ⚠ THIS PROBE'S `check()` TAKES A `const char *` NAME, AN `int`,
		 * AND AN `NSString *` DETAIL — READ BEFORE WRITING (§63.122's rule, and this is the sixth probe carrying this
		 * same shape). **THE CONTRACTS ASSERTED ARE THE ONES THE UNIT CREATED: A FLAG SETS AND READS BACK, A VALUE IS
		 * COPIED RATHER THAN HELD, AND NEITHER LEAKS INTO ANOTHER CONFIGURATION.** */
		NSURLSessionConfiguration *one = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSessionConfiguration *two = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSMutableString *shared = [NSMutableString stringWithString:@"group.one"];
		NSMutableDictionary *proxy = [NSMutableDictionary dictionaryWithObject:@"127.0.0.1" forKey:@"HTTPProxy"];
		BOOL flagsHeld = YES;
		unichar before;

		[one setAllowsUltraConstrainedNetworkAccess:YES];
		[one setEnablesEarlyData:YES];
		[one setRequiresDNSSECValidation:YES];
		[one setSessionSendsLaunchEvents:YES];
		[one setShouldUseExtendedBackgroundIdleMode:YES];
		[one setUsesClassicLoadingMode:YES];
		flagsHeld = [one allowsUltraConstrainedNetworkAccess] && [one enablesEarlyData] &&
			    [one requiresDNSSECValidation] && [one sessionSendsLaunchEvents] &&
			    [one shouldUseExtendedBackgroundIdleMode] && [one usesClassicLoadingMode];
		check("session-configuration-new-flags-round-trip", flagsHeld,
		      [NSString stringWithFormat:@"ultra=%d early=%d dnssec=%d launch=%d idle=%d classic=%d",
			(int)[one allowsUltraConstrainedNetworkAccess], (int)[one enablesEarlyData],
			(int)[one requiresDNSSECValidation], (int)[one sessionSendsLaunchEvents],
			(int)[one shouldUseExtendedBackgroundIdleMode], (int)[one usesClassicLoadingMode]]);

		[one setSharedContainerIdentifier:shared];
		[one setConnectionProxyDictionary:proxy];
		[shared appendString:@".MUTATED"];
		[proxy setObject:@"10.0.0.1" forKey:@"HTTPSProxy"];
		check("session-configuration-new-values-are-copied-not-held",
		      [[one sharedContainerIdentifier] isEqualToString:@"group.one"] &&
		      [[one connectionProxyDictionary] objectForKey:@"HTTPSProxy"] == nil,
		      [NSString stringWithFormat:@"shared=%@ proxies=%@",
			[one sharedContainerIdentifier], [one connectionProxyDictionary]]);

		before = [[two sharedContainerIdentifier] length];
		check("session-configuration-new-doors-do-not-leak-between-instances",
		      [two allowsUltraConstrainedNetworkAccess] == NO &&
		      [two sharedContainerIdentifier] == nil &&
		      [two connectionProxyDictionary] == nil && before == 0,
		      [NSString stringWithFormat:@"other-ultra=%d other-shared=%@",
			(int)[two allowsUltraConstrainedNetworkAccess], [two sharedContainerIdentifier]]);
	}

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
		      [configuration HTTPAdditionalHeaders] != nil &&
		      [[configuration HTTPAdditionalHeaders] count] == 0 &&
		      [configuration HTTPCookieAcceptPolicy] ==
			      NSHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain &&
		      [configuration HTTPCookieStorage] == [NSHTTPCookieStorage sharedHTTPCookieStorage] &&
		      [configuration URLCache] == [NSURLCache sharedURLCache] &&
		      [configuration URLCredentialStorage] == [NSURLCredentialStorage sharedCredentialStorage] &&
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
		      [background timeoutIntervalForRequest] == 60.0 &&
		      [[NSURLSessionConfiguration backgroundSessionConfiguration:@"com.example.bg.old"] identifier]
			      != nil &&
		      [[[NSURLSessionConfiguration backgroundSessionConfiguration:@"com.example.bg.old"] identifier]
			      isEqual:@"com.example.bg.old"],
		      @"the background door stores the identifier and starts from the same defaults; the DEPRECATED "
		      @"spelling answers the same object");
	}

	/* --- EVERY SETTER ROUND-TRIPS ---------------------------------------------------------------- */
	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSArray *classes = [NSArray arrayWithObject:[NSURLProtocol class]];
		NSURLCache *probeCache = [[NSURLCache alloc] initWithMemoryCapacity:512
								  diskCapacity:0
								  directoryURL:nil];
		NSHTTPCookieStorage *probeStore = [[NSHTTPCookieStorage alloc] init];
		NSURLCredentialStorage *probeCredentials = [[NSURLCredentialStorage alloc] init];
		NSMutableDictionary *probeHeaders = [[NSMutableDictionary alloc] init];

		[probeHeaders setObject:@"1" forKey:@"X-Probe"];

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
		[configuration setHTTPAdditionalHeaders:probeHeaders];
		[configuration setHTTPCookieAcceptPolicy:NSHTTPCookieAcceptPolicyNever];
		[configuration setURLCache:probeCache];
		[configuration setHTTPCookieStorage:probeStore];
		[configuration setURLCredentialStorage:probeCredentials];
		/* AND THE HEADER BAG IS SNAPSHOTTED: the config keeps the one-entry copy it took, so this mutation
		 * cannot reach it - which the isEqual below proves, because the comparison is against the ORIGINAL. */
		[probeHeaders setObject:@"2" forKey:@"Y"];

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
		      [[configuration HTTPAdditionalHeaders] isEqual:
			      [NSDictionary dictionaryWithObject:@"1" forKey:@"X-Probe"]] &&
		      [configuration HTTPCookieAcceptPolicy] == NSHTTPCookieAcceptPolicyNever &&
		      [configuration URLCache] == probeCache &&
		      [configuration HTTPCookieStorage] == probeStore &&
		      [configuration URLCredentialStorage] == probeCredentials &&
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
			"backgroundSessionConfigurationWithIdentifier:", "backgroundSessionConfiguration:", NULL
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
			"HTTPAdditionalHeaders", "setHTTPAdditionalHeaders:",
			"HTTPCookieAcceptPolicy", "setHTTPCookieAcceptPolicy:",
			"HTTPCookieStorage", "setHTTPCookieStorage:",
			"URLCache", "setURLCache:",
			"URLCredentialStorage", "setURLCredentialStorage:",
			"protocolClasses", "setProtocolClasses:", NULL
		};
		/* REFUSED: the coder doors every class in this library refuses. The four storage/header doors that
		 * used to be refused HERE have MOVED UP into instanceSelectors - their classes are shipped today
		 * (the class header records the measurement), so the names are asserted PRESENT, not absent. */
		static const char *excluded[] = {
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
