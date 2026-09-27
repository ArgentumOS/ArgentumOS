/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRunLoop.m — the timer and the loop that fires it (F13.18). One file, because the two are one
 * design: a timer names a date and a loop is what waits for dates, so neither is useful without the
 * other and reading them apart would hide that.
 *
 * THE WAIT IS select(2), NOT nanosleep(2). F13.17 measured that this kernel returns from nanosleep
 * early — while the same program's clock-driven deadline loop took its full time — so the loop waits
 * on select's timeout and RE-CHECKS THE CLOCK after every wait rather than trusting it. If select is
 * early too, the loop still fires on time; it simply runs more often than it needs to.
 *
 * THE MODE ARRAYS ARE PARALLEL, and that is worth saying once: each timer remembers the mode it was
 * added for, and a loop running a mode fires the timers added for it or for the common modes.
 */

#import <Foundation/NSRunLoop.h>
#import <Foundation/NSTimer.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
/* THE NOTIFICATION QUEUE'S SEAM (§62.61). Two of NSNotificationQueue's posting styles are DEFINED as points in
 * a run loop — "as soon as possible" is "when the current callout completes" and "when idle" is "when the loop
 * is about to wait" — so the loop has to tell the queue which phase it reached. This is the only file that
 * knows, and FNRunLoopQueue.h is the internal contract between them. */
#import <Foundation/FNRunLoopQueue.h>
/* THE WEAK API LIVES HERE (objc_storeWeak/objc_loadWeak, documented as zeroing), not in
 * <objc/runtime.h>. THE FOUNDATION LIBRARY IS MRC - ARC is a per-file choice and no file in this
 * directory takes it - so a non-owning reference is spelled with the runtime's own functions,
 * exactly as NSNotificationCenter.m does it. */
#include <objc/objc-arc.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

NSString *const NSDefaultRunLoopMode = @"NSDefaultRunLoopMode";
NSString *const NSRunLoopCommonModes = @"NSRunLoopCommonModes";

/* THE SOURCE, AND TWO OF ITS THREE DECISIONS ARE LOAD-BEARING. The target is `__weak`: a run loop lives as
 * long as its thread, so a retained target is a leak nobody can fix, and a target that has gone away is
 * SKIPPED rather than called (measured the hard way would be a dangling pointer). The descriptor is kept
 * as an int rather than an NSFileHandle so that nothing here owns the descriptor — the consumer's file
 * handle does, which is Apple's own rule for `-initWithFileDescriptor:`. */
@interface FNRunLoopSource : NSObject
{
	int _fd;
	BOOL _readable;
	NSString *_mode;
	id _target;			/* ZEROING WEAK: the loop does not keep it alive */
	SEL _selector;
}
- (instancetype)initWithFileDescriptor:(int)fd
				  mode:(NSString *)mode
			      readable:(BOOL)readable
				target:(id)target
			      selector:(SEL)selector;
- (int)fileDescriptor;
- (BOOL)isReadable;
- (BOOL)matchesMode:(NSString *)mode;
- (BOOL)hasTarget:(id)target;
- (BOOL)targetIsAlive;
- (void)fireIfAlive;
@end

@implementation FNRunLoopSource

- (instancetype)initWithFileDescriptor:(int)fd
				  mode:(NSString *)mode
			      readable:(BOOL)readable
				target:(id)target
			      selector:(SEL)selector
{
	self = [super init];
	if (self != nil) {
		_fd = fd;
		_readable = readable;
		_mode = [mode copy];
		objc_storeWeak(&_target, target);
		_selector = selector;
	}
	return self;
}

- (int)fileDescriptor
{
	return _fd;
}

- (BOOL)isReadable
{
	return _readable;
}

/* COMMON MODES FIRE ANY MODE'S SOURCES, exactly as they do for timers — one rule, both kinds of source. */
- (BOOL)matchesMode:(NSString *)mode
{
	return _mode == nil || [mode isEqualToString:_mode] ||
	       [mode isEqualToString:NSRunLoopCommonModes] ||
	       [_mode isEqualToString:NSRunLoopCommonModes];
}

- (BOOL)hasTarget:(id)target
{
	return objc_loadWeak(&_target) == target;
}

- (BOOL)targetIsAlive
{
	return objc_loadWeak(&_target) != nil;
}

- (void)fireIfAlive
{
	id target = objc_loadWeak(&_target);	/* read once: the weak slot can go nil under us */

	if (target != nil) {
		[target performSelector:_selector];
	}
}

- (void)dealloc
{
	objc_storeWeak(&_target, nil);
	[_mode release];
	[super dealloc];
}

@end

static pthread_key_t fn_loop_key;
static pthread_once_t fn_loop_key_once = PTHREAD_ONCE_INIT;
static BOOL fn_loop_key_ready = NO;
static NSRunLoop *fn_first_loop = nil;

/* THE TIMER'S STATE IS READ AND WRITTEN BY THE LOOP, which lives in this file for that reason. */
@interface NSRunLoop (FNPrivate)
- (NSMutableArray *)fnSources;
- (BOOL)fnHasLiveWorkForMode:(NSString *)mode;
- (BOOL)fnHasLiveSourcesForMode:(NSString *)mode;
- (void)fnPollSourcesInMode:(NSString *)mode seconds:(double)seconds dispatch:(BOOL)dispatch;
@end

@interface NSTimer (FNPrivate)
- (void)fnReschedule;
- (BOOL)fnIsDueAt:(double)seconds;
@end

static void fn_make_loop_key(void)
{
	if (pthread_key_create(&fn_loop_key, NULL) == 0) {
		fn_loop_key_ready = YES;
	}
}

static double fn_now(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (double)now.tv_sec + ((double)now.tv_usec / 1000000.0);
}

/* THE WAIT IS CAPPED, and the cap is not about the descriptors: the loop RE-CHECKS THE CLOCK after every
 * wait (F13.17 measured that this kernel can return from a wait early), so a long wait is a series of
 * short ones either way. What select(2) adds here is the OTHER half — with a source registered, the wait
 * ends when the descriptor is ready instead of when the clock says so. */
#define FN_WAIT_CAP 0.05

@implementation NSTimer

+ (NSTimer *)timerWithTimeInterval:(NSTimeInterval)interval
			    target:(id)target
			  selector:(SEL)selector
			  userInfo:(nullable id)userInfo
			   repeats:(BOOL)repeats
{
	return [[self alloc] initWithFireDate:[NSDate dateWithTimeIntervalSinceNow:interval]
				     interval:interval
				       target:target
				     selector:selector
				     userInfo:userInfo
				      repeats:repeats];
}

+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)interval
				     target:(id)target
				   selector:(SEL)selector
				   userInfo:(nullable id)userInfo
				    repeats:(BOOL)repeats
{
	NSTimer *timer = [self timerWithTimeInterval:interval
					      target:target
					    selector:selector
					    userInfo:userInfo
					     repeats:repeats];

	[[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
	return timer;
}

- (instancetype)initWithFireDate:(NSDate *)date
			interval:(NSTimeInterval)interval
			  target:(id)target
			selector:(SEL)selector
			userInfo:(nullable id)userInfo
			 repeats:(BOOL)repeats
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_interval = interval;
	_target = target;
	_selector = selector;
	_userInfo = userInfo;
	_fireDate = date != nil ? date : [NSDate date];
	_repeats = repeats;
	/* VALID FROM THE START, because a timer handed to a caller is one they may FIRE even if they
	 * never add it — which is what +timerWithTimeInterval: is for. */
	_valid = YES;
	return self;
}

- (void)fire
{
	if (!_valid && !_firing) {
		return;
	}
	if (_target == nil || _selector == NULL) {
		return;
	}
	_firing = YES;
	[_target performSelector:_selector withObject:self];
	_firing = NO;
	[self fnReschedule];
}

- (void)fnReschedule
{
	if (_repeats && _valid) {
		/* THE INTERVAL, NOT THE ORIGINAL DATE: a repeating timer means "every N seconds from the
		 * last fire", which is what the code that uses one expects. */
		_fireDate = [NSDate dateWithTimeIntervalSinceNow:_interval];
	} else {
		_valid = NO;
	}
}

- (BOOL)fnIsDueAt:(double)seconds
{
	return _valid && _fireDate != nil && [_fireDate timeIntervalSince1970] <= seconds;
}

- (void)invalidate
{
	_valid = NO;
}

- (BOOL)isValid
{
	return _valid;
}

- (NSDate *)fireDate
{
	return _fireDate;
}

- (void)setFireDate:(NSDate *)fireDate
{
	_fireDate = fireDate;
}

- (NSTimeInterval)timeInterval
{
	return _interval;
}

- (nullable id)userInfo
{
	return _userInfo;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p interval=%.3f repeats=%d valid=%d>", [self class],
				  self, _interval, (int)_repeats, (int)_valid];
}

@end

@implementation NSRunLoop

+ (NSRunLoop *)currentRunLoop
{
	NSRunLoop *loop;

	pthread_once(&fn_loop_key_once, fn_make_loop_key);
	loop = fn_loop_key_ready ? (NSRunLoop *)pthread_getspecific(fn_loop_key) : nil;
	if (loop == nil) {
		loop = [[NSRunLoop alloc] init];
		if (fn_loop_key_ready) {
			pthread_setspecific(fn_loop_key, (void *)loop);
		}
		if (fn_first_loop == nil) {
			/* THE FIRST LOOP MADE IS THE MAIN ONE, which is the only answer "+mainRunLoop" can
			 * give without knowing which thread is main — NSThread knows that, and this class
			 * does not need to. */
			fn_first_loop = loop;
		}
	}
	return loop;
}

+ (NSRunLoop *)mainRunLoop
{
	return fn_first_loop != nil ? fn_first_loop : [self currentRunLoop];
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_timers = [[NSMutableArray alloc] init];
	_modes = [[NSMutableArray alloc] init];
	_currentMode = NSDefaultRunLoopMode;
	return self;
}

- (NSString *)currentMode
{
	return _currentMode;
}

- (void)addTimer:(NSTimer *)timer forMode:(NSString *)mode
{
	if (timer == nil || mode == nil) {
		return;
	}
	[_timers addObject:timer];
	[_modes addObject:mode];
}

/* DOES A MODE FIRE THIS TIMER? The mode it was added for, or the common modes, or the loop is
 * running the common modes — which is the whole of what "common" means here. */
static BOOL fn_mode_fires(NSString *timerMode, NSString *runningMode)
{
	if ([timerMode isEqualToString:runningMode]) {
		return YES;
	}
	if ([timerMode isEqualToString:NSRunLoopCommonModes]) {
		return YES;
	}
	return [runningMode isEqualToString:NSRunLoopCommonModes];
}

- (BOOL)fnHasLiveTimersForMode:(NSString *)mode
{
	NSUInteger i;

	for (i = 0; i < [_timers count]; i++) {
		if ([(NSTimer *)[_timers objectAtIndex:i] isValid] &&
		    fn_mode_fires([_modes objectAtIndex:i], mode)) {
			return YES;
		}
	}
	return NO;
}

- (double)fnNextFire:(NSString *)mode
{
	double soonest = 0;
	NSUInteger i;

	for (i = 0; i < [_timers count]; i++) {
		NSTimer *timer = [_timers objectAtIndex:i];

		if (![timer isValid] || !fn_mode_fires([_modes objectAtIndex:i], mode)) {
			continue;
		}
		{
			double when = [[timer fireDate] timeIntervalSince1970];

			if (soonest == 0 || when < soonest) {
				soonest = when;
			}
		}
	}
	return soonest;
}

- (void)fnPrune
{
	NSUInteger i = 0;

	while (i < [_timers count]) {
		if ([(NSTimer *)[_timers objectAtIndex:i] isValid]) {
			i++;
			continue;
		}
		[_timers removeObjectAtIndex:i];
		[_modes removeObjectAtIndex:i];
	}
}

- (BOOL)runMode:(NSString *)mode beforeDate:(NSDate *)limit
{
	BOOL fired = NO;
	NSUInteger i;

	(void)limit;
	_currentMode = mode != nil ? mode : NSDefaultRunLoopMode;
	for (i = 0; i < [_timers count]; i++) {
		NSTimer *timer = [_timers objectAtIndex:i];

		if (!fn_mode_fires([_modes objectAtIndex:i], _currentMode) || ![timer isValid]) {
			continue;
		}
		if ([timer fnIsDueAt:fn_now()]) {
			[timer fire];
			fired = YES;
		}
	}
	[self fnPrune];
	/* A PASS ALSO POLLS THE SOURCES AND DISPATCHES THEM, with a ZERO timeout: this door is documented as
	 * ONE PASS and the waiting belongs to -runUntilDate:/-run (the deviation the probe pins as
	 * `runmode-one-pass`). */
	[self fnPollSourcesInMode:_currentMode seconds:0 dispatch:YES];
	/* AND THE PASS ENDS BY GIVING THE NOTIFICATION QUEUE ITS "AS SOON AS POSSIBLE" PHASE (§62.61): this pass's
	 * callouts have run, so a notification enqueued NSPostASAP is due NOW — posting it here is exactly what
	 * "posted when the current run-loop callout completes" means. It happens BEFORE the answer below is
	 * computed, because posting is synchronous and may enqueue more; that new work has to be visible. */
	[NSNotificationQueue fnPostPendingWhenIdle:NO mode:_currentMode];
	/* ... and the answer says whether there is still work: a loop with nothing live stops. */
	return fired || [self fnHasLiveWorkForMode:_currentMode];
}


/* ---- the sources (W6a) --------------------------------------------------- */

/* The loop keeps running while EITHER kind of work is live. One function, so the two doors below cannot
 * disagree about what "still has work" means.
 *
 * A QUEUED NOTIFICATION IS WORK (§62.61), and leaving it out of this answer would be a real bug rather than a
 * tidiness point: `-runUntilDate:` asks this question BEFORE its wait, so a loop whose only work was a queued
 * NSPostWhenIdle notification would decide it had nothing to do and return without ever reaching the phase that
 * would post it. */
- (BOOL)fnHasLiveWorkForMode:(NSString *)mode
{
	return [self fnHasLiveTimersForMode:mode] || [self fnHasLiveSourcesForMode:mode] ||
	       [NSNotificationQueue fnHasPendingWorkForMode:mode];
}

/* APPLE'S PORT DOOR: IT IS THE FORWARD AND NOTHING ELSE. A port already knows how to be watched —
 * NSPort declares `-scheduleInRunLoop:forMode:`, and NSSocketPort implements it by registering its
 * descriptor with THIS loop's source seam (§43) — so the run loop's job is to say "watch yourself
 * here" and let the concrete port decide what that means. A loop that kept its own port list would be
 * a second registry to keep in step with the first. */
- (void)addPort:(NSPort *)aPort forMode:(NSRunLoopMode)mode
{
	[aPort scheduleInRunLoop:self forMode:mode];
}

- (void)removePort:(NSPort *)aPort forMode:(NSRunLoopMode)mode
{
	[aPort removeFromRunLoop:self forMode:mode];
}

/* CREATED ON FIRST USE rather than in an initializer, because a run loop is made by three different doors
 * (+currentRunLoop, +mainRunLoop, and NSObject's -init) and a source array that only one of them filled
 * would be a bug waiting for the least-used door. */
- (NSMutableArray *)fnSources
{
	if (_sources == nil) {
		_sources = [[NSMutableArray alloc] init];
	}
	return _sources;
}

- (BOOL)fnHasLiveSourcesForMode:(NSString *)mode
{
	NSMutableArray *sources = [self fnSources];
	NSUInteger i;

	for (i = 0; i < [sources count]; i++) {
		FNRunLoopSource *source = [sources objectAtIndex:i];

		if ([source matchesMode:mode] && [source targetIsAlive]) {
			return YES;
		}
	}
	return NO;
}

/* THE WAIT, WITH THE DESCRIPTORS IN IT — AND A WAIT IS NOT A DISPATCH. `seconds` is how long the caller
 * is willing to wait, and select(2) ends the wait EARLY when a watched descriptor is ready, which is the
 * whole point of a source. `dispatch` decides whether the ready ones are told: `-runUntilDate:` and
 * `-run` WAIT (that is what makes a source-only loop block instead of spin) and the PASS that follows
 * dispatches; `-runMode:beforeDate:` polls with a zero timeout and dispatches, because that door is
 * documented as one pass.
 *
 * MEASURED, because the first version dispatched in both places and a level-triggered source then fired
 * TWICE per iteration: 29862 fires in two seconds, which is a spin and not a run loop. Fire order is
 * registration order, and the snapshot is a COPY because firing can remove a source (a one-shot read
 * does exactly that) and mutating the array under the loop is how this class would crash. */
- (void)fnPollSourcesInMode:(NSString *)mode seconds:(double)seconds dispatch:(BOOL)dispatch
{
	NSMutableArray *sources = [self fnSources];
	fd_set readableSet, writableSet;
	struct timeval timeout;
	NSArray *snapshot;
	int maxFd = -1;
	NSUInteger i;

	FD_ZERO(&readableSet);
	FD_ZERO(&writableSet);
	snapshot = [sources copy];
	for (i = 0; i < [snapshot count]; i++) {
		FNRunLoopSource *source = [snapshot objectAtIndex:i];
		int fd = [source fileDescriptor];

		if (![source matchesMode:mode] || fd < 0 || fd >= FD_SETSIZE) {
			continue;
		}
		if ([source isReadable]) {
			FD_SET(fd, &readableSet);
		} else {
			FD_SET(fd, &writableSet);
		}
		if (fd > maxFd) {
			maxFd = fd;
		}
	}
	if (seconds > FN_WAIT_CAP) {
		seconds = FN_WAIT_CAP;
	}
	if (seconds < 0) {
		seconds = 0;
	}
	timeout.tv_sec = (time_t)seconds;
	timeout.tv_usec = (suseconds_t)((seconds - (double)timeout.tv_sec) * 1000000.0);
	if (timeout.tv_usec < 0) {
		timeout.tv_usec = 0;
	}
	/* A ZERO TIMEOUT WITH NO DESCRIPTORS IS STILL A POLL, and that is the right degenerate case: the pass
	 * has nothing to wait for, so it returns at once. */
	if (select(maxFd + 1, (maxFd >= 0 ? &readableSet : NULL), (maxFd >= 0 ? &writableSet : NULL),
		   NULL, &timeout) <= 0) {
		return;
	}
	if (!dispatch) {
		return;		/* the caller only wanted to wait */
	}
	for (i = 0; i < [snapshot count]; i++) {
		FNRunLoopSource *source = [snapshot objectAtIndex:i];
		int fd = [source fileDescriptor];
		BOOL ready;

		if (![source matchesMode:mode] || fd < 0 || fd >= FD_SETSIZE) {
			continue;
		}
		ready = [source isReadable] ? FD_ISSET(fd, &readableSet) : FD_ISSET(fd, &writableSet);
		if (ready) {
			[source fireIfAlive];
		}
	}
}

- (void)addSourceForFileDescriptor:(int)fd
			      mode:(NSRunLoopMode)mode
			  readable:(BOOL)readable
			    target:(id)target
			  selector:(SEL)selector
{
	[[self fnSources] addObject:[[FNRunLoopSource alloc] initWithFileDescriptor:fd
									     mode:mode
									 readable:readable
									   target:target
									 selector:selector]];
}

- (void)removeSourceForTarget:(id)target
{
	NSMutableArray *sources = [self fnSources];
	NSUInteger i = 0;

	while (i < [sources count]) {
		FNRunLoopSource *source = [sources objectAtIndex:i];

		if ([source hasTarget:target]) {
			[sources removeObjectAtIndex:i];
			continue;
		}
		i++;
	}
}

- (void)runUntilDate:(NSDate *)limit
{
	while ([self fnHasLiveWorkForMode:_currentMode] &&
	       (limit == nil || [limit timeIntervalSinceNow] > 0)) {
		double soonest = [self fnNextFire:_currentMode];

		/* "WHEN IDLE" IS THIS POINT AND NOT ANOTHER ONE (§62.61): the loop is about to WAIT, which is the last
		 * moment at which it can still be said to have had nothing else to do. A NSPostWhenIdle notification
		 * is posted HERE, before the sleep — not after it, which would make "idle" mean "just after waking". */
		[NSNotificationQueue fnPostPendingWhenIdle:YES mode:_currentMode];
		if (soonest > 0) {
			[self fnPollSourcesInMode:_currentMode
					  seconds:soonest - fn_now()
					 dispatch:NO];
		} else if ([self fnHasLiveSourcesForMode:_currentMode]) {
			/* NO TIMER IS DUE, BUT A SOURCE IS STILL WATCHED: wait out the cap rather than spin. */
			[self fnPollSourcesInMode:_currentMode seconds:FN_WAIT_CAP dispatch:NO];
		}
		[self runMode:_currentMode beforeDate:limit];
	}
}

- (void)run
{
	while ([self fnHasLiveWorkForMode:_currentMode]) {
		double soonest = [self fnNextFire:_currentMode];

		[NSNotificationQueue fnPostPendingWhenIdle:YES mode:_currentMode];	/* see -runUntilDate: */
		if (soonest > 0) {
			[self fnPollSourcesInMode:_currentMode
					  seconds:soonest - fn_now()
					 dispatch:NO];
		} else if ([self fnHasLiveSourcesForMode:_currentMode]) {
			[self fnPollSourcesInMode:_currentMode seconds:FN_WAIT_CAP dispatch:NO];
		}
		[self runMode:_currentMode beforeDate:nil];
	}
}

@end
