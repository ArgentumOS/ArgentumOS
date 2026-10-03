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
 * WHAT IS NOT HERE ANY MORE: this note used to say that `-tolerance` and the block-taking
 * initialisers were absent because "this library has no blocks". Both are implemented now
 * (§63.190) — the library HAS blocks, the block APIs this campaign landed say so, and the
 * sentence was older than they are.
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
	NSTimeInterval _tolerance;
	id _block;		/* a BLOCK timer's body, heap-copied */
	id _invocation;		/* an INVOCATION timer's body, retained */
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

/* THE FOUR CONVENIENCE PAIRS AND THE BLOCK INITIALIZER — Apple's own spelling, the block being
 * `void (^)(NSTimer *)`, which RECEIVES THE TIMER ITSELF. Each is the target/selector core above with a
 * body that knows how to be called. */
+ (NSTimer *)timerWithTimeInterval:(NSTimeInterval)interval
			   repeats:(BOOL)repeats
			     block:(void (^)(NSTimer *timer))block;
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)interval
				    repeats:(BOOL)repeats
				      block:(void (^)(NSTimer *timer))block;
+ (NSTimer *)timerWithTimeInterval:(NSTimeInterval)interval
			invocation:(NSInvocation *)invocation
			   repeats:(BOOL)repeats;
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)interval
				 invocation:(NSInvocation *)invocation
				    repeats:(BOOL)repeats;
- (instancetype)initWithFireDate:(NSDate *)date
			interval:(NSTimeInterval)interval
			 repeats:(BOOL)repeats
			   block:(void (^)(NSTimer *timer))block;

/* THE WINDOW A REPEATING TIMER MAY LATE-FIRE IN. Apple's contract is a HINT to the run loop rather than a
 * promise to the caller, and this loop fires as soon as the date has passed; the value is stored and read
 * back, which is what the probe asserts. */
@property NSTimeInterval tolerance;

- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTIMER_H */
