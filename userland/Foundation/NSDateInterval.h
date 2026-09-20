/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateInterval.h — a span of time (W2h, docs/design/foundation-plan.md §14).
 *
 * A VALUE WITH AN INVARIANT: the end is never before the start, so a caller never has to ask in
 * which order a pair of dates was handed over. The two constructors disagree about WHICH fact is
 * primary — one is given the duration and computes the end, the other is given the end and computes
 * the duration — and both are exact because they are the same subtraction read in two directions.
 *
 * WHAT IS NOT, named: `NSSecureCoding` conformance (the protocol is not in this library yet, and
 * this class does not pretend to it).
 */
#ifndef FOUNDATION_NSDATEINTERVAL_H
#define FOUNDATION_NSDATEINTERVAL_H

#import <Foundation/NSDate.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSDateInterval : NSObject <NSCopying>
{
	NSDate *_startDate;
	NSDate *_endDate;
	NSTimeInterval _duration;
}

/* BOTH ARE EXACT, and both RAISE for an interval that cannot exist: a negative duration, or an end
 * before the start. An interval whose ends are out of order is not an interval, and answering one
 * would push the question onto every reader. */
- (instancetype)initWithStartDate:(NSDate *)startDate duration:(NSTimeInterval)duration;
- (instancetype)initWithStartDate:(NSDate *)startDate endDate:(NSDate *)endDate;

@property (readonly, copy) NSDate *startDate;
@property (readonly, copy) NSDate *endDate;
@property (readonly) NSTimeInterval duration;

- (NSComparisonResult)compare:(NSDateInterval *)dateInterval;
- (BOOL)isEqualToDateInterval:(NSDateInterval *)dateInterval;
- (BOOL)intersectsDateInterval:(NSDateInterval *)dateInterval;

/* NIL WHEN THEY DO NOT OVERLAP, and that is the whole reason the method is not `-union` in
 * disguise: an empty intersection is not an interval. */
- (nullable NSDateInterval *)intersectionWithDateInterval:(NSDateInterval *)dateInterval;
- (BOOL)containsDate:(NSDate *)date;

- (NSString *)description;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSDATEINTERVAL_H */
