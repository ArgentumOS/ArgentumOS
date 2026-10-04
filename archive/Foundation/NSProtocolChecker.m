/*
 * NSProtocolChecker.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSProtocolChecker.h for what the filter asks and why a refused selector gets no signature. What is here is
 * that filter, in one place, used by the three doors that have to agree about it.
 */

#import <Foundation/NSProtocolChecker.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#include <objc/runtime.h>

/* DOES THE PROTOCOL DECLARE THIS SELECTOR — required or optional? Asked of the PROTOCOL rather than of the target,
 * because "the target responds to it" is a different question and answering that one would forward precisely the
 * methods a checker exists to keep back. */
static BOOL fn_protocol_declares(Protocol *protocol, SEL selector)
{
	if (protocol == nil || selector == NULL) {
		return NO;
	}
	if (protocol_getMethodDescription(protocol, selector, YES, YES).name != NULL) {
		return YES;		/* declared required */
	}
	return protocol_getMethodDescription(protocol, selector, NO, YES).name != NULL;	/* declared optional */
}

@implementation NSProtocolChecker

+ (id)protocolCheckerWithTarget:(NSObject *)anObject protocol:(Protocol *)aProtocol
{
	return [[[self alloc] initWithTarget:anObject protocol:aProtocol] autorelease];
}

- (instancetype)initWithTarget:(NSObject *)anObject protocol:(Protocol *)aProtocol
{
	/* NO `[super init]` HERE, AND THAT IS A MEASURED FACT ABOUT THIS LIBRARY RATHER THAN A STYLE: `NSProxy` IS A
	 * ROOT CLASS AND HAS NO `-init` AT ALL, so a subclass that called one would be sending a selector the proxy
	 * does not recognise — which is a crash on the first checker anybody makes. `+alloc` is the whole of
	 * construction for a proxy here. */
	_target = [anObject retain];
	_protocol = aProtocol;		/* protocol objects belong to the runtime: keeping one is not owning it */
	return self;
}

- (NSObject *)target
{
	return _target;
}

- (Protocol *)protocol
{
	return _protocol;
}

/* THE FILTER, ASKED ONCE AND USED BY ALL THREE DOORS. They must agree: a door that forwarded while another refused
 * would make the checker's boundary depend on how a caller reached the target. */
- (BOOL)fnMayForward:(SEL)selector
{
	return _target != nil && [_target respondsToSelector:selector] &&
	       fn_protocol_declares(_protocol, selector);
}

- (BOOL)respondsToSelector:(SEL)selector
{
	return [self fnMayForward:selector];
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector
{
	/* THE REFUSAL IS RAISED HERE RATHER THAN SIGNALLED BY A NIL SIGNATURE, AND THAT IS A MEASURED FACT ABOUT THIS
	 * RUNTIME: answering nil is what Apple's documentation implies, and this probe's first run took a SIGBUS at
	 * exactly the refusal — the forwarding path does not survive a missing signature, so a checker that returned
	 * nil would crash the caller instead of telling it. Raising from the door the runtime asks is the same refusal
	 * reached before the machinery that cannot carry it. */
	if (![self fnMayForward:selector]) {
		/* THE PROTOCOL CANNOT BE NAMED IN THE MESSAGE, AND THAT IS A CONSEQUENCE OF ANOTHER UNIT: §62.52 refused
		 * `NSStringFromProtocol` because this runtime's name-to-protocol lookup answers NULL, so the protocol's
		 * name has no door. The selector is enough to act on, and the omission is stated rather than silent. */
		[NSException raise:NSInvalidArgumentException
			    format:@"this protocol checker answers only for its protocol: %@ is not in it",
				   NSStringFromSelector(selector)];
	}
	return [_target methodSignatureForSelector:selector];
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
	SEL selector = [invocation selector];

	if ([self fnMayForward:selector]) {
		[invocation invokeWithTarget:_target];
		return;
	}
	/* THE BASE PROXY RAISES, which is the same refusal the missing signature produces, reached the other way: a
	 * caller who built the invocation by hand gets told as well. */
	[super forwardInvocation:invocation];
}

- (void)dealloc
{
	[_target release];
	[super dealloc];
}

@end
