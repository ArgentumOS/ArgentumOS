/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDate — a point in time, as seconds since the Unix epoch.
 * docs/design/foundation-plan.md, F2; the copying family and NSComparisonResult
 * from the public-API audit.
 *
 * No calendar, no time zones, no locales in v1: a date is a number, comparisons
 * are number comparisons, and `-description` renders it in UTC using libc's own
 * clock conversion. Everything a full calendar would need is a later fidelity
 * slice — and there is no reason to hand-roll what libc already has.
 */

#ifndef FOUNDATION_NSDATE_H
#define FOUNDATION_NSDATE_H

#import <Foundation/NSObject.h>

/* COCOA'S TIME TYPE, and this is where Cocoa declares it too. It was MISSING until a header named it
 * as a return type and the compiler asked what it was — a type the whole library is full of numbers
 * for, with no name for it. */
typedef double NSTimeInterval;

/* NULLABILITY (F6, slice 3): NONNULL by default. A date is a number, so every
 * constructor is total (measured: NSDate.m has no `return nil`/`return NULL`), and
 * the only exception is the locale parameter. -earlierDate:/-laterDate: always
 * answer one of their two operands, which is why neither is nullable. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSDate : NSObject <NSCopying, NSCoding>
{
	double _timeIntervalSince1970;
}

+ (NSDate *)date;
+ (NSDate *)dateWithTimeIntervalSince1970:(double)seconds;
+ (NSDate *)dateWithTimeIntervalSinceNow:(double)seconds;
+ (NSDate *)dateWithTimeInterval:(double)seconds sinceDate:(NSDate *)date;
+ (NSDate *)dateWithTimeIntervalSinceReferenceDate:(double)seconds;
+ (NSDate *)distantPast;
+ (NSDate *)distantFuture;
+ (double)timeIntervalSinceReferenceDate;

- (id)initWithTimeIntervalSince1970:(double)seconds;
- (id)initWithTimeIntervalSinceNow:(double)seconds;
- (id)initWithTimeInterval:(double)seconds sinceDate:(NSDate *)date;
- (id)initWithTimeIntervalSinceReferenceDate:(double)seconds;

- (double)timeIntervalSince1970;
- (double)timeIntervalSinceNow;
- (double)timeIntervalSinceReferenceDate;
- (double)timeIntervalSinceDate:(NSDate *)other;
- (NSDate *)dateByAddingTimeInterval:(double)seconds;
- (NSString *)descriptionWithLocale:(nullable id)locale;

- (BOOL)isEqualToDate:(NSDate *)other;
- (NSComparisonResult)compare:(NSDate *)other;
- (NSDate *)earlierDate:(NSDate *)other;
- (NSDate *)laterDate:(NSDate *)other;

/* Immutable, so copying returns self. There is deliberately NO -mutableCopy: Apple's NSDate does not
 * declare one, and leaving NSObject's in place means [date mutableCopy] RAISES, which is what Apple's does. */

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSDATE_H */
