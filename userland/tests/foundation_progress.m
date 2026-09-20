/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_progress, unit of 1 — F13.20's acceptance for NSProgress.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>.
 *
 * THE CHECK THAT EARNS ITS PLACE IS THE TREE'S ARITHMETIC: a parent with a total of 100 is told that a
 * child stands for 50 of its units; the child is 2 of its own 4 done; and the PARENT must therefore
 * report 35 — its own 10 plus HALF of the 50. No constant in this file can produce that number; it is
 * the scaling, which is the whole reason a progress tree exists rather than a progress number.
 *
 * AND THE SECOND IS THE CEILING: a child that over-reports contributes its SHARE and no more, so a
 * parent cannot be pushed past what it was told to expect.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

/*
 * NSProgressReporting'S OWN FIXTURE: a class that ADOPTS the protocol, because its contract is
 * one property and the honest test is a conforming class rather than the protocol's text.
 */
@interface FnReporter : NSObject <NSProgressReporting>
{
	NSProgress *_progress;
}
- (NSProgress *)progress;
@end

@implementation FnReporter
- (NSProgress *)progress
{
	return _progress;
}
@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-PROGRESS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PROGRESS %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:100];

		[progress setCompletedUnitCount:25];
		check("progress-fraction",
		      progress != nil && [progress totalUnitCount] == 100 &&
		      [progress completedUnitCount] == 25 &&
		      [progress fractionCompleted] == 0.25 && ![progress isFinished],
		      [NSString stringWithFormat:@"%lld/%lld fraction=%.4f finished=%d",
			(long long)[progress completedUnitCount],
			(long long)[progress totalUnitCount], [progress fractionCompleted],
			(int)[progress isFinished]]);
		[progress setCompletedUnitCount:100];
		check("progress-finished",
		      [progress isFinished] && [progress fractionCompleted] == 1.0,
		      [NSString stringWithFormat:@"fraction=%.4f finished=%d",
			[progress fractionCompleted], (int)[progress isFinished]]);
	}

	{
		/* THE SCALING: 2 of the child's 4 units is HALF of the 50 units the parent was told the
		 * child stands for, so the parent's own 10 becomes 35. */
		NSProgress *parent = [NSProgress discreteProgressWithTotalUnitCount:100];
		NSProgress *child;

		[parent setCompletedUnitCount:10];
		[parent becomeCurrentWithPendingUnitCount:50];
		child = [NSProgress progressWithTotalUnitCount:4];
		[parent resignCurrent];
		check("progress-child-attaches-to-current",
		      child != nil && [NSProgress currentProgress] == nil,
		      [NSString stringWithFormat:@"child=%@ current=%@", child,
			[NSProgress currentProgress]]);
		[child setCompletedUnitCount:2];
		check("progress-child-scales-into-parent",
		      [parent completedUnitCount] == 35 && [parent fractionCompleted] == 0.35,
		      [NSString stringWithFormat:@"parent=%lld fraction=%.4f want 35/0.35",
			(long long)[parent completedUnitCount], [parent fractionCompleted]]);
		[child setCompletedUnitCount:4];
		check("progress-child-completed-share",
		      [parent completedUnitCount] == 60,
		      [NSString stringWithFormat:@"parent=%lld want 60",
			(long long)[parent completedUnitCount]]);
		[child setCompletedUnitCount:40];
		check("progress-share-is-the-ceiling",
		      [parent completedUnitCount] == 60,
		      [NSString stringWithFormat:@"parent=%lld want 60 (the share, not 500)",
			(long long)[parent completedUnitCount]]);
	}

	{
		NSProgress *parent = [NSProgress discreteProgressWithTotalUnitCount:10];
		NSProgress *child = [NSProgress progressWithTotalUnitCount:10
								    parent:parent
							  pendingUnitCount:5];

		[parent setCancellable:YES];
		[parent cancel];
		check("progress-cancel-propagates",
		      [parent isCancelled] && [parent isCancellable] && child != nil &&
		      [child isCancelled],
		      [NSString stringWithFormat:@"parent=%d child=%d", (int)[parent isCancelled],
			(int)[child isCancelled]]);
	}

	{
		NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:4];

		[progress setPausable:YES];
		[progress pause];
		{
			BOOL paused = [progress isPaused];

			[progress resume];
			check("progress-pause-resume",
			      paused && ![progress isPaused] && [progress isPausable],
			      [NSString stringWithFormat:@"paused=%d now=%d", (int)paused,
				(int)[progress isPaused]]);
		}
	}

	{
		NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:1];

		[progress setKind:@"ProbeKind"];
		[progress setLocalizedDescription:@"doing the probe"];
		[progress setUserInfoObject:@"carried" forKey:@"note"];
		[progress setUserInfoObject:nil forKey:@"note"];
		[progress setUserInfoObject:@"kept" forKey:@"other"];
		check("progress-kind-and-userinfo",
		      [[progress kind] isEqualToString:@"ProbeKind"] &&
		      [[progress localizedDescription] isEqualToString:@"doing the probe"] &&
		      [[progress userInfo] objectForKey:@"note"] == nil &&
		      [[[progress userInfo] objectForKey:@"other"] isEqualToString:@"kept"],
		      [NSString stringWithFormat:@"kind=%@ userInfo=%@", [progress kind],
			[progress userInfo]]);
	}

	{
		/* A TOTAL OF ZERO IS NEVER FINISHED, and its fraction is zero rather than a division. */
		NSProgress *empty = [NSProgress discreteProgressWithTotalUnitCount:0];

		check("progress-zero-total",
		      empty != nil && ![empty isFinished] && [empty fractionCompleted] == 0.0,
		      [NSString stringWithFormat:@"finished=%d fraction=%.4f", (int)[empty isFinished],
			[empty fractionCompleted]]);
	}

	{
		/* NSProgressReporting (W2h): ONE REQUIRED PROPERTY, and a class that adopts it conforms -
		 * measured with a fixture rather than asserted about the protocol's text. It INHERITS the
		 * NSObject protocol, which is why that one had to be written first, and this check proves
		 * the inheritance works as well as the member. */
		FnReporter *reporter = [[FnReporter alloc] init];

		check("nsprogressreporting",
		      [reporter conformsToProtocol:@protocol(NSProgressReporting)] &&
		      [reporter respondsToSelector:@selector(progress)] &&
		      [reporter conformsToProtocol:@protocol(NSObject)],
		      @"the protocol, its one member, and the NSObject protocol it inherits");
	}

	printf("FOUNDATION-PROGRESS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-PROGRESS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PROGRESS DONE\n");
	return failc ? 1 : 0;
}
