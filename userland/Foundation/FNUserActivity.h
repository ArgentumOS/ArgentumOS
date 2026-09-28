/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNUserActivity.h — THE DELIVERY PATH FOR WHAT ARRIVES FROM ANOTHER DEVICE (§62.89). INTERNAL.
 *
 * WHY IT EXISTS: `NSUserActivityDelegate`'s two continuity doors are about a SECOND DEVICE — it continued the
 * activity, or it is asking for streams — and this system has no second device. Declaring the protocol and
 * stopping there would leave those doors with no caller (the gap §62.85 closed for the spell server's client
 * half and §62.87 and §62.88 closed for their own remote paths). These functions are that caller: they perform
 * exactly what an arriving continuation does — hand the delegate the message, with whatever the caller supplies
 * as the streams — so the path is exercisable today and a real service changes nothing but who calls it.
 */

#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

@class NSInputStream;
@class NSOutputStream;
@class NSUserActivity;

/* TELL THE ACTIVITY IT WAS CONTINUED ELSEWHERE: the delegate's `-userActivityWasContinued:` is sent, if it is
 * written. */
extern void FNUserActivityDeliverContinuation(NSUserActivity *activity);

/* HAND THE ACTIVITY ITS STREAMS: the delegate's `-userActivity:didReceiveInputStream:outputStream:` is sent with
 * the two streams the caller supplies. NOTHING IS MANUFACTURED HERE — the streams are the caller's, because a
 * stream this library invented would be a fiction a delegate could not tell from a real one. */
extern void FNUserActivityDeliverStreams(NSUserActivity *activity, NSInputStream *inputStream,
					 NSOutputStream *outputStream);

NS_ASSUME_NONNULL_END
