/*
 * NSConnection.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSConnection.h for what a connection is here and why the reply goes to a name. What is here is the request
 * and the answer, in one place each: `-handlePortMessage:` serves, `-fnSendInvocation:error:` asks.
 *
 * THE WIRE IS A DICTIONARY, AND IT IS THE ARCHIVER'S: a request carries a selector name, its OBJECT arguments and
 * the name to answer; a reply carries either a value or a message saying why there is none. Nothing in this file
 * formats a byte — that is `NSPortCoder` and the keyed archiver behind it.
 */

#import <Foundation/NSConnection.h>
#import <Foundation/NSDistantObject.h>
#import <Foundation/NSPortCoder.h>
#import <Foundation/NSPortMessage.h>
#import <Foundation/NSMessagePort.h>
#import <Foundation/NSPortNameServer.h>
#import <Foundation/NSMessagePortNameServer.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSException.h>
#include <objc/runtime.h>
#include <unistd.h>

NSString *const NSConnectionDidInitializeNotification = @"NSConnectionDidInitializeNotification";
NSString *const NSConnectionDidDieNotification = @"NSConnectionDidDieNotification";
NSString *const NSConnectionReplyMode = @"NSConnectionReplyMode";
NSString *const NSFailedAuthenticationException = @"NSFailedAuthenticationException";

/* THE REQUEST AND THE REPLY, WHICH ARE THE WHOLE WIRE. */
static NSString *const FNSelectorKey = @"selector";
static NSString *const FNArgumentsKey = @"arguments";
static NSString *const FNReplyNameKey = @"reply";
static NSString *const FNValueKey = @"value";
static NSString *const FNErrorKey = @"error";

/* A REPLY NAME NOBODY ELSE WILL MAKE, because two clients in one process must not answer each other's requests. */
static NSUInteger gReplyCounter = 0;

static NSString *fn_next_reply_name(void)
{
	gReplyCounter++;
	return [NSString stringWithFormat:@"NSConnectionReply-%d-%lu", (int)getpid(), (unsigned long)gReplyCounter];
}

@implementation NSConnection

+ (NSConnection *)connectionWithReceivePort:(NSPort *)receivePort sendPort:(NSPort *)sendPort
{
	return [[[self alloc] initWithReceivePort:receivePort sendPort:sendPort] autorelease];
}

- (instancetype)initWithReceivePort:(NSPort *)receivePort sendPort:(NSPort *)sendPort
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* A CONNECTION THAT WAS NOT GIVEN A RECEIVE PORT MAKES A NAMED ONE, and that is the design rather than a
	 * convenience: a NAMED message port publishes its far end in the name server, so the name IS the address a
	 * reply can be sent to (see the header). */
	if (receivePort == nil) {
		_receivePort = [[NSMessagePort alloc] initWithName:fn_next_reply_name()];
	} else {
		_receivePort = [receivePort retain];
	}
	_sendPort = [sendPort retain];
	_valid = _receivePort != nil;
	[_receivePort setDelegate:self];
	[[NSNotificationCenter defaultCenter] postNotificationName:NSConnectionDidInitializeNotification object:self];
	return self;
}

- (NSPort *)receivePort { return _receivePort; }
- (NSPort *)sendPort { return _sendPort; }
- (BOOL)isValid { return _valid; }

- (id)rootObject { return _rootObject; }

- (void)setRootObject:(id)anObject
{
	id old = _rootObject;

	_rootObject = [anObject retain];
	[old release];
}

- (NSDistantObject *)rootProxy
{
	return [[[NSDistantObject alloc] initWithTarget:nil connection:self] autorelease];
}

- (void)addRunLoop:(NSRunLoop *)runLoop
{
	/* EVERY CONNECTION'S PORT IS WATCHED IN `NSConnectionReplyMode`, AND THAT IS THE ONE NON-OBVIOUS RULE HERE.
	 * Apple's door takes only a run loop, so the mode is this library's to choose — and the choice follows from
	 * what an in-process exchange IS: the client waits by running the loop in reply mode, and the service has to be
	 * pumped BY THAT SAME WAIT for the request to be served at all. Putting a service in the default mode while the
	 * client waits in reply mode is a DEADLOCK, measured by thinking it through before writing the probe: the
	 * service's source would never be watched while the client waited. */
	[_receivePort scheduleInRunLoop:runLoop forMode:NSConnectionReplyMode];
}

- (void)removeRunLoop:(NSRunLoop *)runLoop
{
	[_receivePort removeFromRunLoop:runLoop forMode:NSConnectionReplyMode];
}

/* ---- PUBLISHING A NAME --------------------------------------------------------------------------- */

- (BOOL)registerName:(NSString *)name withNameServer:(NSPortNameServer *)server
{
	if ([name length] == 0 || _receivePort == nil) {
		return NO;
	}
	/* THE NAME A PORT WAS MADE WITH IS PUBLISHED BY THE PORT ITSELF — that is what `NSMessagePort`'s initializer
	 * does — so publishing it again is what "already done" answers. */
	if ([_receivePort isKindOfClass:[NSMessagePort class]] && [[(NSMessagePort *)_receivePort name] isEqual:name]) {
		return server == [NSPortNameServer defaultPortNameServer] ||
		       server == [NSMessagePortNameServer sharedInstance];
	}
	/* A SECOND NAME FOR ONE PORT IS NOT EXPRESSIBLE HERE, AND THE REASON IS MEASURED: publishing a name means
	 * handing the registry the port's FAR END, and `-peerPort` hands it over exactly once (§62.53) — the name a
	 * message port is created with is the name it answers to. A caller who needs another name makes another port. */
	return NO;
}

- (BOOL)registerName:(NSString *)name
{
	return [self registerName:name withNameServer:[NSPortNameServer defaultPortNameServer]];
}

/* ---- THE TWO CLASS DOORS ------------------------------------------------------------------------- */

+ (NSConnection *)serviceConnectionWithName:(NSString *)name
				 rootObject:(id)rootObject
			    usingNameServer:(NSPortNameServer *)server
{
	NSMessagePort *port;
	NSConnection *connection;

	if ([name length] == 0 || rootObject == nil) {
		return nil;
	}
	if (server != [NSPortNameServer defaultPortNameServer] && server != [NSMessagePortNameServer sharedInstance]) {
		/* A NAME IS PUBLISHED BY THE PORT THAT OWNS THE FAR END, and that registration is in the default name
		 * server; another server cannot be handed a name it has no port for. */
		return nil;
	}
	port = [[NSMessagePort alloc] initWithName:name];
	if (port == nil) {
		return nil;
	}
	connection = [[self alloc] initWithReceivePort:port sendPort:nil];
	[port release];
	if (connection == nil) {
		return nil;
	}
	[connection setRootObject:rootObject];
	[connection addRunLoop:[NSRunLoop currentRunLoop]];	/* a service has to be listening to be a service */
	return [connection autorelease];
}

+ (NSConnection *)serviceConnectionWithName:(NSString *)name rootObject:(id)rootObject
{
	return [self serviceConnectionWithName:name
				    rootObject:rootObject
			       usingNameServer:[NSPortNameServer defaultPortNameServer]];
}

+ (id)rootProxyForConnectionWithRegisteredName:(NSString *)name
					  host:(NSString *)hostName
			       usingNameServer:(NSPortNameServer *)server
{
	NSPort *servicePort;
	NSConnection *connection;
	id proxy;

	if (server != [NSPortNameServer defaultPortNameServer] && server != [NSMessagePortNameServer sharedInstance]) {
		return nil;
	}
	servicePort = [server portForName:name host:hostName];
	if (servicePort == nil) {
		return nil;
	}
	connection = [[self alloc] initWithReceivePort:nil sendPort:servicePort];
	if (connection == nil) {
		return nil;
	}
	/* THE CLIENT'S RECEIVE PORT IS WATCHED IN REPLY MODE, because a reply is the only thing it will ever receive
	 * while it waits — which is what `NSConnectionReplyMode` is for. */
	[connection->_receivePort scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSConnectionReplyMode];
	proxy = [connection rootProxy];
	[connection release];		/* the proxy holds it (NSDistantObject.h says so) */
	return proxy;
}

+ (id)rootProxyForConnectionWithRegisteredName:(NSString *)name host:(NSString *)hostName
{
	return [self rootProxyForConnectionWithRegisteredName:name
							host:hostName
					     usingNameServer:[NSPortNameServer defaultPortNameServer]];
}

/* ---- SERVING ------------------------------------------------------------------------------------ */

- (void)fnAnswerRequest:(NSDictionary *)request
{
	NSString *selectorName = [request objectForKey:FNSelectorKey];
	NSArray *arguments = [request objectForKey:FNArgumentsKey];
	NSString *replyName = [request objectForKey:FNReplyNameKey];
	SEL selector = selectorName != nil ? NSSelectorFromString(selectorName) : NULL;
	NSMethodSignature *signature = (selector != NULL) ? [_rootObject methodSignatureForSelector:selector] : nil;
	NSString *error = nil;
	id value = nil;
	NSMutableDictionary *reply;
	NSPort *replyPort;
	NSPortCoder *coder;

	if (_rootObject == nil) {
		error = @"this connection answers for no object";
	} else if (signature == nil) {
		error = [NSString stringWithFormat:@"no such method: %@", selectorName];
	} else if ([signature numberOfArguments] != [arguments count] + 2) {
		error = [NSString stringWithFormat:@"%@ takes %lu argument(s) and %lu were sent",
			 selectorName, (unsigned long)([signature numberOfArguments] - 2),
			 (unsigned long)[arguments count]];
	} else {
		NSUInteger i;
		const char *returnType = [signature methodReturnType];

		for (i = 0; i < [arguments count] && error == nil; i++) {
			const char *type = [signature getArgumentTypeAtIndex:i + 2];

			if (type == NULL || type[0] != '@') {
				error = [NSString stringWithFormat:@"%@ takes something other than objects, and only objects "
					  @"cross a message here", selectorName];
			}
		}
		if (error == nil && returnType != NULL && returnType[0] != '@' && returnType[0] != 'v') {
			error = [NSString stringWithFormat:@"%@ answers with something other than an object, and only "
				  @"objects cross a message here", selectorName];
		}
		if (error == nil) {
			NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];

			for (i = 0; i < [arguments count]; i++) {
				id argument = [arguments objectAtIndex:i];

				[invocation setArgument:&argument atIndex:(NSInteger)(i + 2)];
			}
			/* THE SELECTOR HAS TO BE SET AS WELL AS THE TARGET: `-invokeWithTarget:` sets only the target, and
			 * an invocation without a selector is not a call — the library's own message says so. */
			[invocation setSelector:selector];
			[invocation invokeWithTarget:_rootObject];
			if ([signature methodReturnLength] > 0 && returnType != NULL && returnType[0] == '@') {
				[invocation getReturnValue:&value];
			}
		}
	}
	/* THE ANSWER GOES TO A NAME, because a port cannot be carried in a message. */
	reply = [[NSMutableDictionary alloc] init];
	if (error != nil) {
		[reply setObject:error forKey:FNErrorKey];
	} else if (value != nil) {
		[reply setObject:value forKey:FNValueKey];
	}
	replyPort = [[NSPortNameServer defaultPortNameServer] portForName:replyName];
	coder = [[NSPortCoder alloc] initWithReceivePort:nil sendPort:replyPort components:nil];
	/* A MUTABLE DICTIONARY GOES AS IT IS, and that is a §62.57 change: the archiver writes the class it was handed
	 * (`NSMutableDictionary`) and the reader knows BOTH spellings, so the immutable copy that used to be made here
	 * for the property-list serialiser's sake buys nothing now. */
	[coder encodeObject:reply];
	[coder dispatch];
	[coder release];
	[reply release];
}

- (void)handlePortMessage:(NSPortMessage *)message
{
	NSPortCoder *coder = [[NSPortCoder alloc] initWithReceivePort:_receivePort
							     sendPort:_sendPort
							   components:[message components]];
	id decoded = [coder decodeObject];

	[coder release];
	if (![decoded isKindOfClass:[NSDictionary class]]) {
		return;			/* not a request this library wrote; a message with no protocol is not a request */
	}
	if ([decoded objectForKey:FNReplyNameKey] == nil) {
		/* A REPLY ARRIVING IS NOT A REQUEST: the client is waiting, and this is what it waited for. */
		id old = _replyValue;

		_replyValue = [decoded retain];
		[old release];
		_waitingForReply = NO;
		return;
	}
	[self fnAnswerRequest:decoded];
}

/* ---- ASKING ------------------------------------------------------------------------------------- */

- (id)fnSendInvocation:(NSInvocation *)invocation error:(NSString **)outError
{
	NSMethodSignature *signature = [invocation methodSignature];
	NSMutableArray *arguments = [[NSMutableArray alloc] init];
	NSMutableDictionary *request = [[NSMutableDictionary alloc] init];
	NSDictionary *reply;
	NSPortCoder *coder;
	NSString *error = nil;
	id value = nil;
	NSUInteger i;

	if (outError != NULL) {
		*outError = nil;
	}
	if (_sendPort == nil || ![self isValid]) {
		if (outError != NULL) {
			*outError = @"this connection has nowhere to send";
		}
		[arguments release];
		[request release];
		return nil;
	}
	for (i = 2; i < [signature numberOfArguments]; i++) {
		id argument = nil;

		[invocation getArgument:&argument atIndex:(NSInteger)i];
		/* A NIL ARGUMENT IS REFUSED RATHER THAN SENT — AND §62.57 CORRECTED THE GROUND, WHICH USED TO BE "a message
		 * is a property list, and a property list cannot write down an absence". The coder can write one now: a nil
		 * is `$null` in an archive. THE CRATE IS WHAT REFUSES: a message's arguments are an ARRAY, and an array
		 * cannot hold a nil at all, so there is nothing for the coder to encode. Inventing a sentinel here would be
		 * this library deciding what a caller's nil means, so the caller is told to pass one of their own. */
		if (argument == nil) {
			if (outError != NULL) {
				*outError = [NSString stringWithFormat:@"%@ was sent a nil argument, and a nil cannot cross "
					     @"a message: pass a sentinel of your own",
					     NSStringFromSelector([invocation selector])];
			}
			[arguments release];
			[request release];
			return nil;
		}
		[arguments addObject:argument];
	}
	[request setObject:NSStringFromSelector([invocation selector]) forKey:FNSelectorKey];
	[request setObject:arguments forKey:FNArgumentsKey];
	if ([_receivePort isKindOfClass:[NSMessagePort class]]) {
		[request setObject:[(NSMessagePort *)_receivePort name] forKey:FNReplyNameKey];
	}
	coder = [[NSPortCoder alloc] initWithReceivePort:_receivePort sendPort:_sendPort components:nil];
	/* A MUTABLE DICTIONARY GOES AS IT IS, and that is a §62.57 change: the archiver writes the class it was handed
	 * (`NSMutableDictionary`) and the reader knows BOTH spellings, so the immutable copy that used to be made here
	 * for the property-list serialiser's sake buys nothing now. */
	[coder encodeObject:request];
	_waitingForReply = YES;
	[coder dispatch];
	[coder release];

	/* WAITING IS RUNNING THE LOOP IN REPLY MODE: the receive port was scheduled in it by the class method that
	 * made this connection, so a reply arrives while the caller waits. Bounded, because a service that never
	 * answers must not hang the caller for ever. */
	for (i = 0; i < 250 && _waitingForReply; i++) {
		[[NSRunLoop currentRunLoop] runMode:NSConnectionReplyMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
	if (_waitingForReply) {
		_waitingForReply = NO;
		error = @"the service did not answer";
	} else {
		reply = _replyValue;
		_replyValue = nil;		/* taken by the caller of this method below, then released */
		error = [reply objectForKey:FNErrorKey];
		value = [reply objectForKey:FNValueKey];
		[reply autorelease];
	}
	[arguments release];
	[request release];
	if (outError != NULL) {
		*outError = error;
	}
	return value;
}

- (BOOL)fnIsService
{
	return _rootObject != nil;
}

/* ---- TAKING IT APART --------------------------------------------------------------------------- */

- (void)invalidate
{
	if (!_valid) {
		return;
	}
	_valid = NO;
	[_receivePort invalidate];
	[[NSNotificationCenter defaultCenter] postNotificationName:NSConnectionDidDieNotification object:self];
}

- (void)dealloc
{
	[_receivePort setDelegate:nil];
	[_receivePort release];
	[_sendPort release];
	[_rootObject release];
	[_name release];
	[_replyValue release];
	[super dealloc];
}

@end
