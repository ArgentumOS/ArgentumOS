/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotification — the container a notification centre broadcasts (W4).
 *
 * IT IS A VALUE, and that is the whole class: a NAME (what happened), an OBJECT (who says so — the
 * sender, which is why a userInfo dictionary exists for everything else), and an optional userInfo
 * dictionary. It is IMMUTABLE, so -copy answers the receiver and the centre can hand the same object to
 * every observer.
 *
 * WHAT IS NOT HERE, named: `-initWithCoder:` (the coder conformance work other classes register), and
 * the `NSNotificationName` TYPEDEF Apple declares beside it — a typealias with no behaviour, which the
 * ledger carries as its own row rather than as part of this class.
 */

#ifndef FOUNDATION_NSNOTIFICATION_H
#define FOUNDATION_NSNOTIFICATION_H

#import <Foundation/NSObject.h>

@class NSDictionary;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSNotification : NSObject <NSCopying>
{
	NSString *_name;
	id _object;
	NSDictionary *_userInfo;
}

+ (instancetype)notificationWithName:(NSString *)name object:(nullable id)object;
+ (instancetype)notificationWithName:(NSString *)name
			      object:(nullable id)object
			    userInfo:(nullable NSDictionary *)userInfo;

/* THE DESIGNATED DOOR. The notification OWNS its three parts: the name and the userInfo are copied
 * (they are values the poster must not be able to change underneath the observers) and the object is
 * retained, because the object is the sender's statement of identity for as long as the notification
 * lives — which, for a posted one, is the delivery. */
- (instancetype)initWithName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo;

@property (readonly, copy) NSString *name;
@property (readonly, nullable) id object;
@property (readonly, copy, nullable) NSDictionary *userInfo;

@end

/* APPLE'S TYPE FOR A NOTIFICATION NAME, declared beside the names that are spelled with it. It is a typedef
 * with no behaviour - the type Apple declares for the same constants. */
typedef NSString *NSNotificationName;

/* ---- THE NAMES OF NOTIFICATIONS THIS SYSTEM DOES NOT POST (the coverage slice) ----------------------
 *
 * ONE DELIBERATE SIMPLIFICATION, stated because it is a deviation: in Apple's headers these values are the
 * literal @"NS....Notification" strings AND they are also the CF/AppKit spellings; here every name's value IS
 * its own name, which is this library's existing convention for the locale keys (NSLocale.h) and the only one
 * that can be consistent before anything posts or observes them. NOTHING IN THIS SYSTEM POSTS THESE, so a
 * caller that observes one waits forever - which is why they are here as vocabulary with that said out loud. */
extern NSNotificationName const NSAppleEventManagerWillProcessFirstEventNotification;
extern NSNotificationName const NSClassDescriptionNeededForClassNotification;
extern NSNotificationName const NSExtensionHostDidBecomeActiveNotification;
extern NSNotificationName const NSExtensionHostDidEnterBackgroundNotification;
extern NSNotificationName const NSExtensionHostWillEnterForegroundNotification;
extern NSNotificationName const NSExtensionHostWillResignActiveNotification;
extern NSNotificationName const NSMetadataQueryDidFinishGatheringNotification;
extern NSNotificationName const NSMetadataQueryDidStartGatheringNotification;
extern NSNotificationName const NSMetadataQueryDidUpdateNotification;
extern NSNotificationName const NSMetadataQueryGatheringProgressNotification;
extern NSNotificationName const NSProcessInfoPowerStateDidChangeNotification;
extern NSNotificationName const NSSystemClockDidChangeNotification;
extern NSNotificationName const NSSystemTimeZoneDidChangeNotification;
extern NSNotificationName const NSUbiquityIdentityDidChangeNotification;

/* ---- THE NOTIFICATION QUEUE'S VOCABULARY, AND THE QUEUE'S ABSENCE NAMED (the coverage slice) ----------
 *
 * Apple declares these two sets in THIS header, beside the queue class they configure, so they belong here.
 * THE CLASS ITSELF IS NOT DECLARED IN THIS SYSTEM, AND THE REASON IS RECORDED RATHER THAN IMPLIED: a queue
 * delivers a notification at a point in the RUN LOOP - idle, or as soon as possible without blocking - and this
 * tree's run loop exposes no phase seam to hang that on yet. So the styles and the coalescing policies ship as
 * the vocabulary a conforming caller compiles against, and the queue waits for the seam.
 */
typedef enum {
	NSPostASAP = 1 << 0,
	NSPostWhenIdle = 1 << 1,
	NSPostNow = 1 << 2
} NSPostingStyle;

typedef enum {
	NSNotificationNoCoalescing = 0,
	NSNotificationCoalescingOnName = 1 << 0,
	NSNotificationCoalescingOnSender = 1 << 1
} NSNotificationCoalescing;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNOTIFICATION_H */
