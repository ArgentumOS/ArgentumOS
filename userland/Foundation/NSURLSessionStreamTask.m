/*
 * NSURLSessionStreamTask.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CONNECTED SOCKET AND THE QUEUE THAT SITS IN FRONT OF IT. The design, the quoted semantics and the
 * boundaries are in NSURLSessionStreamTask.h; docs/design/foundation-plan.md §57 and §58 are the measurements
 * they came from.
 *
 * THE SHAPE OF THE THING, BEFORE THE CODE: -resume starts ONE WORKER THREAD, which resolves the host,
 * connects, and then serves the task's queue until the connection is done. The worker is what
 * `poll(2)` waits in, so a read's timeout and a write's timeout are deadlines in the same loop that would
 * otherwise be waiting for readability or writability - which is the only way the two can be enforced
 * together without a second thread. Every door that is not a read or a write is served BY THE SAME WORKER, in
 * queue order, which is what Apple's "completes any enqueued reads and writes, and then ..." means.
 *
 * TLS rides the same loop: once -startSecureConnection has been served, the queue's reads and writes go
 * through SSL_read/SSL_write, and SSL_ERROR_WANT_READ/WANT_WRITE become poll masks rather than errors - a
 * handshake that needs more bytes is not a failure, it is a wait.
 */
#import <Foundation/NSURLSessionStreamTask.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSURLSessionTaskMetrics.h>
#import <Foundation/NSURLError.h>
/* AND THE QUEUE THE HANDLERS HOP THROUGH (§52's rule), plus the session's own queue accessor that this class
 * asks for it by: a message to a selector no visible header declares is a warning, not an error. */
#import <Foundation/NSOperationQueue.h>

#include <Block.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

/* --- THE QUEUE'S UNIT OF WORK -------------------------------------------------------------------- */

typedef NS_ENUM(NSInteger, FNStreamOpKind) {
	FNStreamOpRead = 0,
	FNStreamOpWrite,
	FNStreamOpCloseRead,
	FNStreamOpCloseWrite,
	FNStreamOpStartTLS,
	FNStreamOpStopTLS,
	FNStreamOpCaptureStreams
};

/* ONE ASK IN THE QUEUE. It owns its handler blocks, because a queue outlives the call that filled it - the
 * same reason NSURLSessionTask copies its completion handler. */
@interface FNStreamOp : NSObject
{
	@public
	FNStreamOpKind kind;
	NSUInteger minBytes;
	NSUInteger maxBytes;
	NSTimeInterval timeout;		/* <= 0 means NO deadline, which is the documented meaning of 0 */
	NSData *data;
	void (^readHandler)(NSData *, BOOL, NSError *);
	void (^writeHandler)(NSError *);
}
@end

@implementation FNStreamOp

- (void)dealloc
{
	if (readHandler != NULL) {
		Block_release(readHandler);
	}
	if (writeHandler != NULL) {
		Block_release(writeHandler);
	}
	[data release];
	[super dealloc];
}

@end

/* --- THE TLS CONTEXT, ONE FOR THE PROCESS -------------------------------------------------------- */

/* ONE CONTEXT PER PROCESS RATHER THAN ONE PER TASK: a context is a configuration (a method, a cipher list, a
 * verification mode), and building it per task would rebuild the same object for every connection. IT IS
 * PROCESS-WIDE AND NEVER FREED, which is deliberate - a task may be created and destroyed any number of times
 * and the context has no per-connection state. */
static SSL_CTX *fn_stream_ssl_context(void)
{
	static SSL_CTX *context = NULL;

	if (context == NULL) {
		context = SSL_CTX_new(TLS_client_method());
		if (context != NULL) {
			/* NO VERIFICATION, AND THE REASON IS IN THE HEADER: this tree has no trust store and no
			 * certificate stack, so there is nothing to verify a peer AGAINST. Said rather than implied. */
			SSL_CTX_set_verify(context, SSL_VERIFY_NONE, NULL);
		}
	}
	return context;
}

/* --- THE WORKER'S OWN DOORS, DECLARED BEFORE THEY ARE USED --------------------------------------- */

/* A MESSAGE TO A SELECTOR THE COMPILER CANNOT SEE IS A WARNING HERE RATHER THAN AN ERROR - the trap §56's
 * build hit - and this class messages its own worker from six different doors, so they are declared in one
 * place. Nothing outside this file calls any of them. */
@interface NSURLSessionStreamTask (FNStreamWork)
- (void)fnEnqueue:(FNStreamOp *)operation;
- (void)fnServe;
- (BOOL)fnConnect;
- (void)fnTeardown;
- (void)fnServeOperation:(FNStreamOp *)operation;
- (void)fnServeRead:(FNStreamOp *)operation;
- (void)fnServeWrite:(FNStreamOp *)operation;
- (int)fnPollForWrite:(BOOL)forWriting deadline:(NSDate *)deadline;
- (ssize_t)fnReadInto:(NSMutableData *)collected upTo:(NSUInteger)room;
- (BOOL)fnStartTLS;
- (void)fnHopToDelegateQueue:(void (^)(void))block;
- (void)fnDeliverRead:(FNStreamOp *)operation data:(NSData *)data atEOF:(BOOL)atEOF error:(NSError *)error;
- (void)fnDeliverWrite:(FNStreamOp *)operation error:(NSError *)error;
- (void)fnFailOperation:(FNStreamOp *)operation withError:(NSError *)error;
- (NSError *)fnErrorWithCode:(NSInteger)code;
- (void)fnReportFileDescriptor;
- (void)fnReportReadClosed;
- (void)fnReportWriteClosed;
- (void)fnMaybeEnd;
- (void)fnEndWithError:(NSError *)error;
- (NSURLSessionTaskMetrics *)fnMetricsRecord;
@end

@implementation NSURLSessionStreamTask

/* --- CREATION ------------------------------------------------------------------------------------ */

- (instancetype)fnInitWithHostName:(NSString *)hostName
			      port:(NSInteger)port
			identifier:(NSUInteger)identifier
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_host = [hostName copy];
	_port = port;
	_fd = -1;
	_operations = [[NSMutableArray alloc] init];
	_queue = [[NSCondition alloc] init];
	_taskIdentifier = identifier;		/* the base's own field: a stream task has an identity like any task */
	_priority = 0.5f;			/* Apple's default */
	_state = NSURLSessionTaskStateSuspended;	/* A NEW TASK IS SUSPENDED, as every task is */
	return self;
}

/* --- THE STATE MACHINE, OVERRIDDEN WHERE A STREAM DIFFERS FROM A REQUEST -------------------------- */

/* THE BASE'S -resume ASKS THE SESSION TO RUN A REQUEST, and a stream task has none: so the transition to
 * Running is the same and the WORK is this class's own - one worker thread, which is the whole of the
 * difference from a data task. A stream task with no session still becomes Running, exactly as the base's
 * model does, because the state machine is the base's and the model's probe covers it. */
- (void)resume
{
	if (_state != NSURLSessionTaskStateSuspended || _started) {
		return;
	}
	_state = NSURLSessionTaskStateRunning;
	_started = YES;
	if (_session == nil) {
		return;
	}
	[self retain];	/* the worker's thread retains its target, but the retainer is stated here as well */
	[NSThread detachNewThreadSelector:@selector(fnServe) toTarget:self withObject:nil];
}

/* A CANCEL SHUTS THE DESCRIPTOR DOWN RATHER THAN CLOSING IT: the worker owns the descriptor for the flight,
 * and a close from another thread would be a use-after-free waiting for the worker's next poll. The shutdown
 * is what makes that poll return. */
- (void)cancel
{
	[_queue lock];
	_stopped = 1;
	[_queue broadcast];
	[_queue unlock];
	if (_fd >= 0) {
		shutdown(_fd, SHUT_RDWR);
	}
	[super cancel];		/* the base's rule: Completed, with NSURLErrorCancelled (§56's constant) */
}

- (void)suspend
{
	/* SAID RATHER THAN HALF-DONE: suspending a live connection would have to stop the reads the worker is
	 * already serving, and Apple's own stream task documents no such pause. The state moves, as the base's
	 * model says it does, and the connection is unaffected - which is a boundary of this row. */
	[super suspend];
}

- (void)dealloc
{
	[_host release];
	[_operations release];
	[_queue release];
	[super dealloc];
}

/* --- THE DOORS ----------------------------------------------------------------------------------- */

- (void)readDataOfMinLength:(NSUInteger)minBytes
		  maxLength:(NSUInteger)maxBytes
		    timeout:(NSTimeInterval)timeout
	  completionHandler:(void (^)(NSData *, BOOL, NSError *))completionHandler
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpRead;
	operation->minBytes = minBytes;
	operation->maxBytes = maxBytes;
	operation->timeout = timeout;
	operation->readHandler = Block_copy(completionHandler);
	[self fnEnqueue:operation];
	[operation release];
}

- (void)writeData:(NSData *)data
	  timeout:(NSTimeInterval)timeout
completionHandler:(void (^)(NSError *))completionHandler
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpWrite;
	operation->timeout = timeout;
	operation->data = [data copy];
	operation->writeHandler = Block_copy(completionHandler);
	[self fnEnqueue:operation];
	[operation release];
}

/* THE REFUSED DOOR. It is a QUEUE-ORDERED operation like the others, and it is served like the others - what
 * it cannot do is hand over a pair of streams, because this library's NSStream only ever opens its own
 * descriptor (see the header). So it drains the queue and reports the refusal through the TASK's ending
 * instead of a delegate door that could only lie. */
- (void)captureStreams
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpCaptureStreams;
	[self fnEnqueue:operation];
	[operation release];
}

- (void)closeRead
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpCloseRead;
	[self fnEnqueue:operation];
	[operation release];
}

- (void)closeWrite
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpCloseWrite;
	[self fnEnqueue:operation];
	[operation release];
}

- (void)startSecureConnection
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpStartTLS;
	[self fnEnqueue:operation];
	[operation release];
}

- (void)stopSecureConnection
{
	FNStreamOp *operation = [[FNStreamOp alloc] init];

	operation->kind = FNStreamOpStopTLS;
	[self fnEnqueue:operation];
	[operation release];
}

/* THE ONE PLACE AN OPERATION ENTERS THE QUEUE, and the reason a door cannot forget to wake the worker. */
- (void)fnEnqueue:(FNStreamOp *)operation
{
	[_queue lock];
	if (_stopped) {
		[_queue unlock];
		/* A CANCELLED TASK ANSWERS ITS ASKS RATHER THAN SWALLOWING THEM: a handler that is never called is
		 * worse than one that is called with the error the task already ended with. */
		[self fnFailOperation:operation withError:[self error]];
		return;
	}
	[_operations addObject:operation];
	[_queue signal];
	[_queue unlock];
}

/* --- THE WORKER ---------------------------------------------------------------------------------- */

- (void)fnServe
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

	if ([self fnConnect]) {
		for (;;) {
			FNStreamOp *operation = nil;

			[_queue lock];
			while ([_operations count] == 0 && !_stopped) {
				[_queue wait];
			}
			if ([_operations count] > 0) {
				operation = [[_operations objectAtIndex:0] retain];
				[_operations removeObjectAtIndex:0];
			}
			[_queue unlock];
			if (operation == nil) {
				break;			/* stopped, with the queue empty */
			}
			[self fnServeOperation:operation];
			[operation release];
		}
	}
	[self fnTeardown];
	[pool release];
	[self release];		/* pairs with the retain in -resume */
}

/* THE CONNECTION ITSELF, on the worker's thread so that nothing else ever blocks. */
- (BOOL)fnConnect
{
	struct addrinfo hints;
	struct addrinfo *answers = NULL;
	struct addrinfo *candidate;
	char service[16];
	int fd = -1;

	_startedAt = [NSDate date];
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(service, sizeof(service), "%ld", (long)_port);
	if (getaddrinfo([_host UTF8String], service, &hints, &answers) != 0 || answers == NULL) {
		[self fnEndWithError:[self fnErrorWithCode:NSURLErrorCannotFindHost]];
		return NO;
	}
	for (candidate = answers; candidate != NULL; candidate = candidate->ai_next) {
		fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
		if (fd < 0) {
			continue;
		}
		if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(answers);
	if (fd < 0) {
		[self fnEndWithError:[self fnErrorWithCode:NSURLErrorCannotConnectToHost]];
		return NO;
	}
	_connectedAt = [NSDate date];
	/* NON-BLOCKING FROM HERE ON: the worker is the only reader, and every wait is an explicit poll with a
	 * deadline, which is what makes a read's timeout enforceable at all. */
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	_fd = fd;
	[self fnReportFileDescriptor];	/* nothing public reads it; kept for the worker's own bookkeeping */
	return YES;
}

- (void)fnTeardown
{
	if (_ssl != NULL) {
		SSL_shutdown((SSL *)_ssl);
		SSL_free((SSL *)_ssl);
		_ssl = NULL;
	}
	if (_fd >= 0) {
		close(_fd);
		_fd = -1;
	}
	if (_state != NSURLSessionTaskStateCompleted) {
		[self fnEndWithError:[self error]];
	}
}

/* --- SERVING ONE OPERATION ----------------------------------------------------------------------- */

- (void)fnServeOperation:(FNStreamOp *)operation
{
	switch (operation->kind) {
	case FNStreamOpRead:
		[self fnServeRead:operation];
		break;
	case FNStreamOpWrite:
		[self fnServeWrite:operation];
		break;
	case FNStreamOpCloseRead:
		if (_fd >= 0) {
			shutdown(_fd, SHUT_RD);
		}
		_readClosed = YES;
		[self fnReportReadClosed];
		[self fnMaybeEnd];
		break;
	case FNStreamOpCloseWrite:
		if (_fd >= 0) {
			/* THE HALF-CLOSE ITSELF, and §58.2 is its measured footnote: this call SUCCEEDS here and the peer
			 * never sees the end of file - the kernel sends no FIN for a write-side shutdown. The class does
			 * the correct thing anyway, and its far-end visibility is a recorded kernel work item. */
			shutdown(_fd, SHUT_WR);
		}
		_writeClosed = YES;
		[self fnReportWriteClosed];
		[self fnMaybeEnd];
		break;
	case FNStreamOpStartTLS:
		if (![self fnStartTLS]) {
			/* THE DOOR HAS NO HANDLER, so a failed handshake is the TASK's failure: there is no other
			 * place for it to be reported, and an upgrade that silently did not happen would be worse. */
			[self fnEndWithError:[self fnErrorWithCode:NSURLErrorSecureConnectionFailed]];
		}
		break;
	case FNStreamOpStopTLS:
		if (_ssl != NULL) {
			SSL_shutdown((SSL *)_ssl);
			SSL_free((SSL *)_ssl);
			_ssl = NULL;
		}
		break;
	case FNStreamOpCaptureStreams:
		/* THE REFUSAL, DELIVERED WHERE A CALLER CAN SEE IT: the queue drained (that is what serving this
		 * operation means), and the pair cannot be built - so the task ends saying exactly that. */
		[self fnEndWithError:[self fnErrorWithCode:NSURLErrorUnsupportedURL]];
		break;
	}
}

/* A READ IS "UNTIL AT LEAST minBytes, NEVER MORE THAN maxBytes, AND A DEADLINE THAT CANCELS IT". The loop is
 * the whole of it: poll for readability, read what is there, and stop on the minimum, on the maximum, on end
 * of file, or on the deadline. */
- (void)fnServeRead:(FNStreamOp *)operation
{
	NSMutableData *collected = [[NSMutableData alloc] init];
	NSDate *deadline = operation->timeout > 0 ? [[NSDate alloc] initWithTimeIntervalSinceNow:operation->timeout] : nil;
	NSUInteger want = operation->maxBytes;
	BOOL atEOF = NO;
	NSError *error = nil;

	/* THE LOOP, STATED AS THE THREE WAYS OUT OF IT rather than as one condition: the minimum is reached, the
	 * cap is reached, or the connection said something - end of file, a drop, or the deadline. A `maxLength`
	 * of 0 is read as NO CAP, because a cap of zero would make every read impossible. */
	for (;;) {
		int ready;

		if ([collected length] >= operation->minBytes) {
			break;			/* THE MINIMUM: what a caller asks for when it says "at least this many" */
		}
		if (want > 0 && [collected length] >= want) {
			break;			/* THE CAP */
		}
		ready = [self fnPollForWrite:NO deadline:deadline];
		if (ready <= 0) {
			error = [self fnErrorWithCode:(ready == 0 ? NSURLErrorNetworkConnectionLost
								  : NSURLErrorTimedOut)];
			break;
		}
		if ([self fnReadInto:collected upTo:(want > 0 ? want - [collected length] : 4096)] <= 0) {
			atEOF = YES;		/* the peer closed its write side: the end of the stream */
			break;
		}
	}
	[self fnDeliverRead:operation data:collected atEOF:atEOF error:error];
	[deadline release];
	[collected release];
}

/* A WRITE IS "ALL OF IT, OR THE DEADLINE" - and the promise that ends at the KERNEL, which the header quotes
 * and the probe's far-end EOF is the only evidence for. */
- (void)fnServeWrite:(FNStreamOp *)operation
{
	const uint8_t *bytes = [operation->data bytes];
	NSUInteger length = [operation->data length];
	NSUInteger written = 0;
	NSDate *deadline = operation->timeout > 0 ? [[NSDate alloc] initWithTimeIntervalSinceNow:operation->timeout] : nil;
	NSError *error = nil;

	while (written < length) {
		ssize_t n;

		if ([self fnPollForWrite:YES deadline:deadline] <= 0) {
			error = [self fnErrorWithCode:NSURLErrorTimedOut];
			break;
		}
		if (_ssl != NULL) {
			n = SSL_write((SSL *)_ssl, bytes + written, (int)(length - written));
			if (n <= 0) {
				int reason = SSL_get_error((SSL *)_ssl, (int)n);

				if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) {
					continue;
				}
				error = [self fnErrorWithCode:NSURLErrorNetworkConnectionLost];
				break;
			}
		} else {
			n = write(_fd, bytes + written, length - written);
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR) {
					continue;
				}
				error = [self fnErrorWithCode:NSURLErrorCannotWriteToFile];
				break;
			}
		}
		written += (NSUInteger)n;
	}
	if (operation->writeHandler != NULL) {
		[self fnDeliverWrite:operation error:error];
	}
	[deadline release];
}

/* --- THE WAIT, AND THE TWO WAYS TO READ ---------------------------------------------------------- */

/* POLL, WITH THE DEADLINE AS ITS TIMEOUT. It answers 1 for ready, 0 for the peer hanging up, and -1 for the
 * deadline - which is the ONE place a timeout is decided, for reads and writes alike. */
- (int)fnPollForWrite:(BOOL)forWriting deadline:(NSDate *)deadline
{
	struct pollfd entry;
	int milliseconds = -1;
	int waitingForever = (deadline == nil);
	int ready;

	if (deadline != nil) {
		double remaining = [deadline timeIntervalSinceNow];

		if (remaining <= 0) {
			return -1;
		}
		milliseconds = (int)(remaining * 1000.0);
		if (milliseconds == 0) {
			milliseconds = 1;	/* poll's 0 means "do not wait", which is not what a live deadline means */
		}
	}
	if (_fd < 0) {
		return 0;
	}
	entry.fd = _fd;
	entry.events = forWriting ? POLLOUT : POLLIN;
	entry.revents = 0;
	/* A WAIT WITH NO CALLER DEADLINE IS A LOOP OF BOUNDED SLICES, NOT ONE `poll` WITH A NEGATIVE TIMEOUT, AND ITS
	 * REASON IS MEASURED (§58.1's byte flow): with `poll(fd, POLLIN, -1)` this kernel leaves the waiter parked on
	 * a socket that HAS BECOME READABLE - the far end's whole TLS flight was on the wire (127 + 6 + 28 + 715 +
	 * 286 + 58 bytes, all written), and the handshake poll never noticed. The tree's own probes never pass a
	 * negative timeout; every wait in them is a positive one, and the handshake below is bounded by the same
	 * slices. This is a work item against `poll(2)`, not a licence to leave the class waiting forever. */
	if (waitingForever) {
		milliseconds = 1000;
	}
	for (;;) {
		ready = poll(&entry, 1, milliseconds);
		if (ready < 0 && errno == EINTR) {
			continue;
		}
		if (ready == 0 && waitingForever) {
			continue;	/* a slice that expired, not a verdict: the caller has no deadline to be late for */
		}
		break;
	}
	if (ready <= 0) {
		return -1;
	}
	if ((entry.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
	    (entry.revents & (POLLIN | POLLOUT)) == 0) {
		return 0;
	}
	return 1;
}

/* ONE READ, THROUGH THE TLS SESSION WHEN THERE IS ONE. It answers the bytes read, or 0 for end of file (the
 * caller turns that into atEOF), or -1 for a wait. */
- (ssize_t)fnReadInto:(NSMutableData *)collected upTo:(NSUInteger)room
{
	uint8_t buffer[4096];
	NSUInteger chunk = room < sizeof(buffer) ? room : sizeof(buffer);
	ssize_t got;

	chunk = chunk == 0 ? sizeof(buffer) : chunk;
	if (_ssl != NULL) {
		got = SSL_read((SSL *)_ssl, buffer, (int)chunk);
		if (got <= 0) {
			int reason = SSL_get_error((SSL *)_ssl, (int)got);

			if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) {
				return -1;	/* a wait, not an ending: the loop polls again */
			}
			return 0;
		}
	} else {
		got = read(_fd, buffer, chunk);
		if (got < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				return -1;
			}
			return 0;
		}
	}
	if (got > 0) {
		[collected appendBytes:buffer length:(NSUInteger)got];
	}
	return got;
}

/* --- TLS ----------------------------------------------------------------------------------------- */

- (BOOL)fnStartTLS
{
	SSL_CTX *context = fn_stream_ssl_context();
	SSL *session;
	NSDate *deadline = nil;		/* the handshake has no caller-supplied timeout, so it waits */
	int result;

	if (context == NULL || _fd < 0) {
		return NO;
	}
	SSL_CTX_set_default_verify_paths(context);	/* harmless, and honest about what is not there */
	session = SSL_new(context);
	if (session == NULL) {
		return NO;
	}
	SSL_set_fd(session, _fd);
	for (;;) {
		result = SSL_connect(session);
		if (result == 1) {
			break;
		}
		{
			int reason = SSL_get_error(session, result);

			if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) {
				if ([self fnPollForWrite:(reason == SSL_ERROR_WANT_WRITE) deadline:deadline] != 1) {
					SSL_free(session);
					return NO;
				}
				continue;
			}
		}
		SSL_free(session);
		return NO;
	}
	_ssl = session;
	return YES;
}

/* --- REPORTING ----------------------------------------------------------------------------------- */

/* THE HOP EVERY DOOR IN THIS SESSION USES (§52): the delegate queue when there is one, inline when there is
 * not. The block is COPIED, because a queued block outlives the call that made it - and in MRC a captured
 * block is not retained by the outer one. */
- (void)fnHopToDelegateQueue:(void (^)(void))block
{
	NSOperationQueue *queue = [(NSURLSession *)_session delegateQueue];
	void (^copied)(void);

	if (queue == nil) {
		block();
		return;
	}
	copied = Block_copy(block);
	[self retain];
	[queue addOperationWithBlock:^{
		copied();
		Block_release(copied);
		[self release];
	}];
}

- (void)fnDeliverRead:(FNStreamOp *)operation data:(NSData *)data atEOF:(BOOL)atEOF error:(NSError *)error
{
	void (^handler)(NSData *, BOOL, NSError *) = operation->readHandler;
	NSData *delivered = [data copy];
	NSError *deliveredError = [error retain];

	if (handler == NULL) {
		[delivered release];
		[deliveredError release];
		return;
	}
	[self fnHopToDelegateQueue:^{
		handler(delivered, atEOF, deliveredError);
		[delivered release];
		[deliveredError release];
	}];
}

- (void)fnDeliverWrite:(FNStreamOp *)operation error:(NSError *)error
{
	void (^handler)(NSError *) = operation->writeHandler;
	NSError *deliveredError = [error retain];

	if (handler == NULL) {
		[deliveredError release];
		return;
	}
	[self fnHopToDelegateQueue:^{
		handler(deliveredError);
		[deliveredError release];
	}];
}

/* A FAILED ASK ABOUT A TASK THAT IS ALREADY OVER: the operation came in after -cancel, so it is answered
 * rather than swallowed. */
- (void)fnFailOperation:(FNStreamOp *)operation withError:(NSError *)error
{
	if (operation->kind == FNStreamOpRead) {
		[self fnDeliverRead:operation data:[NSData data] atEOF:NO error:error];
	} else if (operation->kind == FNStreamOpWrite) {
		[self fnDeliverWrite:operation error:error];
	}
}

- (NSError *)fnErrorWithCode:(NSInteger)code
{
	return [[[NSError alloc] initWithDomain:NSURLErrorDomain code:code userInfo:nil] autorelease];
}

- (void)fnReportFileDescriptor
{
	/* NOTHING PUBLIC READS THE DESCRIPTOR - there is no door for it in Apple's class either - so this is
	 * where a future first-party door would sit. It exists so the connect has one place that knows the
	 * connection is up. */
}

- (void)fnReportReadClosed
{
	NSURLSession *session = (NSURLSession *)_session;
	id delegate = [session delegate];

	if (![delegate respondsToSelector:@selector(URLSession:readClosedForStreamTask:)]) {
		return;
	}
	[self retain];
	[self fnHopToDelegateQueue:^{
		[delegate URLSession:session readClosedForStreamTask:self];
		[self release];
	}];
}

- (void)fnReportWriteClosed
{
	NSURLSession *session = (NSURLSession *)_session;
	id delegate = [session delegate];

	if (![delegate respondsToSelector:@selector(URLSession:writeClosedForStreamTask:)]) {
		return;
	}
	[self retain];
	[self fnHopToDelegateQueue:^{
		[delegate URLSession:session writeClosedForStreamTask:self];
		[self release];
	}];
}

/* BOTH HALVES CLOSED IS THE END OF THE CONNECTION, AND THE DESCRIPTOR GOES WITH IT.
 *
 * THE DESCRIPTOR PART IS A CORRECTION WITH A MEASURED LESSON BEHIND IT. The first version ended only the TASK,
 * which left the worker waiting for operations nobody would ever send and the socket open - a descriptor held
 * per stream, and a FAR END THAT NEVER SEES THE CONNECTION END. Probes are how that was found: with the halves
 * closed, the peer's read kept answering EAGAIN for ever.
 *
 * AND ONE HALF OF THE SEMANTICS IS NOT THIS CLASS'S TO GIVE, which the same probe measured: on this kernel a
 * `shutdown(fd, SHUT_WR)` returns success and the PEER NEVER SEES AN END OF FILE - the half-close does not
 * reach the wire (docs/design/foundation-plan.md §58.2). Closing the descriptor is what the far end does see,
 * so that is what this class does when the stream is over, and the half-close's own far-end visibility is
 * recorded as a kernel work item rather than silently claimed. */
- (void)fnMaybeEnd
{
	if (_readClosed && _writeClosed && _state != NSURLSessionTaskStateCompleted) {
		_stopped = 1;
		if (_fd >= 0) {
			shutdown(_fd, SHUT_RDWR);	/* throws the wait the worker is in, so it can tear down */
		}
		[_queue lock];
		[_queue broadcast];
		[_queue unlock];
		[self fnEndWithError:nil];
	}
}

/* --- THE ENDING ---------------------------------------------------------------------------------- */

/* THE TASK'S END, DELIVERED IN APPLE'S ORDER: the metrics record first (§52's rule, and §58's promise that a
 * stream task arrives with one), then -URLSession:task:didCompleteWithError:. */
- (void)fnEndWithError:(NSError *)error
{
	NSURLSession *session = (NSURLSession *)_session;
	id delegate = [session delegate];
	NSURLSessionTaskMetrics *metrics = [self fnMetricsRecord];

	[self fnProtocolDidFinishWithError:error];
	if (delegate == nil) {
		return;
	}
	if ([delegate respondsToSelector:@selector(URLSession:task:didFinishCollectingMetrics:)]) {
		[metrics retain];
		[self retain];
		[self fnHopToDelegateQueue:^{
			[delegate URLSession:session task:self didFinishCollectingMetrics:metrics];
			[metrics release];
			[self release];
		}];
	}
	if ([delegate respondsToSelector:@selector(URLSession:task:didCompleteWithError:)]) {
		NSError *delivered = [[self error] retain];

		[self retain];
		[self fnHopToDelegateQueue:^{
			[delegate URLSession:session task:self didCompleteWithError:delivered];
			[delivered release];
			[self release];
		}];
	}
}

/* WHAT THE CONNECTION COST, from the two instants this class keeps: when the worker started and when the
 * connection came up. IT IS ONE TRANSACTION WITH NO REQUEST AND NO RESPONSE, which is what a stream task's
 * exchange is - and the fields the transport cannot report stay NIL rather than being invented (§50.1's rule,
 * and the reason `secureConnectionStartDate` is absent here too). */
- (NSURLSessionTaskMetrics *)fnMetricsRecord
{
	NSURLSessionTaskTransactionMetrics *transaction = [[NSURLSessionTaskTransactionMetrics alloc] init];
	NSURLSessionTaskMetrics *record = [[NSURLSessionTaskMetrics alloc] init];
	NSDate *end = [NSDate date];
	NSURLSessionTaskMetricsResourceFetchType type = NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad;

	if (_startedAt != nil) {
		[transaction fnSetFetchStartDate:_startedAt];
	}
	if (_connectedAt != nil) {
		[transaction fnSetConnectStartDate:_startedAt];
		[transaction fnSetConnectEndDate:_connectedAt];
	}
	[transaction fnSetResourceFetchType:type];
	[transaction fnSetReusedConnection:NO];
	[record fnSetTransactionMetrics:[NSArray arrayWithObject:transaction]];
	[record fnSetTaskInterval:[[[NSDateInterval alloc] initWithStartDate:_startedAt endDate:end] autorelease]];
	[record fnSetRedirectCount:0];
	[transaction release];
	return [record autorelease];
}

@end
