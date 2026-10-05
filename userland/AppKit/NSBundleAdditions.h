/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundleAdditions.h — AppKit's category on NSBundle (§63.52). MOVED HERE FROM FOUNDATION.
 *
 * ⚠ WHY THEY ARE NOT FOUNDATION'S, MEASURED RATHER THAN ASSUMED (the user's directive, 2026-10-01: *"removing
 * from our Foundation implementation anything which does not belong in Foundation … CoreGraphics or the like
 * are not"*): `-pathForImageResource:` and `-URLForImageResource:` are declared by **AppKit's `NSImage.h`** and
 * `-pathForSoundResource:` by **AppKit's `NSSound.h`** — the macOS 14.5 SDK headers settle it, and Apple's own
 * doc index has **no rows for any of the three**, which is why the ledger never asked for them and why they sat
 * in Foundation unchallenged. They are AppKit's because what a "image resource" and a "sound resource" ARE is
 * decided by `NSImage` and `NSSound`, and neither class is Foundation's.
 *
 * ⚠ AND THEY LIVE IN ONE HEADER HERE WHERE APPLE SPLITS THEM ACROSS TWO, which is a NAMED DEVIATION rather
 * than an oversight: Apple puts the image pair in `NSImage.h` and the sound door in `NSSound.h`, and **this tier
 * has no `NSSound`** — inventing one to hold a one-line delegation would be inventing a class. The intended
 * home, and the reason, are written here so the split can happen when a real `NSSound` arrives.
 */

#ifndef APPKIT_NSBUNDLEADDITIONS_H
#define APPKIT_NSBUNDLEADDITIONS_H

#import <Foundation/NSBundle.h>

NS_ASSUME_NONNULL_BEGIN

/* The name parameter is Apple's `NSImageName` / `NSSoundName`, both of which are `typedef`s of `NSString`. */
@interface NSBundle (NSBundleAppKitAdditions)

/* THE NAME'S EXTENSION IS OPTIONAL, which is the whole point of the door: `@"pic"` finds `pic.png` and
 * `@"logo.png"` finds itself (the literal name wins). A miss is nil. */
- (nullable NSString *)pathForImageResource:(NSString *)name;
- (nullable NSURL *)URLForImageResource:(NSString *)name;
- (nullable NSString *)pathForSoundResource:(NSString *)name;

@end

NS_ASSUME_NONNULL_END

#endif /* APPKIT_NSBUNDLEADDITIONS_H */
