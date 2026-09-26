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
 * DECISION 3: THE CHALLENGE DOOR OF THE SESSION IS *NOT* IMPLEMENTED HERE, WHICH IS THE ACTIVE FORM OF
 * THE REFUSALS THE HEADER NAMES. The session asks its delegate for a disposition and a credential; a
 * connection delegate has no `NSURLAuthenticationChallenge` SENDER to answer through (that class ships
 * no `-sender` — its own header records why, and §62.24 makes it an owed row), so forwarding the
 * question would offer a door a caller cannot use. Not implementing it leaves the session's own
 * default in force, which is what "this delegate cannot answer challenges" means.
 */
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSHTTPURLResponse.h>
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

@interface NSURLConnection () <NSURLSessionDataDelegate>
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
	_task = [[_session dataTaskWithRequest:_request] retain];
	return _task != nil;
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
	if ([_delegate respondsToSelector:@selector(connection:didReceiveResponse:)]) {
		[_delegate connection:self didReceiveResponse:response];
	}
	completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session
	  dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data
{
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
	if ([_delegate respondsToSelector:@selector(connection:willSendRequest:redirectResponse:)]) {
		NSURLRequest *next = [_delegate connection:self
					   willSendRequest:request
					redirectResponse:response];
		completionHandler(next);
		return;
	}
	completionHandler(request);
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	[self fnFinishWithError:error];
}

@end
