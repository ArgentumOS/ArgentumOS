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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSNOTIFICATION_H */
