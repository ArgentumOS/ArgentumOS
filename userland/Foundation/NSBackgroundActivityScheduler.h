/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSBackgroundActivityScheduler — AN ACTIVITY THAT RUNS ITSELF AGAIN LATER (plan §62.66), and the last rows of
 * `App Support / System Interaction`. APPLE-DEPRECATED, AND THEREFORE IN SCOPE: §62.24's decision is that
 * deprecated API is a porting target rather than an exclusion, so the class ships like any other.
 *
 * WHAT IT IS: an object that runs a block now, hands that block a COMPLETION HANDLER, and decides what to do
 * next from the answer — finish, defer, or (for a repeating activity) run again. Apple's own shape, and the two
 * sentences that decide the engine:
 *
 *   * the block "is passed a completion handler as an argument. Configure the block to invoke this handler,
 *     passing it a result of type ... to indicate whether the activity FINISHED or should be DEFERRED";
 *   * `-invalidate`: "when invalidate is used to stop an activity that is currently executing, the activity will
 *     still finish executing" — so it stops FUTURE invocations and never interrupts a running block.
 *
 * WHICH IS WHAT THIS IMPLEMENTATION IS: the engine, on the run loop that already ships (an NSTimer at
 * `-interval`, one shot, rescheduled by the completion handler). TWO PROPERTIES ARE HINTS TO AN OPERATING
 * SYSTEM'S SCHEDULER AND ARE KEPT AND REPORTED RATHER THAN OBEYED, because there is no such scheduler here:
 * `-tolerance` ("a grace period — a range of time before and after the nominal fire date, during which the
 * activity may be invoked") and `-qualityOfService` ("controls how aggressively the system schedules the
 * activity"). This system has no OS deciding when to be aggressive, so the activity fires at its nominal date
 * and the two are what a caller set. That is a NAMED boundary, not a stub: the properties are real state with
 * real answers, and the absence is the scheduler's.
 *
 * AND `-shouldDefer` IS READ-ONLY FOR A REASON: it is the SYSTEM's answer to the app ("if this property
 * contains a value of true, system conditions have changed ... deferral is recommended"), which is why the app
 * checks it and cannot set it. With no such system, it answers NO — and says so here.
 *
 * DEFAULTS, SPLIT THE WAY THE PAGES ARE: `-qualityOfService` is `NSQualityOfServiceBackground` and `-repeats` is
 * NO, both of which Apple states outright ("The default value is NSQualityOfServiceBackground"; "The default
 * value for this property is false"); `-interval` and `-tolerance` start at 0, which is OUR choice because the
 * pages publish no starting value (§11.6.1 D2).
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
/* NSTimeInterval lives with the dates that measure it (NSDate.h), and NSQualityOfService in NSObjCRuntime.h
 * beside the other quality-of-service consumers. */
#import <Foundation/NSDate.h>

NS_ASSUME_NONNULL_BEGIN

@class NSString;

/* WHAT THE BLOCK TELLS THE SCHEDULER WHEN IT IS DONE. Apple publishes the two case names and no values: the
 * numbers are ours (§11.6.1 D2). */
typedef enum {
	NSBackgroundActivityResultFinished = 1,
	NSBackgroundActivityResultDeferred = 2
} NSBackgroundActivityResult;

/* "Configure the block to invoke this handler, passing it a result of type ... to indicate whether the activity
 * finished or should be deferred." */
typedef void (^NSBackgroundActivityCompletionHandler)(NSBackgroundActivityResult result);

@interface NSBackgroundActivityScheduler : NSObject
{
@protected
	NSString *_identifier;			/* copied; the reverse-DNS name the system tracks */
	BOOL _repeats;
	BOOL _shouldDefer;			/* always NO here: the answer belongs to a scheduler this system has none of */
	NSTimeInterval _interval;
	NSTimeInterval _tolerance;
	NSQualityOfService _qualityOfService;
	id _block;				/* Block_copy'd: the activity itself */
	id _timer;				/* NSTimer, retained while an invocation is pending */
	BOOL _active;				/* NO after -invalidate, which is what stops the RESCHEDULING */
}

/* APPLE'S OWN RULE FOR THE ARGUMENT, ENFORCED AT THE DOOR: "a unique string, in reverse DNS notation, that
 * identifies the activity ... nil and zero-length strings are NOT ALLOWED" — so both raise
 * NSInvalidArgumentException rather than producing a scheduler the system could not track. */
- (instancetype)initWithIdentifier:(NSString *)identifier;

@property (readonly, copy) NSString *identifier;
/* The suggested interval between scheduling and invoking the activity, and "for repeating activities ... also
 * the suggested interval between invocations". */
@property NSTimeInterval interval;
/* The grace period around the nominal fire date — KEPT AND REPORTED; see the header's note. */
@property NSTimeInterval tolerance;
/* How aggressively the system schedules — KEPT AND REPORTED; see the header's note. */
@property NSQualityOfService qualityOfService;
/* "A Boolean value indicating whether the activity should be rescheduled after it completes." NO by default. */
@property BOOL repeats;
/* THE SYSTEM'S ANSWER TO THE APP, which is why it is read-only: with no such system it answers NO. */
@property (readonly) BOOL shouldDefer;

/* Run the activity now: the block is called with a completion handler, and its answer decides what happens
 * next — a DEFERRED result reschedules, a FINISHED one reschedules only when `-repeats` is set, and either way
 * nothing is rescheduled once `-invalidate` has been called. */
- (void)scheduleWithBlock:(void (^)(NSBackgroundActivityCompletionHandler completionHandler))block;

/* Stop the activity: a pending invocation is cancelled and none is scheduled again. A block that is EXECUTING
 * still finishes, which is Apple's own rule for this door. */
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
