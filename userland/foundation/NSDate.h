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

#import <foundation/NSObject.h>

@interface NSDate : NSObject <NSCopying>
{
	double _timeIntervalSince1970;
}

+ (NSDate *)date;
+ (NSDate *)dateWithTimeIntervalSince1970:(double)seconds;

- (id)initWithTimeIntervalSince1970:(double)seconds;

- (double)timeIntervalSince1970;
- (double)timeIntervalSinceDate:(NSDate *)other;

- (BOOL)isEqualToDate:(NSDate *)other;
- (NSComparisonResult)compare:(NSDate *)other;
- (NSDate *)earlierDate:(NSDate *)other;
- (NSDate *)laterDate:(NSDate *)other;

/* Immutable, so copying returns self. */
- (id)copy;
- (id)mutableCopy;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;

@end

#endif /* FOUNDATION_NSDATE_H */
