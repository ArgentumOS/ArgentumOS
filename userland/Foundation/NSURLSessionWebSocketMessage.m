/*
 * NSURLSessionWebSocketMessage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE EXCLUSIVITY RULE IS ENFORCED BY CONSTRUCTION RATHER THAN BY CARE: each initialiser stores ONE payload and
 * leaves the other nil, and there is no other way into this class - so there is no code path that can produce a
 * message with both set, and none that can produce one with neither. The type is set in the same place, so it
 * cannot disagree with the payload either.
 *
 * AND THE PAYLOAD IS COPIED, WHICH IS OURS RATHER THAN APPLE'S (they document the accessors and not what
 * happens to the object handed in). Copying is the choice a VALUE type owes its caller: a message built from a
 * mutable NSData must not change when that NSData does - otherwise "what I sent" depends on what someone else
 * does to a buffer afterwards. A test asserts it, because a choice nobody asserts is a choice nobody has.
 *
 * MRC, like every file in this library: `copy` returns +1 and `dealloc` releases, and there is no annotated
 * ownership anywhere near it.
 */
#import <Foundation/NSURLSessionWebSocketMessage.h>

@interface NSURLSessionWebSocketMessage ()
{
	NSURLSessionWebSocketMessageType _type;
	NSData *_data;
	NSString *_string;
}
@end

@implementation NSURLSessionWebSocketMessage

- (instancetype)initWithData:(NSData *)data
{
	self = [super init];
	if(self == nil) {
		return nil;
	}
	if(data == nil) {
		/* A MESSAGE WITHOUT A PAYLOAD IS NOT A MESSAGE: the page gives two initialisers and no third, and a nil
		 * one would make the exclusivity rule above false ("one of them is always nil" - both would be). */
		[self release];
		return nil;
	}
	_type = NSURLSessionWebSocketMessageTypeData;
	_data = [data copy];
	_string = nil;
	return self;
}

- (instancetype)initWithString:(NSString *)string
{
	self = [super init];
	if(self == nil) {
		return nil;
	}
	if(string == nil) {
		[self release];
		return nil;
	}
	_type = NSURLSessionWebSocketMessageTypeString;
	_string = [string copy];
	_data = nil;
	return self;
}

/* THE THREE ACCESSORS THE CLASS STORED FOR AND NEVER ANSWERED. `_type` was written by both initialisers and
 * read by nobody, which is exactly the shape §62's report exists to catch: declared in the header, absent
 * from the implementation, invisible until something asks. The exclusivity the initialisers maintain (one of
 * -data and -string is always nil) is what -type is FOR, so the getters hand back the state as it is. */
- (nullable NSData *)data
{
	return _data;
}

- (nullable NSString *)string
{
	return _string;
}

- (NSURLSessionWebSocketMessageType)type
{
	return _type;
}

- (void)dealloc
{
	[_data release];
	[_string release];
	[super dealloc];
}

@end
