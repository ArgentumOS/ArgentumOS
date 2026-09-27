/*
 * NSSocketPortNameServer.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSSocketPortNameServer` (§62.54) — Apple's name server for the SOCKET port, and the one door in this family
 * that REFUSES. `-portForName:` is the shared registry's (a name is a name), but `-registerPort:name:` answers NO
 * for a socket port, and the reason is measured rather than cautious: a socket port is an ADDRESS, publishing one
 * means telling another process where to connect, and a connection needs an accept path this library does not have
 * yet — `NSConnection` is where it belongs. `NSMachPort`'s pair-backed ports can be registered here, because their
 * far end is something this process holds.
 */

#ifndef FOUNDATION_NSSOCKETPORTNAMESERVER_H
#define FOUNDATION_NSSOCKETPORTNAMESERVER_H

#import <Foundation/NSPortNameServer.h>

@class NSPort, NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSSocketPortNameServer : NSPortNameServer

+ (NSSocketPortNameServer *)sharedInstance;

/* Answers NO when the port is not one whose far end this process holds — see the header note. */
- (BOOL)registerPort:(NSPort *)port name:(NSString *)name;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSOCKETPORTNAMESERVER_H */
