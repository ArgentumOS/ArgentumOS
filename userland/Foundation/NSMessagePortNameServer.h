/*
 * NSMessagePortNameServer.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSMessagePortNameServer` (§62.54) — Apple's name server for its in-process message port, and HERE THE ONE THAT
 * ACTUALLY WORKS: a message port is a socket pair, so the far end is something this process holds and can publish.
 * It answers from the shared registry described in NSPortNameServer.h, and `+defaultPortNameServer` answers it.
 */

#ifndef FOUNDATION_NSMESSAGEPORTNAMESERVER_H
#define FOUNDATION_NSMESSAGEPORTNAMESERVER_H

#import <Foundation/NSPortNameServer.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSMessagePortNameServer : NSPortNameServer

/* A NAME SERVER IS A TABLE, so there is one of these: two tables that disagreed would be a name that resolves
 * differently depending on who asked for it. */
/* THE CONCRETE TYPE, NOT THE BASE ONE, because that is what a caller holds and what makes this server's own doors
 * visible through it. Apple's returns an `id` for the same reason. */
+ (NSMessagePortNameServer *)sharedInstance;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMESSAGEPORTNAMESERVER_H */
