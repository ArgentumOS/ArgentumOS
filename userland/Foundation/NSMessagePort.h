/*
 * NSMessagePort.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSMessagePort` — A PORT WITH A NAME (§62.54), the naming half of the family §11.5 struck by name.
 *
 * IT IS A PORT OVER A SOCKET PAIR, WHICH IS WHAT `NSMachPort` ALREADY IS, so this class DERIVES FROM IT rather
 * than duplicating the pair, the transport and the ownership rules — the same kind of substitution `NSMachPort.h`
 * states one level up, and for the same reason: this system has one port mechanism. What this class adds is a
 * NAME. `-initWithName:` creates the pair and registers THE FAR END in the default name server under that name, so
 * a caller can send to the name; `-name` reports it; and the port takes its own name out of the registry when it
 * is invalidated, because a name that outlives its port is a name that finds a dead end.
 *
 * A CONSEQUENCE FOR A CALLER, STATED RATHER THAN LEFT TO BE FOUND: this class answers `-machPort` and `-peerPort`
 * as well, because it is a mach port here. `-peerPort` answers nil for a NAMED port — the far end was handed to
 * the name server, and "handed over once" means exactly that.
 */

#ifndef FOUNDATION_NSMESSAGEPORT_H
#define FOUNDATION_NSMESSAGEPORT_H

#import <Foundation/NSMachPort.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSMessagePort : NSMachPort
{
	NSString *_name;		/* copied; absent for a port made without one */
}

/* Creates a port and registers the port to SEND TO under `name`. Nil when `name` is empty, because a named port
 * with no name is the one thing this initializer promises not to make. */
- (nullable instancetype)initWithName:(nullable NSString *)name;

- (nullable NSString *)name;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMESSAGEPORT_H */
