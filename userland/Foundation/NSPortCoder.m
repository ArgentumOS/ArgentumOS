/*
 * NSPortCoder.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSPortCoder.h for the envelope rule (one object, one component) and for the two refusals. What is here is the
 * archiver doing the encoding and the port doing the carrying.
 */

#import <Foundation/NSPortCoder.h>
#import <Foundation/NSPortMessage.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSData.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

@implementation NSPortCoder

+ (id)portCoderWithReceivePort:(NSPort *)receivePort
		      sendPort:(NSPort *)sendPort
		    components:(NSArray *)components
{
	return [[[self alloc] initWithReceivePort:receivePort
					 sendPort:sendPort
				       components:components] autorelease];
}

- (instancetype)initWithReceivePort:(NSPort *)receivePort
			   sendPort:(NSPort *)sendPort
			 components:(NSArray *)components
{
	self = [super init];
	if (self != nil) {
		_receivePort = [receivePort retain];
		_sendPort = [sendPort retain];
		_components = components != nil ? [components mutableCopy] : [[NSMutableArray alloc] init];
		_index = 0;
		/* A MESSAGE HERE CARRIES COPIES, so this is a fact about the transport rather than a choice. */
		_isBycopy = YES;
	}
	return self;
}

/* ONE OBJECT, ONE COMPONENT, WRITTEN AS A PROPERTY LIST — AND THE CHOICE WAS MEASURED RATHER THAN ASSUMED. The
 * first version used the keyed archiver, and the probe answered with the library's own words:
 * `+[NSKeyedArchiver unarchiveObjectWithData:] is not implemented` — the DECLARED door is a STUB, so a coder built
 * on it builds messages nothing can read. What IS implemented here is the property-list serialiser, which is what
 * the whole configuration system of this tree runs on.
 *
 * THAT MAKES THE BOUNDARY CONCRETE: the kinds a message can carry are the kinds a property list can write —
 * strings, numbers, dates, data, arrays and dictionaries — and anything else is REFUSED WITH ITS CLASS NAMED, and
 * a nil is refused too, because a property list has no way to write down an absence. */
- (void)encodeObject:(id)anObject
{
	NSError *error = nil;
	NSData *data;

	if (anObject == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"a nil cannot be carried in a message: a message is a property list, and a property "
				   @"list has no way to write down an absence. Use a sentinel of your own."];
	}
	/* THE FORMAT IS XML, AND THAT TOO WAS MEASURED: the binary format this first asked for made the writer answer
	 * nil for a dictionary it can plainly write, and the refusal below used to blame the OBJECT's class for it —
	 * a diagnostic that named the wrong cause. THE SERIALISER'S OWN WORDS ARE CARRIED THROUGH NOW, so the next
	 * failure of this kind says what actually went wrong. */
	data = [NSPropertyListSerialization dataWithPropertyList:anObject
							   format:NSPropertyListXMLFormat_v1_0
							  options:0
							    error:&error];
	if (data == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"%@ cannot be carried in a message: a message is a property list, and the kinds it can "
				   @"write are strings, numbers, dates, data, arrays and dictionaries (%@)", [anObject class],
				   error != nil ? [error localizedDescription] : @"the serialiser gave no reason"];
	}
	[_components addObject:data];
}

- (id)decodeObject
{
	NSData *data;

	if (_index >= [_components count]) {
		return nil;		/* the message carried fewer objects than the reader asked for */
	}
	data = [_components objectAtIndex:_index];
	_index++;
	return [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL];
}

- (void)dispatch
{
	NSPortMessage *message;

	if (_sendPort == nil) {
		return;			/* a coder with nowhere to send is a message nobody asked for */
	}
	message = [[NSPortMessage alloc] initWithSendPort:_sendPort
					      receivePort:_receivePort
					       components:_components];
	[message setMsgid:0];
	/* A FAILED SEND IS NOT RAISED HERE: the caller sees an unanswered request, which is the failure it can act
	 * on, and the port's own invalidate-on-EPIPE has already told the connection. */
	(void)[message sendBeforeDate:[NSDate date]];
	[message release];
}

- (BOOL)isBycopy { return _isBycopy; }
- (BOOL)isByref { return !_isBycopy; }

/* ---- THE TWO DOORS THIS TRANSPORT CANNOT HONOUR ------------------------------------------------ */

- (void)encodePortObject:(NSPort *)aPort
{
	/* RAISED RATHER THAN DROPPED: a caller who asked to send a port has to be told that nothing was sent, because
	 * a component that is not data cannot be written down (§62.53) and a port RIGHT is a kernel feature this system
	 * does not have. */
	(void)aPort;
	[NSException raise:NSInvalidArgumentException
		    format:@"a port cannot be carried in a message: this library's transport sends components that are "
			   @"data, and a port here is a descriptor rather than a right another process could hold"];
}

- (NSPort *)decodePortObject
{
	[NSException raise:NSInvalidArgumentException
		    format:@"a port cannot be received from a message: nothing in this library's transport can carry one"];
	return nil;
}

- (void)dealloc
{
	[_receivePort release];
	[_sendPort release];
	[_components release];
	[super dealloc];
}

@end
