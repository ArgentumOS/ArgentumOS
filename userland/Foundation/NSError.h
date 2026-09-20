/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSError — a value: a domain, a code, and a userInfo dictionary.
 * docs/design/foundation-plan.md, F4.
 *
 * A VALUE, NOT AN OBJECT WITH BEHAVIOUR: Cocoa passes NSError by pointer-to-
 * pointer in its error-out arguments, and nothing about it is mutable. Ours is
 * the same, which is why it is <NSCopying> and why -copy returns self.
 *
 * THE NAME IS FREE. The runtime declares no NSError (checked: zero matches in
 * libobjc2), so unlike Object and NSAutoreleasePool there is no collision to
 * avoid here.
 *
 * -localizedDescription reads NSLocalizedDescriptionKey out of userInfo, and
 * falls back to a rendering of the domain and code when the key is absent — the
 * same contract Cocoa documents, without the localisation machinery underneath.
 */

#ifndef FOUNDATION_NSERROR_H
#define FOUNDATION_NSERROR_H

#import <Foundation/NSObject.h>

@class NSString;
@class NSDictionary;

/* NULLABILITY (F6, slice 3): NONNULL by default. An error is a VALUE, so most of
 * it is total — but three things are genuinely optional, and each one is a fact
 * from the writer (NSError.m), not a guess:
 *   - the two constructs are nullable: -initWithDomain:... is one of the measured
 *     `return nil;` sites, and +errorWithDomain:... is
 *     `return [[self alloc] initWithDomain:...]`, so it PROPAGATES that;
 *   - -userInfo is nullable because the property is `[userInfo copy]` of a
 *     nullable argument, and -isEqualToError: itself branches on `_userInfo == nil`;
 *   - -localizedFailureReason is nullable because it is `[_userInfo objectForKey:]`,
 *     whose result is nullable by definition. -localizedDescription is NOT: the
 *     method falls back to a rendered string, so it always answers an object.
 * The userInfo PARAMETERS are nullable too: Cocoa allows nil, and NSException.m
 * passes nil itself when it builds an exception from a format. */
NS_ASSUME_NONNULL_BEGIN

/* Cocoa's domain type is just a string. */
typedef NSString *NSErrorDomain;

/* Cocoa's userInfo keys, and they are Cocoa's SPELLINGS on purpose: an error a
 * caller built against Cocoa's documentation has to be readable by our code and
 * vice versa. */
extern NSString *const NSLocalizedDescriptionKey;
extern NSString *const NSLocalizedFailureReasonKey;
extern NSString *const NSLocalizedRecoverySuggestionErrorKey;
extern NSString *const NSUnderlyingErrorKey;

@interface NSError : NSObject <NSCopying>
{
	NSString *_domain;
	NSInteger _code;
	NSDictionary *_userInfo;
}

+ (nullable instancetype)errorWithDomain:(NSErrorDomain)domain
			   code:(NSInteger)code
		       userInfo:(nullable NSDictionary *)userInfo;

- (nullable id)initWithDomain:(NSErrorDomain)domain
		code:(NSInteger)code
	    userInfo:(nullable NSDictionary *)userInfo;

- (NSErrorDomain)domain;
- (NSInteger)code;
- (nullable NSDictionary *)userInfo;

- (NSString *)localizedDescription;
- (nullable NSString *)localizedFailureReason;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSERROR_H */
