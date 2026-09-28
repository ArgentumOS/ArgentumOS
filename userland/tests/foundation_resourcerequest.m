/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_resourcerequest.m — THE PROBE FOR §62.88: `NSBundleResourceRequest`, its priority constant and its
 * low-disk-space notice.
 *
 * WHAT THIS PROBE CAN HONESTLY ASSERT IS WHAT THE SYSTEM IS: there are no on-demand resources here, so the
 * conditional door answers YES, the begin door answers nil, and the progress object is COMPLETE. Those are facts
 * about a system whose bundles hold everything they have — and the probe asserts them as facts, TOGETHER WITH the
 * one case where the class must not be cheerful: an EMPTY tag set is invalid and is reported through the error
 * path rather than passing as a successful load of nothing.
 *
 * THE NOTICE IS DRIVEN THROUGH THE SEAM (`FNBundleResourceRequest.h`): the system posts a low-disk-space notice
 * and this system has no such system, so the seam posts it and the probe OBSERVES the registration an app would
 * make (Apple's own example registers with `object:nil`).
 *
 * NOTHING IS ASYNCHRONOUS, AND THAT IS ITSELF ASSERTED: Apple's doors take completion handlers, and in a system
 * with nothing to download the handler runs BEFORE THE DOOR RETURNS. A probe that merely waited for the handler
 * would prove less than that.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNBundleResourceRequest.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-RESOURCEREQUEST %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-RESOURCEREQUEST %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static NSString *fn_raised(void (^block)(void))
{
	@try {
		block();
	} @catch (NSException *e) {
		return [e name];
	}
	return nil;
}

@interface FNLowDiskObserver : NSObject
{
@public
	int count;
	NSString *lastName;
	id lastObject;
}
@end

@implementation FNLowDiskObserver

- (void)spaceIsLow:(NSNotification *)notification
{
	count++;
	lastName = [notification name];
	lastObject = [notification object];
}

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE SURFACE ------------------------------------------------------------------------------ */
	{
		Class cls = objc_getClass("NSBundleResourceRequest");

		check("the-class-and-its-two-names-are-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] &&
		      [NSBundleResourceRequestLowDiskSpaceNotification
			isEqualToString:@"NSBundleResourceRequestLowDiskSpaceNotification"] &&
		      NSBundleResourceRequestLoadingPriorityUrgent == 1.0,
		      @"the class exists, the notice is exported under its name, and the urgent priority is 1.0 "
		      @"(Apple publishes the name; the value is ours — §11.6.1 D2)");
	}

	/* --- 2. WHAT A REQUEST HOLDS --------------------------------------------------------------------- */
	{
		NSMutableSet *tags = [NSMutableSet setWithObject:@"chapter-1"];
		NSBundle *bundle = [NSBundle mainBundle];
		NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc] initWithTags:tags bundle:bundle];

		check("a-request-keeps-the-tags-and-the-bundle-it-was-given",
		      [[request tags] isEqualToSet:tags] && [request bundle] == bundle,
		      @"read-only tags and bundle, as Apple declares them");
		[tags addObject:@"chapter-2"];
		check("the-tags-are-a-copy-not-a-view",
		      [[request tags] count] == 1,
		      @"the request holds the set it was handed, so a caller's later mutation is not the request's "
		      @"business");
		check("the-convenience-initialiser-loads-into-the-main-bundle",
		      [[[NSBundleResourceRequest alloc] initWithTags:[NSSet setWithObject:@"x"]] bundle] ==
			[NSBundle mainBundle],
		      @"-initWithTags: is Apple's main-bundle form of the designated initialiser");

		check("a-nil-tag-set-or-bundle-is-a-programming-error",
		      [fn_raised(^{ (void)[[NSBundleResourceRequest alloc] initWithTags:(NSSet *)nil]; })
			isEqualToString:NSInvalidArgumentException] &&
		      [fn_raised(^{ (void)[[NSBundleResourceRequest alloc] initWithTags:[NSSet set]
									 bundle:(NSBundle *)nil]; })
			isEqualToString:NSInvalidArgumentException],
		      @"there is nothing to manage without tags, and nowhere to load without a bundle");
	}

	/* --- 3. THE TWO ACCESS DOORS, AND THE FACT THAT THEY ARE IMMEDIATE -------------------------------- */
	{
		NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc]
							initWithTags:[NSSet setWithObject:@"chapter-1"]];
		__block BOOL conditionalCalled = NO, available = NO;
		__block BOOL beginCalled = NO;
		__block NSError *beginError = nil;

		[request conditionallyBeginAccessingResourcesWithCompletionHandler:^(BOOL a) {
			conditionalCalled = YES;
			available = a;
		}];
		check("the-conditional-door-answers-yes-before-it-returns",
		      conditionalCalled && available,
		      @"the resources a bundle holds are on the device — and with nothing to download the handler "
		      @"runs IN THE SAME CALL, which is asserted rather than assumed");

		[request beginAccessingResourcesWithCompletionHandler:^(NSError *e) {
			beginCalled = YES;
			beginError = e;
		}];
		check("the-begin-door-grants-access-before-it-returns",
		      beginCalled && beginError == nil,
		      @"access is granted with no error: this system has nothing to fetch");

		check("the-access-doors-tolerate-a-missing-handler",
		      ([request beginAccessingResourcesWithCompletionHandler:(void (^)(NSError *))nil],
		       [request conditionallyBeginAccessingResourcesWithCompletionHandler:(void (^)(BOOL))nil],
		       YES),
		      @"a door with nothing to answer into does nothing rather than raising in the caller's frame");

		[request endAccessingResources];
		check("ending-access-and-then-asking-again-is-safe",
		      ([request conditionallyBeginAccessingResourcesWithCompletionHandler:^(BOOL a) {
			(void)a;
		       }], YES),
		      @"-endAccessingResources is bookkeeping here (there is nothing to purge), and re-acquiring "
		      @"access afterwards is not an error");
	}

	/* --- 4. THE ONE CASE WHERE THE CLASS MUST NOT BE CHEERFUL ---------------------------------------- */
	{
		NSBundleResourceRequest *empty = [[NSBundleResourceRequest alloc] initWithTags:[NSSet set]];
		__block BOOL available = YES;
		__block NSError *error = nil;
		__block BOOL called = NO;

		[empty conditionallyBeginAccessingResourcesWithCompletionHandler:^(BOOL a) {
			called = YES;
			available = a;
		}];
		check("an-empty-tag-set-is-not-available-conditionally",
		      called && available == NO,
		      @"the conditional door has no error channel, so an invalid request answers the one thing it "
		      @"can: the resources are NOT available — which sends the caller where the error is reported");
		[empty beginAccessingResourcesWithCompletionHandler:^(NSError *e) {
			called = YES;
			error = e;
		}];
		check("an-empty-tag-set-is-reported-through-the-documented-error",
		      error != nil && [[error domain] isEqualToString:NSCocoaErrorDomain] &&
		      [error code] == NSBundleOnDemandResourceInvalidTagError,
		      @"NSBundleOnDemandResourceInvalidTagError — the error code this library already declares for "
		      @"exactly this");
	}

	/* --- 5. PROGRESS AND PRIORITY --------------------------------------------------------------------- */
	{
		NSBundleResourceRequest *request = [[NSBundleResourceRequest alloc]
							initWithTags:[NSSet setWithObject:@"chapter-1"]];

		check("the-progress-is-complete-and-the-same-object-each-time",
		      [request progress] != nil && [request progress] == [request progress] &&
		      [[request progress] isFinished] && [[request progress] fractionCompleted] == 1.0,
		      @"a download that does not exist cannot be in progress: a caller observing fractionCompleted "
		      @"gets the truth instead of a spinner that can never move");
		check("the-priority-defaults-to-the-middle-of-apples-range-and-is-settable",
		      [request loadingPriority] == 0.5,
		      @"the default and the constant's value are OURS (§11.6.1 D2) — 0.5 is the middle of the "
		      @"documented 0.0-1.0 range");
		[request setLoadingPriority:NSBundleResourceRequestLoadingPriorityUrgent];
		check("the-priority-is-a-hint-and-is-stored-as-given",
		      [request loadingPriority] == NSBundleResourceRequestLoadingPriorityUrgent &&
		      ([request setLoadingPriority:2.0], [request loadingPriority] == 2.0),
		      @"nothing here acts on the hint (there is nothing to prioritize) and it is neither clamped nor "
		      @"refused — a hint that raised would be a different contract than Apple's");
	}

	/* --- 6. THE NOTICE, THROUGH THE SEAM -------------------------------------------------------------- */
	{
		FNLowDiskObserver *observer = [[FNLowDiskObserver alloc] init];

		/* REGISTERED THE WAY APPLE'S OWN EXAMPLE DOES: by name, with `object:nil`. */
		[[NSNotificationCenter defaultCenter] addObserver:observer
							 selector:@selector(spaceIsLow:)
							     name:NSBundleResourceRequestLowDiskSpaceNotification
							   object:nil];
		FNBundleResourceRequestDeliverLowDiskSpace();
		check("the-low-disk-space-notice-is-delivered-and-nameable",
		      observer->count == 1 &&
		      [observer->lastName isEqualToString:NSBundleResourceRequestLowDiskSpaceNotification] &&
		      observer->lastObject == nil,
		      @"the system posts it with no object; with no system, the seam does — so an app's registration "
		      @"and recovery path is exercisable rather than a name nobody can fire");
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	printf("FOUNDATION-RESOURCEREQUEST RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-RESOURCEREQUEST-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-RESOURCEREQUEST DONE\n");
	return failc ? 1 : 0;
}
