/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundle.m — the constants only. There is no NSBundle class in this system (see the header's note), so this
 * unit defines the notification name and the userInfo key and nothing else.
 */

#import <Foundation/NSBundle.h>

NSNotificationName const NSBundleDidLoadNotification = @"NSBundleDidLoadNotification";
NSString *const NSLoadedClasses = @"NSLoadedClasses";
