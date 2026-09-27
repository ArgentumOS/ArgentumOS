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
#import <Foundation/NSKeyedArchiver.h>
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

/* ONE OBJECT, ONE COMPONENT, WRITTEN AS A KEYED ARCHIVE — AND THE FIRST VERSION OF THIS METHOD IS WHY THIS COMMENT
 * IS LONG. It used the property list instead, on the strength of a probe that had failed with the library's own
 * words:
 *
 *     +[NSKeyedArchiver unarchiveObjectWithData:] is not implemented
 *
 * AND THAT WAS A MIS-ATTRIBUTION, measured when §62.57 re-examined it: `+unarchiveObjectWithData:` is declared on
 * `NSKeyedUnarchiver` (NSKeyedArchiver.h) and implemented on it (NSKeyedArchiver.m, in that class's own block) — the
 * call had been made on `NSKeyedArchiver`, which does not declare it, so that message was the library CORRECTLY
 * reporting an unrecognised selector and NAMING THE CLASS it went to. The class in the message WAS the diagnosis,
 * and it was read as a fact about the library; the recusal that followed — a property-list carrier, and a nil that
 * cannot be written down — was a boundary invented from a wrong cause.
 *
 * (AND THE WORD THAT OPENS AN IMPLEMENTATION BLOCK APPEARS NOWHERE IN THIS FILE, DELIBERATELY: this tree's sweep
 * attributes a file's blocks with a LAST-MATCH rule, so a single comment that spells that keyword — as an earlier
 * version of this very comment did — silently moves the file's WHOLE method list onto the class the comment named,
 * and the sweep then reports every selector here as implemented nowhere. Measured; it cost one build.)
 *
 * SO A MESSAGE CARRIES ANY `NSCoding` OBJECT, and the kinds are not the property list's: the objects table of an
 * archive is what a message is made of. The refusal that remains is the ARCHIVER's own and it names the class — an
 * object that is neither a value type nor `NSCoding` raises there, on this side, before anything is sent. */
- (void)encodeObject:(id)anObject
{
	/* NO ERROR TO REPORT HERE, AND THAT IS A PROPERTY OF THE ARCHIVER RATHER THAN OPTIMISM: it answers data or it
	 * raises. A NIL OBJECT IS CARRIED — it is `$null`, index 0 of the objects table, and the reader answers nil for
	 * that reference (`NSKeyedArchiver.m`, `-fnObjectAtIndex:`) — WHICH IS A FACT ABOUT THE CODER AND NOT ABOUT A
	 * CALL: a nil ARGUMENT is still refused one level up, by `NSConnection`, because the message's crate is an array
	 * and an array cannot hold a nil. The refusal there states that ground, and §62.57 is why it is stated. */
	[_components addObject:[NSKeyedArchiver archivedDataWithRootObject:anObject]];
}

- (id)decodeObject
{
	NSData *data;

	if (_index >= [_components count]) {
		return nil;		/* the message carried fewer objects than the reader asked for */
	}
	data = [_components objectAtIndex:_index];
	_index++;
	return [NSKeyedUnarchiver unarchiveObjectWithData:data];
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
