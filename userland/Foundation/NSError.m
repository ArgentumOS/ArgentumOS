/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSError.m — the error value.
 *
 * MANUAL OWNERSHIP: it stores three owned objects and implements no -retain/-release.
 */

#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>
#include <stdio.h>

/* THE VALUES ARE COCOA'S, INCLUDING ITS ASYMMETRY: three of these drop the "Key"/"ErrorKey"
 * suffix that their NAMES carry, and NSLocalizedDescriptionKey does NOT. F12's probe found the
 * difference the hard way — it looked the message up under the literal "NSLocalizedDescriptionKey"
 * and found nothing, because this line said "NSLocalizedDescription". A caller that serialises a
 * userInfo dictionary is entitled to the same strings Cocoa uses. */
NSString *const NSLocalizedDescriptionKey = @"NSLocalizedDescriptionKey";
NSString *const NSLocalizedFailureReasonKey = @"NSLocalizedFailureReason";
NSString *const NSLocalizedRecoverySuggestionErrorKey = @"NSLocalizedRecoverySuggestion";
NSString *const NSUnderlyingErrorKey = @"NSUnderlyingError";

@implementation NSError

+ (instancetype)errorWithDomain:(NSErrorDomain)domain
			   code:(NSInteger)code
		       userInfo:(NSDictionary *)userInfo
{
	return [[self alloc] initWithDomain:domain code:code userInfo:userInfo];
}

- (id)initWithDomain:(NSErrorDomain)domain
		code:(NSInteger)code
	    userInfo:(NSDictionary *)userInfo
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* Copied, not retained: an error is a value, and a caller that hands over a
	 * mutable dictionary must not be able to edit the error afterwards. */
	_domain = [domain copy];
	_code = code;
	_userInfo = [userInfo copy];
	return self;
}

- (NSErrorDomain)domain
{
	return _domain;
}

- (NSInteger)code
{
	return _code;
}

- (NSDictionary *)userInfo
{
	return _userInfo;
}

- (NSString *)localizedDescription
{
	NSString *described = [_userInfo objectForKey:NSLocalizedDescriptionKey];

	if (described != nil) {
		return described;
	}
	return [NSString stringWithFormat:@"The operation could not be completed. (%@ error %ld.)",
					 _domain, (long)_code];
}

- (NSString *)localizedFailureReason
{
	return [_userInfo objectForKey:NSLocalizedFailureReasonKey];
}

- (BOOL)isEqualToError:(NSError *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if (_code != [other code] || ![[other domain] isEqualToString:_domain]) {
		return NO;
	}
	{
		NSDictionary *theirs = [other userInfo];

		if (theirs == _userInfo) {
			return YES;
		}
		if (theirs == nil || _userInfo == nil) {
			return NO;
		}
		return [_userInfo isEqualToDictionary:theirs];
	}
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSError class]]) {
		return NO;
	}
	return [self isEqualToError:(NSError *)other];
}

- (NSUInteger)hash
{
	return [_domain hash] ^ (NSUInteger)_code;
}

- (NSString *)description
{
	/* Cocoa's shape: domain, code, the localised description, then the userInfo. */
	return [NSString stringWithFormat:@"Error Domain=%@ Code=%ld \"%@\" UserInfo=%@",
					 _domain, (long)_code, [self localizedDescription],
					 (_userInfo != nil) ? [_userInfo description] : @"(null)"];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

- (id)mutableCopy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

@end
