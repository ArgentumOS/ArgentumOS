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

#import <foundation/NSObject.h>

@class NSString;
@class NSDictionary;

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

+ (instancetype)errorWithDomain:(NSErrorDomain)domain
			   code:(NSInteger)code
		       userInfo:(NSDictionary *)userInfo;

- (id)initWithDomain:(NSErrorDomain)domain
		code:(NSInteger)code
	    userInfo:(NSDictionary *)userInfo;

- (NSErrorDomain)domain;
- (NSInteger)code;
- (NSDictionary *)userInfo;

- (NSString *)localizedDescription;
- (NSString *)localizedFailureReason;

@end

#endif /* FOUNDATION_NSERROR_H */
