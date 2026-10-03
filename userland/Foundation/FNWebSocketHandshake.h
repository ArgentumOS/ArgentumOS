/*
 * FNWebSocketHandshake.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNWebSocketHandshake — RFC 6455's OPENING HANDSHAKE AS A LAYER: the request to send, and what a reply must be.
 * §59 slice 3a, internal.
 *
 * WHY THIS IS A LAYER AND NOT THE FIRST HUNDRED LINES OF THE TASK, and it is the same reason the codec is one:
 * THE PART OF A WEBSOCKET CLIENT THAT IS MOST DANGEROUS TO GET WRONG IS ALSO THE PART THAT NEEDS NO SOCKET. The
 * accept digest (§4.2.2) is what tells a client that the thing answering is really a WebSocket server and not a
 * cache, a proxy or a page that happened to return 200 - and a client that accepts a WRONG accept has silently
 * given away the connection. So it is a function of a key and a reply, and this file is where it can be tested
 * against RFC 6455's own worked example rather than against a server that agrees with whatever we wrote.
 *
 * NOT PUBLIC API AND NOT IN Foundation.h (FNPointerTable's precedent). ⚠ The session task that used to
 * compose it left with the session family (§63.159); what composes it now is the probe that drives it
 * against RFC 6455's worked example.
 *
 * THE ONE THING THAT IS NOT HERE, deliberately: the RANDOMNESS behind a key. `FNWebSocketCreateKey` supplies it,
 * because a key generator is not a handshake rule - and everything else takes the key as an ARGUMENT, so the
 * whole of the rest is deterministic.
 */
#ifndef FOUNDATION_FNWEBSOCKETHANDSHAKE_H
#define FOUNDATION_FNWEBSOCKETHANDSHAKE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>

NS_ASSUME_NONNULL_BEGIN

/* §4.1: a NONCE OF SIXTEEN RANDOM BYTES, base64-encoded - which is what goes in Sec-WebSocket-Key. +1. */
NSString *FNWebSocketCreateKey(void);

/* §4.2.2's digest: base64(SHA-1(key + the GUID)), where the GUID is the protocol's own constant and the key is
 * the BASE64 TEXT (not the sixteen bytes it stands for). +1. */
NSString *FNWebSocketAcceptForKey(NSString *key);

/* THE UPGRADE REQUEST, as text: the request line, Host, Upgrade, Connection, the version, the key, and the
 * protocol list when there is one. `path` is the URL's path INCLUDING its query, and "/" when the URL has none. */
NSString *FNWebSocketUpgradeRequest(NSString *host, NSInteger port, NSString *path,
				    NSArray * _Nullable protocols, NSString *key);

/* IS THIS REPLY AN UPGRADE, AND MAY WE TRUST IT? YES only when the status line is 101 AND the reply carries
 * Upgrade: websocket AND Sec-WebSocket-Accept is exactly the digest above. On YES, `outProtocol` is the
 * server's Sec-WebSocket-Protocol when it sent one (else nil) - the caller checks it against what it asked for.
 * On NO, `outReason` is why, for the error the task reports. Both out-parameters are +1 when set. */
BOOL FNWebSocketResponseIsUpgrade(NSData *response, NSString *key, NSString * _Nonnull * _Nonnull outProtocol,
				  NSString * _Nonnull * _Nonnull outReason);

NS_ASSUME_NONNULL_END

#endif /* _FOUNDATION_FNWEBSOCKETHANDSHAKE_H */
