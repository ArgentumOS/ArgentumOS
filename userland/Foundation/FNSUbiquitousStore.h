/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNSUbiquitousStore.h — THE DELIVERY PATH FOR AN EXTERNAL CHANGE (§62.87). INTERNAL.
 *
 * WHY IT EXISTS: `NSUbiquitousKeyValueStoreDidChangeExternallyNotification` is by definition about data arriving
 * from OUTSIDE, and this system has no iCloud service to bring it. Declaring the notification and stopping there
 * would leave a name nobody can post — the state §62.85 avoided for the spell server's client half by giving the
 * doors a caller. This function is that caller: it performs what a service's delivery does — merge the incoming
 * values into the store, then post the notice with the two documented keys — so the whole path is exercisable
 * today and a real service changes nothing but who calls it.
 *
 * WHAT IT DOES NOT DO IS INVENT VALUES: the caller supplies the values and the keys it changed, and the reason.
 * A quota-violating WRITE in this process uses the same path (that is Apple's behaviour too: the violation is
 * reported through the change notice), which is the one case where the "external" notice is posted by something
 * local.
 */

#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSUbiquitousKeyValueStore.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSDictionary;
@class NSString;

/* MERGES `values` INTO THE STORE, then posts `NSUbiquitousKeyValueStoreDidChangeExternallyNotification` with
 * `changedKeys` and `reason` in its userInfo.
 *
 * **`NSNull` IN THE DICTIONARY MEANS REMOVAL, WHICH CORRECTS THIS SEAM'S FIRST CONTRACT.** It said "a nil value
 * removes that key", which a caller cannot express: a dictionary does not hold a nil. `NSNull` is the spelling
 * that can be written, and it is free to mean this because it is NOT one of the property-list types a store
 * accepts — so a delivered `NSNull` cannot be a value the store would have wanted anyway. (An absent key is
 * simply not delivered.) The probe's first run is what found this: the check delivered `NSNull`, the store kept
 * the key, and the contract was the thing that was wrong. */
extern void FNSUbiquitousStoreDeliverExternalChange(NSUbiquitousKeyValueStore *store,
						    NSDictionary *values,
						    NSArray *changedKeys,
						    NSUbiquitousKeyValueStoreChangeReasons reason);

NS_ASSUME_NONNULL_END
