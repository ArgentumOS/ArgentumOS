/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDistributedNotificationCenter.m — the constants only. There is no distributed notification center in this
 * system (see the header's note), so this unit defines the names and the center type and nothing else.
 */

#import <Foundation/NSDistributedNotificationCenter.h>

/* Every name answers its own name: the convention NSNotification.h records, applied to this family. */
NSDistributedNotificationCenterType const NSLocalNotificationCenterType = @"NSLocalNotificationCenterType";
NSString *const NSNotificationDeliverImmediately = @"NSNotificationDeliverImmediately";
NSString *const NSNotificationPostToAllSessions = @"NSNotificationPostToAllSessions";
