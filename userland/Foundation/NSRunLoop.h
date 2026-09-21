/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRunLoop — the loop that waits, and the place timers are kept. F13.18.
 *
 * WHAT IT RUNS, in v1: TIMERS. A run loop with file-descriptor sources, ports, observers and block
 * performers is a different design at each of those words, and only the timer half is here — which is
 * the half a program with no event sources needs to schedule work.
 *
 * THE MODES ARE NAMES, NOT MACHINES: a timer is added for a mode, and a loop running that mode fires
 * it. `NSDefaultRunLoopMode` and `NSRunLoopCommonModes` are the two Cocoa names, and a timer added
 * for the COMMON modes is fired by every mode — which is the whole of what "common" means here.
 *
 * HOW IT WAITS, and this one is a kernel fact worth stating where a caller can read it: the loop's
 * wait is `select(2)` with a timeout, because THIS KERNEL RETURNS FROM `nanosleep(2)` EARLY (measured
 * during F13.17's work). If select's timeout is not honoured either, the loop still fires on time —
 * it re-checks the clock after every wait rather than trusting it — but it will spin, and the CPU
 * cost of that is the C library's and the kernel's to fix, not this class's to hide.
 *
 * WHAT IS NOT HERE, named: run-loop sources and observers, `-performSelector:…` and the block
 * performers, `-runLoop`/`-getCFRunLoop`, and `NSRunLoopCommonModes` as a real mode SET rather than
 * the single name it is treated as.
 */

#ifndef FOUNDATION_NSRUNLOOP_H
#define FOUNDATION_NSRUNLOOP_H

#import <Foundation/NSObject.h>

@class NSDate;
@class NSTimer;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* COCOA'S TWO NAMES, and their values are the names — a caller compares against the constant. */
extern NSString *const NSDefaultRunLoopMode;
extern NSString *const NSRunLoopCommonModes;

/* Apple's modern spelling for a mode, DECLARED because this class's own source door takes one (the older
 * methods below say `NSString *` and mean the same type). */
typedef NSString * NSRunLoopMode;

/* FORWARD-DECLARED: the ivars only need the names, and the implementation imports NSArray.h. */
@class NSMutableArray;

@interface NSRunLoop : NSObject
{
	NSMutableArray *_timers;	/* NSTimer, in no particular order; the loop sorts at each wait */
	NSMutableArray *_modes;		/* the mode each timer in _timers was added for */
	NSString *_currentMode;
	BOOL _running;
	NSMutableArray *_sources;	/* FNRunLoopSource, created on first use */
}

+ (NSRunLoop *)currentRunLoop;
+ (NSRunLoop *)mainRunLoop;

- (NSString *)currentMode;

- (void)addTimer:(NSTimer *)timer forMode:(NSString *)mode;

/* ONE PASS: fire what is due, then answer whether the loop should keep going. The limit is NULLABLE
 * because a caller may want the pass without a deadline, which is exactly what -run does. */
- (BOOL)runMode:(NSString *)mode beforeDate:(nullable NSDate *)limit;
- (void)runUntilDate:(nullable NSDate *)limit;
- (void)run;

/*
 * THE SOURCE DOOR, AND IT IS OURS. A run loop wakes for two kinds of thing: a TIMER, which names a date,
 * and a SOURCE, which names a FILE DESCRIPTOR and says "tell me when it is ready". This class shipped
 * with timers only (§12.3's W6 named the gap), and the door is here rather than private because Apple's
 * own source door is NSPort-shaped — `-addPort:forMode:` — while `NSPort`'s message half went with
 * `NSPortMessage` and `NSPortDelegate`, both APPLE-DEPRECATED and struck by §11.5. A port scheduled here
 * is therefore a descriptor with an object wrapped round it, and the seam is the smaller honest thing.
 *
 * THE CONTRACT, FOUR PARTS:
 *   * `selector` must take NO ARGUMENTS. A source exists to say "ready now"; a caller that wants the
 *     descriptor already has it.
 *   * `target` is held WEAKLY. A run loop lives as long as its thread, so a retained target would be a
 *     leak with no owner to fix it, and a target that has gone away is DROPPED, not called (the rule
 *     NSNotificationCenter already follows).
 *   * `readable` picks which readiness is watched: YES for "there is something to read", NO for "there is
 *     room to write".
 *   * A source OUTLIVES the pass that notices it: the consumer removes it, which is what makes a
 *     one-shot read one.
 */
- (void)addSourceForFileDescriptor:(int)fd
			      mode:(NSRunLoopMode)mode
			  readable:(BOOL)readable
			    target:(id)target
			  selector:(SEL)selector;

/* Every source this target registered, in every mode. */
- (void)removeSourceForTarget:(id)target;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSRUNLOOP_H */
