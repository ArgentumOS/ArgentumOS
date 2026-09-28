/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNUserNotification.h — THE SEAM BETWEEN A NOTIFICATION AND WHAT DELIVERS IT (plan §62.68). INTERNAL, like
 * FNRunLoopQueue.h: the definitions live here so the hard rule holds (nothing declared in a header without a
 * definition) while the PUBLIC surface stays Apple's.
 */

#import <Foundation/NSUserNotification.h>
#import <Foundation/NSUserNotificationCenter.h>

NS_ASSUME_NONNULL_BEGIN

/* THE PROPERTIES APPLE PUBLISHES AS READ-ONLY, which only a delivery can answer. A category rather than public
 * doors because an application that set -isPresented would be writing the delivery's own record — the thing the
 * answer is supposed to be. The centre is the only writer. */
@interface NSUserNotification (FNDelivery)
- (void)fnSetPresented:(BOOL)presented;
- (void)fnSetActualDeliveryDate:(nullable NSDate *)date;
- (void)fnSetActivationType:(NSUserNotificationActivationType)type;
- (void)fnSetResponse:(nullable NSAttributedString *)response;
- (void)fnSetAdditionalActivationAction:(nullable NSUserNotificationAction *)action;
@end

/* THE PLACE A NOTIFICATION UI PLUGS IN. An activation is something a USER does, through a service this system
 * does not have — so the entry point exists here, where a future UI layer can reach it and the acceptance probe
 * can drive it, rather than being invented on the public surface where an application would have no business
 * calling it. */
@interface NSUserNotificationCenter (FNActivation)
- (void)fnActivateNotification:(NSUserNotification *)notification
		      withType:(NSUserNotificationActivationType)type
		      response:(nullable NSAttributedString *)response
	      additionalAction:(nullable NSUserNotificationAction *)action;
@end

NS_ASSUME_NONNULL_END
