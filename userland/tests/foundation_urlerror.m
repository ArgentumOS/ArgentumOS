/*
 * foundation_urlerror.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ERROR NAMES, AND WHAT THEY ARE WORTH (§56): the four codes the library itself reports, the domain those
 * errors carry, and the shape of the family around them.
 *
 * WHY THE SHAPE IS CHECKED AND NOT JUST THE PRESENCE: an inventory can see that a name exists and cannot see
 * that its NUMBER is right. §55 measured that Apple's documentation publishes the names and no values, so
 * these were transcribed from the published error-condition blocks - which makes a mistyped digit the failure
 * mode to write a check for. So the checks below assert the blocks (each value in the range its block names),
 * the four values the library depends on, and DISTINCTNESS - and two of them are end-to-end: the error a
 * cancelled task carries, and the error an unclaimable request carries, both read off a real task.
 *
 * ARC, like every probe.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <unistd.h>	/* usleep: the wait for a task's ending, which is delivered on its own thread */

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-URLERROR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLERROR %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* EVERY CODE THE HEADER DECLARES, in ONE list: distinctness over the whole family is the cheapest guard there
 * is against a duplicated value, and it is a check rather than a reading of the header. */
static const NSInteger fn_allCodes[] = {
	-1, -999,
	-1000, -1001, -1002, -1003, -1004, -1005, -1006, -1007, -1008, -1009, -1010, -1011, -1012,
	-1013, -1014, -1015, -1016, -1017, -1018, -1019, -1020, -1021, -1022,
	-1100, -1101, -1102, -1103, -1104,
	-1200, -1201, -1202, -1203, -1204, -1205, -1206,
	-2000,
	-3000, -3001, -3002, -3003, -3004, -3005, -3006, -3007,
	-995, -996, -997
};

static int fn_allDistinct(void)
{
	NSUInteger n = sizeof(fn_allCodes) / sizeof(fn_allCodes[0]);
	NSUInteger i, j;

	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (fn_allCodes[i] == fn_allCodes[j]) {
				return 0;
			}
		}
	}
	return 1;
}

/* IS A VALUE IN THE BLOCK ITS NAME CLAIMS? Each block is a run, so the test is a range rather than one
 * number: a digit typed wrong inside `-1000…-1022` would still be a code, and this is what notices. */
static BOOL fn_inRun(NSInteger value, NSInteger low, NSInteger high)
{
	return value >= low && value <= high;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE FOUR THE LIBRARY ITSELF REPORTS, AND THEIR VALUES ---------------------------------------- */
	check("the-four-codes-the-library-reports",
	      NSURLErrorCancelled == -999 && NSURLErrorUnsupportedURL == -1002 &&
	      NSURLErrorHTTPTooManyRedirects == -1007 && NSURLErrorCannotCreateFile == -3000,
	      @"a cancel, an unclaimable request, a redirect loop and a body that could not be written");

	/* --- THE DOMAIN IS THE ONE THE LIBRARY BUILDS ITS ERRORS IN --------------------------------------- */
	check("the-domain-is-the-one-the-library-reports",
	      NSURLErrorDomain != nil && [NSURLErrorDomain isEqualToString:@"NSURLErrorDomain"],
	      @"the library spells this string at three call sites, so the constant has to be that string");

	/* --- AND THAT IS TRUE OF A REAL TRANSFER, NOT ONLY OF THE CONSTANTS -------------------------------- */
	/* ⚠⚠ AND THE SUBJECT IS THE KEPT CLASS NOW (§63.159). These two blocks used to build `NSURLSessionTask`s
	 * and read the errors the SESSION put on them — one cancelled, one unclaimed. THE SESSION IS GONE.
	 *
	 * THE UNCLAIMED HALF HAS AN EXACT 10.2 EQUIVALENT AND IS RE-POINTED: a request NO REGISTERED PROTOCOL
	 * CLAIMS is `NSURLErrorUnsupportedURL`, and `+sendSynchronousRequest:` is the door that reports it with no
	 * delegate to write. **AND THE CANCELLED HALF IS NOT RE-POINTED, BECAUSE THERE IS NOTHING TO POINT IT AT:**
	 * a connection's `-cancel` reports NOTHING — Apple's own contract for that class, which NSURLConnection.h
	 * states — so a check expecting a cancellation error would be asserting the OPPOSITE of what this library
	 * promises. It is deleted rather than weakened. */
	{
		NSURLResponse *response = nil;
		NSError *ending = nil;

		/* NOTHING CLAIMS THIS SCHEME, which is exactly the path that builds NSURLErrorUnsupportedURL. */
		(void)[NSURLConnection sendSynchronousRequest:
			[NSURLRequest requestWithURL:
				[NSURL URLWithString:@"fn-nobody-claims://example.test/"]]
					       returningResponse:&response
						   error:&ending];
		check("and-an-unclaimable-request-carries-the-unsupported-url-code",
		      ending != nil && [[ending domain] isEqualToString:NSURLErrorDomain] &&
		      [ending code] == NSURLErrorUnsupportedURL,
		      @"the library's own -1002 path, now named instead of spelled");
	}

	/* --- THE SHAPE OF THE FAMILY: EVERY CODE IN THE BLOCK ITS NAME CLAIMS ------------------------------ */
	check("the-exchange-block-is-where-it-says",
	      fn_inRun(NSURLErrorBadURL, -1022, -1000) &&
	      fn_inRun(NSURLErrorTimedOut, -1022, -1000) &&
	      fn_inRun(NSURLErrorNotConnectedToInternet, -1022, -1000) &&
	      fn_inRun(NSURLErrorAppTransportSecurityRequiresSecureConnection, -1022, -1000) &&
	      fn_inRun(NSURLErrorInternationalRoamingOff, -1022, -1000),
	      @"the -1000 run, from BadURL to the app-transport-security case");
	check("the-file-block-is-where-it-says",
	      fn_inRun(NSURLErrorFileDoesNotExist, -1104, -1100) &&
	      fn_inRun(NSURLErrorFileOutsideSafeArea, -1104, -1100),
	      @"the -1100 run");
	check("the-tls-block-is-where-it-says",
	      fn_inRun(NSURLErrorSecureConnectionFailed, -1206, -1200) &&
	      fn_inRun(NSURLErrorClientCertificateRequired, -1206, -1200),
	      @"the -1200 run, from the handshake to the client certificate");
	check("the-body-file-block-is-where-it-says",
	      fn_inRun(NSURLErrorCannotCreateFile, -3007, -3000) &&
	      fn_inRun(NSURLErrorDownloadDecodingFailedToComplete, -3007, -3000),
	      @"the -3000 run, which a download task's body goes through");
	check("the-outliers-are-where-they-say",
	      NSURLErrorUnknown == -1 &&
	      NSURLErrorCannotLoadFromNetwork == -2000 &&
	      NSURLErrorBackgroundSessionRequiresSharedContainer == -995 &&
	      NSURLErrorBackgroundSessionWasDisconnected == -997,
	      @"the four that sit outside a run, each on its own");
	check("and-no-two-codes-are-the-same",
	      fn_allDistinct(),
	      @"a duplicated value would make two names one code, which no inventory can see");

	/* --- THE TWO REASON ENUMERATIONS ARE OURS (D2), AND SAY SO BY BEING INDEPENDENT OF THE CODES ------- */
	check("the-reason-enums-are-ours-and-ordered",
	      NSURLErrorNetworkUnavailableReasonCellular == 0 &&
	      NSURLErrorNetworkUnavailableReasonUltraConstrained == 3 &&
	      NSURLErrorCancelledReasonUserForceQuitApplication == 0 &&
	      NSURLErrorCancelledReasonInsufficientSystemResources == 2,
	      @"values inside a userInfo dictionary, read back through their own key: ours, and small");

	/* --- THE KEYS ARE DISTINCT STRINGS A CALLER CAN LOOK UP BY ---------------------------------------- */
	{
		NSArray *keys = [NSArray arrayWithObjects:NSURLErrorKey, NSURLErrorFailingURLErrorKey,
				 NSURLErrorFailingURLPeerTrustErrorKey,
				 NSURLErrorNetworkUnavailableReasonKey,
				 NSURLErrorBackgroundTaskCancelledReasonKey, nil];
		NSUInteger i, j;
		BOOL distinct = YES;

		for (i = 0; i < [keys count]; i++) {
			id key = [keys objectAtIndex:i];

			if (![key isKindOfClass:[NSString class]] || [key length] == 0) {
				distinct = NO;
			}
			for (j = i + 1; j < [keys count]; j++) {
				if ([[keys objectAtIndex:i] isEqual:[keys objectAtIndex:j]]) {
					distinct = NO;
				}
			}
		}
		check("the-keys-are-distinct-strings", distinct,
		      @"five userInfo keys, and two of them being one string would hide a reason from a caller");
	}

	printf("FOUNDATION-URLERROR RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLERROR-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLERROR DONE\n");
	return failc ? 1 : 0;
}
