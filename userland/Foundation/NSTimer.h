/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTimer — a message to yourself, later. F13.18, docs/design/foundation-plan.md §10.
 *
 * A TIMER IS A REQUEST, NOT A THREAD: it names WHEN and WHAT, and nothing happens until a RUN LOOP
 * takes it and waits. That is why `+scheduledTimerWithTimeInterval:…` and `+timerWithTimeInterval:…`
 * are different doors — the first hands the timer to the current loop, the second hands it to the
 * CALLER, who must add it or fire it. A timer that is never added is never fired, and `-isValid` is
 * how a caller finds that out rather than by waiting.
 *
 * WHAT IS NOT HERE, named: `-tolerance`, the block-taking initialisers (this library has no blocks in
 * its public headers), and `-compare:`. A repeating timer keeps its INTERVAL after each fire rather
 * than its original date, which is what a repeating timer means to the code that uses one.
 */

#ifndef FOUNDATION_NSTIMER_H
#define FOUNDATION_NSTIMER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
/* NSTimeInterval IS DECLARED IN NSDate.h — the FOURTH time this program has paid for that fact
 * (NSProcessInfo.h, NSThread.h, and twice in this slice's own build), and here it garbles EVERY
 * parameter type in the file when it is missing, because a header that cannot spell the return type
 * of a method makes the method untyped. */
#import <Foundation/NSDate.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSTimer : NSObject
{
	double _interval;
	id _target;
	SEL _selector;
	id _userInfo;
	NSDate *_fireDate;
	BOOL _repeats;
	BOOL _valid;
	BOOL _firing;
}

+ (NSTimer *)timerWithTimeInterval:(NSTimeInterval)interval
			    target:(id)target
			  selector:(SEL)selector
			  userInfo:(nullable id)userInfo
			   repeats:(BOOL)repeats;
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)interval
				     target:(id)target
				   selector:(SEL)selector
				   userInfo:(nullable id)userInfo
				    repeats:(BOOL)repeats;

- (instancetype)initWithFireDate:(NSDate *)date
			interval:(NSTimeInterval)interval
			  target:(id)target
			selector:(SEL)selector
			userInfo:(nullable id)userInfo
			 repeats:(BOOL)repeats;

- (void)fire;
- (void)invalidate;
- (BOOL)isValid;

- (NSDate *)fireDate;
- (void)setFireDate:(NSDate *)fireDate;
- (NSTimeInterval)timeInterval;
- (nullable id)userInfo;

- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTIMER_H */
