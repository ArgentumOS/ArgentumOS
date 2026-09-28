/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSBundleResourceRequest — Apple's on-demand-resources request (§62.88), with its priority constant and its
 * low-disk-space notice. This closes `App Support / On-Demand Resources`.
 *
 * **THE ANSWER THIS CLASS GIVES IS SHORT BECAUSE THE SYSTEM IT RUNS IN IS SHORT: THERE ARE NO ON-DEMAND
 * RESOURCES HERE.** Apple's request exists to fetch tagged resources from the App Store at the moment an app
 * needs them; this system has no such service and no such store, so a bundle's resources are simply PRESENT OR
 * ABSENT, and there is nothing to download. That makes the two doors' answers FACTS rather than stubs: the
 * conditional door answers **YES** ("the resources are already on the device" — they are, as far as anything here
 * can be), the begin door answers **nil** error ("access was granted"), and `-progress` is **COMPLETE**, because a
 * download that does not exist cannot be in progress. A caller that follows Apple's flow — ask conditionally,
 * then fall back to the downloading door — works unchanged and never waits.
 *
 * WHAT IS *NOT* CLAIMED, WHICH MATTERS MORE THAN WHAT IS: the class does not pretend to know which resources a
 * tag names. There is no tag manifest in this system's bundles, so a request with tags that name nothing is
 * indistinguishable from one whose resources are all present — and the header says so rather than inventing a
 * lookup that would answer a question nobody can answer. The ONE tag rule that IS enforced is the documented
 * one: an EMPTY tag set is invalid, and the doors report `NSBundleOnDemandResourceInvalidTagError` (an error
 * code this library already declares in NSError.h) instead of pretending to have loaded nothing successfully.
 *
 * THE LOW-DISK-SPACE NOTICE IS THE SYSTEM'S TO POST, and this system has no such system: the internal seam
 * `FNBundleResourceRequest.h` posts it, so an app's registration and recovery path is exercisable rather than
 * being a notification name nobody can fire. (That is the same device §62.85 and §62.87 used.)
 *
 * APPLE PUBLISHES THE PRIORITY CONSTANT'S NAME; ITS VALUE AND THE DEFAULT PRIORITY ARE OURS (§11.6.1 D2): the
 * urgent constant is 1.0, the top of the documented 0.0–1.0 range and the value that means "as fast as
 * possible", and a fresh request starts at 0.5, the middle of that range.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSError;
@class NSBundle;
@class NSProgress;
@class NSSet;
@class NSString;

@interface NSBundleResourceRequest : NSObject
{
@private
	id _tags;		/* retained */
	id _bundle;		/* retained */
	id _progress;		/* created on first use, so a request that is never asked costs nothing */
	double _loadingPriority;
}

/* MANAGING RESOURCES BY TAG — a request names the tags it needs and which bundle to load them into. A nil tag
 * set or bundle is a PROGRAMMING ERROR and raises (there is nothing to manage, and nowhere to load), which is
 * why those two parameters are annotated NULLABLE: the header says what the implementation ACCEPTS, and what it
 * accepts here is nil, refused loudly. An EMPTY tag set is a different case — invalid rather than a programming
 * error — and is reported through the doors' error path instead of raised. */
- (instancetype)initWithTags:(nullable NSSet *)tags;
- (instancetype)initWithTags:(nullable NSSet *)tags bundle:(nullable NSBundle *)bundle;

/* THE TWO ACCESS DOORS, in Apple's order: ask whether the resources are already here, and fall back to the
 * downloading door when they are not. See the file's note for what each answers in a system with no on-demand
 * resources — and note that the completion handler is called BEFORE THIS METHOD RETURNS, because there is
 * nothing to wait for. A caller that assumes asynchrony is not broken by that; a caller that requires it should
 * not assume it. The handler parameters are NULLABLE because a caller may pass none, and the doors then do
 * nothing rather than raising inside the caller's frame. */
- (void)beginAccessingResourcesWithCompletionHandler:
	(nullable void (^)(NSError *_Nullable error))completionHandler;
- (void)conditionallyBeginAccessingResourcesWithCompletionHandler:
	(nullable void (^)(BOOL resourcesAvailable))completionHandler;

/* DONE WITH THE TAGS: Apple's system makes the resources purgeable now. Here it is bookkeeping — there is
 * nothing to purge — and calling an access door afterwards simply re-acquires access, so the pair is safe to
 * use repeatedly. */
- (void)endAccessingResources;

/* The tags and the bundle this request manages, as given. */
- (NSSet *)tags;
- (NSBundle *)bundle;

/* The download's progress. **IT IS COMPLETE HERE**, which is what a caller observing `fractionCompleted` or
 * `-isFinished` needs to know rather than watching a spinner that can never move. The object is stable across
 * calls (the same NSProgress), created on first use. */
- (NSProgress *)progress;

/* A HINT, not a limit: the documented range is 0.0–1.0 and Apple's store compares requests by it. Nothing here
 * acts on it (there is nothing to prioritize), and it is neither clamped nor refused — a hint that raised would
 * be a different contract than the one Apple documents. */
- (double)loadingPriority;
- (void)setLoadingPriority:(double)priority;

@end

/* THE URGENT PRIORITY, for a request whose resources the user is waiting on. Apple publishes the NAME; the value
 * is ours (§11.6.1 D2) — see the file's note. */
FOUNDATION_EXPORT const double NSBundleResourceRequestLoadingPriorityUrgent;
/* Posted to the default notification centre when the system cannot free enough space for a request. The
 * recovery is the app's: drop the requests it does not need. Registered with `object:nil`, as Apple's own
 * example does. */
FOUNDATION_EXPORT NSString * const NSBundleResourceRequestLowDiskSpaceNotification;

NS_ASSUME_NONNULL_END
