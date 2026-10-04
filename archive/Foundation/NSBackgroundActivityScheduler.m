/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBackgroundActivityScheduler.m — the engine, on the run loop (§62.66). MANUAL OWNERSHIP.
 *
 * THE WHOLE MECHANISM IS ONE ONE-SHOT TIMER AND ONE DECISION. The timer is scheduled at `-interval`; when it
 * fires the activity's block runs and is handed a completion handler; that handler's answer decides whether
 * another timer is scheduled:
 *
 *   DEFERRED  always comes back      — the activity has not finished, whatever -repeats says;
 *   FINISHED  comes back only if     -repeats is set.
 *
 * AND `-invalidate` STOPS THE RESCHEDULING RATHER THAN THE WORK: Apple's rule is that "the activity will still
 * finish executing" when it is already running, so this file cancels the pending timer and clears the ACTIVE
 * flag the rescheduler reads — which leaves a running block to complete and its answer to change nothing.
 *
 * A BLOCK THAT OUTLIVES THIS METHOD IS RETAINED AROUND: under manual ownership a block captures `self` WITHOUT
 * retaining it, so a completion handler made in -fnFireTimer: could otherwise run against a freed scheduler.
 * The retain is balanced where the handler runs, and the (documented) consequence is the one Cocoa's own
 * NSTimer has as well: a handler the caller never invokes keeps its scheduler alive until it is.
 */

#import <Foundation/NSBackgroundActivityScheduler.h>
#import <Foundation/NSTimer.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>

#include <Block.h>

@implementation NSBackgroundActivityScheduler

- (instancetype)initWithIdentifier:(NSString *)identifier
{
	if (identifier == nil || [identifier length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSBackgroundActivityScheduler: the identifier is the reverse-DNS name the system "
				   @"tracks this activity by, and Apple's own rule is that nil and zero-length strings "
				   @"are not allowed"];
	}
	self = [super init];
	if (self != nil) {
		_identifier = [[NSString alloc] initWithString:identifier];
		/* APPLE'S TWO DEFAULTS, AS PUBLISHED: the background quality of service, and a NON-repeating activity
		 * — "The default value for this property is false". `-interval` and `-tolerance` start at 0, which is
		 * ours because their pages publish no starting value. */
		_qualityOfService = NSQualityOfServiceBackground;
		_repeats = NO;
	}
	return self;
}

- (void)dealloc
{
	if (_timer != nil) {
		[(NSTimer *)_timer invalidate];
		[(NSTimer *)_timer release];
	}
	if (_block != NULL) {
		Block_release(_block);
	}
	[_identifier release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

- (NSString *)identifier { return _identifier; }
- (BOOL)repeats { return _repeats; }
- (void)setRepeats:(BOOL)value { _repeats = value; }
- (NSTimeInterval)interval { return _interval; }
- (void)setInterval:(NSTimeInterval)value { _interval = value; }
- (NSTimeInterval)tolerance { return _tolerance; }
- (void)setTolerance:(NSTimeInterval)value { _tolerance = value; }
- (NSQualityOfService)qualityOfService { return _qualityOfService; }
- (void)setQualityOfService:(NSQualityOfService)value { _qualityOfService = value; }
- (BOOL)shouldDefer { return _shouldDefer; }

/* ---- THE ENGINE --------------------------------------------------------------------------------------- */

- (void)scheduleWithBlock:(void (^)(NSBackgroundActivityCompletionHandler))block
{
	if (block == NULL) {
		return;
	}
	[self fnStopTimer];
	if (_block != NULL) {
		Block_release(_block);
	}
	_block = Block_copy(block);
	/* SCHEDULING AGAIN IS ALLOWED AFTER -invalidate, and that is OURS: Apple says what -invalidate stops (the
	 * activity) and not whether the object may be used again, and a scheduler that could never be restarted
	 * would be a surprising reading of a mutable object. */
	_active = YES;
	[self fnStartTimer];
}

/* ONE SHOT, ON THE CURRENT RUN LOOP: the activity is a REQUEST the loop honours, and RESCHEDULING is the
 * completion handler's business rather than the timer's.
 *
 * AND THERE IS A FLOOR UNDER THE INTERVAL, WHICH THE PROBE MEASURED RATHER THAN THE DESIGNER ASSUMING IT: an
 * activity that reschedules ITSELF with an interval of ZERO produces a timer that is ALREADY DUE, and this run
 * loop's pass re-reads its timer list as it walks it — so the freshly added timer is fired inside the SAME pass,
 * which reschedules another one, and the pass never ends. It is not a hypothetical: a DEFERRED activity with
 * `-interval` 0 spun forever the first time this class was driven. So zero and negative intervals mean "the
 * FLOOR" (a millisecond) rather than "now", which keeps an activity prompt and keeps the loop a loop. */ 
#define FN_MIN_INTERVAL 0.001

- (void)fnStartTimer
{
	NSTimer *timer;
	NSTimeInterval delay = _interval > FN_MIN_INTERVAL ? _interval : FN_MIN_INTERVAL;

	if (!_active) {
		return;
	}
	timer = [NSTimer timerWithTimeInterval:delay
					target:self
				      selector:@selector(fnFireTimer:)
				      userInfo:nil
				       repeats:NO];
	_timer = [timer retain];
	[[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
}

- (void)fnStopTimer
{
	if (_timer != nil) {
		[(NSTimer *)_timer invalidate];
		[(NSTimer *)_timer release];
		_timer = nil;
	}
}

/* THE FIRE: the block is called with a completion handler, and this method RETURNS WITHOUT KNOWING what the
 * activity will answer — which is the whole point of the handler. The scheduler is retained across that window
 * (see this file's opening comment) and released where the handler runs. */
- (void)fnFireTimer:(NSTimer *)timer
{
	void (^block)(NSBackgroundActivityCompletionHandler) =
		(void (^)(NSBackgroundActivityCompletionHandler))_block;

	(void)timer;
	[self fnStopTimer];
	if (block == NULL) {
		return;
	}
	[self retain];
	{
		/* THE HANDLER IS INVOKED ONCE, which is what Apple tells the block to do: a second invocation is
		 * ignored rather than rescheduling twice. `__block` because the block writes it. */
		__block BOOL answered = NO;

		block(^(NSBackgroundActivityResult result) {
			if (answered) {
				return;
			}
			answered = YES;
			[self fnCompleted:result];
			[self release];		/* balanced by the retain above */
		});
	}
}

- (void)fnCompleted:(NSBackgroundActivityResult)result
{
	/* "-invalidate ... the activity will still finish executing" — so an answer that arrives after an
	 * invalidation is ACCEPTED here and simply does not reschedule. */
	if (result == NSBackgroundActivityResultDeferred) {
		if (_active) {
			[self fnStartTimer];
		}
		return;
	}
	if (_repeats && _active) {
		[self fnStartTimer];
	}
}

- (void)invalidate
{
	/* THE FUTURE, NOT THE PRESENT: the pending timer goes and the rescheduler's flag clears, while a block that
	 * is already running is left to finish. */
	_active = NO;
	[self fnStopTimer];
}

@end
