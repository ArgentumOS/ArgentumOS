/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTermOfAddress.m — how to address someone, as a value (§62.79). MANUAL OWNERSHIP.
 *
 * TWO FIELDS AND FOUR DOORS. The predefined terms and +currentUser answer values with nothing stated (the pronoun
 * data that would distinguish them is not carried in this system - see the header), and
 * +localizedForLanguageIdentifier:withPronouns: is the one door that builds a term from real data.
 *
 * THE FIELDS ARE WRITTEN BY A PRIVATE INITIALISER of this file's own, NOT by -setValue:forKey:. This file's first
 * version used the KVC door and thereby SHADOWED NSObject's KVC on this class with different semantics - a real
 * hazard for any caller that treated the class as a KVC participant, and not a thing to leave in a value type.
 */

#import <Foundation/NSTermOfAddress.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

@interface NSTermOfAddress ()
- (instancetype)fnInitWithLanguageIdentifier:(nullable NSString *)language
				   pronouns:(nullable NSArray *)pronouns;
@end

@implementation NSTermOfAddress

- (instancetype)fnInitWithLanguageIdentifier:(nullable NSString *)language
				   pronouns:(nullable NSArray *)pronouns
{
	self = [super init];
	if (self != nil) {
		_languageIdentifier = language != nil ? [[NSString alloc] initWithString:language] : nil;
		_pronouns = pronouns != nil ? [[NSArray alloc] initWithArray:pronouns] : nil;
	}
	return self;
}

/* THE FOUR VALUES WITH NOTHING STATED, each a FRESH instance: a term is a value a caller may keep, and sharing one
 * would make one holder's lifetime the others' problem. The three predefined terms carry NO PRONOUNS here, which
 * the header states as this system's state rather than as a property of the terms. */
+ (instancetype)feminine
{
	return [[[self alloc] init] autorelease];
}

+ (instancetype)masculine
{
	return [[[self alloc] init] autorelease];
}

+ (instancetype)neutral
{
	return [[[self alloc] init] autorelease];
}

+ (instancetype)currentUser
{
	return [[[self alloc] init] autorelease];
}

+ (instancetype)localizedForLanguageIdentifier:(NSString *)language
				  withPronouns:(NSArray *)pronouns
{
	if (language == nil || [language length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+[NSTermOfAddress localizedForLanguageIdentifier:withPronouns:] needs a language "
				   @"identifier"];
	}
	return [[[self alloc] fnInitWithLanguageIdentifier:language pronouns:pronouns] autorelease];
}

- (nullable NSString *)languageIdentifier { return _languageIdentifier; }
- (nullable NSArray *)pronouns { return _pronouns; }

- (void)dealloc
{
	[_languageIdentifier release];
	[_pronouns release];
	[super dealloc];
}

- (id)copy
{
	return [[NSTermOfAddress alloc] fnInitWithLanguageIdentifier:_languageIdentifier pronouns:_pronouns];
}

- (BOOL)isEqual:(id)other
{
	NSTermOfAddress *that;

	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSTermOfAddress class]]) {
		return NO;
	}
	that = other;
	return (_languageIdentifier == that->_languageIdentifier ||
		[_languageIdentifier isEqual:that->_languageIdentifier]) &&
	       (_pronouns == that->_pronouns || [_pronouns isEqual:that->_pronouns]);
}

- (NSUInteger)hash
{
	return [_languageIdentifier hash] ^ [_pronouns hash];
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_languageIdentifier forKey:@"languageIdentifier"];
	[coder encodeObject:_pronouns forKey:@"pronouns"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [self fnInitWithLanguageIdentifier:[coder decodeObjectForKey:@"languageIdentifier"]
					 pronouns:[coder decodeObjectForKey:@"pronouns"]];
}

@end
