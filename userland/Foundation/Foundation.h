/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * Foundation.h — the umbrella.
 *
 * A consumer writes `#import <Foundation/Foundation.h>`. The path is lower-case
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

#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSByteOrder.h>
#import <Foundation/NSUUID.h>
#import <Foundation/NSDecimal.h>
#import <Foundation/NSDecimalNumber.h>
#import <Foundation/NSDateInterval.h>
#import <Foundation/NSValueTransformer.h>
#import <Foundation/NSAffineTransform.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSProxy.h>
#import <Foundation/NSUndoManager.h>
#import <Foundation/NSJSONSerialization.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSIndexSet.h>
#import <Foundation/NSIndexPath.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSKeyValueCoding.h>
#import <Foundation/NSSortDescriptor.h>
#import <Foundation/NSPredicate.h>
#import <Foundation/NSExpression.h>
#import <Foundation/NSFormatter.h>
#import <Foundation/NSDateFormatter.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSListFormatter.h>
#import <Foundation/NSPersonNameComponents.h>
#import <Foundation/NSISO8601DateFormatter.h>
#import <Foundation/NSDateIntervalFormatter.h>
#import <Foundation/NSByteCountFormatter.h>
#import <Foundation/NSRelativeDateTimeFormatter.h>
#import <Foundation/NSDateComponentsFormatter.h>
/* W12's first slice: the unit machinery and the value that carries one. None of these four needs ICU —
 * the arithmetic is the converters' — which is why they are not in FN_FOUNDATION_ICU. */
#import <Foundation/NSUnit.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSDimension.h>
#import <Foundation/NSUnitInformationStorage.h>
#import <Foundation/NSMeasurement.h>
/* W12's dimensional families. Each is a table and a set of class methods — and none needs ICU, for the same
 * reason the machinery above needs none: the arithmetic is the converters'. */
#import <Foundation/NSUnitTemperature.h>
#import <Foundation/NSUnitDuration.h>
#import <Foundation/NSUnitLength.h>
#import <Foundation/NSUnitMass.h>
#import <Foundation/NSUnitArea.h>
#import <Foundation/NSUnitAngle.h>
#import <Foundation/NSUnitSpeed.h>
#import <Foundation/NSUnitAcceleration.h>
#import <Foundation/NSUnitFrequency.h>
#import <Foundation/NSUnitEnergy.h>
#import <Foundation/NSUnitPower.h>
#import <Foundation/NSUnitElectric.h>
#import <Foundation/NSUnitIlluminance.h>
#import <Foundation/NSUnitDispersion.h>
#import <Foundation/NSUnitFuelEfficiency.h>
#import <Foundation/NSUnitVolume.h>
#import <Foundation/NSUnitPressure.h>
#import <Foundation/NSUnitConcentrationMass.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSCountedSet.h>
#import <Foundation/NSOrderedSet.h>
#import <Foundation/NSMutableOrderedSet.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSKeyValueObserving.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSKeyedArchiver.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSURLComponents.h>
#import <Foundation/NSRegularExpression.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSTimer.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSOperation.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSProgress.h>

#endif /* FOUNDATION_FOUNDATION_H */
