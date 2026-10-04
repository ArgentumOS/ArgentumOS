/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotificationCenter — the registry and the delivery (W4).
 *
 * A REGISTRATION IS A FILTER PAIR, and that is the whole model: `name` and `object` may each be nil, and
 * nil means "this criterion is not used". Name-only receives every notification with that name from
 * anyone; object-only receives every notification from that sender; both-nil receives everything. THE
 * POSTER DOES NOT CHOOSE ITS AUDIENCE — the filters do, which is why -postNotification: takes a whole
 * notification rather than a name.
 *
 * DELIVERY IS SYNCHRONOUS, on the posting thread, and the order is not promised (Apple says so, and it
 * keeps the snapshot below honest). The BLOCK form's `queue:` parameter is how a caller asks for
 * elsewhere: nil runs the block right there, a queue hands it to that queue.
 *
 * OBSERVERS ARE HELD WEAKLY (zeroing weak, through the runtime), so a centre never keeps an observer
 * alive and an observer that is deallocated without removing itself is SKIPPED rather than messaged.
 * The object FILTER is weak for the same reason. THE BLOCK FORM'S TOKEN MUST STILL BE REMOVED BY HAND —
 * Apple's own caveat — because nothing else can know when that observation is finished with.
 */

#ifndef FOUNDATION_NSNOTIFICATIONCENTER_H
#define FOUNDATION_NSNOTIFICATIONCENTER_H

#import <Foundation/NSObject.h>

@class NSDictionary;
@class NSLock;
@class NSMutableArray;
@class NSNotification;
@class NSOperationQueue;

NS_ASSUME_NONNULL_BEGIN

@interface NSNotificationCenter : NSObject
{
	NSMutableArray *_observers;
	NSLock *_lock;
}

/* THE PROCESS'S CENTRE. One per process, made once, and the one system notifications are posted to. */
+ (NSNotificationCenter *)defaultCenter;

/* THE SELECTOR FORM: the observer is messaged with the notification and must take exactly one
 * argument. Registering the same observer twice is allowed (Apple says so) and delivers twice. */
- (void)addObserver:(id)observer
	   selector:(SEL)selector
	       name:(nullable NSString *)name
	     object:(nullable id)object;

/* THE BLOCK FORM, and its return value is the price of it: THE TOKEN MUST BE HANDED BACK to
 * -removeObserver: when the observation is no longer wanted. The block is COPIED and the centre holds
 * it, and `queue` is where it runs — nil meaning the posting thread, synchronously. */
- (id)addObserverForName:(nullable NSString *)name
		  object:(nullable id)object
		   queue:(nullable NSOperationQueue *)queue
	      usingBlock:(void (^)(NSNotification *notification))block;

/* POSTING. -postNotification: is the general door; the two name-taking forms build the notification for
 * a caller that has no other use for it, which is the common case. */
- (void)postNotification:(NSNotification *)notification;
- (void)postNotificationName:(NSString *)name object:(nullable id)object;
- (void)postNotificationName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo;

/* REMOVING, the specific door first: `name` and `object` filter which of this observer's registrations
 * go, and nil means "not used as a criterion" — the same reading they have when registering. */
- (void)removeObserver:(id)observer;
- (void)removeObserver:(id)observer name:(nullable NSString *)name object:(nullable id)object;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNOTIFICATIONCENTER_H */
