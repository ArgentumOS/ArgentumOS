/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLConnection.m — the engine over NSURLProtocol. The design, the two deviations, the refusals and the
 * one work item are in NSURLConnection.h; what is here is the machinery and the four decisions it needed.
 *
 * DECISION 1: THE TRANSPORT IS AN `NSURLProtocol` AND THIS CLASS IS ITS CLIENT. That is Apple's 10.2
 * arrangement, and it is why the class survives the 10.2 surface cut at all: the previous implementation was
 * 566 lines of `NSURLSessionDataDelegate` — a class the baseline KEEPS, built on a family it REMOVES (§63.140).
 * The step is `+fnProtocolClassForRequest:` (NSURLProtocol.h: the consultation door §52 shipped for exactly
 * this), then ONE INSTANCE with `self` as its client, then `-startLoading`; everything after that is a
 * translation of one of the seven client doors.
 *
 * DECISION 2: THE ENDING OWNS THE LIFETIME, AND THE CYCLE IS BROKEN THERE. `-start` retains this object —
 * Apple's documented "a connection keeps itself alive until it is finished" — and the ending releases it; the
 * protocol retains this object as its client (`-client` is a strong property), so RELEASING THE PROTOCOL is
 * what gives up that second retain. Because the teardown can therefore be the release that frees `self` while
 * `self` is the receiver, EVERY ending takes a GUARD retain first and gives up two at the end. The shape is
 * the one the previous engine proved over thirteen checks, kept rather than reinvented.
 *
 * DECISION 3: `-cancel` ENTERS THE ENDING ITSELF, AND THAT IS THIS SEAM'S OWN SHAPE. The bridge reports
 * NOTHING for a stopped transfer — `transfer.stopped` is a branch in its own ending that says exactly that —
 * so a cancel that only called `-stopLoading` would never return the self-retain, and the object would live
 * forever with no callback able to finish it. The ending is entered with `_cancelled` set, so nothing is
 * reported (Apple's contract for this class is silent after a cancel) and the teardown still runs.
 *
 * DECISION 4: THE CHALLENGE ANSWERS THROUGH ITS SENDER, AND THE COMPLETION HANDLER IS NOT ALSO CALLED WHEN A
 * DELEGATE DOOR EXISTS. `[challenge.sender useCredential:…]` IS the continuation the transport is blocked on,
 * so answering both would answer one challenge twice. A delegate that implements no door means THE DEFAULT,
 * WITHOUT WAITING — the rule every door in this library keeps, and the reason a delegate that cares about none
 * of this is never blocked on.
 */
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLError.h>	/* NSURLErrorDomain and NSURLErrorUnsupportedURL: neither is
					 * imported by the headers above, and an invented code would be worse */
#import <Foundation/NSCachedURLResponse.h>
#import <Foundation/NSData.h>	/* NSMutableData is declared here too (NSData.h:194) */
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>	/* BEFORE NSOperationQueue.h, WHICH USES NSMutableArray AND DOES NOT IMPORT
				 * IT: the include order in this file is load-bearing (§63.146's third correction) */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSFileManager.h>	/* NSTemporaryDirectory() (NSFileManager.h:753) */
#import <Foundation/NSInputStream.h>
#import <Foundation/NSURLAuthenticationChallenge.h>	/* -protectionSpace, and -sender in the header */
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSURLProtectionSpace.h>
#import <Foundation/NSAutoreleasePool.h>

#include <pthread.h>
#include <unistd.h>

/* THE STATE THE SYNCHRONOUS FORM WAITS ON. A struct on the CALLER'S STACK, a pointer to it in the
 * connection, and a pair of pthread primitives — the idiom NSFileCoordinator already uses here.
 * IT IS SAFE BECAUSE THE CALLER OUTLIVES THE WRITER: the transport can only write before the wait ends, and
 * the wait only ends after the writer has set `done` — which is set LAST, under the lock. */
typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	NSMutableData *data;		/* retained by the writer, handed to the caller */
	NSURLResponse *response;	/* ditto */
	NSError *error;			/* ditto */
	BOOL done;
} fn_sync_state;

/* THE DOWNLOAD FILES GET A NAME NOTHING ELSE WILL PICK, because two connections in one process can be
 * downloading at once and a fixed name would have them writing into each other's file. */
static unsigned long fn_download_serial = 0;

/* THE CLIENT CONFORMANCE IS DECLARED HERE RATHER THAN IN THE HEADER, because it is HOW THIS CLASS DRIVES A
 * TRANSPORT rather than part of the class's own API — a caller never sees these doors. */
@interface NSURLConnection () <NSURLProtocolClient>
+ (NSError *)fnUnsupportedURLErrorForRequest:(NSURLRequest *)request;
- (void)fnSetSyncState:(void *)state;
- (void)fnBeginTransferWithRequest:(NSURLRequest *)request;
- (void)fnTearDown;
- (void)fnFinishWithError:(NSError *)error;
- (BOOL)fnDelegateIsADownloadDelegate;
- (void)fnDeliver:(void (^)(void))work;
- (NSString *)fnMakeDownloadPath;
- (void)fnReportDownloadProgressForChunk:(NSUInteger)chunk;
- (NSError *)fnUnsupportedURLError;
@end

@implementation NSURLConnection

/* --- LIFECYCLE ------------------------------------------------------------------------------------ */

- (instancetype)initWithRequest:(NSURLRequest *)request
		       delegate:(id)delegate
	       startImmediately:(BOOL)startImmediately
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* A CONNECTION NEEDS A URL TO RUN ANYTHING, and Apple's answer to a request without one is a nil
	 * connection rather than a connection that fails later. Asked here, once, so no other method has to ask
	 * again. `_request` is retained (not copied): this is the request a transfer STARTS FROM, and it is
	 * handed to the protocol as it stands. */
	if ([request URL] == nil) {
		[self release];
		return nil;
	}
	_request = [request retain];
	_delegate = delegate;	/* UNRETAINED, as Apple's is: the delegate owns the connection */
	if (startImmediately) {
		/* THE PARAMETER IS THE REASON THIS INITIALIZER EXISTS: `startImmediately:NO` gives a caller the
		 * chance to `-setDelegateQueue:` first, and YES is what `+connectionWithRequest:delegate:` means.
		 * IGNORING IT - the first version of this file did - made that common door create a connection
		 * that NOTHING EVER STARTED, which reads as a hang rather than as a mistake. */
		[self start];
	}
	return self;
}

- (instancetype)initWithRequest:(NSURLRequest *)request delegate:(id)delegate
{
	return [self initWithRequest:request delegate:delegate startImmediately:YES];
}

+ (NSURLConnection *)connectionWithRequest:(NSURLRequest *)request delegate:(id)delegate
{
	return [[[self alloc] initWithRequest:request delegate:delegate
			     startImmediately:YES] autorelease];
}

- (void)dealloc
{
	/* NO TEARDOWN OF A RUNNING TRANSFER HERE, and that is deliberate: a connection that is running is
	 * retained by ITSELF (DECISION 2) AND by its protocol, so this can only run before `-start` or after the
	 * ending, when the protocol is already released and nil. */
	[_request release];
	[_currentRequest release];
	[_protocol release];
	[_delegateQueue release];
	[_downloadData release];
	[_downloadPath release];
	[super dealloc];
}

/* --- THE CLASS DOORS ------------------------------------------------------------------------------ */

+ (BOOL)canHandleRequest:(NSURLRequest *)request
{
	/* ASKED OF THE LOADING SYSTEM RATHER THAN ANSWERED WITH A LIST OF SCHEMES: the protocol registry is the
	 * thing that decides whether a request can be initiated, so a second copy of that knowledge here would be
	 * a second thing to keep right. §46/§52's arrangement, consulted instead of duplicated — and it is now
	 * the SAME door `-start` consults, which is why the two cannot disagree. */
	return [NSURLProtocol fnProtocolClassForRequest:request] != nil;
}

+ (NSError *)fnUnsupportedURLErrorForRequest:(NSURLRequest *)request
{
	(void)request;
	return [NSError errorWithDomain:NSURLErrorDomain
				   code:NSURLErrorUnsupportedURL
			       userInfo:@{NSLocalizedDescriptionKey:
					@"this request has no URL, so no protocol can claim it"}];
}

/* THE SYNCHRONOUS FORM: AN ORDINARY CONNECTION WITH NO DELEGATE, AND A MUTEX+CONDITION WAIT THAT THE ENDING
 * SIGNALS. The session's completion-handler task is gone with the session (§63.145 step 4), and nothing is
 * lost: a connection reports its ending exactly once, and this form has nothing to stream. */
+ (NSData *)sendSynchronousRequest:(NSURLRequest *)request
		 returningResponse:(NSURLResponse **)response
			     error:(NSError **)error
{
	fn_sync_state state;
	NSURLConnection *connection;

	/* THE OUT-PARAMETERS ARE CLEARED FIRST, so a caller that ignores the return value never reads a stale
	 * response or error into a claim about this transfer. */
	if (response != NULL) {
		*response = nil;
	}
	if (error != NULL) {
		*error = nil;
	}

	state.data = nil;
	state.response = nil;
	state.error = nil;
	state.done = NO;
	pthread_mutex_init(&state.mutex, NULL);
	pthread_cond_init(&state.cond, NULL);

	connection = [[NSURLConnection alloc] initWithRequest:request
						    delegate:nil
					    startImmediately:NO];
	if (connection == nil) {
		/* NO URL AT ALL IS NO TRANSFER, and Apple's answer for it is a failure reported through the error
		 * door rather than an exception. */
		if (error != NULL) {
			*error = [self fnUnsupportedURLErrorForRequest:request];
		}
		pthread_mutex_destroy(&state.mutex);
		pthread_cond_destroy(&state.cond);
		return nil;
	}
	/* THE STATE IS INSTALLED BEFORE `-start`, because the ending can arrive on the transport's thread at any
	 * moment after it — including from `-start` itself, when no protocol claims the request. */
	[connection fnSetSyncState:&state];
	[connection start];

	pthread_mutex_lock(&state.mutex);
	while (!state.done) {
		/* AN INTERRUPTION IS NOT AN ENDING: the transfer is still running, and the caller asked for the
		 * transfer's outcome rather than for this thread's convenience. */
		pthread_cond_wait(&state.cond, &state.mutex);
	}
	pthread_mutex_unlock(&state.mutex);

	pthread_mutex_destroy(&state.mutex);
	pthread_cond_destroy(&state.cond);

	/* THE WAIT IS OVER, SO NOTHING CAN WRITE THE STATE AGAIN: `done` is set last and under the lock, and the
	 * reader has just read it there. Detaching first keeps the pointer from outliving the struct. */
	[connection fnSetSyncState:NULL];
	[connection release];

	if (response != NULL) {
		*response = [state.response autorelease];
	} else {
		[state.response release];
	}
	if (error != NULL) {
		*error = [state.error autorelease];
	} else {
		[state.error release];
	}
	return [state.data autorelease];
}

/* --- THE TRANSFER --------------------------------------------------------------------------------- */

- (void)fnSetSyncState:(void *)state
{
	_syncState = state;
}

- (void)start
{
	if (_started || _cancelled || _finished) {
		return;	/* idempotent: the transfer has already been asked to run, or it is over */
	}
	_started = YES;

	/* APPLE'S SELF-RETENTION, AND THE ENDING IS ITS RELEASE (DECISION 2). Taken BEFORE anything can fail,
	 * because a transfer that fails to start still ends. */
	[self retain];

	if ([NSURLProtocol fnProtocolClassForRequest:_request] == Nil) {
		/* NO REGISTERED PROTOCOL CLAIMS THIS REQUEST, so there is nothing to start and no callback that
		 * would ever arrive: Apple's answer is a FAILURE through the error door, not silence — and it is
		 * the same fact `+canHandleRequest:` reports. */
		[self fnFinishWithError:[self fnUnsupportedURLError]];
		return;
	}
	[self fnBeginTransferWithRequest:_request];
}

- (void)cancel
{
	if (_finished) {
		return;
	}
	_cancelled = YES;
	if (!_started) {
		/* NOT STARTED, SO THERE IS NOTHING TO END and no self-retain to return: Apple's contract is that a
		 * connection cancelled before it starts never starts, and `-start` refuses once `_cancelled` is set.
		 * Entering the ending here would over-release, which is the one thing this shape must not do. */
		return;
	}
	/* THE ENDING IS ENTERED HERE RATHER THAN WAITED FOR (DECISION 3): the transport reports nothing at all
	 * for a stopped transfer, so there is no later callback to carry the self-retain back. */
	[self fnFinishWithError:nil];
}

- (void)setDelegateQueue:(NSOperationQueue *)queue
{
	if (_started) {
		return;	/* the transfer is already reporting; see the header's "only before -start" */
	}
	[queue retain];
	[_delegateQueue release];
	_delegateQueue = queue;
}

- (NSURLRequest *)originalRequest
{
	return _request;
}

- (NSURLRequest *)currentRequest
{
	/* §54's rule: a followed redirect moves THIS one, and the original never moves. */
	if (_currentRequest != nil) {
		return _currentRequest;
	}
	return _request;
}

/* THE ONE PLACE A TRANSFER BEGINS. Both callers are here rather than duplicated: `-start` and the redirect
 * door, which re-issues with the request the delegate answered. */
- (void)fnBeginTransferWithRequest:(NSURLRequest *)request
{
	Class protocolClass = [NSURLProtocol fnProtocolClassForRequest:request];
	NSURLProtocol *previous = _protocol;
	NSURLProtocol *protocol;
	NSURLRequest *canonical;

	if (protocolClass == Nil) {
		[self fnFinishWithError:[self fnUnsupportedURLError]];
		return;
	}
	/* CANONICALISED BY THE PROTOCOL, which is what `+canonicalRequestForRequest:` is for (§52): the class
	 * that claims the request is the one that says what the request it runs looks like. */
	canonical = [protocolClass canonicalRequestForRequest:request];
	protocol = [[protocolClass alloc] initWithRequest:canonical cachedResponse:nil client:self];
	if (protocol == nil) {
		[self fnFinishWithError:[self fnUnsupportedURLError]];
		return;
	}
	/* THE NEW ONE IS IN PLACE BEFORE THE OLD ONE IS GIVEN UP, so a re-entrant callback from the transport
	 * being replaced finds this connection in a consistent state rather than a half-swapped one.
	 *
	 * AND THE OLD ONE IS AUTORELEASED RATHER THAN RELEASED, WHICH IS THE ONE THING THIS SWAP MUST GET RIGHT:
	 * when this is a REDIRECT, `previous` is the protocol whose own frame is on the stack — the bridge
	 * reports the redirect from INSIDE `curl_easy_perform` and goes on to report its metrics and its ending —
	 * so freeing it here would be a use-after-free in the transport. The bridge's transfer runs inside an
	 * autorelease pool of its own and drains it at the very end, so an autorelease is exactly the lifetime
	 * that lets the old protocol finish its frame. */
	_protocol = protocol;
	[previous autorelease];
	[protocol startLoading];
}

/* --- THE ENDING ----------------------------------------------------------------------------------- */

/* EVERY ENDING COMES THROUGH HERE: the two success doors, the failure door, a cancel, and a transfer that
 * could not be started at all. The guard is the whole point of the shape (DECISION 2). */
- (void)fnFinishWithError:(NSError *)error
{
	BOOL download;

	if (_finished) {
		return;	/* exactly one ending per connection, whatever arrives and in what order */
	}
	_finished = YES;

	/* THE GUARD: the teardown below can be the release that frees this object, because the protocol holds it
	 * as its client. Freeing `self` inside `self`'s own method is a use-after-free waiting for the next
	 * statement. */
	[self retain];

	download = [self fnDelegateIsADownloadDelegate];

	if (_syncState != NULL) {
		/* THE SYNCHRONOUS FORM IS WAITING and it is not a delegate: the answer is the state, written under
		 * the same lock the reader holds. `done` IS SET LAST, so no reader can see a half-written answer. */
		fn_sync_state *state = (fn_sync_state *)_syncState;

		pthread_mutex_lock(&state->mutex);
		state->error = [error retain];
		state->done = YES;
		pthread_cond_signal(&state->cond);
		pthread_mutex_unlock(&state->mutex);
		_syncState = NULL;
	} else if (_cancelled) {
		/* NOTHING IS REPORTED: a cancel is not a failure, and Apple's contract for this class is silent
		 * after one. Stated in the header where a caller meets it. */
	} else if (error != nil) {
		id delegate = _delegate;

		if ([delegate respondsToSelector:@selector(connection:didFailWithError:)]) {
			[self fnDeliver:^{
				[delegate connection:self didFailWithError:error];
			}];
		}
	} else if (download) {
		/* THE DOWNLOAD'S ENDING IS A FILE, so the body is written where the door says it is and the URL is
		 * handed over. A DOWNLOAD WHOSE BODY NEVER ARRIVED HAS NO FILE TO HAND OVER, and Apple's contract is
		 * that the delegate is told when there IS one — so nothing is reported and nothing is invented. */
		NSData *body = _downloadData;
		NSString *path = _downloadPath;

		if (body != nil && path != nil && [body writeToFile:path atomically:YES]) {
			id delegate = _delegate;
			NSURL *destination = [NSURL fileURLWithPath:path];

			_downloadData = nil;	/* handed over: the file is the caller's to move */
			[self fnDeliver:^{
				[delegate connectionDidFinishDownloading:self destinationURL:destination];
			}];
		}
	} else {
		id delegate = _delegate;

		if ([delegate respondsToSelector:@selector(connectionDidFinishLoading:)]) {
			[self fnDeliver:^{
				[delegate connectionDidFinishLoading:self];
			}];
		}
	}

	[self fnTearDown];
	/* THE -start RETAIN IS GIVEN UP ONLY IF -start TOOK IT, which is the one thing this shape must not get
	 * wrong: `-start` is the ONLY thing that retains on Apple's contract, so a connection that never started
	 * has no such retain to return — and the ending can be entered without one, because a transport's client
	 * door may be asked of this class directly (that is exactly how a probe exercises the redirect
	 * translation on an idle object). Releasing unconditionally there would be one release too many. */
	if (_started) {
		[self release];	/* the -start retain */
	}
	[self release];	/* the guard */
}

- (void)fnTearDown
{
	NSURLProtocol *protocol = _protocol;

	/* CLEARED FIRST, so a callback that arrives while the transport is being stopped finds a connection that
	 * is finished and a protocol of nil — and messaging nil is the no-op this class wants there. */
	_protocol = nil;
	[protocol stopLoading];
	[protocol release];	/* gives up the protocol's own retain of this object as its client */
}

/* THE DISPATCH RULE, ASKED BY SELECTOR (the header says why a conformance would be worse: a protocol's
 * metadata exists only when something in the process adopts it, so a conformance test would make the
 * behaviour depend on a linker detail). */
- (BOOL)fnDelegateIsADownloadDelegate
{
	return _delegate != nil &&
	       [_delegate respondsToSelector:@selector(connectionDidFinishDownloading:destinationURL:)];
}

/* WHERE A DELEGATE CALL GOES. With no queue it is the transport's thread, which is deviation (1) in the
 * header; with one, the block travels through it and a SERIAL queue preserves the order the transport reported
 * in. THE DELEGATE IS CAPTURED BY THE CALLER AND PASSED IN rather than read from the ivar at delivery time,
 * so the block's own copy keeps it alive for as long as the call is pending — reading an unretained delegate
 * when the call finally runs could otherwise message a delegate that had already gone away. */
- (void)fnDeliver:(void (^)(void))work
{
	NSOperationQueue *queue = _delegateQueue;

	if (queue == nil) {
		work();
		return;
	}
	[queue addOperationWithBlock:work];
}

/* --- THE CLIENT DOORS ------------------------------------------------------------------------------
 *
 * EVERY ONE OF THESE IS A TRANSLATION AND NOTHING ELSE — the rule is that no connection-level semantics are
 * invented here, because the loading system already has them. Where the two contracts differ at all (the
 * redirect, which the seam REPORTS rather than decides), the difference is named in the code. */

/* SOMEWHERE ELSE. The seam hands over the request the 3xx proposes and stops there, so FOLLOWING is this
 * class's act: the delegate is asked first (Apple's door and Apple's return value) and its answer is what the
 * next transfer runs — or `nil`, in which case the 3xx itself is the response. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    wasRedirectedToRequest:(NSURLRequest *)request
	 redirectResponse:(NSURLResponse *)redirectResponse
{
	NSURLRequest *next = request;

	(void)protocol;

	if ([_delegate respondsToSelector:@selector(connection:willSendRequest:redirectResponse:)]) {
		next = [(id <NSURLConnectionDataDelegate>)_delegate connection:self
							      willSendRequest:request
							   redirectResponse:redirectResponse];
	}
	if (next == nil) {
		/* DO NOT FOLLOW, WHICH MEANS THE 3xx IS THE ANSWER: the delegate hears the response it just refused
		 * to leave, and the transfer ends there. Apple's contract says the same thing.
		 *
		 * AND `currentRequest` DOES NOT MOVE, WHICH IS §54's RULE APPLIED THE RIGHT WAY ROUND: that property
		 * is "the request this connection is running", so it moves when a redirect is FOLLOWED and stays put
		 * when the delegate says no — the connection is still running the original request, and the 3xx is
		 * the answer to it. */
		if ([_delegate respondsToSelector:@selector(connection:didReceiveResponse:)]) {
			id delegate = _delegate;

			[self fnDeliver:^{
				[delegate connection:self didReceiveResponse:redirectResponse];
			}];
		}
		[self fnFinishWithError:nil];
		return;
	}
	/* §54's rule: the request the connection is running is now the one the server sent it to. */
	[next retain];
	[_currentRequest release];
	_currentRequest = next;
	[self fnBeginTransferWithRequest:next];
}

- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse
{
	/* NOTHING TO DO, AND IT IS NOT A REFUSAL: this class never hands a cached answer to a protocol — it
	 * passes `nil` for `cachedResponse:` in every transfer it starts — so this notification cannot arise from
	 * anything here. It stays implemented because the CLIENT protocol requires it of every client. */
	(void)protocol;
	(void)cachedResponse;
}

- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy
{
	(void)protocol;
	(void)policy;	/* an ADVICE for a store, and this class has none to advise: see the header's work item */

	if (_syncState != NULL) {
		fn_sync_state *state = (fn_sync_state *)_syncState;

		pthread_mutex_lock(&state->mutex);
		[state->response release];
		state->response = [response retain];
		pthread_mutex_unlock(&state->mutex);
		return;
	}
	if ([self fnDelegateIsADownloadDelegate]) {
		/* THE RESPONSE IS THE FILE'S HEAD, NOT A DELEGATE'S BUSINESS: a download delegate hears the FILE,
		 * and Apple's protocol for it has no response door. The length is kept because the PROGRESS door
		 * reports it. */
		_downloadExpected = (long long)[response expectedContentLength];
		return;
	}
	if ([_delegate respondsToSelector:@selector(connection:didReceiveResponse:)]) {
		id delegate = _delegate;

		[self fnDeliver:^{
			[delegate connection:self didReceiveResponse:response];
		}];
	}
}

- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data
{
	(void)protocol;

	if (_syncState != NULL) {
		fn_sync_state *state = (fn_sync_state *)_syncState;

		pthread_mutex_lock(&state->mutex);
		if (state->data == nil) {
			state->data = [[NSMutableData alloc] init];
		}
		[state->data appendData:data];
		pthread_mutex_unlock(&state->mutex);
		return;
	}
	if ([self fnDelegateIsADownloadDelegate]) {
		/* THE BODY IS WHAT THE FILE WILL HOLD, and the only thing the delegate hears as it arrives is the
		 * PROGRESS door below — the data doors belong to a data delegate (the dispatch rule). */
		if (_downloadData == nil) {
			_downloadData = [[NSMutableData alloc] init];
			_downloadPath = [[self fnMakeDownloadPath] retain];
		}
		[_downloadData appendData:data];
		_downloadBytes += [data length];
		[self fnReportDownloadProgressForChunk:[data length]];
		return;
	}
	if ([_delegate respondsToSelector:@selector(connection:didReceiveData:)]) {
		id delegate = _delegate;

		[self fnDeliver:^{
			[delegate connection:self didReceiveData:data];
		}];
	}
}

- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
		  completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
					      NSURLCredential * _Nullable))completionHandler
{
	id delegate = _delegate;

	(void)protocol;

	/* APPLE'S PRECEDENCE, WRITTEN OUT RATHER THAN IMPLIED (the header states it for a caller; this is the
	 * code that keeps it). THE MODERN DOOR SUPERSEDES THE DEPRECATED PAIR, so a delegate that implements it is
	 * the only one asked — and it must answer THROUGH THE CHALLENGE'S SENDER, which is the continuation this
	 * handler is (DECISION 4). */
	if ([delegate respondsToSelector:
			@selector(connection:willSendRequestForAuthenticationChallenge:)]) {
		[(id <NSURLConnectionDelegate>)delegate connection:self
			willSendRequestForAuthenticationChallenge:challenge];
		return;
	}
	/* THE GATE IS ASKED NEXT, and a NO means "do not authenticate": the transfer continues WITHOUT
	 * credentials, which is the default handling this completion handler names. */
	if ([delegate respondsToSelector:
			@selector(connection:canAuthenticateAgainstProtectionSpace:)]) {
		if (![(id <NSURLConnectionDelegate>)delegate connection:self
				canAuthenticateAgainstProtectionSpace:[challenge protectionSpace]]) {
			completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
			return;
		}
	}
	/* THEN THE DEPRECATED CHALLENGE DOOR, answered exactly as the modern one is. */
	if ([delegate respondsToSelector:
			@selector(connection:didReceiveAuthenticationChallenge:)]) {
		[(id <NSURLConnectionDelegate>)delegate connection:self
			didReceiveAuthenticationChallenge:challenge];
		return;
	}
	/* AND NO DOOR AT ALL MEANS THE DEFAULT, WITHOUT WAITING — the rule every door in this library keeps. */
	completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol
{
	(void)protocol;
	[self fnFinishWithError:nil];
}

- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error
{
	(void)protocol;
	[self fnFinishWithError:error];
}

/* --- THE FIRST-PARTY DOORS THE SEAM ADDS (§52, §62.32, §62.36) ------------------------------------- */

/* THE UPLOAD'S PROGRESS: the transport counts the bytes as they leave and reports them here, and the
 * delegate's door is where they arrive. Apple's connection door spells them NSInteger where the seam's uses
 * int64_t, so this is a cast rather than a recount — the numbers are the transport's own running totals. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    fnDidSendBodyData:(int64_t)bytesSent
      totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{
	id delegate = _delegate;

	(void)protocol;

	if (![delegate respondsToSelector:
			@selector(connection:didSendBodyData:totalBytesWritten:
				  totalBytesExpectedToWrite:)]) {
		return;
	}
	[self fnDeliver:^{
		/* THE CAST IS THE PROTOCOL APPLE DECLARES THIS ON, which is the DATA protocol and was the DOWNLOAD
		 * one until §63.156 moved it: a cast to the wrong protocol is not a compile error in ObjC — it is
		 * `-Wobjc-method-access`, and it is how the move announced itself. */
		[(id <NSURLConnectionDataDelegate>)delegate connection:self
		    didSendBodyData:(NSInteger)bytesSent
	       totalBytesWritten:(NSInteger)totalBytesSent
	   totalBytesExpectedToWrite:(NSInteger)totalBytesExpectedToSend];
	}];
}

/* THE RE-SEND'S BODY, ASKED OF THE CLIENT FOR THE ONE ATTEMPT NO OTHER DOOR CAN REACH (§62.36): the transport
 * re-issues a 401 itself, so this class never sees the second attempt. The delegate's answer, including nil,
 * goes straight back — Apple's contract for a replacement stream is "a new, UNOPENED one". */
- (NSInputStream *)URLProtocol:(NSURLProtocol *)protocol fnNewBodyStreamForReSend:(NSURLRequest *)request
{
	id delegate = _delegate;

	(void)protocol;

	if (![delegate respondsToSelector:@selector(connection:needNewBodyStream:)]) {
		return nil;
	}
	return [(id <NSURLConnectionDataDelegate>)delegate connection:self needNewBodyStream:request];
}

/* --- THE DOWNLOAD'S FILE -------------------------------------------------------------------------- */

- (NSString *)fnMakeDownloadPath
{
	return [NSTemporaryDirectory() stringByAppendingPathComponent:
		[NSString stringWithFormat:@"NSURLConnection-%d-%lu.tmp", (int)getpid(),
		   ++fn_download_serial]];
}

/* THE DOWNLOAD'S PROGRESS DOOR (§62.29), AND THE NUMBERS ARE THIS CLASS'S OWN: the seam reports neither
 * progress nor a running total, so `totalBytesWritten` is what has been accumulated for the file and
 * `expectedTotalBytes` is what the response published (0 when it published none). */
- (void)fnReportDownloadProgressForChunk:(NSUInteger)chunk
{
	id delegate = _delegate;
	unsigned long long total = _downloadBytes;
	long long expected = _downloadExpected;

	if (![delegate respondsToSelector:
			@selector(connection:didWriteData:totalBytesWritten:expectedTotalBytes:)]) {
		return;
	}
	[self fnDeliver:^{
		[(id <NSURLConnectionDownloadDelegate>)delegate connection:self
							 didWriteData:(long long)chunk
						    totalBytesWritten:(long long)total
						    expectedTotalBytes:(long long)expected];
	}];
}

- (NSError *)fnUnsupportedURLError
{
	return [NSError errorWithDomain:NSURLErrorDomain
				   code:NSURLErrorUnsupportedURL
			       userInfo:@{NSLocalizedDescriptionKey:
					@"no registered protocol claims this request's URL"}];
}

@end
