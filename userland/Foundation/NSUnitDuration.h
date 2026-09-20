/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitDuration — spans of time, from picoseconds to hours. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE UNIT IS SECONDS, which is not a choice: the second is the SI base unit of time, and Apple's own
 * list runs down from hours to picoseconds by pure decimal prefixes. Every coefficient is therefore an
 * exact power of ten, and the probe can assert them without a rounding argument.
 *
 * NOT TO BE CONFUSED WITH NSDateComponentsFormatter'S DURATIONS. That class renders a span the way a LOCALE
 * says durations, in whatever units it chooses; this family is the same span as a VALUE with a unit
 * attached, and the two meet through NSMeasurement (whose components door is NSDateComponentsFormatter's).
 */

#ifndef FOUNDATION_NSUNITDURATION_H
#define FOUNDATION_NSUNITDURATION_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitDuration : NSDimension

+ (NSUnitDuration *)hours;
+ (NSUnitDuration *)minutes;
+ (NSUnitDuration *)seconds;
+ (NSUnitDuration *)milliseconds;
+ (NSUnitDuration *)microseconds;
+ (NSUnitDuration *)nanoseconds;
+ (NSUnitDuration *)picoseconds;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITDURATION_H */
