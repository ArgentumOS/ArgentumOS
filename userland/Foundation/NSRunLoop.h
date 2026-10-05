/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRunLoop — the loop that waits, and the place timers, sources, performers and notification posts are kept.
 * F13.18 (§62.62 added the performers and the visitors the queue needed).
 *
 * WHAT IT RUNS: TIMERS (§12.3's W6), SOURCES (§43), PERFORMERS (§62.62) and the suspended notifications a
 * thread's NSNotificationQueue holds (§62.61). A loop with ports, observers and a CoreFoundation bridge is a
 * different design at each of those words, and what is not here is named at the foot of this comment rather
 * than left to be discovered.
 *
 * THE MODES ARE NAMES, NOT MACHINES: an entry is added for a mode, and a loop running that mode carries it.
 * `NSDefaultRunLoopMode` and `NSRunLoopCommonModes` are the two Cocoa names, and an entry added for the COMMON
 * modes is carried by every mode — which is the whole of what "common" means here, and it is now ONE function
 * (§62.62, FNRunLoopModes.h) rather than a rule each kind of entry spelled for itself.
 *
 * HOW IT WAITS, and this one is a kernel fact worth stating where a caller can read it: the loop's
 * wait is `select(2)` with a timeout, because THIS KERNEL RETURNS FROM `nanosleep(2)` EARLY (measured
 * during F13.17's work). If select's timeout is not honoured either, the loop still fires on time —
 * it re-checks the clock after every wait rather than trusting it — but it will spin, and the CPU
 * cost of that is the C library's and the kernel's to fix, not this class's to hide.
 *
 * WHAT IS NOT HERE, named, AND THIS LIST SHRANK RATHER THAN GREW (§62.62 corrected it): run-loop OBSERVERS
 * (`-addObserver:forKeyPath:…` and the activity notifications), `-runLoop`/`-getCFRunLoop` — which are
 * CoreFoundation doors and this system has NO CoreFoundation, so there is nothing for them to answer (the same
 * ground §11.6.1's D13 register gives NSFileSecurity), and `NSRunLoopCommonModes` as a real mode SET rather than
 * the single name it is treated as. It no longer names the SOURCES (they landed in §43) or the PERFORMERS
 * (they land with this comment).
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

/* FORWARD-DECLARED: the ivars and the performer doors only need the names, and the implementation imports
 * NSArray.h. A port is forward-declared for the same reason AND because the dependency runs the other way:
 * NSPort.h imports THIS header, so importing it back would be a cycle. */
@class NSArray;
@class NSMutableArray;
@class NSPort;

@interface NSRunLoop : NSObject
{
	NSMutableArray *_timers;	/* NSTimer, in no particular order; the loop sorts at each wait */
	NSMutableArray *_modes;		/* the mode each timer in _timers was added for */
	NSString *_currentMode;
	BOOL _running;
	NSMutableArray *_sources;	/* FNRunLoopSource, created on first use */
	NSMutableArray *_performers;	/* FNPerformer, created on first use (§62.62) */
}

+ (NSRunLoop *)currentRunLoop;
+ (NSRunLoop *)mainRunLoop;

- (NSString *)currentMode;

- (void)addTimer:(NSTimer *)timer forMode:(NSString *)mode;

/*
 * APPLE'S PORT DOOR, AND IT IS CURRENT API RATHER THAN A DEPRECATED ONE: the class overview names port
 * objects as run-loop input sources beside mouse and keyboard events, and neither of these two methods
 * carries a deprecation. THEY WERE UNIMPLEMENTABLE UNTIL §43 — a door that takes a port has nothing to
 * take while no port class exists — and now they are the forward they were always meant to be: the port
 * is told to schedule ITSELF, and a concrete port is what decides how.
 *
 * The descriptor door above (`-addSourceForFileDescriptor:…`, W6a) is still what a caller without a
 * port wants, and it is OURS; these two are Apple's shape over the same loop.
 */
- (void)addPort:(NSPort *)aPort forMode:(NSRunLoopMode)mode;
- (void)removePort:(NSPort *)aPort forMode:(NSRunLoopMode)mode;

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

/*
 * THE PERFORMERS (§62.62), Apple's five doors, and their contract is Apple's to state — read off the pages
 * rather than recalled, because two of the clauses are the kind a guess gets wrong:
 *
 *   * "This method sets up a timer to perform the aSelector message ... AT THE START OF THE NEXT RUN LOOP
 *     ITERATION", so a performer is a REQUEST the loop honours, not a call;
 *   * "messages with a LOWER order value are sent BEFORE messages with a higher order value" (so 0 runs first);
 *   * the receiver RETAINS the target and the argument until the message is sent — an ownership rule with teeth,
 *     and the reason this class holds both rather than borrowing them;
 *   * the message is sent only while the loop is running in one of the given `modes`, and otherwise waits.
 *
 * `-cancelPerformSelector:target:argument:` requires SELECTOR AND ARGUMENT to match as well as the target;
 * `-cancelPerformSelectorsWithTarget:` cancels by TARGET ALONE and "removes the perform requests for the object
 * from ALL modes".
 *
 * THE BLOCK FORMS are macOS 10.12+ and are the same request with a block instead of a selector. Apple's own page
 * for `-performBlock:` does NOT state which modes it uses; the resolution recorded on Apple's developer forum
 * ("performBlock: is equivalent to performInModes:block: with an array containing the default run loop mode") is
 * what this implements, and it is cited here because the alternative readings would differ observably.
 */
- (void)performSelector:(SEL)aSelector
		 target:(id)target
	       argument:(nullable id)arg
		  order:(NSUInteger)order
		  modes:(NSArray *)modes;
- (void)cancelPerformSelector:(SEL)aSelector target:(id)target argument:(nullable id)arg;
- (void)cancelPerformSelectorsWithTarget:(id)target;
- (void)performBlock:(void (^)(void))block;
- (void)performInModes:(NSArray *)modes block:(void (^)(void))block;


/* §63.203: THREE DOORS. `-acceptInputForMode:beforeDate:` is this loop's one turn spelled Apple's way;
 * `-limitDateForMode:` reports the next timer due in that mode (a stated reading: the earliest fire date
 * among the timers that mode would fire, or nil); `-configureAsServer` IS A NO-OP BY APPLE'S OWN
 * DOCUMENTATION — "This method does nothing" — so implementing it as one is faithful rather than lazy.
 * `-getCFRunLoop` is NOT here: it returns the CFRunLoopRef behind the loop, and the CF core is a separate
 * accepted plan (docs/design/foundation-cf-core-plan.md). */
- (BOOL)acceptInputForMode:(NSRunLoopMode)mode beforeDate:(NSDate *)limitDate;
- (nullable NSDate *)limitDateForMode:(NSRunLoopMode)mode;
- (void)configureAsServer;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSRUNLOOP_H */
