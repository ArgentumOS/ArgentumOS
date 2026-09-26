/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDistributedNotificationCenter.h — the vocabulary of a notification that crosses process boundaries.
 *
 * THE CLASS IS NOT HERE, AND THE REASON IS THE SAME ONE NSItemProvider's HEADER RECORDS: a distributed
 * notification exists to carry a message between processes, and this system's interprocess story is its own (a
 * session pasteboard rather than a distributed notification bus). So the suspension behaviours, the posting
 * options, the center-type type and the names ship as the vocabulary a conforming caller compiles against, with
 * the door absent rather than stubbed.
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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDISTRIBUTEDNOTIFICATIONCENTER_H */
