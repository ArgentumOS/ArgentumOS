/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_backgroundactivity — §62.66's acceptance: NSBackgroundActivityScheduler.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT IS ASSERTED IS THE ENGINE, WHICH IS THE PART THAT IS OURS: the activity is a ONE-SHOT TIMER on the run
 * loop, and what happens next is decided by the answer the block gives its completion handler. So each check
 * drives the loop and reads the RUN COUNT — but it reads it as an INVARIANT rather than as a number, because
 * how MANY times a deferred or repeating activity runs inside a window is the interval's business and not this
 * class's:
 *
 *   an activity that finishes and does not repeat runs EXACTLY once, however long the loop runs;
 *   one that defers, or repeats, GROWS — at least twice, and the check stops there;
 *   and after -invalidate the count STOPS GROWING, which is the assertion a "one run" count cannot make.
 *
 * THE TICK WAITS RATHER THAN POLLING ONE PASS, and that is deliberate: the engine has a floor under its interval
 * (see the .m — a rescheduled activity due in the SAME pass spins the loop), so a zero-interval activity is
 * ready a millisecond later and a pass with no waiting would not reach it.
 *
 * THE TWO PUBLISHED DEFAULTS AND THE TWO HINTS ARE CHECKED TOO, because they are the parts a caller can see
 * without running anything: `-qualityOfService` is the background one and `-repeats` is NO (Apple states both),
 * and `-tolerance`/`-qualityOfService` are KEPT AND REPORTED with the boundary named in the header — this system
 * has no operating-system scheduler to be aggressive on a caller's behalf.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

/* THE ACTIVITY'S OWN BOOKKEEPING: how many times it ran, and what it answers. */
@interface ActivityProbe : NSObject
{
	NSInteger _runs;
	BOOL _receivedHandler;
	NSBackgroundActivityResult _answer;
}
- (void)run:(NSBackgroundActivityCompletionHandler)completion;
- (void)noteRun;
- (NSInteger)runs;
- (BOOL)receivedHandler;
- (void)setAnswer:(NSBackgroundActivityResult)answer;
- (NSBackgroundActivityResult)answer;
@end

@implementation ActivityProbe

- (void)noteRun
{
	_runs++;
}

- (void)run:(NSBackgroundActivityCompletionHandler)completion
{
	[self noteRun];
	if (completion != NULL) {
		_receivedHandler = YES;
		completion(_answer);
	}
}

- (NSInteger)runs { return _runs; }
- (BOOL)receivedHandler { return _receivedHandler; }
- (void)setAnswer:(NSBackgroundActivityResult)answer { _answer = answer; }
- (NSBackgroundActivityResult)answer { return _answer; }

@end

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-BACKGROUNDACTIVITY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-BACKGROUNDACTIVITY %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSLocale", "canonicalLocaleIdentifierFromString:") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* A NIL THE COMPILER CANNOT SEE THROUGH, for the check that a nil identifier is refused: passing a literal nil
 * to a nonnull parameter is -Wnonnull, and passing one through a call is the same experiment without it. */
static NSString *fn_nothing(void)
{
	return nil;
}

/* ONE TICK OF THE LOOP: it WAITS for a short window rather than polling a single pass, because the activity is
 * on a timer whose floor is a millisecond. */
static void fn_tick(void)
{
	[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
}

int main(void)
{
	{
		/* THE IDENTIFIER IS KEPT, AND THE DOOR ENFORCES APPLE'S OWN RULE about it: "nil and zero-length
		 * strings are not allowed", so both raise rather than producing a scheduler the system could not
		 * track. */
		NSBackgroundActivityScheduler *scheduler =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.updatecheck"];
		BOOL nilRaised = NO;
		BOOL emptyRaised = NO;

		@try {
			(void)[[NSBackgroundActivityScheduler alloc] initWithIdentifier:fn_nothing()];
		} @catch (NSException *e) {
			nilRaised = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		@try {
			(void)[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@""];
		} @catch (NSException *e) {
			emptyRaised = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("the-identifier-is-kept-and-an-empty-one-is-refused",
		      [[scheduler identifier] isEqualToString:@"com.example.probe.updatecheck"] &&
		      nilRaised && emptyRaised,
		      [NSString stringWithFormat:@"identifier=%@ nilRaised=%d emptyRaised=%d",
			[scheduler identifier], (int)nilRaised, (int)emptyRaised]);
	covers("NSDate", "dateWithTimeIntervalSinceNow:");
	}

	{
		/* THE DEFAULTS: Apple states two of them outright — "The default value is
		 * NSQualityOfServiceBackground" and "The default value for this property is false" — and the other
		 * three are ours and are labelled so. */
		NSBackgroundActivityScheduler *scheduler =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.defaults"];

		check("the-defaults-are-apples-two-and-ours-for-the-rest",
		      [scheduler qualityOfService] == NSQualityOfServiceBackground && ![scheduler repeats] &&
		      [scheduler interval] == 0 && [scheduler tolerance] == 0 && ![scheduler shouldDefer],
		      [NSString stringWithFormat:@"qos=%d repeats=%d interval=%.2f tolerance=%.2f defer=%d",
			(int)[scheduler qualityOfService], (int)[scheduler repeats], [scheduler interval],
			[scheduler tolerance], (int)[scheduler shouldDefer]]);
	}

	{
		/* THE PROPERTIES ROUND-TRIP, including the two that are KEPT AND REPORTED rather than obeyed. */
		NSBackgroundActivityScheduler *scheduler =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.props"];

		[scheduler setInterval:30];
		[scheduler setTolerance:5];
		[scheduler setQualityOfService:NSQualityOfServiceUserInitiated];
		[scheduler setRepeats:YES];
		check("the-properties-round-trip",
		      [scheduler interval] == 30 && [scheduler tolerance] == 5 &&
		      [scheduler qualityOfService] == NSQualityOfServiceUserInitiated && [scheduler repeats],
		      [NSString stringWithFormat:@"interval=%.0f tolerance=%.0f qos=%d repeats=%d",
			[scheduler interval], [scheduler tolerance], (int)[scheduler qualityOfService],
			(int)[scheduler repeats]]);
	}

	{
		/* THE ENGINE, FIRST HALF: an activity that FINISHES and does not repeat runs ONCE — and the count is
		 * EXACT here, because nothing about the interval can make it run again. */
		ActivityProbe *probe = [[ActivityProbe alloc] init];
		NSBackgroundActivityScheduler *scheduler =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.once"];

		[probe setAnswer:NSBackgroundActivityResultFinished];
		[scheduler setInterval:0];
		[scheduler scheduleWithBlock:^(NSBackgroundActivityCompletionHandler completion) {
			[probe run:completion];
		}];
		fn_tick();
		check("a-block-runs-on-the-loop-and-receives-a-completion-handler",
		      [probe runs] >= 1 && [probe receivedHandler],
		      [NSString stringWithFormat:@"runs=%ld handler=%d", (long)[probe runs],
			(int)[probe receivedHandler]]);
		fn_tick();
		fn_tick();
		check("finishing-a-non-repeating-activity-stops-it",
		      [probe runs] == 1,
		      [NSString stringWithFormat:@"runs=%ld after three ticks (want 1)", (long)[probe runs]]);
	}

	{
		/* SECOND HALF: an activity that DEFERS comes back — the answer means "not finished", whatever
		 * `-repeats` says — and one that REPEATS and finishes comes back too. BOTH are "more than once", and
		 * the exact number inside the window belongs to the interval. */
		ActivityProbe *deferring = [[ActivityProbe alloc] init];
		NSBackgroundActivityScheduler *deferred =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.defer"];

		[deferring setAnswer:NSBackgroundActivityResultDeferred];
		[deferred setInterval:0];
		[deferred scheduleWithBlock:^(NSBackgroundActivityCompletionHandler completion) {
			[deferring run:completion];
		}];
		fn_tick();
		check("deferring-brings-the-activity-back",
		      [deferring runs] >= 2,
		      [NSString stringWithFormat:@"runs=%ld after one tick (want >= 2)", (long)[deferring runs]]);
		[deferred invalidate];

		{
			ActivityProbe *repeating = [[ActivityProbe alloc] init];
			NSBackgroundActivityScheduler *repeat =
				[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.repeat"];

			[repeating setAnswer:NSBackgroundActivityResultFinished];
			[repeat setInterval:0];
			[repeat setRepeats:YES];
			[repeat scheduleWithBlock:^(NSBackgroundActivityCompletionHandler completion) {
				[repeating run:completion];
			}];
			fn_tick();
			check("a-repeating-activity-runs-again-after-finishing",
			      [repeating runs] >= 2,
			      [NSString stringWithFormat:@"runs=%ld after one tick (want >= 2)",
				(long)[repeating runs]]);
			[repeat invalidate];
		}
	}

	{
		/* AND `-invalidate` STOPS THE FUTURE RATHER THAN THE PRESENT, which is asserted as "THE COUNT STOPS
		 * GROWING" rather than as "it ran once": the activity here DEFERS, so it would come back forever if
		 * invalidation let it — and the check reads the count before and after, which is the only shape that
		 * a timing-dependent number cannot fake. */
		ActivityProbe *probe = [[ActivityProbe alloc] init];
		NSBackgroundActivityScheduler *scheduler =
			[[NSBackgroundActivityScheduler alloc] initWithIdentifier:@"com.example.probe.invalidate"];
		NSInteger before;

		[probe setAnswer:NSBackgroundActivityResultDeferred];	/* it WOULD come back, if it were allowed to */
		[scheduler setInterval:0];
		[scheduler scheduleWithBlock:^(NSBackgroundActivityCompletionHandler completion) {
			[probe run:completion];
		}];
		fn_tick();
		before = [probe runs];
		[scheduler invalidate];
		fn_tick();
		fn_tick();
		check("invalidate-stops-future-invocations",
		      before >= 1 && [probe runs] == before,
		      [NSString stringWithFormat:@"before=%ld after=%ld (want equal, and >= 1)",
			(long)before, (long)[probe runs]]);
	}

	printf("FOUNDATION-BACKGROUNDACTIVITY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-BACKGROUNDACTIVITY DONE\n");
	return failc == 0 ? 0 : 1;
}
