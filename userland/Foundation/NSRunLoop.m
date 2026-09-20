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
#import <Foundation/NSDate.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

NSString *const NSDefaultRunLoopMode = @"NSDefaultRunLoopMode";
NSString *const NSRunLoopCommonModes = @"NSRunLoopCommonModes";

static pthread_key_t fn_loop_key;
static pthread_once_t fn_loop_key_once = PTHREAD_ONCE_INIT;
static BOOL fn_loop_key_ready = NO;
static NSRunLoop *fn_first_loop = nil;

/* THE TIMER'S STATE IS READ AND WRITTEN BY THE LOOP, which lives in this file for that reason. */
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

/* A WAIT, BOUNDED BY THE CALLER: a pure timeout on select, which needs no descriptors at all. */
static void fn_wait(double seconds)
{
	struct timeval timeout;

	if (seconds <= 0) {
		return;
	}
	if (seconds > 0.05) {
		seconds = 0.05;		/* the loop re-checks the clock, so a long wait is a series */
	}
	timeout.tv_sec = (time_t)seconds;
	timeout.tv_usec = (suseconds_t)((seconds - (double)timeout.tv_sec) * 1000000.0);
	if (timeout.tv_usec < 0) {
		timeout.tv_usec = 0;
	}
	select(0, NULL, NULL, NULL, &timeout);
}

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
	/* ONE PASS, and the answer says whether there is still work: a loop with nothing live stops. */
	return fired || [self fnHasLiveTimersForMode:_currentMode];
}

- (void)runUntilDate:(NSDate *)limit
{
	while ([self fnHasLiveTimersForMode:_currentMode] &&
	       (limit == nil || [limit timeIntervalSinceNow] > 0)) {
		double soonest = [self fnNextFire:_currentMode];

		if (soonest > 0) {
			fn_wait(soonest - fn_now());
		}
		[self runMode:_currentMode beforeDate:limit];
	}
}

- (void)run
{
	while ([self fnHasLiveTimersForMode:_currentMode]) {
		double soonest = [self fnNextFire:_currentMode];

		if (soonest > 0) {
			fn_wait(soonest - fn_now());
		}
		[self runMode:_currentMode beforeDate:nil];
	}
}

@end
