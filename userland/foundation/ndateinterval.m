/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ndateinterval.m — the implementation (W2h).
 */
#import <foundation/NSDateInterval.h>
#import <foundation/NSException.h>
#import <foundation/NSString.h>

@implementation NSDateInterval

- (instancetype)initWithStartDate:(NSDate *)startDate duration:(NSTimeInterval)duration
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* AN INTERVAL THAT CANNOT EXIST IS REFUSED RATHER THAN STORED: the order of the ends is this
	 * class's one invariant, and every method below reads it without checking. */
	if (duration < 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"an NSDateInterval's duration cannot be negative (%g)", duration];
		return nil;
	}
	_startDate = [startDate copy];
	_duration = duration;
	_endDate = [[NSDate alloc] initWithTimeIntervalSinceReferenceDate:
			[startDate timeIntervalSinceReferenceDate] + duration];
	return self;
}

- (instancetype)initWithStartDate:(NSDate *)startDate endDate:(NSDate *)endDate
{
	NSTimeInterval span = [endDate timeIntervalSinceDate:startDate];

	if (span < 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"an NSDateInterval's end cannot precede its start"];
		return nil;
	}
	return [self initWithStartDate:startDate duration:span];
}

- (NSDate *)startDate
{
	return _startDate;
}

- (NSDate *)endDate
{
	return _endDate;
}

- (NSTimeInterval)duration
{
	return _duration;
}

- (NSComparisonResult)compare:(NSDateInterval *)dateInterval
{
	return [_startDate compare:[dateInterval startDate]];
}

- (BOOL)isEqualToDateInterval:(NSDateInterval *)dateInterval
{
	return [_startDate isEqualToDate:[dateInterval startDate]] &&
	       [_endDate isEqualToDate:[dateInterval endDate]];
}

- (BOOL)intersectsDateInterval:(NSDateInterval *)dateInterval
{
	return ([_startDate compare:[dateInterval endDate]] == NSOrderedAscending) &&
	       ([[dateInterval startDate] compare:_endDate] == NSOrderedAscending);
}

- (NSDateInterval *)intersectionWithDateInterval:(NSDateInterval *)dateInterval
{
	NSDate *laterStart;
	NSDate *earlierEnd;

	if (![self intersectsDateInterval:dateInterval]) {
		return nil;
	}
	laterStart = ([_startDate compare:[dateInterval startDate]] == NSOrderedDescending)
		? _startDate : [dateInterval startDate];
	earlierEnd = ([_endDate compare:[dateInterval endDate]] == NSOrderedAscending)
		? _endDate : [dateInterval endDate];
	return [[NSDateInterval alloc] initWithStartDate:laterStart endDate:earlierEnd];
}

- (BOOL)containsDate:(NSDate *)date
{
	return ([_startDate compare:date] != NSOrderedDescending) &&
	       ([_endDate compare:date] != NSOrderedAscending);
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@ to %@>", [self class], _startDate, _endDate];
}

- (BOOL)isEqual:(id)other
{
	return [other isKindOfClass:[NSDateInterval class]] &&
	       [self isEqualToDateInterval:other];
}

- (NSUInteger)hash
{
	/* THE HASH IS OURS: Cocoa's is unspecified, and equal intervals hash equally because the two
	 * components it folds are the two the equality test compares. */
	return [_startDate hash] ^ [_endDate hash];
}

/* THIS CLASS OWNS TWO OBJECTS, so it is one of the few here that must give them back. Its
 * neighbours hold doubles and raw bytes and need no dealloc; this one would leak an interval's
 * worth of dates on every construction without it. */
- (void)dealloc
{
	[_startDate release];
	[_endDate release];
	[super dealloc];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable: the copy IS the receiver */
}

@end
