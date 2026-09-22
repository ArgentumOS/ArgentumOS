/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLResponse — A RESPONSE IS A VALUE. docs/design/foundation-plan.md W7, §46.
 *
 * THE ANSWER'S METADATA, and nothing else: what a URL answered with — its MIME type, its length, its
 * text encoding — is a value a caller reads, and the BYTES it describes are somebody else's (the
 * transport's, in a later slice). So this class is the value half of the pair NSURLRequest opened, and
 * it carries no connection, no session and no loader.
 *
 * `expectedContentLength` IS `long long` AND ITS UNKNOWN VALUE ALREADY SHIPS: NSURLResponseUnknownLength
 * is `-1`, declared in NSObjCRuntime.h since F13.5's arithmetic sweep, and it is the value this class
 * answers when the length is not known.
 *
 * -suggestedFilename IS A RULE, NOT A TABLE: the last path component of the URL's path, or "Unknown"
 * when there is none — which is Apple's documented behaviour, phrased as the rule rather than as the
 * examples. A file URL therefore answers its file name.
 *
 * REFUSED BY NAME: `-initWithCoder:`/`-encodeWithCoder:` (NSSecureCoding) — a response's archived form
 * is Apple's own keyed structure with unpublished keys, so it is refused rather than invented, exactly
 * as NSURLRequest refuses the coder door.
 */

#ifndef FOUNDATION_NSURLRESPONSE_H
#define FOUNDATION_NSURLRESPONSE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>	/* NSURLResponseUnknownLength */

@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@interface NSURLResponse : NSObject <NSCopying>
{
	NSURL *_url;
	NSString *_MIMEType;
	long long _expectedContentLength;
	NSString *_textEncodingName;
}

/* THE DESIGNATED INITIALIZER, and Apple's shape: a response is exactly these four facts. A nil MIME type
 * or text-encoding name is allowed and means "not known". */
- (instancetype)initWithURL:(NSURL *)URL
		   MIMEType:(nullable NSString *)MIMEType
	expectedContentLength:(NSInteger)length
	   textEncodingName:(nullable NSString *)name;

@property (nullable, readonly, copy) NSURL *URL;
@property (nullable, readonly, copy) NSString *MIMEType;
/* NSURLResponseUnknownLength (-1) when the length is not known. */
@property (readonly) long long expectedContentLength;
@property (nullable, readonly, copy) NSString *textEncodingName;
/* The last path component of the URL's path, or "Unknown" when there is none. */
@property (readonly, copy) NSString *suggestedFilename;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLRESPONSE_H */
