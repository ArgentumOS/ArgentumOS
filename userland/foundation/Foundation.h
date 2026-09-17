/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * Foundation.h — the umbrella.
 *
 * A consumer writes `#import <foundation/Foundation.h>`. The path is lower-case
 * on purpose: Apple's Foundation is <Foundation/…>, and the wall's gate fails
 * the build if a first-party file ever imports that spelling.
 *
 * docs/design/foundation-plan.md — F0 ships the ROOT CLASS. It ships no pool
 * class, because the RUNTIME adopts any class named NSAutoreleasePool as its own
 * pool object (§6 of the plan), so pools are the runtime's and ARC's
 * `@autoreleasepool` is the interface to them.
 */

#ifndef FOUNDATION_FOUNDATION_H
#define FOUNDATION_FOUNDATION_H

#import <foundation/NSObjCRuntime.h>
#import <foundation/NSObject.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#import <foundation/NSData.h>
#import <foundation/NSDate.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSError.h>
#import <foundation/NSException.h>

#endif /* FOUNDATION_FOUNDATION_H */
