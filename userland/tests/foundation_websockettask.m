/*
 * foundation_websockettask — THE TASK ITSELF (§59 slice 3b): the layers meeting the substrate, over a REAL
 * connection to a RAW peer.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE PEER IS THE PROBE'S OWN, and it is raw in the sense that matters: it speaks HTTP for exactly one exchange
 * (the upgrade), and RFC 6455 for everything after it THROUGH THE SAME CODEC THE TASK USES - a client frame is
 * unmasked on the server's side and a server frame is never masked, which is the direction rule the codec already
 * enforces. Nothing else is between the two: no TLS, no proxy, no library.
 *
 * WHAT THE LEGS PROVE, IN THE ORDER THEY HAPPEN, because the order IS the contract:
 *   * THE UPGRADE: the peer accepts only when the request carries a key it can digest, and the client only
 *     continues when the reply's accept matches that key - so `didOpenWithProtocol:` firing at all means both
 *     halves of §4.2.2 agreed;
 *   * THE SUBPROTOCOL: asked for by list, chosen by the server, REPORTED BY THE DELEGATE (the door exists for
 *     nothing else);
 *   * A MESSAGE BOTH WAYS: the peer ECHOES what it read, so the message the client receives is the peer's own
 *     account of what it got rather than a copy of what the client thinks it sent;
 *   * A PING AND A PONG: the pong handler is the only place that answer can arrive;
 *   * THE CLOSE: the peer answers with ITS code and reason (1001 + "peer says bye"), and both the delegate door
 *     and -closeCode/-closeReason must carry them;
 *   * and ONE ENDING, because a task that reports twice is worse than one that never does.
 *
 * ARC, like every probe.
 */
#import <Foundation/Foundation.h>
#import <Foundation/FNWebSocketFraming.h>
#import <Foundation/FNWebSocketHandshake.h>	/* the peer computes the accept with the SAME layer the client checks it with */
#include <tls.h>	/* the wss: leg's peer: the same substrate §58.1 used, over a blocking descriptor */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>	/* mkdir, for the directory the certificate is made in */

#define FN_WS_TEST_PORT	46911

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-WSTASK %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-WSTASK %s FAIL: %s\n", name, [[why description] UTF8String]);
	}
}

static void fnPause(int milliseconds)
{
	struct pollfd none;

	none.fd = -1;
	none.events = 0;
	none.revents = 0;
	poll(&none, 0, milliseconds);
}

/* --- THE PEER: ONE UPGRADE, THEN RFC 6455 THROUGH THE CODEC ---------------------------------------- */

static ssize_t fnReadSome(int fd, char *buffer, size_t room)
{
	ssize_t got;

	for(;;) {
		got = read(fd, buffer, room);
		if(got >= 0) {
			return got;
		}
		if(errno != EAGAIN && errno != EINTR) {
			return got;
		}
		fnPause(10);
	}
}

static void fnPeer(int listener)
{
	char buffer[4096];
	char reply[512];
	ssize_t got;
	int fd;
	int n;
	const char *keyLine = NULL;
	NSString *key = nil;
	NSString *expectedAccept = nil;	/* NOT 'accept': that name is the socket call below, and a local would shadow it */
	NSMutableData *arrived = [NSMutableData data];
	NSData *out;

	fd = accept(listener, NULL, NULL);
	if(fd < 0) {
		_exit(1);
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);

	/* THE UPGRADE REQUEST, read until its blank line. The KEY is the only part that matters to the reply. */
	n = 0;
	for(;;) {
		got = fnReadSome(fd, buffer + n, sizeof(buffer) - 1 - n);
		if(got <= 0) {
			_exit(2);
		}
		n += (int)got;
		buffer[n] = '\0';
		if(strstr(buffer, "\r\n\r\n") != NULL) {
			break;
		}
		if(n > (int)sizeof(buffer) - 2) {
			_exit(3);
		}
	}
	keyLine = strstr(buffer, "Sec-WebSocket-Key: ");
	if(keyLine == NULL) {
		_exit(4);		/* no key: nothing to accept, and the client's check depends on this */
	}
	{
		char *end = strstr(keyLine, "\r\n");
		char value[128];
		size_t length = (size_t)(end - (keyLine + 19));

		if(end == NULL || length >= sizeof(value)) {
			_exit(5);
		}
		memcpy(value, keyLine + 19, length);
		value[length] = '\0';
		key = [NSString stringWithUTF8String:value];
		expectedAccept = FNWebSocketAcceptForKey(key);
	}
	/* ONE SUBPROTOCOL IS NEGOTIATED when the client offered any, and "chat" is chosen so the delegate has a
	 * value to report that the client did not invent. */
	{
		const char *chosen = (strstr(buffer, "Sec-WebSocket-Protocol: ") != NULL) ? "chat" : NULL;

		n = snprintf(reply, sizeof(reply),
			     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			     "Sec-WebSocket-Accept: %s\r\n%s%s\r\n",
			     [expectedAccept UTF8String],
			     chosen != NULL ? "Sec-WebSocket-Protocol: " : "",
			     chosen != NULL ? chosen : "");
		write(fd, reply, (size_t)n);
	}

	/* THE CLIENT'S FIRST FRAME IS THE MESSAGE: echo it back UNMASKED, so the client's receive handler is
	 * answering the peer's own account of what it read. */
	for(;;) {
		FNWebSocketFrame frame;
		NSInteger consumed;

		got = fnReadSome(fd, buffer, sizeof(buffer));
		if(got <= 0) {
			_exit(6);
		}
		[arrived appendBytes:buffer length:(NSUInteger)got];
		consumed = FNWebSocketParseFrame([arrived bytes], [arrived length], YES, &frame);
		if(consumed == 0) {
			continue;
		}
		if(consumed < 0) {
			_exit(7);
		}
		if(frame.opcode == FNWebSocketOpcodeText || frame.opcode == FNWebSocketOpcodeBinary) {
			/* UNMASK BEFORE ECHOING, AND THIS IS THE HALF OF §5.3 THE CODEC DELIBERATELY LEAVES TO ITS CALLER:
			 * a parse hands back the bytes as they arrived, so a client frame's payload is STILL MASKED. The first
			 * version echoed those bytes verbatim and the client received its own message as garbage - the same
			 * "asserting the wrong half of a contract" mistake slice 2's probe made, met here from the other side. */
			NSMutableData *plain = [NSMutableData dataWithBytes:frame.payload
								     length:(NSUInteger)frame.payloadLength];

			if(frame.masked) {
				FNWebSocketApplyMask((uint8_t *)[plain mutableBytes], [plain length], frame.maskKey);
			}
			out = FNWebSocketCreateFrame(YES, frame.opcode, [plain bytes],
						     (size_t)[plain length], NULL);
			write(fd, [out bytes], [out length]);
			[arrived replaceBytesInRange:NSMakeRange(0, (NSUInteger)consumed)
					   withBytes:(const void *)"" length:0];
			break;
		}
		[arrived replaceBytesInRange:NSMakeRange(0, (NSUInteger)consumed)
				   withBytes:(const void *)"" length:0];
	}

	/* THEN THE PING, THEN THE CLOSE - each answered in kind. The close's OWN code and reason are the peer's, and
	 * they are what the delegate and the two properties must end up carrying. */
	{
		int sawPing = 0;

		for(;;) {
			FNWebSocketFrame frame;
			NSInteger consumed;

			got = fnReadSome(fd, buffer, sizeof(buffer));
			if(got <= 0) {
				_exit(8);
			}
			[arrived appendBytes:buffer length:(NSUInteger)got];
			consumed = FNWebSocketParseFrame([arrived bytes], [arrived length], YES, &frame);
			if(consumed == 0) {
				continue;
			}
			if(consumed < 0) {
				_exit(9);
			}
			if(frame.opcode == FNWebSocketOpcodePing && !sawPing) {
				sawPing = 1;
				out = FNWebSocketCreateFrame(YES, FNWebSocketOpcodePong, frame.payload,
							     (size_t)frame.payloadLength, NULL);
				write(fd, [out bytes], [out length]);
			} else if(frame.opcode == FNWebSocketOpcodeClose) {
				uint8_t payload[16] = { 0x03, 0xE9 };	/* 1001, going away */

				memcpy(payload + 2, "peer says bye", 13);
				out = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeClose, payload, 15, NULL);
				write(fd, [out bytes], [out length]);
				break;
			}
			[arrived replaceBytesInRange:NSMakeRange(0, (NSUInteger)consumed)
					   withBytes:(const void *)"" length:0];
		}
	}
	close(fd);
	_exit(0);
}

/* --- THE wss: LEG'S PEER: THE SAME RAW PEER, OVER TLS ----------------------------------------------- */

/* THE CERTIFICATE, made on the guest by `openssl req` - the same fixture §58.1's probe uses, for the same
 * reason (the tool is the pin's own generator and the entropy question is the plan's, not a probe's). */
static int fn_makeCertificate(const char *dir, const char *certPath, const char *keyPath)
{
	pid_t child;
	int status = -1;
	char logPath[512];

	snprintf(logPath, sizeof(logPath), "%s/req.log", dir);
	child = fork();
	if(child == 0) {
		int log = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if(log >= 0) {
			dup2(log, 1);
			dup2(log, 2);
		}
		execl("/System/Tools/openssl", "openssl", "req", "-x509", "-newkey", "rsa:2048",
		      "-keyout", keyPath, "-out", certPath, "-days", "1", "-nodes",
		      "-subj", "/CN=localhost", (char *)NULL);
		_exit(127);
	}
	if(child < 0) {
		return -1;
	}
	if(waitpid(child, &status, 0) < 0) {
		return -1;
	}
	return status;
}

/* A BLOCKING-WITH-PAUSES TLS READ, because libtls asks again rather than waiting when it wants more bytes. */
static ssize_t fnTlsRead(struct tls *connection, char *buffer, size_t room)
{
	ssize_t got;

	for(;;) {
		got = tls_read(connection, buffer, room);
		if(got != TLS_WANT_POLLIN && got != TLS_WANT_POLLOUT) {
			return got;
		}
		fnPause(5);
	}
}

static int fnTlsWriteAll(struct tls *connection, const char *bytes, size_t length)
{
	size_t sent = 0;

	while(sent < length) {
		ssize_t put = tls_write(connection, bytes + sent, length - sent);

		if(put == TLS_WANT_POLLIN || put == TLS_WANT_POLLOUT) {
			fnPause(5);
			continue;
		}
		if(put <= 0) {
			return -1;
		}
		sent += (size_t)put;
	}
	return 0;
}

/* THE PEER, OVER TLS: WHAT THE PLAINTEXT ONE DOES, THROUGH A TUNNEL - because the point of this leg is that the
 * TASK is what puts the TLS there, and the peer must be a TLS server for that to mean anything. It speaks the
 * upgrade in HTTP and RFC 6455 through the same codec, and answers ONE message and the close. */
@interface FNTlsWsPeer : NSObject
{
	@public
	BOOL bound;
	int port;
	char cert[512];
	char key[512];
}
- (void)run;
@end

@implementation FNTlsWsPeer

- (void)run
{
	struct tls_config *config = tls_config_new();
	struct tls *ctx = tls_server();
	struct tls *connection = NULL;
	struct sockaddr_in addr;
	int listener;
	int fd;
	int one = 1;
	char buffer[4096];
	int n = 0;
	NSMutableData *arrived = [NSMutableData data];
	NSString *expectedAccept = nil;

	if(config == NULL || ctx == NULL) {
		return;
	}
	if(tls_config_set_cert_file(config, cert) == -1 || tls_config_set_key_file(config, key) == -1 ||
	   tls_configure(ctx, config) == -1) {
		return;
	}
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if(listener < 0) {
		return;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 4) != 0) {
		return;
	}
	bound = YES;
	fd = accept(listener, NULL, NULL);
	if(fd < 0) {
		return;
	}
	{
		int flags = fcntl(fd, F_GETFL, 0);

		if(flags >= 0) {
			fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);	/* libtls's default BIO is a blocking one */
		}
	}
	for(;;) {
		int rv = tls_accept_socket(ctx, &connection, fd);

		if(rv == 0) {
			break;
		}
		if(rv != TLS_WANT_POLLIN && rv != TLS_WANT_POLLOUT) {
			return;
		}
	}
	for(;;) {
		int rv = tls_handshake(connection);

		if(rv == 0) {
			break;
		}
		if(rv != TLS_WANT_POLLIN && rv != TLS_WANT_POLLOUT) {
			return;
		}
	}

	/* THE UPGRADE, through the tunnel: the request until its blank line, then the digest, then the 101. */
	for(;;) {
		ssize_t got = fnTlsRead(connection, buffer + n, sizeof(buffer) - 1 - n);

		if(got <= 0) {
			return;
		}
		n += (int)got;
		buffer[n] = '\0';
		if(strstr(buffer, "\r\n\r\n") != NULL) {
			break;
		}
		if(n > (int)sizeof(buffer) - 2) {
			return;
		}
	}
	{
		char *keyLine = strstr(buffer, "Sec-WebSocket-Key: ");

		if(keyLine == NULL) {
			return;
		}
		{
			char *end = strstr(keyLine, "\r\n");
			char value[128];
			size_t length = (size_t)(end - (keyLine + 19));

			if(end == NULL || length >= sizeof(value)) {
				return;
			}
			memcpy(value, keyLine + 19, length);
			value[length] = '\0';
			expectedAccept = FNWebSocketAcceptForKey([NSString stringWithUTF8String:value]);
		}
		{
			char reply[512];
			int written = snprintf(reply, sizeof(reply),
					       "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
					       "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n"
					       "Sec-WebSocket-Protocol: chat\r\n\r\n",
					       [expectedAccept UTF8String]);

			if(fnTlsWriteAll(connection, reply, (size_t)written) != 0) {
				return;
			}
		}
	}

	/* ONE MESSAGE, ECHOED - unmasked, because that is what a server's frames are - and then the close. */
	for(;;) {
		FNWebSocketFrame frame;
		NSInteger consumed;
		ssize_t got = fnTlsRead(connection, buffer, sizeof(buffer));

		if(got <= 0) {
			return;
		}
		[arrived appendBytes:buffer length:(NSUInteger)got];
		consumed = FNWebSocketParseFrame([arrived bytes], [arrived length], YES, &frame);
		if(consumed == 0) {
			continue;
		}
		if(consumed < 0) {
			return;
		}
		if(frame.opcode == FNWebSocketOpcodeText || frame.opcode == FNWebSocketOpcodeBinary) {
			NSMutableData *plain = [NSMutableData dataWithBytes:frame.payload
								     length:(NSUInteger)frame.payloadLength];
			NSData *out;

			if(frame.masked) {
				FNWebSocketApplyMask((uint8_t *)[plain mutableBytes], [plain length], frame.maskKey);
			}
			out = FNWebSocketCreateFrame(YES, frame.opcode, [plain bytes], (size_t)[plain length], NULL);
			if(fnTlsWriteAll(connection, [out bytes], [out length]) != 0) {
				return;
			}
		} else if(frame.opcode == FNWebSocketOpcodeClose) {
			uint8_t payload[16] = { 0x03, 0xE9 };	/* 1001, going away */
			NSData *out;

			memcpy(payload + 2, "peer says bye", 13);
			out = FNWebSocketCreateFrame(YES, FNWebSocketOpcodeClose, payload, 15, NULL);
			fnTlsWriteAll(connection, [out bytes], [out length]);
			break;
		}
		[arrived replaceBytesInRange:NSMakeRange(0, (NSUInteger)consumed)
				   withBytes:(const void *)"" length:0];
	}
	tls_close(connection);
	tls_free(connection);
	tls_free(ctx);
	tls_config_free(config);
	close(fd);
	close(listener);
}

@end

/* --- THE DELEGATE: WHAT ONLY IT CAN SEE ------------------------------------------------------------ */

@interface FNTaskWatcher : NSObject <NSURLSessionWebSocketDelegate>
{
	@public
	int opened;
	int closed;
	int completions;
	NSInteger closedCode;
	NSString *openedProtocol;
	NSData *closedReason;
}
@end

@implementation FNTaskWatcher

- (void)URLSession:(NSURLSession *)session
   webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
didOpenWithProtocol:(NSString *)protocol
{
	(void)session;
	(void)webSocketTask;
	opened++;
	openedProtocol = protocol;	/* ARC owns the assignment: the ivar is __strong and nothing here retains */
}

- (void)URLSession:(NSURLSession *)session
   webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
  didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
	   reason:(NSData *)reason
{
	(void)session;
	(void)webSocketTask;
	closed++;
	closedCode = (NSInteger)closeCode;
	closedReason = reason;		/* ARC owns it too, for the same reason as the open door's protocol */
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
	(void)session;
	(void)task;
	completions++;
	/* PRINTED, because a completion carries the reason and a check that only counts completions cannot say WHY:
	 * the first run of this probe failed every connection check and left the error unread. */
	if(error != nil) {
		printf("FOUNDATION-WSTASK-DIAG didCompleteWithError: %s\n", [[error description] UTF8String]);
	}
}

@end

int main(void)
{
	FNTaskWatcher *watcher = [[FNTaskWatcher alloc] init];
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
	NSOperationQueue *queue = [[NSOperationQueue alloc] init];
	NSURLSession *session;
	NSURLSessionWebSocketTask *task;
	NSURLSessionWebSocketTask *bad;
	NSURL *url;
	pid_t peer;
	int listener;
	struct sockaddr_in addr;
	int one = 1;
	int tick;
	__block int gotMessage = 0;
	__block int gotPong = 0;
	__block NSString *echo = nil;
	__block NSString *peerCloseReason = nil;

	setvbuf(stdout, NULL, _IONBF, 0);

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(FN_WS_TEST_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	/* BIND, WHICH THE FIRST VERSION OMITTED - and the failure it causes is worth naming: a socket that is only
	 * LISTENED on is bound to port 0, so it listens on some port nobody knows while the client connects to the one
	 * this file names. The symptom is NSURLErrorCannotConnectToHost from the task and a peer whose accept never
	 * returns. (kernel_loopback_tcp's header records the same fact: bind(2) to port 0 SUCCEEDS here and leaves the
	 * port unresolved.) */
	if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		printf("FOUNDATION-WSTASK-DIAG the peer could not bind its port\n");
	}
	listen(listener, 4);

	peer = fork();
	if(peer == 0) {
		fnPeer(listener);
		_exit(0);
	}

	/* THE DELEGATE QUEUE IS REAL, because the doors are promised on it (§52): a check that read the watcher from
	 * the main thread while the delegate ran elsewhere would be reading a race. */
	session = [NSURLSession sessionWithConfiguration:configuration delegate:watcher delegateQueue:queue];
	url = [NSURL URLWithString:[NSString stringWithFormat:@"ws://127.0.0.1:%d/chat", FN_WS_TEST_PORT]];
	task = [session webSocketTaskWithURL:url protocols:@[ @"chat", @"superchat" ]];
	[task resume];

	/* --- THE UPGRADE, AND THE SUBPROTOCOL THE SERVER CHOSE --------------------------------------------- */
	for(tick = 0; tick < 600 && watcher->opened == 0; tick++) {
		fnPause(10);
	}
	check("the-upgrade-is-accepted", watcher->opened == 1,
	      [NSString stringWithFormat:@"the peer answers only when the key digests, so this means §4.2.2 agreed on both sides: opened=%d",
	       watcher->opened]);
	check("and-the-subprotocol-is-the-one-the-server-chose",
	      [watcher->openedProtocol isEqualToString:@"chat"],
	      [NSString stringWithFormat:@"we offered chat and superchat, the peer chose chat, the delegate reported '%@'",
	       watcher->openedProtocol]);

	/* --- A MESSAGE BOTH WAYS, WITH THE PEER AS THE WITNESS -------------------------------------------- */
	[task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:@"ping-text"]
	    completionHandler:^(NSError *error) {
		if(error != nil) {
			printf("FOUNDATION-WSTASK-DIAG the send failed: %s\n", [[error description] UTF8String]);
		}
	}];
	[task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message, NSError *error) {
		gotMessage = 1;
		echo = [[message string] copy];
		if(error != nil) {
			printf("FOUNDATION-WSTASK-DIAG the receive failed: %s\n", [[error description] UTF8String]);
		}
	}];
	for(tick = 0; tick < 600 && !gotMessage; tick++) {
		fnPause(10);
	}
	/* THE ECHO IS THE POINT: what came back is what the PEER read, not what we think we wrote. */
	check("a-message-reaches-the-far-end-and-comes-back",
	      gotMessage && [echo isEqualToString:@"ping-text"],
	      [NSString stringWithFormat:@"the peer echoed what it read: '%@'", echo]);

	/* --- THE PING, WHOSE ANSWER HAS NOWHERE ELSE TO GO ------------------------------------------------- */
	[task sendPingWithPongReceiveHandler:^(NSError *error) {
		(void)error;
		gotPong = 1;
	}];
	for(tick = 0; tick < 600 && !gotPong; tick++) {
		fnPause(10);
	}
	check("a-ping-is-answered-with-a-pong", gotPong,
	      @"the pong handler is the ONLY place that answer can arrive, which is what makes it the thing to assert");

	/* --- THE CLOSE, AND THE PEER'S OWN CODE AND REASON ------------------------------------------------- */
	[task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure
			   reason:[@"bye" dataUsingEncoding:NSUTF8StringEncoding]];
	for(tick = 0; tick < 600 && watcher->closed == 0; tick++) {
		fnPause(10);
	}
	peerCloseReason = [[NSString alloc] initWithData:watcher->closedReason encoding:NSUTF8StringEncoding];
	check("the-close-carries-the-peers-code-and-reason",
	      watcher->closed == 1 && watcher->closedCode == 1001 &&
	      [peerCloseReason isEqualToString:@"peer says bye"],
	      [NSString stringWithFormat:@"the peer answered 1001 + 'peer says bye'; the delegate saw %ld + '%@'",
	       (long)watcher->closedCode, peerCloseReason]);
	check("and-the-task-reports-them-too",
	      [task closeCode] == NSURLSessionWebSocketCloseCodeGoingAway &&
	      [[[NSString alloc] initWithData:[task closeReason] encoding:NSUTF8StringEncoding] isEqualToString:@"peer says bye"],
	      @"closeCode and closeReason are the same two facts without a delegate in the way");
	check("and-the-task-ends-exactly-once", watcher->completions == 1,
	      [NSString stringWithFormat:@"a task that reports twice is worse than one that never does: %d", watcher->completions]);

	/* --- THE SESSION'S SCHEME RULE, AND A RESERVED CLOSE CODE ------------------------------------------ */
	bad = [session webSocketTaskWithURL:[NSURL URLWithString:@"http://127.0.0.1/chat"]];
	check("only-ws-and-wss-urls-make-a-task", bad == nil,
	      @"Apple's rule ('the provided URL must have a ws or wss scheme'), answered with nil rather than with a task that would fail later in a worse way");
	{
		NSURLSessionWebSocketTask *plain = [session webSocketTaskWithURL:url];

		[plain cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNoStatusReceived reason:nil];	/* 1005 */
		for(tick = 0; tick < 400 && watcher->completions < 2; tick++) {
			fnPause(10);
		}
		check("and-a-reserved-close-code-is-refused-rather-than-sent",
		      watcher->completions >= 2 &&
		      [plain closeCode] != NSURLSessionWebSocketCloseCodeNoStatusReceived,
		      @"§7.4.1: 1005 may be REPORTED and never sent, so the door refuses rather than putting it on the wire");
	}

	/* --- THE wss: LEG: THE SAME UPGRADE, THROUGH A TUNNEL THE TASK ITSELF MUST BUILD ---------------------
	 *
	 * WHAT MAKES THIS A TEST OF THE TASK RATHER THAN OF TLS: the peer is a TLS SERVER and nothing else. If
	 * -fnStartHandshake wrote the upgrade in the clear - which is what it did until the row above landed - this
	 * peer would read a WebSocket request where a ClientHello belongs and answer nothing, and every check below
	 * would fail. The task has to secure the stream first, and the substrate's serial queue is what orders the
	 * two without a wait. */
	{
		FNTlsWsPeer *tlsPeer = [[FNTlsWsPeer alloc] init];
		FNTaskWatcher *tlsWatcher = [[FNTaskWatcher alloc] init];
		NSOperationQueue *tlsQueue = [[NSOperationQueue alloc] init];
		NSURLSession *tlsSession;
		NSURLSessionWebSocketTask *tlsTask;
		NSURL *tlsURL;
		NSString *dir = @"/System/Temporary Files/streamtask-tls";
		NSString *certPath = [dir stringByAppendingPathComponent:@"cert.pem"];
		NSString *keyPath = [dir stringByAppendingPathComponent:@"key.pem"];
		int reqStatus;
		pid_t tlsPid;
		__block int tlsMessage = 0;
		__block NSString *tlsEcho = nil;

		/* THE DIRECTORY FIRST, WHICH THE FIRST VERSION DID NOT MAKE - and the failure it causes is worth naming:
		 * `openssl req` cannot write into a directory that is not there, so the certificate is never made, the
		 * TLS peer's tls_configure fails, it never binds, and the leg's client gets
		 * NSURLErrorCannotConnectToHost - three steps from a missing mkdir. (The stream-task probe makes this same
		 * directory, which is why the name is shared and only this leg's own mkdir was missing.) */
		mkdir([dir UTF8String], 0755);
		reqStatus = fn_makeCertificate([dir UTF8String], [certPath UTF8String], [keyPath UTF8String]);
		snprintf(tlsPeer->cert, sizeof(tlsPeer->cert), "%s", [certPath UTF8String]);
		snprintf(tlsPeer->key, sizeof(tlsPeer->key), "%s", [keyPath UTF8String]);
		tlsPeer->port = FN_WS_TEST_PORT + 1;
		[NSThread detachNewThreadSelector:@selector(run) toTarget:tlsPeer withObject:nil];
		for(tick = 0; tick < 300 && !tlsPeer->bound; tick++) {
			fnPause(10);
		}
		check("the-tls-fixture-is-up", reqStatus == 0 && tlsPeer->bound,
		      @"`openssl req` made a certificate and a libtls peer is listening with it");

		tlsSession = [NSURLSession sessionWithConfiguration:configuration delegate:tlsWatcher delegateQueue:tlsQueue];
		tlsURL = [NSURL URLWithString:[NSString stringWithFormat:@"wss://127.0.0.1:%d/chat", tlsPeer->port]];
		tlsTask = [tlsSession webSocketTaskWithURL:tlsURL protocols:@[ @"chat" ]];
		[tlsTask resume];
		for(tick = 0; tick < 600 && tlsWatcher->opened == 0; tick++) {
			fnPause(10);
		}
		check("a-wss-url-gets-its-upgrade-through-a-tunnel", tlsWatcher->opened == 1,
		      @"the peer speaks TLS and NOTHING else, so an upgrade that arrived at all proves the task secured the stream first");

		[tlsTask sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:@"through-the-tunnel"]
		    completionHandler:^(NSError *error) {
			(void)error;
		}];
		[tlsTask receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message, NSError *error) {
			(void)error;
			tlsMessage = 1;
			tlsEcho = [[message string] copy];
		}];
		for(tick = 0; tick < 600 && !tlsMessage; tick++) {
			fnPause(10);
		}
		check("and-a-message-travels-over-it",
		      tlsMessage && [tlsEcho isEqualToString:@"through-the-tunnel"],
		      [NSString stringWithFormat:@"the TLS peer echoed what it read: '%@'", tlsEcho]);

		[tlsTask cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
		for(tick = 0; tick < 600 && tlsWatcher->closed == 0; tick++) {
			fnPause(10);
		}
		check("and-the-close-ends-it-over-tls", tlsWatcher->closed == 1 && tlsWatcher->closedCode == 1001,
		      [NSString stringWithFormat:@"the same close handshake, through the tunnel: %ld", (long)tlsWatcher->closedCode]);
		(void)tlsPid;
	}

	/* --- AND THE PEER'S OWN ACCOUNT, WHICH IS ITS EXIT CODE: it has a distinct code for every way it can fail,
	 * so 0 says every exchange above happened in the order it expected. --------------------------------- */
	{
		int status = -1;
		int reaped = 0;

		for(tick = 0; tick < 50; tick++) {
			if(waitpid(peer, &status, WNOHANG) == peer) {
				reaped = 1;
				break;
			}
			fnPause(100);
		}
		if(!reaped) {
			kill(peer, SIGKILL);
			waitpid(peer, &status, 0);
		}
		check("the-peer-walked-the-whole-script",
		      reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		      [NSString stringWithFormat:@"its exit code names the step it failed at: %d",
		       (reaped && WIFEXITED(status)) ? WEXITSTATUS(status) : -1]);
	}

	printf("FOUNDATION-WSTASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-WSTASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-WSTASK DONE\n");
	return failc ? 1 : 0;
}
