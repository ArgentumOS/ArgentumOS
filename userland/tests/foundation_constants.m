/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_constants.m — THE PROBE FOR §62.103: the long tail of small constants, plus the two thread
 * notifications that have real producers.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * EVERY VALUE THAT APPLE PUBLISHES IS COMPARED WITH IT, and every value that is THIS LIBRARY'S is
 *     PINNED here so it cannot drift silently — the two sentinel calendar units, the OpenStep reserved base,
 *     the string ceiling, the bookmark option (ours at 1<<5, Apple's at 256: the header says which is which),
 *     the XML entity kind's number in this tree's own enum, and the undo run-loop ordering's 350000.
 *   * THE TWO THREAD NOTIFICATIONS ARE CHECKED BY MAKING THEM HAPPEN: an observer is registered, a thread is
 *     started and finishes, and BOTH the will-become-multithreaded notice and the thread-will-exit notice must
 *     arrive — the first is the only way this probe can tell a posted name from a declared one.
 *   * AND THE NAMES WHOSE VALUE IS THEIR NAME (the archive root key, the progress kind, the file-protection
 *     class, the stream service type, the discardable key, the two failing-URL keys and the cookie notice) are
 *     compared against their own names, because that string is what a plist or a log spells.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sched.h>	/* sched_yield, for the bounded wait on another thread */

static int okc = 0, failc = 0;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CONSTANTS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CONSTANTS %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* THE TWO COUNTERS THE THREAD CHECKS READ. Counted rather than ordered, because the posts happen on another
 * thread and only their NUMBER is the probe's business. */
static int fn_became_multi = 0;
static int fn_thread_exited = 0;
static id fn_exited_object = nil;

@interface FnQuietThread : NSObject
- (void)run;
@end
@implementation FnQuietThread
- (void)run { }
@end

int main(void)
{
	/* 1. THE CALENDAR SENTINELS. */
	check("calendar-unit-sentinels",
	      NSCalendarUnitIsLeapMonth == (1UL << 30) && NSCalendarUnitIsRepeatedDay == (1UL << 31) &&
	      NSCalendarUnitIsLeapMonth != NSCalendarUnitIsRepeatedDay &&
	      NSCalendarUnitIsLeapMonth != NSCalendarUnitDay &&
	      NSCalendarUnitIsRepeatedDay != NSCalendarUnitYear,
	      @"Apple's top two bits, and no collision with the component units");

	/* 2. THE OPENSTEP RESERVED BASE, THE STRING CEILING, THE BOOKMARK OPTION AND ITS TYPE. */
	{
		NSURLBookmarkFileCreationOptions asType = NSURLBookmarkCreationMinimalBookmark;

		check("openstep-reserved-base-and-string-ceiling",
		      NSOpenStepUnicodeReservedBase == 0xF400 && NSMaximumStringLength == (INT_MAX - 1),
		      @"0xF400 for the reserved range's floor, INT_MAX - 1 for the legacy ceiling");
		check("bookmark-option-and-its-type",
		      NSURLBookmarkCreationPreferFileIDResolution == (1 << 5) &&
		      NSURLBookmarkCreationPreferFileIDResolution != NSURLBookmarkCreationMinimalBookmark &&
		      NSURLBookmarkCreationPreferFileIDResolution != NSURLBookmarkCreationSecurityScopeAllowOnlyReadAccess &&
		      asType != 0,
		      @"this enum's own scheme, and a value a caller can hold in the legacy option type");
	}

	/* 3. THE THREE URL RESOURCE KEYS. */
	check("url-resource-keys-name-themselves",
	      [NSURLTypeIdentifierKey isEqualToString:@"NSURLTypeIdentifierKey"] &&
	      [NSURLFileSecurityKey isEqualToString:@"NSURLFileSecurityKey"] &&
	      [NSThumbnail1024x1024SizeKey isEqualToString:@"NSThumbnail1024x1024SizeKey"],
	      @"the type identifier, the file security and the 1024x1024 thumbnail");

	/* 4. THE THREAD NOTIFICATIONS, BY MAKING THEM HAPPEN. */
	{
		FnQuietThread *target = [[FnQuietThread alloc] init];
		id observerBecame = [[NSNotificationCenter defaultCenter]
			addObserverForName:NSWillBecomeMultiThreadedNotification
				    object:nil queue:nil usingBlock:^(NSNotification *note) {
					    (void)note;
					    fn_became_multi++;
				    }];
		id observerExit = [[NSNotificationCenter defaultCenter]
			addObserverForName:NSThreadWillExitNotification
				    object:nil queue:nil usingBlock:^(NSNotification *note) {
					    fn_thread_exited++;
					    fn_exited_object = [note object];
				    }];
		NSThread *thread = [[NSThread alloc] initWithTarget:target selector:@selector(run) object:nil];
		long spins = 0;

		[thread start];
		/* A BOUNDED SPIN, NOT A SLEEP LOOP, AND THE REASON IS MEASURED: this system's `usleep` is UNRELIABLE
		 * (a known fact about FNX — a sleep can return immediately), so a wait built on it gives up before the
		 * other thread has posted. The flags arrive when that thread posts, and a yield-bounded spin gives it a
		 * large but finite number of iterations to do so. THE GUEST RUN IS WHAT FOUND THIS: on the host the
		 * sleep loop passed, and in the guest the exit notice was simply never waited for. */
		while (spins++ < 20000000L && (fn_became_multi == 0 || fn_thread_exited == 0)) {
			sched_yield();
		}
		[[NSNotificationCenter defaultCenter] removeObserver:observerBecame];
		[[NSNotificationCenter defaultCenter] removeObserver:observerExit];

		check("thread-notifications-are-really-posted",
		      fn_became_multi == 1 && fn_thread_exited == 1 && fn_exited_object == thread,
		      [NSString stringWithFormat:@"became=%d exited=%d object=%@ (want the thread)", fn_became_multi,
						  fn_thread_exited, fn_exited_object]);
	}

	/* 5. AND THE NAMES A CALLER COULD POST OR OBSERVE ITSELF. */
	{
		__block int cookie = 0;
		id observer = [[NSNotificationCenter defaultCenter]
			addObserverForName:NSHTTPCookieManagerAcceptPolicyChangedNotification
				    object:nil queue:nil usingBlock:^(NSNotification *note) {
					    (void)note;
					    cookie++;
				    }];

		[[NSNotificationCenter defaultCenter]
			postNotificationName:NSHTTPCookieManagerAcceptPolicyChangedNotification object:nil];
		[[NSNotificationCenter defaultCenter] removeObserver:observer];

		check("the-cookie-notice-and-the-single-threaded-name",
		      cookie == 1 &&
		      [NSHTTPCookieManagerAcceptPolicyChangedNotification
			 isEqualToString:@"NSHTTPCookieManagerAcceptPolicyChangedNotification"] &&
		      [NSDidBecomeSingleThreadedNotification isEqualToString:@"NSDidBecomeSingleThreadedNotification"],
		      @"the policy notice is a usable name, and the single-threaded one is declared (Apple posts it never)");
	}

	/* 6. THE FILE HANDLE'S MONITOR MODES. */
	check("file-handle-monitor-modes",
	      NSFileHandleNotificationMonitorModes != nil &&
	      [NSFileHandleNotificationMonitorModes count] > 0 &&
	      [NSFileHandleNotificationMonitorModes containsObject:NSDefaultRunLoopMode],
	      @"the one array the background monitor asks the run loop in");

	/* 7. THE NAMES WHOSE VALUE IS THEIR NAME, AND THE TWO NUMBERS LEFT. */
	check("keys-and-kinds-value-their-names",
	      [NSKeyedArchiveRootObjectKey isEqualToString:@"NSKeyedArchiveRootObjectKey"] &&
	      [NSFileProtectionCompleteWhenUserInactive isEqualToString:@"NSFileProtectionCompleteWhenUserInactive"] &&
	      [NSStreamNetworkServiceTypeVoIP isEqualToString:@"NSStreamNetworkServiceTypeVoIP"] &&
	      [NSUndoManagerGroupIsDiscardableKey isEqualToString:@"NSUndoManagerGroupIsDiscardableKey"] &&
	      [NSURLErrorFailingURLStringErrorKey isEqualToString:@"NSURLErrorFailingURLStringErrorKey"] &&
	      [NSErrorFailingURLStringKey isEqualToString:@"NSErrorFailingURLStringKey"],
	      @"every one of these is compared and stored as its own name");

	check("undo-run-loop-ordering-and-the-xml-entity-kind",
	      NSUndoCloseGroupingRunLoopOrdering == 350000 &&
	      NSXMLEntityPredefined == 119 && NSXMLEntityPredefined > NSXMLEntityUnparsedKind &&
	      NSOperationQueueDefaultMaxConcurrentOperationCount == -1,
	      @"350000 for the grouping priority, this enum's next value for the predefined entity, -1 for 'the queue decides'");

	/* 8. THE userInfo KEY TYPE. */
	{
		NSErrorUserInfoKey key = @"FNProbeKey";
		NSError *error = [NSError errorWithDomain:@"FnProbe" code:1 userInfo:@{ key : @"value" }];

		check("error-user-info-key-type",
		      error != nil && [[[error userInfo] objectForKey:key] isEqualToString:@"value"],
		      @"an NSErrorUserInfoKey is spellable and usable as a dictionary key");
	}

	/* ⚠⚠ §62.104'S LAST TWO ENUMS WERE THE COMPARISON PREDICATE'S OPTION TYPE AND THE SORT OPTIONS,
	 * AND ONLY THE SECOND SURVIVES (§63.161): the first is declared in NSPredicate.h, which left with the
	 * predicate family, so its check went with it. The sort options are NSComparator's and NSDictionary's,
	 * and they stay. */

	/* AND THE SORT OPTIONS, MEASURED RATHER THAN DECLARED: `NSSortStable` is a PROMISE, so the check is that
	 * elements comparing EQUAL keep the order they arrived in — through BOTH doors. */
	{
		NSArray *items = @[ @"bb", @"a", @"cc", @"d", @"ee" ];	/* lengths 2,1,2,1,2 */
		NSArray *sorted = [items sortedArrayWithOptions:NSSortStable
					      usingComparator:^NSComparisonResult(id a, id b) {
			NSUInteger la = [a length];
			NSUInteger lb = [b length];

			return la < lb ? NSOrderedAscending : (la > lb ? NSOrderedDescending
								      : NSOrderedSame);
		}];
		NSMutableArray *mutable = [items mutableCopy];

		[mutable sortWithOptions:NSSortStable usingComparator:^NSComparisonResult(id a, id b) {
			NSUInteger la = [a length];
			NSUInteger lb = [b length];

			return la < lb ? NSOrderedAscending : (la > lb ? NSOrderedDescending
								      : NSOrderedSame);
		}];

		check("sort-options-and-the-stability-promise",
		      NSSortConcurrent == (1UL << 0) && NSSortStable == (1UL << 4) &&
		      [sorted count] == 5 && [[sorted objectAtIndex:0] isEqualToString:@"a"] &&
		      [[sorted objectAtIndex:1] isEqualToString:@"d"] &&
		      [[sorted objectAtIndex:2] isEqualToString:@"bb"] &&
		      [[sorted objectAtIndex:3] isEqualToString:@"cc"] &&
		      [[sorted objectAtIndex:4] isEqualToString:@"ee"] &&
		      [mutable isEqualToArray:sorted],
		      [NSString stringWithFormat:@"stable order must be a, d, bb, cc, ee and both doors must agree: %@ / %@",
						  sorted, mutable]);
	}


	/* 11. THE KVC EXCEPTION NAME (§62.106), WHICH IS VOCABULARY AND NOT A RAISE SITE IN THIS LIBRARY: Apple's
	 * contract is that a KVC *implementor* raises it to refuse a manipulation on purpose, so the probe asserts the
	 * property that makes it usable — it names a real exception, with the reason it was raised for. */
	{
		int raised = 0;

		@try {
			[NSException raise:NSOperationNotSupportedForKeyException format:@"read-only key %@", @"title"];
		} @catch (NSException *e) {
			raised = [[e name] isEqualToString:NSOperationNotSupportedForKeyException] &&
			         [[e reason] isEqualToString:@"read-only key title"];
		}

		check("kvc-exception-name-is-what-implementors-raise",
		      [NSOperationNotSupportedForKeyException isEqualToString:@"NSOperationNotSupportedForKeyException"] &&
		      raised,
		      @"the name is its own string, and raising it yields an exception whose name and reason are the ones asked for");
	}

	printf("FOUNDATION-CONSTANTS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CONSTANTS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CONSTANTS DONE\n");
	return failc ? 1 : 0;
}
