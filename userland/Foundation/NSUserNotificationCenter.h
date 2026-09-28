/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSUserNotificationCenter + NSUserNotificationCenterDelegate — WHERE NOTIFICATIONS GO (plan §62.68).
 * APPLE-DEPRECATED AND IN SCOPE (§62.24), for the reason §62.67 states.
 *
 * THE TWO HALVES OF THIS CLASS ARE APPLE'S AND THEY ARE NOT SYMMETRIC, which is the first thing this file had
 * to get right rather than assume:
 *
 *   * `-deliverNotification:` PRESENTS, and Apple's page says so unconditionally: "The `isPresented` property of
 *     the `NSUserNotification` object will ALWAYS be set to true if a notification is delivered using this
 *     method." So a delivery through that door answers -isPresented YES whatever anyone's preferences say;
 *   * `-scheduleNotification:` QUEUES, and the queue is the array -scheduledNotifications reads: "Specifies an
 *     array of scheduled user notifications that have not yet been delivered. Newly scheduled notifications are
 *     added to the end of the array. You may also bulk-schedule notifications by SETTING this array. Bulk setting
 *     new scheduled notifications UNSCHEDULES existing notifications."
 *
 * SO THE ENGINE IS ONE ONE-SHOT TIMER PER SCHEDULED NOTIFICATION, armed for its `-deliveryDate` on the run loop
 * that scheduled it — the same shape §62.66 landed, and for the same reason: this library's run loop is the only
 * clock a notification can be due against. A delivery through a timer follows `-deliveryRepeatInterval`, when
 * there is one, by asking the SHIPPED NSCalendar to add the components — a repeat is a calendar question, not an
 * arithmetic one.
 *
 * AND ONE RULE IS OURS, MARKED AS SUCH (§11.6.1 D2), because Apple's delegation door is defined in terms of a
 * fact this system does not have: `-userNotificationCenter:shouldPresentNotification:` is documented as being
 * sent "when the user notification center has DECIDED NOT TO PRESENT your notification" — a decision Apple's
 * centre makes from the frontmost application. **THIS SYSTEM HAS NO FRONTMOST-APPLICATION SIGNAL, SO THE DELEGATE
 * IS THE SIGNAL:** a notification that comes due on the scheduled path is presented unless its delegate refuses
 * it, and a delegate that does not implement the door means present. (A `-deliverNotification:` call is presented
 * regardless, per Apple's own sentence above, so the two doors do not contradict each other.)
 *
 * WHAT THIS CLASS HONESTLY DOES NOT HAVE, stated rather than stubbed: there is no presentation service — nothing
 * draws the notification, so "presented" is a RECORD this class keeps and what that record is good for is the
 * caller's business; `-isRemote` is always NO because remote notifications would need a push service; and an
 * ACTIVATION arrives through the internal seam in FNUserNotification.h, where a future UI layer plugs in, because
 * an activation is something a user does through a service that does not exist yet.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSMutableArray;
@class NSUserNotification;
@class NSUserNotificationCenter;

/* THE THREE THINGS AN APPLICATION CAN BE TOLD OR ASKED, and all three are OPTIONAL: a delegate that implements
 * one door is not required to implement the others, which is what `@optional` states here and what Apple's own
 * header states. The centre asks before calling. */
@protocol NSUserNotificationCenterDelegate <NSObject>
@optional
- (void)userNotificationCenter:(NSUserNotificationCenter *)center
	    didDeliverNotification:(NSUserNotification *)notification;
- (void)userNotificationCenter:(NSUserNotificationCenter *)center
	  didActivateNotification:(NSUserNotification *)notification;
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
	shouldPresentNotification:(NSUserNotification *)notification;
@end

@interface NSUserNotificationCenter : NSObject
{
@private
	id _delegate;						/* NOT retained: a centre that owned its delegate would be a cycle */
	NSMutableArray *_delivered;
	NSMutableArray *_scheduled;
	NSMutableArray *_timers;				/* one per scheduled notification, in the same order */
}

/* THE ONE CENTRE. Apple's page calls it "the default user notification center"; the class is a singleton in the
 * same sense NSNotificationCenter's default is, and there is no per-application one to make. */
+ (NSUserNotificationCenter *)defaultUserNotificationCenter;

/* THE DELEGATE, held WITHOUT retention (an `unowned(unsafe)` reference in Apple's shape), because the delegate
 * is usually the object the centre is describing the delivery TO. */
- (nullable id<NSUserNotificationCenterDelegate>)delegate;
- (void)setDelegate:(nullable id<NSUserNotificationCenterDelegate>)delegate;

/* DELIVERY: present now, and record it. The record is -deliveredNotifications, newest last. */
- (void)deliverNotification:(NSUserNotification *)notification;
- (NSArray *)deliveredNotifications;
- (void)removeDeliveredNotification:(NSUserNotification *)notification;
- (void)removeAllDeliveredNotifications;

/* SCHEDULING: the queue of notifications that have not been delivered yet, and the doors that move one in or
 * out of it. Setting -scheduledNotifications REPLACES the queue — Apple's sentence, and the reason the setter is
 * here rather than only the adder. */
- (void)scheduleNotification:(NSUserNotification *)notification;
- (void)removeScheduledNotification:(NSUserNotification *)notification;
- (NSArray *)scheduledNotifications;
- (void)setScheduledNotifications:(NSArray *)notifications;

@end

NS_ASSUME_NONNULL_END
