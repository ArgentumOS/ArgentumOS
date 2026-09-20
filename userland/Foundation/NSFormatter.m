/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFormatter.m — the abstract base of the value-to-text family (F13).
 *
 * MANUAL OWNERSHIP. It owns no storage: the base is behaviour and nothing else — which is also why
 * its half of NSCoding below is empty and honest rather than absent.
 */

#import <Foundation/NSFormatter.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>

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

/*
 * THE THREE MEMBERS W11 FOUND MISSING (2026-09-20). Two have Apple's default written in the
 * documentation and are quoted here; the third has none, and says so.
 */
- (nullable NSAttributedString *)attributedStringForObjectValue:(nullable id)object
					   withDefaultAttributes:(nullable NSDictionary *)attributes
{
	(void)object;
	(void)attributes;
	/* Apple, verbatim: "The default implementation returns nil to indicate that the formatter
	 * object does not provide an attributed string." */
	return nil;
}

- (nullable NSString *)editingStringForObjectValue:(nullable id)object
{
	/* Apple, verbatim: "The default implementation of this method invokes -stringForObjectValue:".
	 * One line, and it is the whole contract — which also means a subclass that overrides only the
	 * string door gets a working editing string for free, and the base raises here exactly where
	 * the string door raises. */
	return [self stringForObjectValue:object];
}

- (BOOL)isPartialStringValid:(NSString * _Nullable * _Nullable)partialStringPtr
	 proposedSelectedRange:(nullable NSRangePointer)proposedSelRangePtr
		originalString:(NSString *)originalString
	  originalSelectedRange:(NSRange)originalSelectedRange
	       errorDescription:(NSString * _Nullable * _Nullable)error
{
	(void)proposedSelRangePtr;
	(void)originalString;
	(void)originalSelectedRange;
	/* APPLE PUBLISHES NO DEFAULT FOR THIS FORM — its page describes what a subclass should do and
	 * stops — so this choice is ours and the header says so. It DELEGATES to the three-argument
	 * form, which keeps one behaviour where there could be two: a subclass that implemented only
	 * the simpler door is still honoured, and one that overrides neither keeps "no opinion".
	 *
	 * A missing in-string is answered directly rather than forwarded, because the simpler door's
	 * parameter is nonnull: forwarding a nil through it would pass a value the contract forbids,
	 * which is a worse answer than NO. */
	if (partialStringPtr == NULL || *partialStringPtr == nil) {
		if (error != NULL) {
			*error = nil;
		}
		return NO;
	}
	return [self isPartialStringValid:*partialStringPtr
			  newEditingString:NULL
			   errorDescription:error];
}

/*
 * NSCoding ON THE BASE: no state to write and none to read — the base is behaviour, and a formatter's
 * settings belong to the subclass that has them (NSDateFormatter's archival is its own). The pair is
 * implemented rather than left absent because a class that DECLARES the protocol and answers neither
 * half is a class whose archive lies by omission.
 */
- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	return [super init];
}

- (id)copy
{
	/* The base has no state, so it is its own copy. A subclass with settings overrides this —
	 * NSDateFormatter does, because a formatter is MUTABLE and a shared copy would be a trap. */
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

@end
