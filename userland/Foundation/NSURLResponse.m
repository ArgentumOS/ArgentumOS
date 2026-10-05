/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLResponse.m — the value's machinery. The design and the reasoning are in NSURLResponse.h.
 */
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

/* NIL-SAFE EQUALITY: two absent fields are equal, and an absent one never equals a present one. */
static BOOL fn_response_object_equal(NSObject *a, NSObject *b)
{
	if (a == b) {
		return YES;
	}
	if (a == nil || b == nil) {
		return NO;
	}
	return [a isEqual:b];
}

@implementation NSURLResponse

- (instancetype)initWithURL:(NSURL *)URL
		   MIMEType:(NSString *)MIMEType
	expectedContentLength:(NSInteger)length
	   textEncodingName:(NSString *)name
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_url = [URL copy];
	_MIMEType = [MIMEType copy];
	_expectedContentLength = (long long)length;
	_textEncodingName = [name copy];
	return self;
}

- (NSURL *)URL
{
	return _url;
}

- (NSString *)MIMEType
{
	return _MIMEType;
}

- (long long)expectedContentLength
{
	return _expectedContentLength;
}

- (NSString *)textEncodingName
{
	return _textEncodingName;
}

- (NSString *)suggestedFilename
{
	/* THE RULE (Apple's, phrased once): the last path component of the URL's path, or "Unknown" when
	 * there is no path. A file URL therefore answers its file name, and a URL with an empty path
	 * answers "Unknown" rather than an empty string. */
	NSString *path = [_url path];
	NSString *last;

	if (path == nil || [path length] == 0) {
		return @"Unknown";
	}
	if ([path hasSuffix:@"/"]) {
		path = [path substringToIndex:[path length] - 1];
	}
	last = [path lastPathComponent];
	return [last length] > 0 ? last : @"Unknown";
}

- (BOOL)isEqual:(id)other
{
	NSURLResponse *response;

	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSURLResponse class]]) {
		return NO;
	}
	response = (NSURLResponse *)other;
	return fn_response_object_equal(_url, [response URL]) &&
	       fn_response_object_equal(_MIMEType, [response MIMEType]) &&
	       _expectedContentLength == [response expectedContentLength] &&
	       fn_response_object_equal(_textEncodingName, [response textEncodingName]);
}

- (NSUInteger)hash
{
	/* THE HASH IS OURS (D2): folded from the two components the equality test leans on most. */
	return [_url hash] ^ (NSUInteger)_expectedContentLength;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@ mime=%@ length=%d>",
		[self class], _url, _MIMEType, (int)_expectedContentLength];
}

- (void)dealloc
{
	[_url release];
	[_MIMEType release];
	[_textEncodingName release];
	[super dealloc];
}

- (id)copy
{
	return [self retain];	/* immutable: the copy IS the receiver (plan §15.2) */
}

@end
