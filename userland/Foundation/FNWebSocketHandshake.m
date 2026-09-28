/*
 * FNWebSocketHandshake.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE DIGEST IS THE WHOLE POINT OF THIS FILE (§4.2.2), and it is one line of arithmetic over two constants:
 * Sec-WebSocket-Accept = base64(SHA-1(key ++ "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")). The GUID is in the RFC
 * because a server that is not a WebSocket server must not be able to answer correctly BY ACCIDENT: hashing it
 * together with the client's nonce is what makes the reply unforgeable by anything that did not read the RFC.
 *
 * AND THE HEADERS ARE MATCHED WITHOUT REGARD TO CASE, which is HTTP's rule rather than a convenience: a server
 * that spells it `UPGRADE:` or `sec-websocket-accept:` is a correct server, and a client that compared bytes
 * would reject it.
 *
 * MRC, like every file in this library.
 */
#import <Foundation/FNWebSocketHandshake.h>
#import <Foundation/NSCharacterSet.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FN_WS_GUID	"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define FN_WS_KEY_BYTES	16

NSString *FNWebSocketCreateKey(void)
{
	unsigned char nonce[FN_WS_KEY_BYTES];
	NSData *bytes;
	NSString *key;
	size_t i;
	BOOL proper = (RAND_bytes(nonce, (int)sizeof(nonce)) == 1);

	/* §4.1: sixteen random bytes, AND THEY COME FROM LIBCRYPTO - which is already linked, because the digest
	 * below needs it. NOT A COUNTER AND NOT THE TIME: a nonce a peer can predict is one a cache can collide
	 * with, and that digest is the only thing standing between this client and a proxy answering for the server.
	 *
	 * AND THE FALLBACK IS A RECORDED LIMITATION RATHER THAN A SILENT ONE: if the CSPRNG cannot produce bytes
	 * (no entropy source yet, on a system whose entropy question is still open - docs/design/libressl-plan.md),
	 * the nonce is mixed from the clock and the pid. That is WEAK, it is named here, and it is what the handshake
	 * proceeds on - because refusing to open a socket at all is the worse of the two. */
	if(!proper) {
		unsigned long mix = (unsigned long)time(NULL) ^ ((unsigned long)getpid() << 16);

		for(i = 0; i < sizeof(nonce); i++) {
			mix = mix * 1103515245UL + 12345UL;
			nonce[i] = (unsigned char)((mix >> 16) & 0xFF);
		}
	}
	bytes = [[NSData alloc] initWithBytes:nonce length:sizeof(nonce)];
	key = [bytes base64EncodedStringWithOptions:0];
	[bytes release];
	return key;
}

NSString *FNWebSocketAcceptForKey(NSString *key)
{
	unsigned char digest[SHA_DIGEST_LENGTH];
	NSMutableData *material;
	NSData *hashed;
	NSString *accept;
	const char *keyBytes = [key UTF8String];

	material = [[NSMutableData alloc] initWithCapacity:strlen(keyBytes) + sizeof(FN_WS_GUID)];
	[material appendBytes:keyBytes length:strlen(keyBytes)];
	[material appendBytes:FN_WS_GUID length:sizeof(FN_WS_GUID) - 1];	/* the GUID without its NUL */
	SHA1((const unsigned char *)[material bytes], [material length], digest);
	[material release];

	hashed = [[NSData alloc] initWithBytes:digest length:sizeof(digest)];
	accept = [hashed base64EncodedStringWithOptions:0];
	[hashed release];
	return accept;
}

NSString *FNWebSocketUpgradeRequest(NSString *host, NSInteger port, NSString *path,
				    NSArray *protocols, NSString *key)
{
	NSMutableString *request = [[NSMutableString alloc] init];
	NSString *separator = (port == 80 || port == 443) ? @"" : [NSString stringWithFormat:@":%ld", (long)port];

	/* THE PATH IS NEVER EMPTY: "GET  HTTP/1.1" is not a request, and a ws:// URL with no path means "/". */
	[request appendFormat:@"GET %@ HTTP/1.1\r\n", ([path length] > 0 ? path : @"/")];
	[request appendFormat:@"Host: %@%@\r\n", host, separator];
	[request appendString:@"Upgrade: websocket\r\n"];
	[request appendString:@"Connection: Upgrade\r\n"];
	[request appendFormat:@"Sec-WebSocket-Key: %@\r\n", key];
	[request appendString:@"Sec-WebSocket-Version: 13\r\n"];
	/* NO Sec-WebSocket-Extensions, EVER: this library offers none (§59's boundary, and Apple's own shape).
	 * A server that insists on one fails the handshake, which is the honest answer. */
	if([protocols count] > 0) {
		[request appendString:@"Sec-WebSocket-Protocol: "];
		[request appendString:[protocols componentsJoinedByString:@", "]];
		[request appendString:@"\r\n"];
	}
	[request appendString:@"\r\n"];
	return request;
}

/* ONE HEADER'S VALUE, CASE-INSENSITIVELY, OUT OF AN HTTP REPLY'S TEXT. Returns nil when the header is absent -
 * which is a different answer from an empty value, and the caller needs to tell them apart. */
static NSString *fnHeaderValue(NSString *reply, NSString *name)
{
	NSArray *lines = [reply componentsSeparatedByString:@"\r\n"];
	NSUInteger i;

	for(i = 0; i < [lines count]; i++) {
		NSString *line = [lines objectAtIndex:i];
		NSRange colon = [line rangeOfString:@":"];

		if(colon.location == NSNotFound) {
			continue;
		}
		if([[line substringToIndex:colon.location] caseInsensitiveCompare:name] == NSOrderedSame) {
			NSString *value = [line substringFromIndex:colon.location + 1];

			return [value stringByTrimmingCharactersInSet:
				[NSCharacterSet whitespaceCharacterSet]];
		}
	}
	return nil;
}

BOOL FNWebSocketResponseIsUpgrade(NSData *response, NSString *key, NSString * _Nonnull * _Nonnull outProtocol,
				  NSString * _Nonnull * _Nonnull outReason)
{
	NSString *reply = [[NSString alloc] initWithData:response encoding:NSUTF8StringEncoding];
	NSString *firstLine;
	NSString *upgrade;
	NSString *accept;
	NSString *expected;
	NSString *protocol;
	NSArray *lines;
	NSArray *parts;
	BOOL good = NO;

	*outProtocol = nil;
	*outReason = nil;

	if(reply == nil) {
		*outReason = [@"the server's reply was not HTTP text" copy];
		return NO;
	}
	lines = [reply componentsSeparatedByString:@"\r\n"];
	firstLine = ([lines count] > 0) ? [lines objectAtIndex:0] : @"";
	parts = [firstLine componentsSeparatedByString:@" "];

	/* THE STATUS LINE, AND THE CODE IS WHAT MATTERS - not the reason text, which a server may spell any way it
	 * likes or omit entirely: "HTTP/1.1 101 Switching Protocols" and a bare "HTTP/1.1 101" must both pass. */
	if([parts count] < 2 || ![[parts objectAtIndex:1] isEqualToString:@"101"]) {
		*outReason = [[NSString alloc] initWithFormat:@"the server answered %@ instead of 101",
			      ([parts count] > 1 ? [parts objectAtIndex:1] : @"nothing")];
	} else if((upgrade = fnHeaderValue(reply, @"Upgrade")) == nil ||
		  [upgrade caseInsensitiveCompare:@"websocket"] != NSOrderedSame) {
		*outReason = [@"the reply did not say Upgrade: websocket" copy];
	} else if((accept = fnHeaderValue(reply, @"Sec-WebSocket-Accept")) == nil) {
		*outReason = [@"the reply carried no Sec-WebSocket-Accept" copy];
	} else {
		/* THE CHECK THAT MAKES THE HANDSHAKE WORTH DOING (§4.2.2): the digest ties the reply to the nonce THIS
		 * client sent, so a cache, a proxy, or any server that can answer 101 cannot take the connection. */
		expected = FNWebSocketAcceptForKey(key);
		if(![accept isEqualToString:expected]) {
			*outReason = [[NSString alloc] initWithFormat:@"Sec-WebSocket-Accept was '%@', not the digest of our key",
				      accept];
		} else {
			good = YES;
			protocol = fnHeaderValue(reply, @"Sec-WebSocket-Protocol");
			if(protocol != nil) {
				*outProtocol = [protocol copy];
			}
		}
		[expected release];
	}
	[reply release];
	return good;
}
