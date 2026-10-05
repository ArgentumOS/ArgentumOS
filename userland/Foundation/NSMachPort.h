/*
 * NSMachPort.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSMachPort` — THE PORT APPLE'S `+[NSPort port]` ANSWERS (§62.53).
 *
 * IT WAS STRUCK BY NAME BY §11.5, with the whole family, "because Mach is what it exists for" — and §62.24's
 * policy retired the deprecation ground as a strike, so it is back. WHAT COMES BACK IS NOT MACH: this system has
 * no Mach port rights, no port namespace and no port naming, so the port here is built on the one mechanism this
 * library has — AN AF_UNIX SOCKET PAIR, with the library's message transport (`NSSocketPort.m`, §62.53) carrying
 * what a message is. The substitution is stated here rather than left to be discovered, and three consequences of
 * it are refused AT THE DOOR rather than approximated:
 *
 *   * `+portWithMachPort:`, `-initWithMachPort:` and their options-taking forms REFUSE (nil). A bare NUMBER names
 *     no port on this system: Mach port rights are the thing that makes a number meaningful, and there are none.
 *     A port here is MADE and its handle read with `-machPort` — never rebuilt from a number.
 *   * `-machPort` answers THIS SYSTEM'S handle for the port: the descriptor of its own end of the pair. Apple's
 *     is a Mach port right's name; this is an `int`, and saying so is the honest version of the same call.
 *   * `NSMachPortDelegate`'s `-handleMachMessage:` is DECLARED AND NEVER CALLED, and that is stated rather than
 *     hidden: a `mach_msg_header_t` is not something this system produces, so a delegate of a port in this
 *     library receives messages through the door it inherits from `NSPortDelegate` — `-handlePortMessage:`.
 *
 * `-peerPort` IS OURS, and it is the stand-in for what Mach called a port namespace: with no rights to name,
 * the way a second holder reaches this port is THE OTHER END OF ITS PAIR. It may be asked for ONCE — the end is
 * handed over, and a second ask answers nil rather than making two ports share one descriptor.
 *
 * APPLE'S `NSMachPort` IS `NSSocketPort`'S SIBLING AND HERE IT IS A SUBCLASS, which is the second stated
 * substitution: the socket port IS the message transport in this library, so a sibling would have to duplicate
 * it. A subclass inherits the transport, the run-loop source and the ownership rules instead.
 */

#ifndef FOUNDATION_NSMACHPORT_H
#define FOUNDATION_NSMACHPORT_H

#import <Foundation/NSPort.h>
#import <Foundation/NSSocketPort.h>

@class NSData;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* Apple's options for the deallocation of port rights, which are the rights this system does not have. They are
 * declared because a caller's source names them, and they are the only values `-machPort`-taking doors would
 * accept — and those doors refuse, so no value here ever reaches anything. The VALUES are ours (§11.6.1 D2):
 * a bit each, in the order Apple's documentation lists them. */
typedef enum {
	NSMachPortDeallocateNone	= 0,
	NSMachPortDeallocateSendRight	= (1UL << 0),
	NSMachPortDeallocateReceiveRight = (1UL << 1)
} NSMachPortOptions;

@protocol NSMachPortDelegate <NSPortDelegate>
@optional
/* DECLARED FOR THE SHAPE OF APPLE'S PROTOCOL AND NEVER CALLED — see the header note. The door a delegate is
 * actually handed a message through is `-handlePortMessage:`, inherited above. */
- (void)handleMachMessage:(void *)msg;
@end

@interface NSMachPort : NSSocketPort
{
	int _peerSocket;		/* the other end of this port's pair, or -1 once it has been handed out */
}

+ (nullable NSPort *)portWithMachPort:(uint32_t)machPort;
+ (nullable NSPort *)portWithMachPort:(uint32_t)machPort options:(NSMachPortOptions)options;
- (nullable instancetype)initWithMachPort:(uint32_t)machPort;
- (nullable instancetype)initWithMachPort:(uint32_t)machPort options:(NSMachPortOptions)options;

/* This system's handle for this port: the descriptor of its own end of the pair. */
- (uint32_t)machPort;

/* OURS — the other end, handed over once. Nil for a port that is not a pair, or one whose end is already out. */
- (nullable NSPort *)peerPort;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMACHPORT_H */
