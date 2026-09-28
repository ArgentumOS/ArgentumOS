/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNBundleResourceRequest.h — THE DELIVERY PATH FOR THE LOW-DISK-SPACE NOTICE (§62.88). INTERNAL.
 *
 * WHY IT EXISTS: `NSBundleResourceRequestLowDiskSpaceNotification` is posted BY THE SYSTEM when it cannot free
 * enough space for a resource request, and this system has no such system. Declaring the name and stopping there
 * would leave an app's registration and its recovery path (drop the low-priority requests) untestable — the same
 * gap §62.85 and §62.87 closed for their own remote halves. This function posts the notice, so the path is
 * exercisable now and a real storage service changes nothing but who calls it.
 */

#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

/* Posts `NSBundleResourceRequestLowDiskSpaceNotification` to the default notification centre with NO object and
 * no userInfo — which is how Apple's own example registers for it (`object:nil`). */
extern void FNBundleResourceRequestDeliverLowDiskSpace(void);

NS_ASSUME_NONNULL_END
