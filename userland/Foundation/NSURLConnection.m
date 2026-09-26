/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLConnection.m — the facade over the loading system. The design, the two deviations and the
 * refusals are in NSURLConnection.h; what is here is the machinery and the three decisions it needed.
 *
 * DECISION 1: A PRIVATE SESSION PER CONNECTION, BECAUSE STREAMING IS NOT OPTIONAL. A session holds ONE
 * delegate, so a connection that wants the session's per-chunk callbacks has to be the delegate of a
 * session it owns. The alternative — the shared session with a completion handler — hands over the
 * whole body at the ending, which would make `-connection:didReceiveData:` fire once with everything
 * and turn a documented streaming contract into a lie. `+sendSynchronousRequest:…` DOES use the shared
 * session, and it may: there is no delegate there to stream to.
 *
 * DECISION 2: THE SESSION RETAINS ITS DELEGATE, SO THE CYCLE IS BROKEN WHERE IT WAS MADE. Our
 * NSURLSession retains its delegate (§53's own line), and this connection retains its session — a
 * cycle, and one that would leak the session, the task and the connection together. It is broken at
 * the ENDING rather than at dealloc: the session is released, which lets it deallocate and give up its
 * own retain of this object (its delegate). Apple's documented behaviour for this class supplies the
 * other half — A CONNECTION KEEPS ITSELF ALIVE UNTIL IT IS FINISHED — so `-start` retains and the
 * ending releases, and a caller who drops its reference immediately still gets its delegate calls.
 *
 * DECISION 3: THE SESSION'S CHALLENGE DOOR *IS* IMPLEMENTED, AND IT DEFERS THE ANSWER TO THE DELEGATE'S
 * OWN SENDER. It could not be, once: `NSURLAuthenticationChallenge` shipped no `-sender`, so forwarding the
 * question would have offered a door a caller could not answer. §62.27 landed the sender (an owed row since
 * §62.24) and this is what it unblocked. THE TRANSLATION IS BY REFERENCE, NOT BY COPY: the challenge is
 * handed to the delegate AS IS, so the sender inside it is the TRANSPORT'S OWN thunk over the continuation
 * the transfer is blocked on — the delegate's answer therefore resumes the transfer directly, with no
 * second answer path anywhere. That is also why the session's completion handler is NOT called when a
 * delegate door exists: calling it AND letting the sender call it would answer the same challenge twice.
 */
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSData.h>	/* NSMutableData is declared here too (NSData.h:194) */
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSArray.h>

#include <pthread.h>

/* THE STATE THE SYNCHRONOUS FORM WAITS ON. A struct on the caller's stack, a pointer to it in the
 * completion block, and a pair of pthread primitives — the idiom NSFileCoordinator already uses here.
 * IT IS SAFE BECAUSE THE CALLER OUTLIVES THE BLOCK: the block can only run before the wait ends, and
 * the wait only ends after the block has set `done`. */
typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	NSData *data;			/* retained by the block, released by the caller */
	NSURLResponse *response;	/* ditto */
	NSError *error;			/* ditto */
	BOOL done;
} fn_sync_state;

@interface NSURLConnection () <NSURLSessionDataDelegate, NSURLSessionDownloadDelegate>
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
	 * connection rather than a connection that fails later. Asked here, once, so no other method has to
	 * ask again. `_request` is retained (not copied): Apple's `originalRequest` is a copy, but the copy
	 * that matters is the one the TASK holds, and the accessor below hands that back. */
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
	 * retained by itself (DECISION 2), so this can only run before `-start` or after the ending, when
	 * the session and the task are already gone. */
	[_request release];
	[_currentRequest release];
	[_session release];
	[_task release];
	[_delegateQueue release];
	[super dealloc];
}

/* --- THE CLASS DOORS ------------------------------------------------------------------------------ */

+ (BOOL)canHandleRequest:(NSURLRequest *)request
{
	/* ASKED OF THE LOADING SYSTEM RATHER THAN ANSWERED WITH A LIST OF SCHEMES: the protocol registry is
	 * the thing that decides whether a request can be initiated, so a second copy of that knowledge here
	 * would be a second thing to keep right. §46/§52's arrangement, consulted instead of duplicated. */
	return [NSURLProtocol fnProtocolClassForRequest:request] != nil;
}

+ (NSData *)sendSynchronousRequest:(NSURLRequest *)request
		 returningResponse:(NSURLResponse **)response
			     error:(NSError **)error
{
	fn_sync_state state;
	fn_sync_state *sp = &state;	/* THE BLOCK CAPTURES THIS, NOT THE STRUCT: see the note above the
					 * assignments - a captured struct is a const COPY, so writing through it
					 * would have been both illegal and invisible to this frame. */
	NSURLSession *session;
	NSURLSessionDataTask *task;

	/* THE OUT-PARAMETERS ARE CLEARED FIRST, so a caller that ignores the return value never reads a
	 * stale response or error into a claim about this transfer. */
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

	session = [NSURLSession sharedSession];
	task = [session dataTaskWithRequest:request
	       completionHandler:^(NSData *data, NSURLResponse *taskResponse, NSError *taskError) {
		/* THE BLOCK RUNS ON THE LOADING SYSTEM'S THREAD AND ONLY TOUCHES THE STATE UNDER THE LOCK.
		 * The three values are RETAINED here because the caller reads them after this block has
		 * returned, and nothing else keeps them alive. */
		pthread_mutex_lock(&sp->mutex);
		sp->data = [data retain];
		sp->response = [taskResponse retain];
		sp->error = [taskError retain];
		sp->done = YES;
		pthread_cond_signal(&sp->cond);
		pthread_mutex_unlock(&sp->mutex);
	}];
	[task resume];

	pthread_mutex_lock(&state.mutex);
	while (!state.done) {
		/* AN INTERRUPTION IS NOT AN ENDING: the transfer is still running, and the caller asked for the
		 * transfer's outcome rather than for this thread's convenience. */
		pthread_cond_wait(&state.cond, &state.mutex);
	}
	pthread_mutex_unlock(&state.mutex);

	pthread_mutex_destroy(&state.mutex);
	pthread_cond_destroy(&state.cond);

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

/* THE SESSION, MADE ONCE AND LAZILY, WITH THIS CONNECTION AS ITS DELEGATE. Lazily because
 * `-setDelegateQueue:` is documented as meaningful before `-start`, so the queue a caller sets has to
 * reach the session that will use it. */
- (BOOL)fnMakeSessionAndTask
{
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];

	_session = [[NSURLSession sessionWithConfiguration:configuration
						  delegate:self
					     delegateQueue:_delegateQueue] retain];
	if (_session == nil) {
		return NO;
	}
	if ([self fnDelegateIsADownloadDelegate]) {
		/* THE DOWNLOAD PATH (slice 2): the body goes to a file and the completion hands over its
		 * LOCATION. The block is the session's door, and this class is what turns it into the delegate's
		 * own ending - see fnFinishDownloadAtLocation:withError:. */
		_task = [[_session downloadTaskWithRequest:_request
					completionHandler:^(NSURL *location, NSURLResponse *response,
							    NSError *error) {
			(void)response;	/* the download protocol has no response door: Apple's contract for it is
					 * the FILE, and a delegate that wants the head of the answer is a data
					 * delegate. */
			[self fnFinishDownloadAtLocation:location withError:error];
		}] retain];
	} else {
		_task = [[_session dataTaskWithRequest:_request] retain];
	}
	return _task != nil;
}

/* THE DISPATCH RULE, ASKED BY SELECTOR (the header says why conformance would be worse). */
- (BOOL)fnDelegateIsADownloadDelegate
{
	return [_delegate respondsToSelector:@selector(connectionDidFinishDownloading:destinationURL:)];
}

- (void)start
{
	if (_started || _cancelled || _finished) {
		return;	/* idempotent: the transfer has already been asked to run, or it is over */
	}
	_started = YES;
	if (![self fnMakeSessionAndTask]) {
		return;
	}
	/* APPLE'S SELF-RETENTION, AND ITS RELEASE IS AT THE ENDING (DECISION 2). Taken AFTER the session
	 * exists, because the session is what will call us back, and given up in the ending that the session
	 * delivers. */
	[self retain];
	[_task resume];
}

- (void)cancel
{
	if (_finished) {
		return;
	}
	_cancelled = YES;
	/* THE ENDING STILL ARRIVES (the task reports a cancellation error) and `fnFinishWithError:` is where
	 * the delegate call is SUPPRESSED — a cancelled connection reports nothing, per the header. */
	[_task cancel];
}

- (void)setDelegateQueue:(NSOperationQueue *)queue
{
	if (_started) {
		return;	/* the session that would use it already exists; see the header's "only before -start" */
	}
	[queue retain];
	[_delegateQueue release];
	_delegateQueue = queue;
}

- (NSURLRequest *)originalRequest
{
	if (_task != nil) {
		return [_task originalRequest];
	}
	return _request;
}

- (NSURLRequest *)currentRequest
{
	NSURLRequest *current;

	if (_task != nil) {
		current = [_task currentRequest];
		if (current != nil) {
			return current;	/* §54's rule, inherited: a followed redirect moves THIS one */
		}
	}
	return _request;
}

/* --- THE ENDING ----------------------------------------------------------------------------------- */

- (void)fnFinishWithError:(NSError *)error
{
	if (_finished) {
		return;
	}
	_finished = YES;

	/* THE AGENT'S RETAIN, AND THE GUARD AROUND IT. `-start` retained this object on Apple's documented
	 * contract, so this is where that retain goes; the second pair exists because THE RELEASE BELOW CAN
	 * BE THE LAST ONE — tearing the session down gives up ITS retain of this object too, since this
	 * object is that session's delegate. Freeing `self` inside `self`'s own method is a use-after-free
	 * waiting for the next statement. */
	[self retain];

	if (_cancelled) {
		/* NOTHING IS REPORTED: a cancel is not a failure, and Apple's contract for this class is silent
		 * after one. Stated in the header where a caller meets it. */
	} else if (error != nil) {
		if ([_delegate respondsToSelector:@selector(connection:didFailWithError:)]) {
			[_delegate connection:self didFailWithError:error];
		}
	} else {
		if ([_delegate respondsToSelector:@selector(connectionDidFinishLoading:)]) {
			[_delegate connectionDidFinishLoading:self];
		}
	}

	[self fnTearDown];
	[self release];	/* the -start retain */
	[self release];	/* the guard */
}

/* THE DOWNLOAD'S ENDING, WHICH IS THE SAME SHAPE AS fnFinishWithError: WITH A DIFFERENT SUCCESS DOOR -
 * and the shape includes the guard, for the reason that method documents. */
- (void)fnFinishDownloadAtLocation:(NSURL *)location withError:(NSError *)error
{
	if (_finished) {
		return;
	}
	_finished = YES;
	[self retain];

	if (_cancelled) {
		/* nothing is reported, exactly as a cancelled data connection reports nothing */
	} else if (error != nil) {
		if ([_delegate respondsToSelector:@selector(connection:didFailWithError:)]) {
			[_delegate connection:self didFailWithError:error];
		}
	} else if (location != nil) {
		[_delegate connectionDidFinishDownloading:self destinationURL:location];
	}

	[self fnTearDown];
	[self release];	/* the -start retain */
	[self release];	/* the guard */
}

- (void)fnTearDown
{
	[_task release];
	_task = nil;

	/* THE CYCLE IS BROKEN HERE (DECISION 2): the session is asked to stop, and then our retain of it is
	 * dropped, which lets it deallocate and give up its own retain of this object. */
	[_session finishTasksAndInvalidate];
	[_session release];
	_session = nil;
}

/* --- THE SESSION'S DELEGATE DOORS THIS CLASS TRANSLATES -------------------------------------------------
 *
 * EVERY ONE OF THESE IS A TRANSLATION AND NOTHING ELSE — the rule is that no connection-level semantics
 * are invented here, because the loading system already has them. Where the two contracts differ at all
 * (the redirect), the difference is named in the code. */

- (void)URLSession:(NSURLSession *)session
	  dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
	/* THE DECISION DOOR IS ANSWERED AT ONCE AND THE BODY IS NEVER HELD: NSURLConnection has no
	 * equivalent of "wait while the delegate decides" — Apple's `-connection:didReceiveResponse:` is a
	 * notification, not a question — so Allow is the only faithful translation. */
	/* AND A DOWNLOAD DELEGATE IS NOT TOLD, even though the session delivers this door for EVERY task it
	 * runs, download tasks included (measured: the probe's download delegate saw one response and one data
	 * call before this guard existed). A download's contract is the FILE. The handler is still called,
	 * because the transfer WAITS for it - only the delegate is spared. */
	if (![self fnDelegateIsADownloadDelegate]) {
		if ([_delegate respondsToSelector:@selector(connection:didReceiveResponse:)]) {
			[_delegate connection:self didReceiveResponse:response];
		}
	}
	completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session
	  dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data
{
	if ([self fnDelegateIsADownloadDelegate]) {
		return;	/* the bytes are the download's FILE, not a delegate's stream: see the door above */
	}
	if ([_delegate respondsToSelector:@selector(connection:didReceiveData:)]) {
		[_delegate connection:self didReceiveData:data];
	}
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
willPerformHTTPRedirection:(NSHTTPURLResponse *)response
	newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest *))completionHandler
{
	/* THE ONE PLACE THE TWO PROTOCOLS HAVE THE SAME SHAPE, SO NOTHING IS TRANSLATED: the delegate's
	 * RETURN VALUE IS the session's completion-handler argument, and `nil` means "do not follow" on both
	 * sides. A delegate that implements no such door is not asked and the redirect IS followed. */
	/* A DOWNLOAD DELEGATE IS NOT ASKED, AND THAT IS ITS PROTOCOL'S OWN SHAPE RATHER THAN A CHOICE HERE:
	 * NSURLConnectionDownloadDelegate declares no redirect door, so there is nothing to ask it. The
	 * redirect is followed; currentRequest still moves, because the task's does. */
	if ([self fnDelegateIsADownloadDelegate]) {
		completionHandler(request);
		return;
	}
	if ([_delegate respondsToSelector:@selector(connection:willSendRequest:redirectResponse:)]) {
		NSURLRequest *next = [_delegate connection:self
					   willSendRequest:request
					redirectResponse:response];
		completionHandler(next);
		return;
	}
	completionHandler(request);
}

/* THE CACHE DECISION, TRANSLATED (§62.33): the session asks what to store and this class asks its delegate -
 * whose NIL means "keep nothing" on both sides, so the answer passes through unchanged. A delegate that
 * implements no such door is not asked here either, and the session's own answer stands. */
- (void)URLSession:(NSURLSession *)session
	  dataTask:(NSURLSessionDataTask *)dataTask
willCacheResponse:(NSCachedURLResponse *)proposedResponse
 completionHandler:(void (^)(NSCachedURLResponse *))completionHandler
{
	if ([_delegate respondsToSelector:@selector(connection:willCacheResponse:)]) {
		completionHandler([_delegate connection:self willCacheResponse:proposedResponse]);
		return;
	}
	completionHandler(proposedResponse);
}

/* THE UPLOAD'S PROGRESS DOOR (§62.32): the session's numbers, which are the transport's, in this protocol's
 * own spelling. Apple's connection door spells them NSInteger where the session's uses int64_t, so this is a
 * cast rather than a recount. */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
   didSendBodyData:(int64_t)bytesSent
    totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{
	if ([_delegate respondsToSelector:
			@selector(connection:didSendBodyData:totalBytesWritten:
				  totalBytesExpectedToWrite:)]) {
		[_delegate connection:self
		    didSendBodyData:(NSInteger)bytesSent
	       totalBytesWritten:(NSInteger)totalBytesSent
	   totalBytesExpectedToWrite:(NSInteger)totalBytesExpectedToSend];
	}
}

/* THE DOWNLOAD'S PROGRESS DOOR (§62.29), AND IT IS A TRANSLATION LIKE EVERY OTHER DOOR HERE: the session's
 * numbers are the task's own running totals, and the connection's protocol spells the same three facts. */
- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
       didWriteData:(int64_t)bytesWritten
  totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite
{
	if ([_delegate respondsToSelector:
			@selector(connection:didWriteData:totalBytesWritten:expectedTotalBytes:)]) {
		[_delegate connection:self
			 didWriteData:(long long)bytesWritten
		    totalBytesWritten:(long long)totalBytesWritten
		    expectedTotalBytes:(long long)totalBytesExpectedToWrite];
	}
}

/* THE AUTHENTICATION TRANSLATION, WITH APPLE'S PRECEDENCE WRITTEN OUT RATHER THAN IMPLIED (the header
 * states it for a caller; this is the code that keeps it):
 *
 *   * THE MODERN DOOR SUPERSEDES THE DEPRECATED PAIR, so a delegate that implements it is the only one
 *     asked — and it must answer, through the challenge's sender;
 *   * THE GATE IS ASKED NEXT, and a NO means "do not authenticate": the transfer continues WITHOUT
 *     credentials, which is what the session's own default handling means for a 401;
 *   * THEN THE DEPRECATED CHALLENGE DOOR, answered exactly as the modern one is (the same sender);
 *   * AND NO DOOR AT ALL MEANS THE DEFAULT, WITHOUT WAITING — the rule every door in this library keeps,
 *     and the reason a delegate that cares about none of this is never blocked on.
 *
 * `NSURLAuthenticationChallenge`'s sender is the LOADING SYSTEM'S thunk over the continuation this handler
 * IS, so a delegate that answers through the sender calls that continuation directly; there is exactly one
 * answer path and it is the transport's.
 */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
			    NSURLCredential *))completionHandler
{
	id delegate = _delegate;

	if ([delegate respondsToSelector:
			@selector(connection:willSendRequestForAuthenticationChallenge:)]) {
		[(id <NSURLConnectionDelegate>)delegate connection:self
			willSendRequestForAuthenticationChallenge:challenge];
		return;	/* the delegate answered through challenge.sender, as the header requires */
	}
	if ([delegate respondsToSelector:
			@selector(connection:canAuthenticateAgainstProtectionSpace:)]) {
		if (![(id <NSURLConnectionDelegate>)delegate connection:self
				canAuthenticateAgainstProtectionSpace:[challenge protectionSpace]]) {
			completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
			return;
		}
	}
	if ([delegate respondsToSelector:
			@selector(connection:didReceiveAuthenticationChallenge:)]) {
		[(id <NSURLConnectionDelegate>)delegate connection:self
			didReceiveAuthenticationChallenge:challenge];
		return;	/* answered through the sender, like the modern door */
	}
	completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	/* A DOWNLOAD'S ENDING ARRIVES TWICE, AND ONLY ONE OF THEM IS THE REPORT: the task's own
	 * -fnProtocolDidFinishWithError: notifies THIS session-delegate door and only then calls the download
	 * handler. Reporting here would tell a download delegate that its transfer had finished LOADING - a
	 * DATA-door contract it never adopted - and, worse, would tear the connection down (releasing the
	 * -start retain) BEFORE the location existed. So this door DEFERS to the download handler, which is
	 * the ending for a download. The probe pins it: a download delegate's data doors are never called. */
	if ([self fnDelegateIsADownloadDelegate]) {
		return;
	}
	[self fnFinishWithError:error];
}

@end
