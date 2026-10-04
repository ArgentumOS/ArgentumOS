/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCachedURLResponse — A STORED ANSWER IS A VALUE: a response, the bytes it is the metadata for, a
 * caller's note, and the policy that says whether a store may keep it.
 * docs/design/foundation-plan.md W7; the transport plan's slice 2a.
 *
 * IT IS A VALUE BECAUSE NOTHING HERE STORES ANYTHING. `NSURLCache` — the thing that would — is a later
 * slice, and this type is the unit it will hold. That split is why this header ships with no transport,
 * no session and no store: it is an answer's cached FORM, passed around by value, and the only thing it
 * has an opinion about is the policy the caller attaches to it.
 *
 * NSURLCacheStoragePolicy'S VALUES ARE OURS UNDER §11.6.1 D2, because Apple publishes the CASE NAMES and
 * the case names only. They are 0, 1 and 2 in declaration order, `cache-storage-policy-values` pins every
 * one of them, and this paragraph exists so that no reader has to guess whether the numbers were read
 * from somewhere.
 *
 * `storagePolicy` AND `userInfo` ARE WHAT THE TWO INITIALIZERS DIFFER BY: -initWithResponse:data: answers
 * Apple's documented defaults — storage allowed, no user info — which is the same "convenience door
 * answers the default" shape NSURLRequest's two doors have, and the probe pins it rather than assuming it.
 *
 * REFUSED BY NAME: the coder doors (-initWithCoder:/-encodeWithCoder:, NSSecureCoding), because a cached
 * response's archived form is Apple's own keyed structure with unpublished keys, so implementing it would
 * invent a new format wearing Apple's name — the same refusal NSURLRequest and NSURLResponse make; and
 * `NSURLCache`, which is the STORE and a later slice. This type says what MAY be kept, not where.
 */

#ifndef FOUNDATION_NSCACHEDURLRESPONSE_H
#define FOUNDATION_NSCACHEDURLRESPONSE_H

#import <Foundation/NSObject.h>

@class NSData;
@class NSDictionary;
@class NSURLResponse;

NS_ASSUME_NONNULL_BEGIN

/* WHETHER A STORE MAY KEEP AN ANSWER, which is a CALLER'S INSTRUCTION rather than a property of the
 * answer. The values are ours (D2): Apple publishes and documents the case names and no numbers. */
typedef NS_ENUM(NSUInteger, NSURLCacheStoragePolicy) {
	NSURLCacheStorageAllowed = 0,
	NSURLCacheStorageAllowedInMemoryOnly = 1,
	NSURLCacheStorageNotAllowed = 2,
};

@interface NSCachedURLResponse : NSObject <NSCopying>
{
	NSURLResponse *_response;
	NSData *_data;
	NSDictionary *_userInfo;
	NSURLCacheStoragePolicy _storagePolicy;
}

/* THE CONVENIENCE DOOR answers the documented defaults: NSURLCacheStorageAllowed and no user info. */
- (instancetype)initWithResponse:(NSURLResponse *)response data:(NSData *)data;
- (instancetype)initWithResponse:(NSURLResponse *)response
			    data:(NSData *)data
			userInfo:(nullable NSDictionary *)userInfo
		   storagePolicy:(NSURLCacheStoragePolicy)storagePolicy;

/* THE RESPONSE AND THE BYTES ARE THE ANSWER; userInfo is the caller's own note and is never interpreted
 * here. All four are snapshots: the init copies what it is handed, so a mutable dictionary a caller keeps
 * editing cannot change what is stored (the same rule NSURLRequest's header fields keep). */
@property (readonly, copy) NSURLResponse *response;
@property (readonly, copy) NSData *data;
@property (nullable, readonly, copy) NSDictionary *userInfo;
@property (readonly) NSURLCacheStoragePolicy storagePolicy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCACHEDURLRESPONSE_H */
