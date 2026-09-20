/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsformatter.m — the abstract base of the value-to-text family (F13).
 *
 * MANUAL OWNERSHIP. It owns no storage: the base is behaviour and nothing else.
 */

#import <foundation/NSFormatter.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>

@implementation NSFormatter

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	(void)object;
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: the base class has no rule that turns a value into text — "
			   "a subclass (NSDateFormatter, NSNumberFormatter, …) must implement it",
			   [self class], @"stringForObjectValue:"];
	return nil;
}

- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error
{
	(void)object;
	(void)string;
	if (error != NULL) {
		*error = nil;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: the base class has no rule that turns text into a value",
			   [self class], @"getObjectValue:forString:errorDescription:"];
	return NO;
}

- (BOOL)isPartialStringValid:(NSString *)partialString
	    newEditingString:(NSString * _Nullable * _Nullable)newString
	    errorDescription:(NSString * _Nullable * _Nullable)error
{
	(void)partialString;
	if (newString != NULL) {
		*newString = nil;
	}
	if (error != NULL) {
		*error = nil;
	}
	/* Apple's default, and it means "no opinion" rather than "invalid": the caller keeps its
	 * own behaviour. A formatter that CAN validate overrides this. */
	return NO;
}

- (id)copy
{
	/* The base has no state, so it is its own copy. A subclass with settings overrides this —
	 * NSDateFormatter does, because a formatter is MUTABLE and a shared copy would be a trap. */
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

@end
