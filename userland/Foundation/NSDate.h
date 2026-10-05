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

@class NSCalendarDate;	/* §63.229: returned by the CalendarFormat: doors */
@class NSTimeZone;	/* named by the CalendarFormat doors; declared in its own header */

@interface NSDate : NSObject <NSCopying, NSCoding>
{
	double _timeIntervalSince1970;
}

+ (NSDate *)date;
+ (NSDate *)dateWithTimeIntervalSince1970:(double)seconds;
+ (NSDate *)dateWithTimeIntervalSinceNow:(double)seconds;
+ (NSDate *)dateWithTimeInterval:(double)seconds sinceDate:(NSDate *)date;
+ (NSDate *)dateWithTimeIntervalSinceReferenceDate:(double)seconds;
/* THE SR ABSOLUTE TIME PAIR (§63.197). "SR" is CoreFoundation's software reference absolute time: SECONDS
 * SINCE 2001-01-01 GMT, the same epoch -timeIntervalSinceReferenceDate: uses, which is why these three are
 * one-line doors onto it rather than a second time base. `now` is Swift's spelling of +date. */
+ (id)dateWithSRAbsoluteTime:(double)seconds;
- (id)initWithSRAbsoluteTime:(double)seconds;
@property (readonly) double srAbsoluteTime;
@property (class, readonly) NSDate *now;

/* THE LEGACY DOORS (§63.197). +dateWithString: takes Apple's documented "%Y-%m-%d %H:%M:%S +0000" and
 * answers nil when it does not match. -addTimeInterval: is the pre-10.6 spelling of
 * -dateByAddingTimeInterval:. The two -...CalendarFormat: doors are NOT here: they go through NSCalendarDate,
 * whose description doors take a format and an optional LOCALE but no time zone — see §63.197. */
+ (nullable NSDate *)dateWithString:(NSString *)description;
- (NSDate *)addTimeInterval:(double)seconds;
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


/* §63.229: THE TWO LEGACY CalendarFormat: DOORS, and §63.197's blocker is what they answer: NSCalendarDate's
 * FORMAT AND ZONE ARE THE SETTABLE PAIR, so neither needs a second formatting engine. The `timeZone:locale:`
 * description spelling does not exist on NSCalendarDate (the LOCALE form does), so the zone is set ON THE
 * OBJECT and the locale passed through. */
- (NSCalendarDate * _Nonnull)dateWithCalendarFormat:(nullable NSString *)format
				  timeZone:(nullable NSTimeZone *)aTimeZone;
- (NSString * _Nullable)descriptionWithCalendarFormat:(nullable NSString *)format
					    timeZone:(nullable NSTimeZone *)aTimeZone
					      locale:(nullable id)locale;
@end

#endif /* FOUNDATION_NSDATE_H */
