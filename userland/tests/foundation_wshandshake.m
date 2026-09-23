/*
 * foundation_wshandshake — RFC 6455's OPENING HANDSHAKE as a layer (§59 slice 3a).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NO SOCKET, NO SERVER, AND THAT IS THE POINT: the part of a WebSocket client most dangerous to get wrong is also
 * the part that needs no connection. A client that accepts a WRONG Sec-WebSocket-Accept has silently handed its
 * connection to whatever answered - a cache, a proxy, or a page that happened to return 101 - so the digest is
 * tested against RFC 6455'S OWN WORKED EXAMPLE rather than against a server that agrees with whatever we wrote.
 *
 * WHAT IT PINS:
 *   * THE KNOWN ANSWER (§4.2.2): the key dGhlIHNhbXBsZSBub25jZQ== must produce s3pPLMBiTxaQ9kYGzzhZRbK+xOo=.
 *     If that line is wrong, nothing else here means anything;
 *   * THE NONCE IS SIXTEEN RANDOM BYTES (§4.1), base64-encoded - and two calls differ;
 *   * the REQUEST: the request line (with a path, even when the URL has none), Host, Upgrade, Connection, the
 *     version, the key, and the protocol list when there is one - and NO Sec-WebSocket-Extensions ever;
 *   * THE REPLY: 101 plus Upgrade plus the right accept, with the headers matched WITHOUT REGARD TO CASE (HTTP's
 *     rule, and a server spelling it UPGRADE: is a correct server); a bare "HTTP/1.1 101" with no reason phrase
 *     passes; a wrong accept, a non-101 status, or a 101 without the upgrade header are all refused with a reason;
 *   * the negotiated protocol comes back when the server sent one.
 *
 * ARC, like every probe.
 */
#import <Foundation/Foundation.h>
#import <Foundation/FNWebSocketHandshake.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-WSHANDSHAKE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-WSHANDSHAKE %s FAIL: %s\n", name, [[why description] UTF8String]);
	}
}

static NSData *fnReply(NSString *text)
{
	return [text dataUsingEncoding:NSUTF8StringEncoding];
}

int main(void)
{
	/* THE RFC'S OWN EXAMPLE, VERBATIM FROM §4.2.2's handshake. */
	NSString *rfcKey = @"dGhlIHNhbXBsZSBub25jZQ==";
	NSString *rfcAccept = @"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
	NSString *accept;
	NSString *request;
	NSString *key;
	NSString *key2;
	NSString *protocol = nil;
	NSString *reason = nil;
	NSData *keyBytes;
	BOOL upgraded;

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE KNOWN ANSWER ---------------------------------------------------------------------------- */
	accept = FNWebSocketAcceptForKey(rfcKey);
	check("the-accept-matches-rfc-6455s-own-example",
	      [accept isEqualToString:rfcAccept],
	      [NSString stringWithFormat:@"RFC 6455 §4.2.2 says '%@' for '%@'; we made '%@'", rfcAccept, rfcKey, accept]);

	/* --- THE NONCE ------------------------------------------------------------------------------------ */
	key = FNWebSocketCreateKey();
	key2 = FNWebSocketCreateKey();
	keyBytes = [[NSData alloc] initWithBase64EncodedString:key options:0];
	check("a-key-is-sixteen-random-bytes-in-base64",
	      keyBytes != nil && [keyBytes length] == 16,
	      [NSString stringWithFormat:@"'%@' decoded to %lu bytes", key, (unsigned long)[keyBytes length]]);
	check("and-two-keys-are-not-the-same",
	      ![key isEqualToString:key2],
	      @"a nonce a peer can predict is one a cache can collide with");

	/* --- THE REQUEST ---------------------------------------------------------------------------------- */
	request = FNWebSocketUpgradeRequest(@"example.com", 80, @"/chat", @[ @"chat", @"superchat" ], rfcKey);
	check("the-request-asks-for-the-upgrade",
	      [request hasPrefix:@"GET /chat HTTP/1.1\r\n"] &&
	      [request rangeOfString:@"\r\nHost: example.com\r\n"].location != NSNotFound &&
	      [request rangeOfString:@"\r\nUpgrade: websocket\r\n"].location != NSNotFound &&
	      [request rangeOfString:@"\r\nConnection: Upgrade\r\n"].location != NSNotFound &&
	      [request rangeOfString:@"\r\nSec-WebSocket-Version: 13\r\n"].location != NSNotFound &&
	      [request rangeOfString:[NSString stringWithFormat:@"\r\nSec-WebSocket-Key: %@\r\n", rfcKey]].location != NSNotFound &&
	      [request hasSuffix:@"\r\n\r\n"],
	      request);
	check("and-the-protocol-list-travels-comma-separated",
	      [request rangeOfString:@"\r\nSec-WebSocket-Protocol: chat, superchat\r\n"].location != NSNotFound,
	      request);
	check("and-no-extensions-are-ever-offered",
	      [request rangeOfString:@"Sec-WebSocket-Extensions"].location == NSNotFound,
	      @"§59's boundary: a server that insists on one fails the handshake, which is the honest answer");
	{
		NSString *bare = FNWebSocketUpgradeRequest(@"example.com", 8080, @"", nil, rfcKey);

		check("and-a-url-with-no-path-still-has-one",
		      [bare hasPrefix:@"GET / HTTP/1.1\r\n"] &&
		      [bare rangeOfString:@"\r\nHost: example.com:8080\r\n"].location != NSNotFound &&
		      [bare rangeOfString:@"Sec-WebSocket-Protocol"].location == NSNotFound,
		      bare);
	}

	/* --- THE REPLY ------------------------------------------------------------------------------------ */
	upgraded = FNWebSocketResponseIsUpgrade(
		fnReply([NSString stringWithFormat:@"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %@\r\n\r\n", rfcAccept]),
		rfcKey, &protocol, &reason);
	check("a-good-reply-is-an-upgrade", upgraded && protocol == nil && reason == nil,
	      [NSString stringWithFormat:@"upgraded=%d reason=%@", (int)upgraded, reason]);

	upgraded = FNWebSocketResponseIsUpgrade(
		fnReply([NSString stringWithFormat:@"HTTP/1.1 101 Switching Protocols\r\nUPGRADE: WebSocket\r\nSEC-WEBSOCKET-ACCEPT: %@\r\n\r\n", rfcAccept]),
		rfcKey, &protocol, &reason);
	check("and-the-headers-do-not-care-about-case", upgraded,
	      @"HTTP's rule: a server spelling it UPGRADE: is a correct server, and a client comparing bytes would refuse it");

	upgraded = FNWebSocketResponseIsUpgrade(fnReply(@"HTTP/1.1 101\r\nUpgrade: websocket\r\n"), rfcKey, &protocol, &reason);
	check("and-a-bare-101-with-no-reason-phrase-is-refused-for-the-right-reason",
	      !upgraded && reason != nil && [reason rangeOfString:@"Sec-WebSocket-Accept"].location != NSNotFound,
	      [NSString stringWithFormat:@"the status is fine and the ACCEPT is missing: %@", reason]);

	protocol = nil;
	reason = nil;
	upgraded = FNWebSocketResponseIsUpgrade(
		fnReply([NSString stringWithFormat:@"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: WRONGWRONGWRONGWRONGWRONGWRO=\r\nSec-WebSocket-Protocol: chat\r\n\r\n"]),
		rfcKey, &protocol, &reason);
	check("a-wrong-accept-is-refused", !upgraded && protocol == nil && reason != nil,
	      [NSString stringWithFormat:@"this is the check that keeps a proxy from taking the connection: %@", reason]);

	reason = nil;
	upgraded = FNWebSocketResponseIsUpgrade(fnReply(@"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"), rfcKey, &protocol, &reason);
	check("and-a-non-101-reply-is-refused-with-the-status-named",
	      !upgraded && reason != nil && [reason rangeOfString:@"200"].location != NSNotFound,
	      [NSString stringWithFormat:@"the caller needs to know WHAT answered: %@", reason]);

	reason = nil;
	upgraded = FNWebSocketResponseIsUpgrade(
		fnReply([NSString stringWithFormat:@"HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: %@\r\n\r\n", rfcAccept]),
		rfcKey, &protocol, &reason);
	check("and-a-101-without-the-upgrade-header-is-refused", !upgraded && reason != nil,
	      [NSString stringWithFormat:@"101 alone is not an upgrade: %@", reason]);

	protocol = nil;
	upgraded = FNWebSocketResponseIsUpgrade(
		fnReply([NSString stringWithFormat:@"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: %@\r\nSec-WebSocket-Protocol: superchat\r\n\r\n", rfcAccept]),
		rfcKey, &protocol, &reason);
	check("and-the-negotiated-protocol-comes-back",
	      upgraded && [protocol isEqualToString:@"superchat"],
	      [NSString stringWithFormat:@"the caller must be able to check it against what it asked for: %@", protocol]);

	printf("FOUNDATION-WSHANDSHAKE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-WSHANDSHAKE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-WSHANDSHAKE DONE\n");
	return failc ? 1 : 0;
}
