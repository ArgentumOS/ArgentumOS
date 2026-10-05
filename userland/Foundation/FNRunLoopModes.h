/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNRunLoopModes.h — THE MODE RULE, IN ONE COPY (2026-09-26, plan §62.62).
 *
 * INTERNAL: not Apple's API, not in Foundation.h. It exists because THREE things now ask the same question —
 * "may this entry be delivered in the mode the loop is running?" — and they had better give the same answer:
 *
 *   * a TIMER, added for a mode (`-addTimer:forMode:`, F13.18);
 *   * a NOTIFICATION, enqueued `forModes:` (§62.61);
 *   * a PERFORMER, scheduled for modes (§62.62).
 *
 * AND THEY DID NOT, WHICH IS WHY THIS HEADER WAS WRITTEN. Timers have always treated `NSRunLoopCommonModes` as
 * "every mode" — the rule NSRunLoop.h states as "a timer added for the COMMON modes is fired by every mode ...
 * which is the whole of what 'common' means here" — while the notification queue compared mode names with
 * -isEqual:, so a notification enqueued `forModes:@[NSRunLoopCommonModes]` could NEVER be posted. That is a
 * defect the queue's unit exposed and this one fixes, by there being one function instead of two spellings.
 *
 * THE RULE, in full, and it is short: an entry fires when its mode IS the running mode, OR when its mode is the
 * COMMON modes (the common set is everything), OR when the loop is running the COMMON modes (so anything is
 * carried). nil never matches.
 */

#ifndef FOUNDATION_FNRUNLOOPMODES_H
#define FOUNDATION_FNRUNLOOPMODES_H

#import <Foundation/NSObject.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* MAY AN ENTRY SCHEDULED FOR `entryMode` BE DELIVERED WHILE THE LOOP RUNS `runningMode`? */
BOOL FNRunLoopModeAllows(NSString *entryMode, NSString *runningMode);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNRUNLOOPMODES_H */
