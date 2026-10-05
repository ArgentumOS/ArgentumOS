/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDistributedNotificationCenter.h — a notification center whose notifications CAN cross process boundaries, and
 * the boundary this system draws under that.
 *
 * THE CLASS SHIPS, AND THE DECISION THAT KEPT IT OUT IS REVERSED — the same reversal §62.23 made for
 * the removed post-baseline families, and for the same reason: "the door is absent rather than stubbed" is the right rule for a door,
 * and the wrong one for a CLASS whose LOCAL half is real. What is real here is everything a process can do with a
 * notification center on its own: observers with per-observation SUSPENSION BEHAVIOURS (drop, hold, coalesce, or
 * deliver anyway), a suspended center that holds or coalesces what arrives, and the resume that flushes it.
 *
 * WHAT IS NOT HERE IS THE BUS, AND THE DOORS THAT EXIST ONLY FOR IT SAY SO BY NAME: `+notificationCenterForType:`
 * answers this process's center for `NSLocalNotificationCenterType` and REFUSES any other type — this system's
 * interprocess story is its own (a session pasteboard, not a distributed notification bus) — and the
 * `NSDistributedNotificationPostToAllSessions` option is REFUSED for the same ground. A notification posted here
 * is delivered to THIS process's observers and leaves nowhere else, which is stated rather than implied.
 *
 * THE VALUES FOLLOW THE CONVENTION THE NSNOTIFICATION LANDING ESTABLISHED for notification-name constants: each
 * answers its OWN NAME. That is a recorded deviation from Apple's headers, where these are the literal
 * @"NS...Notification" strings (see NSNotification.h for the reasoning), and it is the only spelling that can be
 * consistent before anything posts one.
 */

#ifndef FOUNDATION_NSDISTRIBUTEDNOTIFICATIONCENTER_H
#define FOUNDATION_NSDISTRIBUTEDNOTIFICATIONCENTER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>	/* the superclass, whose machinery this class routes into */

NS_ASSUME_NONNULL_BEGIN

/* APPLE SPELLS THESE TWO SETS DIFFERENTLY AND THE DIFFERENCE IS REAL: NSNotificationSuspensionBehavior is what a
 * center does with a notification while delivery is suspended, and NSDistributedNotificationOptions is what a
 * poster asks for. Both are bit sets here, as their names say. */
typedef enum {
	NSNotificationSuspensionBehaviorDrop = 1 << 0,
	NSNotificationSuspensionBehaviorCoalesce = 1 << 1,
	NSNotificationSuspensionBehaviorHold = 1 << 2,
	NSNotificationSuspensionBehaviorDeliverImmediately = 1 << 3
} NSNotificationSuspensionBehavior;

typedef enum {
	NSDistributedNotificationDeliverImmediately = 1 << 0,
	NSDistributedNotificationPostToAllSessions = 1 << 1
} NSDistributedNotificationOptions;

/* The CENTER TYPE: a value a caller passes to name which center it means. Declared as the string type Apple
 * declares, with the one value this vocabulary carries. */
typedef NSString *NSDistributedNotificationCenterType;

extern NSDistributedNotificationCenterType const NSLocalNotificationCenterType;
extern NSString *const NSNotificationDeliverImmediately;
extern NSString *const NSNotificationPostToAllSessions;


/* THE CENTER. It is an NSNotificationCenter (Apple's own inheritance) whose registry carries one more thing per
 * observation than the superclass's: WHAT TO DO WITH A NOTIFICATION THAT ARRIVES WHILE DELIVERY IS SUSPENDED.
 * The superclass's own storage is never populated by this class - every door below routes into this one's registry
 * - so the two halves cannot disagree. */
@class NSDictionary;
@class NSMutableArray;
@class NSNotification;

@interface NSDistributedNotificationCenter : NSNotificationCenter
{
@private
	NSMutableArray *_observations;		/* the records: observer, selector, name, object, behaviour */
	NSMutableArray *_pending;		/* what a suspended center is holding, in arrival order */
	BOOL _suspended;
}

/* THIS PROCESS'S CENTER. Apple's `+defaultCenter` is the notification center's; this one is its own, which is what
 * the type door below returns for the local type. */
+ (NSDistributedNotificationCenter *)defaultCenter;

/* THE TYPE DOOR: the LOCAL type is this process's center, and any other type is REFUSED BY NAME because this
 * system has no distributed bus to name. */
+ (NSDistributedNotificationCenter *)notificationCenterForType:(NSDistributedNotificationCenterType)centerType;

/* OBSERVING, with the behaviour that decides what happens to a notification arriving while suspended. The
 * inherited three-argument door registers with Apple's default for this class, COALESCE. */
- (void)addObserver:(id)observer
	   selector:(SEL)selector
	       name:(nullable NSString *)name
	     object:(nullable id)object
suspensionBehavior:(NSNotificationSuspensionBehavior)behavior;
- (void)addObserver:(id)observer
	   selector:(SEL)selector
	       name:(nullable NSString *)name
	     object:(nullable id)object;
- (void)removeObserver:(id)observer name:(nullable NSString *)name object:(nullable id)object;
- (void)removeObserver:(id)observer;

/* POSTING. `deliverImmediately:` and the options door both reach the same engine; the options door refuses the
 * CROSS-SESSION option by name and honours the immediate one. */
- (void)postNotificationName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo
	   deliverImmediately:(BOOL)deliverImmediately;
- (void)postNotificationName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo
		     options:(NSDistributedNotificationOptions)options;

/* SUSPENSION: while suspended, an arriving notification is dropped, held or coalesced according to the observing
 * record's behaviour - and a record that asked for DELIVER_IMMEDIATELY still receives it. Resuming flushes what
 * was held. */
- (void)setSuspended:(BOOL)suspended;


/* §63.235: SUSPENSION IS THE DELIVERY GATE, not a flag nothing reads: this class's own posting path consults it,
 * so a suspended centre queues nothing to its observers until it is resumed — Apple's contract for the door. */
@property (getter=isSuspended) BOOL suspended;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDISTRIBUTEDNOTIFICATIONCENTER_H */
