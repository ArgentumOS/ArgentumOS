/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTextCheckingResult.m — the implementation, MANUAL OWNERSHIP (the library's own policy; ARC is for
 * everything that USES the Foundation). docs/design/foundation-plan.md §62.18.
 *
 * EVERY FACTORY IS THE SAME THREE LINES WITH A DIFFERENT PAYLOAD, which is the point of the class: the type
 * says what was found, the ranges say where, and the payload says the rest — and a result that does not carry
 * a payload answers nil for it because its ivar was never set, rather than because an accessor remembered to
 * check a type. That is why the ivars are the contract and the accessors are one line each.
 */

#import <Foundation/NSTextCheckingResult.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSRegularExpression.h>
#import <Foundation/NSString.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSURL.h>

#include <stdlib.h>
#include <string.h>
#include <objc/runtime.h>

/* ---- THE KEYS. Their values are ours (§11.6.1 D2): Apple publishes the names and not the spellings, and a
 * caller compares against the constant. The spelling of each is its own name with the family prefix removed,
 * which is what makes a dictionary dumped by a diagnostic readable. */
NSTextCheckingKey const NSTextCheckingNameKey = @"Name";
NSTextCheckingKey const NSTextCheckingJobTitleKey = @"JobTitle";
NSTextCheckingKey const NSTextCheckingOrganizationKey = @"Organization";
NSTextCheckingKey const NSTextCheckingStreetKey = @"Street";
NSTextCheckingKey const NSTextCheckingCityKey = @"City";
NSTextCheckingKey const NSTextCheckingStateKey = @"State";
NSTextCheckingKey const NSTextCheckingZIPKey = @"ZIP";
NSTextCheckingKey const NSTextCheckingCountryKey = @"Country";
NSTextCheckingKey const NSTextCheckingPhoneKey = @"Phone";
NSTextCheckingKey const NSTextCheckingAirlineKey = @"Airline";
NSTextCheckingKey const NSTextCheckingFlightKey = @"Flight";

/* ONE COMPARISON FOR EVERY PAYLOAD: nil is equal to nil, and otherwise the object's own -isEqual: decides. */
static BOOL fn_equal(id a, id b)
{
	if (a == b) {
		return YES;
	}
	if (a == nil || b == nil) {
		return NO;
	}
	return [a isEqual:b];
}

@implementation NSTextCheckingResult

/* THE SHARED BODY OF EVERY FACTORY: allocate, copy the ranges, and let the caller set the type and payload. */
+ (instancetype)resultWithRanges:(const NSRange *)ranges count:(NSUInteger)count
{
	return [self fnResultWithRanges:ranges count:count];
}

+ (instancetype)fnResultWithRanges:(const NSRange *)ranges count:(NSUInteger)count
{
	NSTextCheckingResult *result = [[self alloc] init];

	if (result == nil) {
		return nil;
	}
	if (count > 0) {
		result->_ranges = malloc(count * sizeof(NSRange));
		if (result->_ranges == NULL) {
			[result release];
			return nil;
		}
		if (ranges != NULL) {
			memcpy(result->_ranges, ranges, count * sizeof(NSRange));
		} else {
			memset(result->_ranges, 0, count * sizeof(NSRange));
		}
	}
	result->_count = count;
	return result;
}

/* ONE COPY WITH NEW RANGES AND EVERY OTHER FIELD CARRIED OVER. It is a method rather than a function so that a
 * subclass that grows a payload field keeps working: `[self class]`, and the fields below are this class's. */
- (NSTextCheckingResult *)fnResultWithRanges:(const NSRange *)ranges count:(NSUInteger)count
{
	NSTextCheckingResult *copy = [[self class] fnResultWithRanges:ranges count:count];

	if (copy == nil) {
		return nil;
	}
	copy->_resultType = _resultType;
	copy->_replacementString = [_replacementString copy];
	copy->_alternativeStrings = [_alternativeStrings copy];
	copy->_regularExpression = [_regularExpression copy];
	copy->_components = [_components copy];
	copy->_URL = [_URL copy];
	copy->_addressComponents = [_addressComponents copy];
	copy->_phoneNumber = [_phoneNumber copy];
	copy->_date = [_date copy];
	copy->_timeZone = [_timeZone copy];
	copy->_duration = _duration;
	copy->_orthography = [_orthography copy];
	copy->_grammarDetails = [_grammarDetails copy];
	return copy;
}

/* ---- THE FACTORIES ------------------------------------------------------------------------------- */

+ (NSTextCheckingResult *)replacementCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeReplacement;
	result->_replacementString = [replacementString copy];
	return result;
}

+ (NSTextCheckingResult *)regularExpressionCheckingResultWithRanges:(NSRangePointer)ranges
							      count:(NSUInteger)count
						   regularExpression:(NSRegularExpression *)regularExpression
{
	NSTextCheckingResult *result = [self fnResultWithRanges:ranges count:count];

	result->_resultType = NSTextCheckingTypeRegularExpression;
	result->_regularExpression = [regularExpression copy];
	return result;
}

+ (NSTextCheckingResult *)linkCheckingResultWithRange:(NSRange)range URL:(NSURL *)url
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeLink;
	result->_URL = [url copy];
	return result;
}

+ (NSTextCheckingResult *)addressCheckingResultWithRange:(NSRange)range
					      components:(NSDictionary *)components
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeAddress;
	result->_addressComponents = [components copy];
	/* -components IS THE GENERAL ACCESSOR Apple added in 10.12: it answers the dictionary the kind carries, so
	 * an address result answers the same VALUE as -addressComponents (one copy, two doors) and a transit result
	 * answers the one it was built with. */
	result->_components = result->_addressComponents;
	return result;
}

+ (NSTextCheckingResult *)transitInformationCheckingResultWithRange:(NSRange)range
							 components:(NSDictionary *)components
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeTransitInformation;
	result->_components = [components copy];
	return result;
}

+ (NSTextCheckingResult *)phoneNumberCheckingResultWithRange:(NSRange)range
						 phoneNumber:(NSString *)phoneNumber
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypePhoneNumber;
	result->_phoneNumber = [phoneNumber copy];
	return result;
}

+ (NSTextCheckingResult *)dateCheckingResultWithRange:(NSRange)range date:(NSDate *)date
{
	return [self dateCheckingResultWithRange:range date:date
					timeZone:[NSTimeZone systemTimeZone] duration:0.0];
}

+ (NSTextCheckingResult *)dateCheckingResultWithRange:(NSRange)range
						 date:(NSDate *)date
					     timeZone:(NSTimeZone *)timeZone
					     duration:(NSTimeInterval)duration
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeDate;
	result->_date = [date copy];
	result->_timeZone = [timeZone copy];
	result->_duration = duration;
	return result;
}

+ (NSTextCheckingResult *)dashCheckingResultWithRange:(NSRange)range
				      replacementString:(NSString *)replacementString
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeDash;
	result->_replacementString = [replacementString copy];
	return result;
}

+ (NSTextCheckingResult *)quoteCheckingResultWithRange:(NSRange)range
				       replacementString:(NSString *)replacementString
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeQuote;
	result->_replacementString = [replacementString copy];
	return result;
}

+ (NSTextCheckingResult *)spellCheckingResultWithRange:(NSRange)range
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeSpelling;
	return result;
}

+ (NSTextCheckingResult *)correctionCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	/* NOT A CALL TO THE THREE-ARGUMENT DOOR WITH NIL: Apple declares `alternativeStrings` non-nullable, so the
	 * shorter factory leaves the ivar UNSET rather than passing a null it would have to be told to accept. The
	 * two results differ in exactly one field, which is what the shorter door is for. */
	result->_resultType = NSTextCheckingTypeCorrection;
	result->_replacementString = [replacementString copy];
	return result;
}

+ (NSTextCheckingResult *)correctionCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString
					   alternativeStrings:(NSArray *)alternativeStrings
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeCorrection;
	result->_replacementString = [replacementString copy];
	result->_alternativeStrings = [alternativeStrings copy];
	return result;
}

+ (NSTextCheckingResult *)orthographyCheckingResultWithRange:(NSRange)range
						 orthography:(NSOrthography *)orthography
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeOrthography;
	result->_orthography = [orthography copy];
	return result;
}

+ (NSTextCheckingResult *)grammarCheckingResultWithRange:(NSRange)range
						 details:(NSArray *)details
{
	NSTextCheckingResult *result = [self fnResultWithRanges:&range count:1];

	result->_resultType = NSTextCheckingTypeGrammar;
	result->_grammarDetails = [details copy];
	return result;
}

/* ---- THE RANGES AND THE PAYLOADS ----------------------------------------------------------------- */

- (NSTextCheckingType)resultType
{
	return _resultType;
}

- (NSUInteger)numberOfRanges
{
	return _count;
}

- (NSRange)range
{
	return [self rangeAtIndex:0];
}

- (NSRange)rangeAtIndex:(NSUInteger)index
{
	if (index >= _count) {
		return NSMakeRange(NSNotFound, 0);
	}
	return _ranges[index];
}

/* THE NAME IS A PROPERTY OF THE PATTERN, so the map lives on the expression (see
 * NSRegularExpression.h's -indexOfCaptureGroupNamed:) and this asks the expression the result already stores.
 * A result of no kind, or a name no group carries, answers NSNotFound - Apple's "nothing here". */
- (NSRange)rangeWithName:(NSString *)name
{
	NSUInteger index;

	if (_regularExpression == nil || name == nil) {
		return NSMakeRange(NSNotFound, 0);
	}
	index = [_regularExpression indexOfCaptureGroupNamed:name];
	if (index == NSNotFound) {
		return NSMakeRange(NSNotFound, 0);
	}
	return [self rangeAtIndex:index];
}

- (NSString *)replacementString
{
	return _replacementString;
}

- (NSArray *)alternativeStrings
{
	return _alternativeStrings;
}

- (NSRegularExpression *)regularExpression
{
	return _regularExpression;
}

- (NSDictionary *)components
{
	return _components;
}

- (NSURL *)URL
{
	return _URL;
}

- (NSDictionary *)addressComponents
{
	return _addressComponents;
}

- (NSString *)phoneNumber
{
	return _phoneNumber;
}

- (NSDate *)date
{
	return _date;
}

- (NSTimeZone *)timeZone
{
	return _timeZone;
}

- (NSTimeInterval)duration
{
	return _duration;
}

- (NSOrthography *)orthography
{
	return _orthography;
}

- (NSArray *)grammarDetails
{
	return _grammarDetails;
}

/* ---- SHIFTING ---------------------------------------------------------------------------------------
 *
 * THE RULE IS OURS BECAUSE APPLE PUBLISHES NONE (see the header). A shift that would leave any range with a
 * negative location is a caller error, so it raises - and it raises BEFORE building anything, so a failed
 * adjustment leaves no half-built result behind. */
- (NSTextCheckingResult *)resultByAdjustingRangesWithOffset:(NSInteger)offset
{
	NSRange *adjusted;
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		long long location;

		if (_ranges[i].location == NSNotFound) {
			continue;	/* a group that matched nothing keeps its NSNotFound */
		}
		location = (long long)_ranges[i].location + (long long)offset;
		if (location < 0) {
			[NSException raise:NSInvalidArgumentException
				    format:@"-[%s resultByAdjustingRangesWithOffset:%lld]: "
					   "range %lu at %lu would start before the text",
				   class_getName(object_getClass(self)), (long long)offset,
				   (unsigned long)i, (unsigned long)_ranges[i].location];
		}
	}
	adjusted = malloc(_count > 0 ? _count * sizeof(NSRange) : 1);
	if (adjusted == NULL) {
		return nil;
	}
	for (i = 0; i < _count; i++) {
		if (_ranges[i].location == NSNotFound) {
			adjusted[i] = _ranges[i];
			continue;
		}
		adjusted[i] = NSMakeRange((NSUInteger)((long long)_ranges[i].location + (long long)offset),
					  _ranges[i].length);
	}
	{
		NSTextCheckingResult *result = [self fnResultWithRanges:adjusted count:_count];

		free(adjusted);
		return result;
	}
}

/* ---- IDENTITY, AND THE THREE OBJECT PROTOCOL METHODS ---------------------------------------------- */

/* IMMUTABLE, SO A COPY IS THE RECEIVER - Apple's contract, and it is what the ownership rule this library
 * keeps (§62) requires: `[self retain]` and self. The FIRST version of this class answered a NEW ranges-only
 * object, which was invisible while ranges were all it had and would have dropped every payload the moment
 * this file grew them. */
- (id)copy
{
	return [self retain];
}

- (BOOL)isEqual:(id)other
{
	NSTextCheckingResult *them;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSTextCheckingResult class]]) {
		return NO;
	}
	them = (NSTextCheckingResult *)other;
	if (them->_resultType != _resultType || them->_count != _count) {
		return NO;
	}
	if (_count > 0 && memcmp(_ranges, them->_ranges, _count * sizeof(NSRange)) != 0) {
		return NO;
	}
	/* THE PAYLOADS ARE COMPARED, because two results of the same kind and the same range that found
	 * different things are different results. Each comparison is written out rather than looped over a table:
	 * the ivars have different types, and a table of offsets is a bug waiting for a field to be added. */
	if (!fn_equal(_replacementString, them->_replacementString)) return NO;
	if (!fn_equal(_alternativeStrings, them->_alternativeStrings)) return NO;
	if (!fn_equal(_regularExpression, them->_regularExpression)) return NO;
	if (!fn_equal(_components, them->_components)) return NO;
	if (!fn_equal(_URL, them->_URL)) return NO;
	if (!fn_equal(_addressComponents, them->_addressComponents)) return NO;
	if (!fn_equal(_phoneNumber, them->_phoneNumber)) return NO;
	if (!fn_equal(_date, them->_date)) return NO;
	if (!fn_equal(_timeZone, them->_timeZone)) return NO;
	if (_duration != them->_duration) return NO;
	if (!fn_equal(_orthography, them->_orthography)) return NO;
	if (!fn_equal(_grammarDetails, them->_grammarDetails)) return NO;
	return YES;
}

- (NSUInteger)hash
{
	NSUInteger hash = (NSUInteger)_resultType;

	hash = hash * 31 + _count;
	if (_count > 0) {
		hash = hash * 31 + _ranges[0].location;
	}
	return hash;
}

- (NSString *)description
{
	NSMutableString *out = [NSMutableString stringWithFormat:@"<match %lu ranges", (unsigned long)_count];
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		[out appendFormat:@" (%lu,%lu)", (unsigned long)_ranges[i].location,
				   (unsigned long)_ranges[i].length];
	}
	[out appendFormat:@" type=%llu", (unsigned long long)_resultType];
	if (_URL != nil) {
		[out appendFormat:@" url=%@", _URL];
	}
	if (_date != nil) {
		[out appendFormat:@" date=%@", _date];
	}
	if (_phoneNumber != nil) {
		[out appendFormat:@" phone=%@", _phoneNumber];
	}
	if (_replacementString != nil) {
		[out appendFormat:@" replacement=%@", _replacementString];
	}
	[out appendString:@">"];
	return out;
}

- (void)dealloc
{
	NSRange *ranges = _ranges;

	_ranges = NULL;
	_count = 0;
	free(ranges);
	[_replacementString release];
	[_alternativeStrings release];
	[_regularExpression release];
	[_components release];
	[_URL release];
	[_addressComponents release];
	[_phoneNumber release];
	[_date release];
	[_timeZone release];
	[_orthography release];
	[_grammarDetails release];
	[super dealloc];
}

@end
