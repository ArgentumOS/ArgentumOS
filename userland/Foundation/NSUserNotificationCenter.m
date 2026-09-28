/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserNotificationCenter.m — the registry and its clock (§62.68). MANUAL OWNERSHIP.
 *
 * ONE ARRAY AND ONE TIMER PER ENTRY, kept in step: `_scheduled` and `_timers` share their indices, so every door
 * that moves a notification in or out of the queue moves its timer in the same operation. That is the whole
 * design — the queue's ORDER is Apple's promise ("added to the end of the array"), and the timers exist only to
 * carry out the order the queue already states.
 *
 * EVERY TIMER IS ARMED ON THE RUN LOOP THAT SCHEDULED IT (`-currentRunLoop`), because that is the thread the
 * notification was handed to and the only clock this system offers. A `-deliveryDate` that is nil or already past
 * means "due on the next pass", and the FLOOR §62.66 measured applies here for the same reason: a timer whose
 * interval is zero is already due, and adding it to the loop that is walking its timer list fires it inside the
 * same pass. A single-shot timer that REMOVES ITSELF when it fires cannot spin the loop the way the background
 * activity could, but the floor keeps a nil date from firing before the caller's own statement has returned.
 */

#import <Foundation/NSUserNotificationCenter.h>
#import <Foundation/NSUserNotification.h>
#import <Foundation/FNUserNotification.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSString.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSTimer.h>

/* THE SMALLEST GAP A SCHEDULED DELIVERY CAN HAVE (see the header comment). */
#define FN_NOTIFICATION_MIN_DELAY 0.001

/* THE FOUR STEPS THE PUBLIC DOORS ARE BUILT FROM, each used before its definition and none of them public: the
 * record a delivery leaves, arming a notification's timer, what a timer does when it fires, and a repeat. */
@interface NSUserNotificationCenter ()
- (void)fnRecordDeliveryOf:(NSUserNotification *)notification presented:(BOOL)presented;
- (NSTimer *)fnArmTimerFor:(NSUserNotification *)notification;
- (void)fnScheduledDelivery:(NSTimer *)timer;
- (void)fnScheduleRepeatOf:(NSUserNotification *)notification;
@end

@implementation NSUserNotificationCenter

+ (NSUserNotificationCenter *)defaultUserNotificationCenter
{
	static NSUserNotificationCenter *shared = nil;

	if (shared == nil) {
		shared = [[NSUserNotificationCenter alloc] init];
	}
	return shared;
}

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_delivered = [[NSMutableArray alloc] init];
		_scheduled = [[NSMutableArray alloc] init];
		_timers = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[_delivered release];
	[_scheduled release];
	[_timers release];
	[super dealloc];
}

- (nullable id<NSUserNotificationCenterDelegate>)delegate { return _delegate; }

- (void)setDelegate:(nullable id<NSUserNotificationCenterDelegate>)delegate
{
	_delegate = delegate;		/* NOT retained, and not released: the delegate is not ours */
}

/* ---- DELIVERY -------------------------------------------------------------------------------------------- */

/* PRESENT AND RECORD, and PRESENTED IS ALWAYS YES — Apple's sentence about this door, not a reading of it: "The
 * isPresented property of the NSUserNotification object will always be set to true if a notification is delivered
 * using this method." The delivery DATE is this class's own answer to "when did it go", and the delegate is told
 * afterwards, when the record is already consistent. */
- (void)deliverNotification:(NSUserNotification *)notification
{
	[self fnRecordDeliveryOf:notification presented:YES];
}

- (void)fnRecordDeliveryOf:(NSUserNotification *)notification presented:(BOOL)presented
{
	[notification fnSetPresented:presented];
	[notification fnSetActualDeliveryDate:[NSDate date]];
	[_delivered addObject:notification];
	if ([_delegate respondsToSelector:@selector(userNotificationCenter:didDeliverNotification:)]) {
		[_delegate userNotificationCenter:self didDeliverNotification:notification];
	}
}

- (NSArray *)deliveredNotifications
{
	return [[_delivered copy] autorelease];
}

- (void)removeDeliveredNotification:(NSUserNotification *)notification
{
	NSUInteger index = [_delivered indexOfObjectIdenticalTo:notification];

	if (index != NSNotFound) {
		[_delivered removeObjectAtIndex:index];
	}
}

- (void)removeAllDeliveredNotifications
{
	[_delivered removeAllObjects];
}

/* ---- SCHEDULING ------------------------------------------------------------------------------------------ */

- (void)scheduleNotification:(NSUserNotification *)notification
{
	[_scheduled addObject:notification];
	[_timers addObject:[self fnArmTimerFor:notification]];
}

/* THE TIMER THAT CARRIES OUT THE QUEUE'S PROMISE: one shot, at the notification's own -deliveryDate, on this
 * thread's run loop. */
- (NSTimer *)fnArmTimerFor:(NSUserNotification *)notification
{
	NSDate *when = [notification deliveryDate];
	NSTimeInterval delay = when != nil ? [when timeIntervalSinceNow] : 0.0;
	NSTimer *timer;

	if (delay < FN_NOTIFICATION_MIN_DELAY) {
		delay = FN_NOTIFICATION_MIN_DELAY;
	}
	timer = [NSTimer timerWithTimeInterval:delay
					target:self
				      selector:@selector(fnScheduledDelivery:)
				      userInfo:notification
				       repeats:NO];
	[[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
	return timer;
}

/* A SCHEDULED DELIVERY ARRIVES, and the first thing it does is leave the queue: the notification is no longer
 * "scheduled but not yet delivered", which is what the array promises. A delivery that arrives after
 * -removeScheduledNotification: has already taken it out finds it gone and does nothing — Apple's own note says a
 * removal cannot promise otherwise ("if the user notification's [date] occurs before the cancellation finishes,
 * the notification may still be delivered"), and the reverse case is the one this code decides: no queue entry,
 * no delivery.
 *
 * THE PRESENTATION DECISION IS OURS (see the header): the delegate is asked, and only a delegate that says NO
 * prevents presentation — a delegate that does not implement the door means present. */
- (void)fnScheduledDelivery:(NSTimer *)timer
{
	NSUserNotification *notification = [timer userInfo];
	NSUInteger index = [_scheduled indexOfObjectIdenticalTo:notification];
	BOOL present = YES;

	if (index == NSNotFound) {
		return;		/* removed while the timer was outstanding */
	}
	[_scheduled removeObjectAtIndex:index];
	[_timers removeObjectAtIndex:index];
	if ([_delegate respondsToSelector:@selector(userNotificationCenter:shouldPresentNotification:)]) {
		present = [_delegate userNotificationCenter:self shouldPresentNotification:notification];
	}
	[self fnRecordDeliveryOf:notification presented:present];
	[self fnScheduleRepeatOf:notification];
}

/* A REPEAT IS A CALENDAR QUESTION, so it goes to the shipped NSCalendar rather than to arithmetic: the components
 * are added to the delivery date through the calendar, and the result is queued again with its timer. There is no
 * repeat when `-deliveryRepeatInterval` is unset, and a notification with no delivery date has no date to repeat
 * FROM — it is delivered once and dropped, rather than repeating "every day" from an unstated now. */
- (void)fnScheduleRepeatOf:(NSUserNotification *)notification
{
	NSDateComponents *interval = [notification deliveryRepeatInterval];
	NSDate *from = [notification deliveryDate];
	NSDate *next;
	NSCalendar *calendar;

	if (interval == nil || from == nil) {
		return;
	}
	calendar = [NSCalendar currentCalendar];
	if (calendar == nil) {
		return;
	}
	next = [calendar dateByAddingComponents:interval toDate:from options:NSCalendarOptionsNone];
	if (next == nil || [next timeIntervalSinceNow] <= 0.0) {
		return;		/* an interval that does not move the date forward would repeat forever */
	}
	[notification setDeliveryDate:next];
	[notification fnSetPresented:NO];
	[self scheduleNotification:notification];
}

- (void)removeScheduledNotification:(NSUserNotification *)notification
{
	NSUInteger index = [_scheduled indexOfObjectIdenticalTo:notification];

	/* "If the notification is not in the scheduled list, nothing happens" — Apple's own sentence, and the reason
	 * this door is quiet rather than an error. */
	if (index == NSNotFound) {
		return;
	}
	[(NSTimer *)[_timers objectAtIndex:index] invalidate];
	[_timers removeObjectAtIndex:index];
	[_scheduled removeObjectAtIndex:index];
}

- (NSArray *)scheduledNotifications
{
	return [[_scheduled copy] autorelease];
}

/* BULK SETTING UNSCHEDULES WHAT WAS THERE — Apple's sentence: "Bulk setting new scheduled notifications
 * unschedules existing notifications." So the old timers are invalidated and the new array is adopted as the
 * queue, in the order it was given. */
- (void)setScheduledNotifications:(NSArray *)notifications
{
	NSUInteger i;

	for (i = 0; i < [_timers count]; i++) {
		[(NSTimer *)[_timers objectAtIndex:i] invalidate];
	}
	[_timers removeAllObjects];
	[_scheduled removeAllObjects];
	for (i = 0; i < [notifications count]; i++) {
		NSUserNotification *notification = [notifications objectAtIndex:i];

		[_scheduled addObject:notification];
		[_timers addObject:[self fnArmTimerFor:notification]];
	}
}

@end

/* THE ACTIVATION SEAM (FNUserNotification.h): what a user's click would reach, when a UI layer exists to send
 * one. The type, the response and the additional action are all recorded ON THE NOTIFICATION — which is what the
 * read-only properties are for — and then the delegate is told. An activation is also a delivery: the record says
 * so, because Apple's page for the activation door describes notifications that have been "delivered". */
@implementation NSUserNotificationCenter (FNActivation)

- (void)fnActivateNotification:(NSUserNotification *)notification
		      withType:(NSUserNotificationActivationType)type
		      response:(nullable NSAttributedString *)response
	      additionalAction:(nullable NSUserNotificationAction *)action
{
	[notification fnSetActivationType:type];
	[notification fnSetResponse:response];
	[notification fnSetAdditionalActivationAction:action];
	if ([_delivered indexOfObjectIdenticalTo:notification] == NSNotFound) {
		[self fnRecordDeliveryOf:notification presented:YES];
	}
	if ([_delegate respondsToSelector:@selector(userNotificationCenter:didActivateNotification:)]) {
		[_delegate userNotificationCenter:self didActivateNotification:notification];
	}
}

@end
