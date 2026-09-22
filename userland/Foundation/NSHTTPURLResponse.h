/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHTTPURLResponse — THE HTTP RESPONSE IS ITS STATUS AND ITS HEADERS. docs/design/foundation-plan.md
 * W7, §46.
 *
 * IT IS NSURLResponse PLUS TWO FACTS THAT ARE HTTP'S OWN: a status code and the header fields. And the
 * initializer DOES MORE THAN STORE THEM, which is Apple's documented behaviour and the one piece of
 * arithmetic in this slice: the inherited MIME type, text-encoding name and expected length are DERIVED
 * from the headers — `Content-Type` (its media type, and its `charset` parameter) and `Content-Length`.
 * That derivation is asserted in the probe, because a class that answered "unknown" for a response whose
 * headers SAY would be quietly useless.
 *
 * +localizedStringForStatusCode: IS RFC 9110's PHRASE REGISTRY, and that is why it is a table this
 * project may keep: the reason phrases are published by the IETF (§15 of RFC 9110, with §15.5.17's
 * 418 from RFC 2324), it is the same class of source as RFC 3986 was for NSURLComponents, and the probe
 * pins the registry's own rows rather than anything this file could be written to match. A code outside
 * the registry answers NIL — stated plainly rather than papered over with a generic string, because a
 * caller that needs a phrase for a code the registry does not define has a code worth looking at.
 *
 * "LOCALIZED" IS A NAME APPLE CHOSE AND THE REGISTRY IS ENGLISH: there is no translation table for HTTP
 * reason phrases in this tree, so the header states that the answer is the registry's own ASCII text and
 * that a caller wanting a translation must supply one. The method keeps Apple's name so a program that
 * calls it compiles and behaves as documented here.
 *
 * -valueForHTTPHeaderField: IS CASE-INSENSITIVE, as it is on the request (RFC 9110 §5.1).
 */

#ifndef FOUNDATION_NSHTTPURLRESPONSE_H
#define FOUNDATION_NSHTTPURLRESPONSE_H

#import <Foundation/NSURLResponse.h>

@class NSString;
@class NSDictionary;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@interface NSHTTPURLResponse : NSURLResponse
{
	NSInteger _statusCode;
	NSDictionary *_allHeaderFields;	/* ALWAYS an immutable snapshot (nil -> empty) */
}

/* `HTTPVersion` is accepted, validated and not stored: Apple exposes no getter for it, so keeping it
 * would be a private field nothing can read — the header says so rather than implying it is kept. */
- (instancetype)initWithURL:(NSURL *)URL
		 statusCode:(NSInteger)statusCode
		HTTPVersion:(nullable NSString *)HTTPVersion
	       headerFields:(nullable NSDictionary *)headerFields;

@property (readonly) NSInteger statusCode;
@property (readonly, copy) NSDictionary *allHeaderFields;

/* Case-insensitive, like the request's door. */
- (nullable NSString *)valueForHTTPHeaderField:(NSString *)field;

/* RFC 9110 §15's phrase for a status code, or nil for a code the registry does not define. */
+ (nullable NSString *)localizedStringForStatusCode:(NSInteger)statusCode;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSHTTPURLRESPONSE_H */
