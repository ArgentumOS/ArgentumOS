/*
 * NSPortNameServer.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSPortNameServer` — A NAME FOR A PORT (§62.54). Struck by name by §11.5 with the rest of the family, and back
 * under §62.24's policy.
 *
 * WHAT A NAME SERVER IS FOR, AND WHAT THIS ONE CAN HONESTLY DO. Apple's publishes a port so that ANOTHER PROCESS
 * can find it, and its three subclasses exist because each named a different transport. This system has ONE
 * transport — a socket pair — so there is ONE REGISTRY, shared by all three subclasses, and IT IS PROCESS-LOCAL:
 * `-portForName:` answers a port connected to the registered one when the name was registered BY THIS PROCESS, and
 * nil otherwise. A name another process could find needs a published address and an accept path, and that is what
 * `NSConnection` owns; this header says so rather than letting a caller discover it.
 *
 * THREE RULES OF THE REGISTRY, STATED HERE BECAUSE THEY ARE CHOICES RATHER THAN FACTS:
 *
 *   * A NAME IS A KEY: registering an already-registered name REPLACES the port and answers YES. Replacing is the
 *     useful reading — a process that restarts and re-registers its own name should not have to remove first — and
 *     it is stated so a caller can rely on it either way.
 *   * WHAT IS HANDED OUT IS THE PORT TO SEND TO, not the port that answers for the name.
 *   * REMOVING IS EXPLICIT: `-removePortForName:` forgets a name, and an invalidated port forgets its own.
 */

#ifndef FOUNDATION_NSPORTNAMESERVER_H
#define FOUNDATION_NSPORTNAMESERVER_H

#import <Foundation/NSObject.h>

@class NSPort, NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSPortNameServer : NSObject

/* THE DEFAULT SERVER, which is the message-port one here: `NSMessagePortNameServer` is what Apple's
 * `+defaultPortNameServer` answers on a system whose default transport is the in-process message port. */
+ (NSPortNameServer *)defaultPortNameServer;

- (BOOL)registerPort:(NSPort *)port name:(NSString *)name;
- (void)removePortForName:(NSString *)name;

/* The port to send to, or nil when this process has no such name. */
- (nullable NSPort *)portForName:(NSString *)name;

/* A HOST IS NOT A THING A PROCESS-LOCAL REGISTRY CAN LOOK UP, so this answers the local port when the host is nil,
 * empty, "localhost", the loopback address, or this machine's own name — and nil for anything else, which is the
 * honest answer to "find me that name on another machine" when no mechanism exists to ask one. */
- (nullable NSPort *)portForName:(NSString *)name host:(nullable NSString *)host;


/* §63.211: the SYSTEM spelling of the default name server — Apple's own alias of the door above. */
+ (NSPortNameServer *)systemDefaultPortNameServer;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPORTNAMESERVER_H */
