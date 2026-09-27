/*
 * NSMessagePort.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSMessagePort.h for what the name means and which class this derives from, and why. What is here is the
 * registration, and the one thing a registration must never do: outlive the port it names.
 */

#import <Foundation/NSMessagePort.h>
#import <Foundation/NSPortNameServer.h>
#import <Foundation/NSMessagePortNameServer.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSString.h>

@implementation NSMessagePort

- (instancetype)initWithName:(NSString *)name
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if ([name length] == 0) {
		[self release];
		return nil;
	}
	_name = [name copy];
	/* THE FAR END IS HANDED TO THE NAME SERVER AND IS THEN GONE — `-peerPort` gives it up, which is what makes
	 * this port the one that ANSWERS for the name while the registry holds the one to SEND to. */
	{
		NSPort *peer = [self peerPort];

		if (peer != nil) {
			[[NSPortNameServer defaultPortNameServer] registerPort:peer name:_name];
		}
	}
	return self;
}

- (NSString *)name
{
	return _name;
}

- (void)invalidate
{
	/* BEFORE `-super`, BECAUSE A REGISTRATION IS SOMETHING THIS OBJECT STILL OWNS. A name outliving its port
	 * would be a name that answers with a port nobody is reading. */
	if ([_name length] > 0) {
		[[NSPortNameServer defaultPortNameServer] removePortForName:_name];
	}
	[super invalidate];
}

- (void)dealloc
{
	[_name release];
	[super dealloc];
}

@end
