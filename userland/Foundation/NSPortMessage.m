/*
 * NSPortMessage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSPortMessage.h for the family's history and for why a component must be data.
 */

#import <Foundation/NSPortMessage.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

@implementation NSPortMessage

- (instancetype)initWithSendPort:(NSPort *)sendPort
		     receivePort:(NSPort *)receivePort
		      components:(NSArray *)components
{
	self = [super init];
	if (self != nil) {
		_sendPort = [sendPort retain];
		_receivePort = [receivePort retain];
		_components = components != nil ? [components mutableCopy] : [[NSMutableArray alloc] init];
		_msgid = 0;
	}
	return self;
}

- (NSArray *)components { return _components; }
- (NSPort *)receivePort { return _receivePort; }
- (NSPort *)sendPort { return _sendPort; }

- (unsigned int)msgid { return _msgid; }
- (void)setMsgid:(unsigned int)msgid { _msgid = msgid; }

- (BOOL)sendBeforeDate:(NSDate *)date
{
	if (_sendPort == nil) {
		return NO;		/* a message with nowhere to go is not an error, it is an unsent message */
	}
	/* THE PORT DOES THE WRITING: a message is what a caller builds, and how it becomes bytes is the transport's
	 * business. A port that cannot carry it — one that is invalid, or a component that is not data — answers NO
	 * from there, so this door has one answer and one place to give it. */
	return [_sendPort sendBeforeDate:date msgid:(NSUInteger)_msgid components:_components
				    from:_receivePort reserved:0];
}

- (void)dealloc
{
	[_sendPort release];
	[_receivePort release];
	[_components release];
	[super dealloc];
}

@end
