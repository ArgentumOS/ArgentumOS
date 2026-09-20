/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnit.m — the symbol and the identity. docs/design/foundation-plan.md §12.3 W12 (first slice).
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words). One owned
 * string, copied in the initializer, released on dealloc.
 *
 * -isEqual: IS IDENTITY (pointer equality), and the header says why: a unit is what a measurement's
 * arithmetic is defined against, so two units that merely spell their symbol the same are not the same
 * unit. `-hash` follows the same decision, and `-copy` answers a RETAINED SELF because a unit is
 * immutable — there is nothing to duplicate.
 */

#import <Foundation/NSUnit.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>

#include <stdint.h>

@implementation NSUnit

- (instancetype)initWithSymbol:(NSString *)symbol
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* COPIED: a caller may hand in an NSMutableString and edit it afterwards — the family's usual rule
	 * for a string that becomes part of an object's identity. */
	_symbol = [symbol copy];
	return self;
}

- (void)dealloc
{
	[_symbol release];
	[super dealloc];
}

- (NSString *)symbol
{
	return _symbol;
}

/* IDENTITY, not symbol comparison — see the header. The units a measurement is built from are the same
 * unit only when they ARE the same object, which is what makes -measurementByConvertingToUnit: meaningful:
 * converting to a DIFFERENT unit that happens to spell its symbol the same is a real conversion. */
- (BOOL)isEqual:(id)other
{
	return (other == self);
}

- (NSUInteger)hash
{
	return (NSUInteger)(uintptr_t)self;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@>", [self class], _symbol];
}

- (id)copy
{
	/* A unit is immutable and its identity IS its value, so a copy is the same object (+1: `copy` is an
	 * OWNED family, plan §15.2). */
	return [self retain];
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_symbol forKey:@"NS.unitSymbol"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithSymbol:[coder decodeObjectForKey:@"NS.unitSymbol"]];
}

@end
