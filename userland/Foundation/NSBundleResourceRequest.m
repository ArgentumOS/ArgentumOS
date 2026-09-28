/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundleResourceRequest.m — the on-demand-resources request in a system without on-demand resources (§62.88).
 * MANUAL OWNERSHIP.
 *
 * THE TWO DOORS ANSWER IMMEDIATELY AND IN THE SAME CALL (see the header for why that is the truth here), so
 * there is no asynchrony to arrange and no run loop to wait on. The ONE branch that is not a straight answer is
 * an EMPTY tag set: it is invalid, and the doors report `NSBundleOnDemandResourceInvalidTagError` instead of
 * reporting success for a request that manages nothing.
 */

#import <Foundation/NSBundleResourceRequest.h>
#import <Foundation/FNBundleResourceRequest.h>
#import <Foundation/NSBundle.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSProgress.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>

const double NSBundleResourceRequestLoadingPriorityUrgent = 1.0;
NSString * const NSBundleResourceRequestLowDiskSpaceNotification =
	@"NSBundleResourceRequestLowDiskSpaceNotification";

/* The default priority: the middle of Apple's documented 0.0-1.0 range (the value is ours - see the header). */
#define FN_ODR_DEFAULT_PRIORITY 0.5

@implementation NSBundleResourceRequest

- (instancetype)initWithTags:(NSSet *)tags bundle:(NSBundle *)bundle
{
	if (tags == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"a resource request needs a tag set — there is nothing to manage without one"];
	}
	if (bundle == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"a resource request needs a bundle to load its resources into"];
	}
	self = [super init];
	if (self != nil) {
		_tags = [tags copy];
		_bundle = [bundle retain];
		_loadingPriority = FN_ODR_DEFAULT_PRIORITY;
	}
	return self;
}

- (instancetype)initWithTags:(NSSet *)tags
{
	/* APPLE'S CONVENIENCE INITIALISER LOADS INTO THE MAIN BUNDLE. */
	return [self initWithTags:tags bundle:[NSBundle mainBundle]];
}

- (void)dealloc
{
	[(id)_tags release];
	[(id)_bundle release];
	[(id)_progress release];
	[super dealloc];
}

/* THE ONE ERROR THIS CLASS CAN PRODUCE, built from an error code this library already declares. */
- (NSError *)fnInvalidTagError
{
	return [NSError errorWithDomain:NSCocoaErrorDomain
				   code:NSBundleOnDemandResourceInvalidTagError
			       userInfo:[NSDictionary dictionaryWithObject:
					@"a resource request with no tags manages nothing"
								      forKey:NSLocalizedDescriptionKey]];
}

- (void)beginAccessingResourcesWithCompletionHandler:(void (^)(NSError *_Nullable error))completionHandler
{
	if (completionHandler == nil) {
		return;		/* a door with nothing to answer INTO does nothing, rather than raising in a caller's
				 * frame — the completion handler is the only channel this API has */
	}
	if ([(NSSet *)_tags count] == 0) {
		completionHandler([self fnInvalidTagError]);
		return;
	}
	/* NOTHING TO DOWNLOAD, SO ACCESS IS GRANTED: the resources a bundle holds are on the device, which is the
	 * only thing "already available" can mean here. */
	completionHandler(nil);
}

- (void)conditionallyBeginAccessingResourcesWithCompletionHandler:
	(void (^)(BOOL resourcesAvailable))completionHandler
{
	if (completionHandler == nil) {
		return;
	}
	if ([(NSSet *)_tags count] == 0) {
		/* THE CONDITIONAL DOOR HAS NO ERROR CHANNEL (Apple's handler takes only a BOOL), so an invalid request
		 * answers the one thing it can: the resources are NOT available, which sends a caller down the path
		 * where the error is reported. Answering YES here would be the lie this branch exists to avoid. */
		completionHandler(NO);
		return;
	}
	completionHandler(YES);
}

- (void)endAccessingResources
{
	/* BOOKKEEPING ONLY (see the header): there is nothing here to purge, and the access doors re-acquire. */
}

- (NSSet *)tags { return _tags; }
- (NSBundle *)bundle { return _bundle; }

- (NSProgress *)progress
{
	if (_progress == nil) {
		_progress = [[NSProgress progressWithTotalUnitCount:1] retain];
		/* COMPLETE, and completed HERE rather than left at zero: a caller that observes fractionCompleted or
		 * -isFinished gets the truth — there is no download to wait for — instead of a spinner that can never
		 * move. */
		[(NSProgress *)_progress setCompletedUnitCount:1];
	}
	return _progress;
}

- (double)loadingPriority { return _loadingPriority; }
- (void)setLoadingPriority:(double)priority { _loadingPriority = priority; }

@end

void FNBundleResourceRequestDeliverLowDiskSpace(void)
{
	[[NSNotificationCenter defaultCenter] postNotificationName:NSBundleResourceRequestLowDiskSpaceNotification
							    object:nil
							  userInfo:nil];
}
