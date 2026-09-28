/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLocalizedNumberFormatRule.m — the rule as a value (§62.81). MANUAL OWNERSHIP.
 *
 * NO STATE, AND THE FOUR PROMISES A VALUE MAKES: it copies to a distinct equal object, it compares and hashes by
 * what it is (the automatic rule is the automatic rule), and it archives. A rule that could not be copied or sent
 * to another thread would not be a value.
 */

#import <Foundation/NSLocalizedNumberFormatRule.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSString.h>

@implementation NSLocalizedNumberFormatRule

+ (NSLocalizedNumberFormatRule *)automatic
{
	return [[[NSLocalizedNumberFormatRule alloc] init] autorelease];
}

- (void)dealloc
{
	[super dealloc];
}

- (id)copy
{
	/* A DISTINCT EQUAL OBJECT rather than the receiver: -copy is an owned family here (§15.2), and a caller that
	 * mutated one rule must not be mutating another. */
	return [[NSLocalizedNumberFormatRule alloc] init];
}

- (BOOL)isEqual:(id)other
{
	return other != nil && [other isKindOfClass:[NSLocalizedNumberFormatRule class]];
}

- (NSUInteger)hash
{
	return [NSStringFromClass([self class]) hash];
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;	/* the automatic rule has no state: writing nothing IS the encoding */
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	self = [super init];
	return self;
}

@end
