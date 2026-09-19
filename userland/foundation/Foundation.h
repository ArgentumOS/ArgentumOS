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
 *
 * THE OWNERSHIP RULE, STATED ONCE FOR THE WHOLE LIBRARY (F13.21), because it is
 * the one thing a reader of any file here needs and no single file can say:
 *
 *   THE LIBRARY IS COMPILED WITHOUT ARC, and not by preference — this system's
 *   libobjc2 uses the LEGACY runtime ABI, and clang refuses -fobjc-arc on it
 *   ("not supported on platforms using the legacy runtime"). Enabling ARC would
 *   mean changing the runtime's object layout, not adding a flag.
 *
 *   SO THE MODEL IS MANUAL, AND IT IS THE SAME IN EVERY FILE: a method STORES
 *   the pointers it is given and owns NOTHING. An object handed to the library
 *   must outlive its use of it; a collection does not keep its members alive; a
 *   delegate, target or observer is the caller's to keep. The exceptions are
 *   the classes that own C STORAGE (a buffer, a mutex, a compiled regex), and
 *   each of those frees what it allocated in -dealloc and says so in its own
 *   header.
 *
 *   THE CONSEQUENCE, WHICH IS WHY THIS IS WRITTEN DOWN: a caller must not
 *   release a pointer it has handed in until the library is done with it, and a
 *   library call that outlives its caller's scope — a thread, a timer, a queue —
 *   is the caller's to keep alive. Every file in this library was written to
 *   that rule; the four that were not were fixed when they were found.
 */

#ifndef FOUNDATION_FOUNDATION_H
#define FOUNDATION_FOUNDATION_H

#import <foundation/NSObjCRuntime.h>
#import <foundation/NSByteOrder.h>
#import <foundation/NSUUID.h>
#import <foundation/NSDateInterval.h>
#import <foundation/NSValueTransformer.h>
#import <foundation/NSAffineTransform.h>
#import <foundation/NSAutoreleasePool.h>
#import <foundation/NSObject.h>
#import <foundation/NSGeometry.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#import <foundation/NSData.h>
#import <foundation/NSDate.h>
#import <foundation/NSDateComponents.h>
#import <foundation/NSTimeZone.h>
#import <foundation/NSCalendar.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSError.h>
#import <foundation/NSException.h>
#import <foundation/NSCharacterSet.h>
#import <foundation/NSIndexSet.h>
#import <foundation/NSIndexPath.h>
#import <foundation/NSLocale.h>
#import <foundation/NSMethodSignature.h>
#import <foundation/NSInvocation.h>
#import <foundation/NSEnumerator.h>
#import <foundation/NSPropertyListSerialization.h>
#import <foundation/NSURL.h>
#import <foundation/NSKeyValueCoding.h>
#import <foundation/NSSortDescriptor.h>
#import <foundation/NSPredicate.h>
#import <foundation/NSExpression.h>
#import <foundation/NSFormatter.h>
#import <foundation/NSDateFormatter.h>
#import <foundation/NSNumberFormatter.h>
#import <foundation/NSSet.h>
#import <foundation/NSCountedSet.h>
#import <foundation/NSOrderedSet.h>
#import <foundation/NSMutableOrderedSet.h>
#import <foundation/NSValue.h>
#import <foundation/NSNull.h>
#import <foundation/NSKeyValueObserving.h>
#import <foundation/NSCoding.h>
#import <foundation/NSCoder.h>
#import <foundation/NSKeyedArchiver.h>
#import <foundation/NSProcessInfo.h>
#import <foundation/NSFileManager.h>
#import <foundation/NSURLComponents.h>
#import <foundation/NSRegularExpression.h>
#import <foundation/NSLock.h>
#import <foundation/NSThread.h>
#import <foundation/NSTimer.h>
#import <foundation/NSRunLoop.h>
#import <foundation/NSOperation.h>
#import <foundation/NSOperationQueue.h>
#import <foundation/NSProgress.h>

#endif /* FOUNDATION_FOUNDATION_H */
