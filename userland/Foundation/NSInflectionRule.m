/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInflectionRule.m — the rule as a value (§62.79). MANUAL OWNERSHIP.
 *
 * The base class is a NAME ("let the system decide") and holds no state; the explicit subclass holds the one thing
 * a rule can say, which is the morphology it would inflect with. Both copy, compare by their own rule, and archive
 * — a rule that could not be passed to another lock or thread would not be a value.
 */

#import <Foundation/NSInflectionRule.h>
#import <Foundation/NSMorphology.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSString.h>

@implementation NSInflectionRule

/* A FRESH VALUE EVERY CALL, INCLUDING FOR +automaticRule: the object is mutable-by-subclass, so a shared one
 * would be a shared answer - the reading §62.78 used for +userMorphology. */
+ (NSInflectionRule *)automaticRule
{
	return [[[NSInflectionRule alloc] init] autorelease];
}

+ (BOOL)canInflectLanguage:(NSString *)language
{
	(void)language;
	/* NO, AND THE GROUND IS THE ABSENCE OF AN AGREEMENT MODEL: nothing here agrees a grammar. */
	return NO;
}

+ (BOOL)canInflectPreferredLocalization
{
	return NO;		/* the same ground, without a language to name */
}

- (void)dealloc
{
	[super dealloc];
}

/* A RULE COPY IS A VALUE, and for the base class that means a fresh name - not the same object, because a caller
 * that mutates one must not be mutating the other. */
- (id)copy
{
	return [[NSInflectionRule alloc] init];
}

- (BOOL)isEqual:(id)other
{
	return other != nil && [other isKindOfClass:[NSInflectionRule class]] &&
	       ![other isKindOfClass:[NSInflectionRuleExplicit class]] ==
	       ![self isKindOfClass:[NSInflectionRuleExplicit class]];
}

- (NSUInteger)hash
{
	return [NSStringFromClass([self class]) hash];
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;	/* the automatic rule has no state to write, and writing nothing is the honest encoding */
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	self = [super init];
	return self;
}

@end

@implementation NSInflectionRuleExplicit

- (instancetype)initWithMorphology:(NSMorphology *)morphology
{
	self = [super init];
	if (self != nil) {
		_morphology = [morphology copy];
	}
	return self;
}

- (NSMorphology *)morphology { return _morphology; }

- (void)dealloc
{
	[_morphology release];
	[super dealloc];
}

- (id)copy
{
	return [[NSInflectionRuleExplicit alloc] initWithMorphology:_morphology];
}

- (BOOL)isEqual:(id)other
{
	return other != nil && [other isKindOfClass:[NSInflectionRuleExplicit class]] &&
	       [((NSInflectionRuleExplicit *)other)->_morphology isEqual:_morphology];
}

- (NSUInteger)hash
{
	return [_morphology hash] ^ 0x1F;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_morphology forKey:@"morphology"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	id morphology = [coder decodeObjectForKey:@"morphology"];

	self = [super init];
	if (self != nil) {
		_morphology = [morphology copy];
	}
	return self;
}

@end
