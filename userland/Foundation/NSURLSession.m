/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSession.m — creation, the task factories and invalidation. The design is in NSURLSession.h.
 */
#import <Foundation/NSURLSession.h>
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSURLCache.h>
/* WHAT THE ENDING DELIVERS FIRST (§52): the session is what assembles the task's record, so it needs the
 * public getters AND the internal `fn` writer category. */
#import <Foundation/NSURLSessionTaskMetrics.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
/* AND THE ERROR NAMES (§56): the unsupported-URL code and the hop-limit failure are constants now, so the
 * library no longer spells their values where it reports them. */
#import <Foundation/NSURLError.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#include <stdio.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSLock.h>	/* NSCondition lives HERE, not in a header of its own */

/* THE PER-TRANSFER CLIENT, AND IT IS WHY THE SESSION DOES NOT HAVE TO MAP A PROTOCOL BACK TO ITS TASK.
 * A protocol reports through its CLIENT, and the thing that knows which task a transfer belongs to is this
 * object: it holds the task (retained, for the flight) and, UNRETAINED, the session it reports its ending
 * to. Declared here rather than in a header because nothing outside this file makes one.
 *
 * THE TWO REFERENCES GO IN OPPOSITE DIRECTIONS ON PURPOSE: the session keeps its transfers alive (so a
 * protocol is not deallocated mid-flight) and the transfer reaches back without owning, which is what
 * keeps the pair from being a cycle. */
/* THE SESSION'S OWN ENDING DOOR, declared here because the transfer (below) calls it and nothing outside
 * this file can: a session does not publish "a transfer of mine has ended" to the world. */
/* THE UPLOAD FACTORIES' SHARED HALF, declared here because the two public doors are one implementation. */
@interface NSURLSession (FNSessionUpload)
- (NSURLSessionUploadTask *)fnUploadTaskWithRequest:(NSURLRequest *)request
					    handler:(void (^)(NSData *, NSURLResponse *, NSError *))handler;
@end

/* THE TRANSFER IS DECLARED AFTER THIS CATEGORY IN THE FILE, so its name is forward declared here: the
 * category's two new doors take it, and a declaration may not wait for a later one to be read. */
@class FNSessionTransfer;

@interface NSURLSession (FNSessionTransferControl)
- (void)fnTransferDidEnd:(NSURLProtocol *)protocol;
/* §54: THE START, IN ONE PLACE, and the redirect decision that needs it. A followed redirect runs the next
 * request on the SAME task with the SAME client, so "start a transfer" cannot stay inside -fnTaskDidResume:
 * alone: a second client would leave the metrics and the hop count behind, which is precisely what §52's
 * record is made of. */
- (void)fnStartTransferForTask:(NSURLSessionTask *)task
		       request:(NSURLRequest *)request
			client:(FNSessionTransfer *)client;
- (NSURLRequest * _Nullable)fnAskAboutRedirectForTask:(NSURLSessionTask *)task
					    response:(NSHTTPURLResponse *)response
					    proposed:(NSURLRequest *)request;
@end

@interface FNSessionTransfer : NSObject <NSURLProtocolClient>
{
	/* THE BYTES THIS TRANSFER RECEIVED, kept ONLY so the cache can store what it saw: the
	 * task accumulates its own copy and does not expose it (Apple's data task hands its bytes to the
	 * completion handler and nowhere else), so without this there is nothing to cache. Nothing reads it
	 * DURING the transfer, so it is not a second streaming buffer. */
	NSMutableData *_body;
	NSURLSessionTask *_task;
	NSURLSession *_session;
	NSCondition *_decision;	/* the response decision's wait, owned by the transfer so it outlives a call */
	/* THE TASK'S RECORD, ACCUMULATED AS THE PROTOCOL REPORTS IT (§52): ONE ENTRY PER TRANSACTION (a
	 * challenge-answered re-issue is a second one), the instant the transfer began — which is what the
	 * task's own span is measured from — and how many redirects this loading system was told about. The
	 * transactions are the PROTOCOL's measurements; the assembly around them is this class's. */
	NSMutableArray *_transactions;
	NSDate *_started;
	NSInteger _redirects;
	/* THE REDIRECT DECISION'S CONSEQUENCE, HELD UNTIL THE ATTEMPT'S RECORD ARRIVES (§54). The bridge reports a
	 * redirect from INSIDE curl's header callback, so acting on the decision there would end the task (or
	 * start the next hop) with a record that does not yet hold the very transaction that caused it - MEASURED,
	 * not theorised: the declined leg's record came out EMPTY and a twenty-hop chain's came out one short.
	 * So the decision is REMEMBERED here and taken in -fnDidCollectMetrics:, which is the first moment the
	 * record is complete and IN ORDER. The invariant that makes this safe is the transport's: it reports a
	 * record for every attempt that ran, including the one it aborted on a redirect. */
	NSURLRequest *_pendingFollow;
	NSURLResponse *_pendingResponse;
	int _pendingAction;		/* 0 none, 1 go there, 2 stay, 3 too many hops */
}
- (instancetype)initWithTask:(NSURLSessionTask *)task session:(NSURLSession *)session;
@end

/* THE PRIVATE DECLARATIONS THE TRANSFER NEEDS IN ITS OWN HEADER-LESS WAY: the class above is
 * declared, and this category is the ending helper its implementations call. */
@interface FNSessionTransfer (FNSessionTransferEnding)
- (void)fnTellTheTaskDelegate;
/* AND THE DELIVERY THAT COMES FIRST AT THE ENDING, which is why it is its own method rather than a block
 * inside the other one: the ORDER is the contract (§52), and a separate call is something a reader of the
 * ending can see. */
- (void)fnTellMetrics;
- (void)fnTellTheDownloadDelegate;
@end


/* THE POLICY IS DERIVED FROM THE RESPONSE, the one signal v1 reads: `Cache-Control: no-store` is the server
 * saying do not keep this, and anything else may be kept. NSURLRequest's own cache policy is a second input
 * Apple honours and this slice does not read - the narrowing section 49.2 records rather than hides. */
static NSURLCacheStoragePolicy fn_policyForResponse(NSURLResponse *response)
{
	if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
		NSString *control = [[(NSHTTPURLResponse *)response allHeaderFields] objectForKey:@"Cache-Control"];

		if (control != nil && [control rangeOfString:@"no-store"
					     options:NSCaseInsensitiveSearch].location != NSNotFound) {
			return NSURLCacheStorageNotAllowed;
		}
	}
	return NSURLCacheStorageAllowed;
}

@implementation FNSessionTransfer

- (instancetype)initWithTask:(NSURLSessionTask *)task session:(NSURLSession *)session
{
	self = [super init];
	if (self != nil) {
		_task = [task retain];
		_session = session;	/* unretained */
		/* THE RECORD'S CLOCK STARTS HERE (§52): this object is made immediately before -startLoading, so
		 * this instant IS when the task began to run - which is what the task's span is measured from -
		 * and the array is where the protocol's per-transaction reports land. */
		_started = [[NSDate date] retain];
		_transactions = [[NSMutableArray alloc] init];
	}
	return self;
}

/* EVERY CALLBACK IS A TRANSLATION INTO THE TASK'S OWN STATE, and nothing more: the protocol streams, the
 * task accumulates, and the ending is what reports. */
/* THE CLIENT'S ANSWER TO A CHALLENGE, and it is a HOP INTO THE SESSION'S OWN DOOR rather than a second
 * implementation: the delegate resolution (task door first, session door as the fallback, and
 * PerformDefaultHandling when there is no delegate at all) lives there and is asked once. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
		    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
						NSURLCredential * _Nullable))completionHandler
{
	(void)protocol;
	[_session fnAskForCredentialForTask:_task challenge:challenge completionHandler:completionHandler];
}

/* WHAT THE PROTOCOL MEASURED, KEPT UNTIL THE TASK ENDS (§52): the report is per TRANSACTION, so this is an
 * APPEND and never a store - a transfer that was challenged and re-issued reports twice, and Apple's record
 * carries one entry per transaction, IN ORDER. THE ASSEMBLY IS NOT HERE: the task's span and its redirect
 * count belong to the ending, where its start and its outcome are both known - and a task that never ran a
 * transfer reports an EMPTY list rather than nothing, because the door's contract is its place in the
 * sequence.
 *
 * AND §54'S DECISION IS TAKEN HERE, because this is the first moment the record is complete: the redirect was
 * decided inside the transport's header callback (see -wasRedirectedToRequest:), where this attempt's
 * transaction had not been reported yet. */
/* UPLOAD PROGRESS, FROM THE TRANSFER TO THE DELEGATE (§62.32), and it sits beside the metrics door because it
 * is the same kind of report: something the transport knows and Apple's NSURLProtocolClient has no door for.
 * `bytesSent` is the delta the bridge computed, so a delegate can sum it or ignore it and still read the two
 * totals correctly. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    fnDidSendBodyData:(int64_t)bytesSent
      totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{
	id <NSURLSessionTaskDelegate> delegate = (id <NSURLSessionTaskDelegate>)[_session delegate];
	NSOperationQueue *queue = [_session delegateQueue];

	(void)protocol;
	if (![delegate respondsToSelector:
		@selector(URLSession:task:didSendBodyData:totalBytesSent:totalBytesExpectedToSend:)]) {
		return;
	}
	if (queue == nil) {
		[delegate URLSession:_session
				task:_task
		     didSendBodyData:bytesSent
		      totalBytesSent:totalBytesSent
		totalBytesExpectedToSend:totalBytesExpectedToSend];
		return;
	}
	/* THE SAME HOP, WITH THE SAME RETAINS: see the note in -URLProtocol:didLoadData:. */
	[self retain];
	[delegate retain];
	[queue addOperationWithBlock:^{
		[delegate URLSession:_session
				task:_task
		     didSendBodyData:bytesSent
		      totalBytesSent:totalBytesSent
		totalBytesExpectedToSend:totalBytesExpectedToSend];
		[delegate release];
		[self release];
	}];
}

- (void)URLProtocol:(NSURLProtocol *)protocol fnDidCollectMetrics:(NSURLSessionTaskTransactionMetrics *)metrics
{
	[_transactions addObject:metrics];
	if (_pendingAction == 0) {
		return;
	}
	/* WHAT EACH DECISION DOES, and why the follow does its two steps in THIS order: the task is moved to the
	 * target and the next transfer is STARTED BEFORE the old flight is dropped, because the new protocol
	 * retains this client - so releasing the old protocol (which was holding it) cannot free the object still
	 * on the stack. All three end the flight, which is what lets the session release the protocol. */
	if (_pendingAction == 1) {
		_redirects++;
		[_task fnProtocolDidRedirectToRequest:_pendingFollow];
		[_session fnStartTransferForTask:_task request:_pendingFollow client:self];
	} else if (_pendingAction == 2) {
		[_task fnProtocolDidReceiveResponse:_pendingResponse];
		[_task fnProtocolDidFinishWithError:nil];
		[self fnTellTheTaskDelegate];
	/* THE FAILURE IS NOW APPLE'S OWN CODE AND DOMAIN (§56): §54 shipped this as a code in the bridge's own
	 * domain *because* the NSURLError* mass was not declared - and the mass is declared now, so a caller reads
	 * "too many redirects" where it used to read "code 2 of something else". */
	} else {
		NSError *tooMany = [[NSError alloc] initWithDomain:@"NSURLErrorDomain"
							      code:NSURLErrorHTTPTooManyRedirects
							  userInfo:nil];

		[_task fnProtocolDidFinishWithError:tooMany];
		[self fnTellTheTaskDelegate];
		[tooMany release];
	}
	_pendingAction = 0;
	[_pendingFollow release];
	_pendingFollow = nil;
	[_pendingResponse release];
	_pendingResponse = nil;
	[_session fnTransferDidEnd:protocol];
}

- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy
{
	id <NSURLSessionDataDelegate> delegate;
	__block NSInteger disposition = -1;

	(void)policy;
	[_task fnProtocolDidReceiveResponse:response];

	/* NO DOOR MEANS ALLOW, AND NO WAIT: a delegate that does not implement the decision must not make the
	 * transfer wait for an answer nobody will send. */
	delegate = (id <NSURLSessionDataDelegate>)[_session delegate];
	if (![delegate respondsToSelector:
			@selector(URLSession:dataTask:didReceiveResponse:completionHandler:)]) {
		return;
	}

	/* THE BODY WAITS HERE. This method is called SYNCHRONOUSLY from the bridge's own thread, so blocking in
	 * it is what holds the transfer at the head of the answer - and the decision arrives on the delegate
	 * queue, which is a DIFFERENT thread always, because the transfer is never run on that queue. The
	 * condition belongs to the transfer rather than to this call so that a handler called after the wait has
	 * been released touches something that is still alive. */
	if (_decision == nil) {
		_decision = [[NSCondition alloc] init];
	}
	/* THE DELEGATE IS ASKED OUTSIDE THE LOCK, AND THAT IS NOT A STYLE CHOICE: with no delegate queue the
	 * handler is called SYNCHRONOUSLY, on this very thread, and NSCondition's lock is NOT RECURSIVE - so
	 * holding it across the call deadlocks the transfer against its own handler. Measured: six checks failed
	 * and the probe took its full wait, before this was moved. The lock now guards only the WRITE and the
	 * WAIT, never the call between them. */
	[self retain];	/* the handler may outlive this call; in MRC a block does not retain what it captures */
	[delegate URLSession:_session
		    dataTask:(NSURLSessionDataTask *)_task
	   didReceiveResponse:response
	    completionHandler:^(NSURLSessionResponseDisposition chosen) {
		[_decision lock];
		disposition = (NSInteger)chosen;
		[_decision broadcast];
		[_decision unlock];
		[self release];
	}];
	[_decision lock];
	while (disposition < 0) {
		[_decision wait];
	}
	[_decision unlock];

	if (disposition == NSURLSessionResponseCancel) {
		/* CANCELLING THE TASK IS ONLY HALF OF IT, AND THE PROBE SAID SO: the task's own -cancel sets its
		 * state, and the PROTOCOL keeps delivering the body unless it is stopped too - so a cancelled
		 * response arrived with its bytes. -stopLoading is the seam's door for exactly this, and the bridge
		 * honours it at the next chunk.
		 *
		 * AND THE DELEGATE IS TOLD THE ENDING HERE, because the protocol will now report NOTHING (a stopped
		 * transfer has no finish and no failure to report): the task is already Completed, so
		 * -fnProtocolDidFinishWithError: would return early and the delegate would never hear that its task
		 * ended. */
		[_task cancel];
		[protocol stopLoading];
		[self fnTellTheTaskDelegate];
	}
	/* BecomeDownload and BecomeStream are HONOURED AS Allow: the conversion belongs with the sibling that
	 * would receive it, and the header says so. */
}

- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data
{
	id <NSURLSessionDataDelegate> delegate;
	NSOperationQueue *queue;

	[_task fnProtocolDidLoadData:data];
	[self fnTellDownloadProgress:data];	/* the task has counted it; this is where a delegate hears */
	if (_body == nil) {
		_body = [[NSMutableData alloc] init];
	}
	[_body appendData:data];
	/* AND THE DELEGATE IS TOLD, IF IT ASKED TO BE: the same bytes the task accumulates, one call per
	 * chunk - asked with -respondsToSelector: because every member of these protocols is optional. */
	delegate = (id <NSURLSessionDataDelegate>)[_session delegate];
	if (![delegate respondsToSelector:@selector(URLSession:dataTask:didReceiveData:)]) {
		return;
	}
	queue = [_session delegateQueue];
	if (queue == nil) {
		[delegate URLSession:_session dataTask:(NSURLSessionDataTask *)_task didReceiveData:data];
		return;
	}
	/* THE HOP TO THE SESSION'S OWN QUEUE, AND THE RETAINS AROUND IT ARE THE POINT. THIS LIBRARY IS MRC, SO
	 * A BLOCK DOES NOT RETAIN WHAT IT CAPTURES: everything the queued work touches - the transfer itself,
	 * the delegate and the bytes - is retained HERE and released once the block has run. Skipping that is a
	 * use-after-free, which is the failure mode this whole row was about.
	 *
	 * AND THE QUEUE IS EXPECTED TO BE SERIAL (the header says so, as Apple does): a CONCURRENT one may run
	 * these blocks in any order, so a delegate that appends data could see the ending first. */
	[self retain];
	[delegate retain];
	[data retain];
	[queue addOperationWithBlock:^{
		[delegate URLSession:_session dataTask:(NSURLSessionDataTask *)_task didReceiveData:data];
		[data release];
		[delegate release];
		[self release];
	}];
}

/* THE DOWNLOAD PROGRESS DOOR, DISPATCHED ON THE SAME TERMS AS THE CHUNK DOOR ABOVE - which is what makes the
 * two consistent: a delegate that wants every chunk and one that wants a running total are asking the same
 * question of the same transfer.
 *
 * THE NUMBERS ARE THE TASK'S, NOT A SECOND ACCOUNTING: `countOfBytesReceived` and
 * `countOfBytesExpectedToReceive` are the task's own public properties, already maintained by
 * `-fnProtocolDidLoadData:`, so this door cannot drift from what the task reports about itself. The chunk's
 * length is `bytesWritten`; the expected total is the response's, and 0 or -1 means the server never said. */
- (void)fnTellDownloadProgress:(NSData *)data
{
	id <NSURLSessionDownloadDelegate> delegate = (id <NSURLSessionDownloadDelegate>)[_session delegate];
	NSOperationQueue *queue = [_session delegateQueue];

	if (![_task isKindOfClass:[NSURLSessionDownloadTask class]] ||
	    ![delegate respondsToSelector:
		@selector(URLSession:downloadTask:didWriteData:totalBytesWritten:
			  totalBytesExpectedToWrite:)]) {
		return;
	}
	if (queue == nil) {
		[delegate URLSession:_session
		       downloadTask:(NSURLSessionDownloadTask *)_task
			didWriteData:(int64_t)[data length]
		   totalBytesWritten:[_task countOfBytesReceived]
	   totalBytesExpectedToWrite:[_task countOfBytesExpectedToReceive]];
		return;
	}
	/* THE SAME HOP, WITH THE SAME RETAINS: see the note in -URLProtocol:didLoadData:. */
	[self retain];
	[delegate retain];
	[data retain];
	[queue addOperationWithBlock:^{
		[delegate URLSession:_session
		       downloadTask:(NSURLSessionDownloadTask *)_task
			didWriteData:(int64_t)[data length]
		   totalBytesWritten:[_task countOfBytesReceived]
	   totalBytesExpectedToWrite:[_task countOfBytesExpectedToReceive]];
		[data release];
		[delegate release];
		[self release];
	}];
}

- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol
{
	/* STORING BELONGS HERE BECAUSE THE BODY IS HERE AND THE TASK IS WHAT HAS BOTH: a protocol reports and
	 * the loading system puts things away, which is whose job Apple considers this too. */
	{
		NSURLResponse *seen = [_task response];

		if (seen != nil && _body != nil) {
			NSCachedURLResponse *cached = [[NSCachedURLResponse alloc]
							initWithResponse:seen
								     data:_body
								 userInfo:nil
							    storagePolicy:fn_policyForResponse(seen)];

			[[NSURLCache sharedURLCache] storeCachedResponse:cached forRequest:[_task originalRequest]];
			[cached release];
		}
		[_body release];
		_body = nil;
	}
	[_task fnProtocolDidFinishWithError:nil];
	[self fnTellTheTaskDelegate];
	[_session fnTransferDidEnd:protocol];
}

/* THE METRICS GO FIRST, AND THE ORDER IS THE CONTRACT (§52): Apple delivers
 * -URLSession:task:didFinishCollectingMetrics: BEFORE -URLSession:task:didCompleteWithError:, so a delegate
 * that wants the numbers has them when it decides what the outcome meant. ON A DELEGATE QUEUE the order is
 * kept by enqueueing this one first - the queue is expected to be SERIAL, as the header says - and with no
 * queue both calls are synchronous and already in order.
 *
 * THE RECORD IS THE LOADING SYSTEM'S, ASSEMBLED ACROSS THE TWO THINGS THAT KNOW IT: the transactions are the
 * PROTOCOL's (each one measured at the handle), and the span and the redirect count are this object's - the
 * span from its own creation, which is when the task began to run, to this instant. */
/* THE DOWNLOAD DELEGATE'S FINISHING DOOR, AND IT IS ONLY EVER CALLED FOR A DOWNLOAD WITH A FILE: `location`
 * is nil when the transfer failed (the task writes nothing on an error, by its own design), and a door told
 * about a location holding a partial body would be worse than one told nothing. */
- (void)fnTellTheDownloadDelegate
{
	id <NSURLSessionDownloadDelegate> delegate = (id <NSURLSessionDownloadDelegate>)[_session delegate];
	NSURL *location;

	if (![_task isKindOfClass:[NSURLSessionDownloadTask class]]) {
		return;
	}
	location = [(NSURLSessionDownloadTask *)_task location];
	if (location == nil ||
	    ![delegate respondsToSelector:@selector(URLSession:downloadTask:didFinishDownloadingToURL:)]) {
		return;
	}
	/* THE DELEGATE QUEUE IS HONOURED HERE TOO, but NOT with a hop that could reorder it against the
	 * completion door: both are dispatched from the same call, in order, so a queue that is serial keeps
	 * the order Apple states. */
	[delegate URLSession:_session
	       downloadTask:(NSURLSessionDownloadTask *)_task
    didFinishDownloadingToURL:location];
}

- (void)fnTellMetrics
{
	id <NSURLSessionTaskDelegate> delegate = (id <NSURLSessionTaskDelegate>)[_session delegate];
	NSURLSessionTaskMetrics *metrics;
	NSOperationQueue *queue;

	if (![delegate respondsToSelector:@selector(URLSession:task:didFinishCollectingMetrics:)]) {
		return;	/* no door means nothing is delivered - and nothing was measured FOR, either */
	}
	metrics = [[NSURLSessionTaskMetrics alloc] init];
	[metrics fnSetTransactionMetrics:(_transactions != nil ? _transactions : [NSArray array])];
	/* THE TASK'S SPAN IS ITS OWN AND NOT THE SUM OF ITS TRANSACTIONS: it runs from the start of the whole
	 * task to its ending, and the gaps between transactions are part of what it took. */
	[metrics fnSetTaskInterval:[[[NSDateInterval alloc] initWithStartDate:_started
								     endDate:[NSDate date]] autorelease]];
	[metrics fnSetRedirectCount:_redirects];

	queue = [_session delegateQueue];
	if (queue == nil) {
		[delegate URLSession:_session task:_task didFinishCollectingMetrics:metrics];
		[metrics release];
		return;
	}
	/* THE SAME HOP, WITH THE SAME RETAINS: see the note in -URLProtocol:didLoadData:. */
	[self retain];
	[delegate retain];
	[queue addOperationWithBlock:^{
		[delegate URLSession:_session task:_task didFinishCollectingMetrics:metrics];
		[metrics release];
		[delegate release];
		[self release];
	}];
}

/* THE ENDING IS REPORTED TO THE TASK DELEGATE FOR BOTH OUTCOMES, and the error is the TASK'S rather than a
 * parameter of this method: that is what makes one door serve success and failure alike, which is how Apple
 * declares it and why a caller cannot forget the failure case. */
- (void)fnTellTheTaskDelegate
{
	id <NSURLSessionTaskDelegate> delegate = (id <NSURLSessionTaskDelegate>)[_session delegate];
	NSOperationQueue *queue;
	NSError *error;

	/* AND THE METRICS GO FIRST EVEN WHEN THE COMPLETION DOOR IS NOT IMPLEMENTED, which is why this call sits
	 * ABOVE the guard rather than inside it: Apple declares the two as separate notifications, and a
	 * delegate may implement either one. */
	/* THE ORDER IS APPLE'S AND IT IS LOad-BEARING: a download's FILE is reported BEFORE its task is, so a
	 * delegate that moves the file in the first door has it in place by the time the second says the
	 * transfer is over. AND IT SITS ABOVE THE COMPLETION DOOR'S GUARD for the reason the metrics call does -
	 * Apple declares these as separate notifications, and a delegate may implement either one. */
	[self fnTellTheDownloadDelegate];
	[self fnTellMetrics];
	if (![delegate respondsToSelector:@selector(URLSession:task:didCompleteWithError:)]) {
		return;
	}
	error = [_task error];
	queue = [_session delegateQueue];
	if (queue == nil) {
		[delegate URLSession:_session task:_task didCompleteWithError:error];
		return;
	}
	/* THE SAME HOP, WITH THE SAME RETAINS: see the note in -URLProtocol:didLoadData:. */
	[self retain];
	[delegate retain];
	[error retain];
	[queue addOperationWithBlock:^{
		[delegate URLSession:_session task:_task didCompleteWithError:error];
		[error release];
		[delegate release];
		[self release];
	}];
}

- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error
{
	[_task fnProtocolDidFinishWithError:error];
	[self fnTellTheTaskDelegate];
	[_session fnTransferDidEnd:protocol];
}

/* THE REDIRECT IS DECIDED HERE, AND ACTED ON WHERE THE RECORD IS COMPLETE (§54). CURLOPT_FOLLOWLOCATION=0
 * is what makes "who decides" structural rather than polite, so this is the ONE place the question is asked -
 * and the bridge asks it from INSIDE curl's header callback, which is why the ANSWER is only REMEMBERED here
 * (see the ivars): the attempt's own transaction has not been reported at this moment, and acting now would
 * deliver a record missing it.
 *
 * THE DECISION HAS THREE SHAPES, and only one of them is a follow:
 *   * GO THERE - the SAME client (this object) will run the next request on the SAME task, which is what keeps
 *     the metrics (§52) and the hop count together, and what makes the task's `currentRequest` the target
 *     while `originalRequest` stays what the caller made;
 *   * STAY - a DECLINED redirect is not a failure: the task finishes with the 3xx it received, and the hop is
 *     not counted, because `redirectCount` counts what was PERFORMED;
 *   * TOO MANY - the hop limit, which is OURS and says so: Apple publishes no number, so a limit here is
 *     permitted variation rather than a difference - but a chain that never ends must still END. TWENTY, which
 *     is what the browsers a user of this system has met use. (§54 shipped this failure in the bridge's own
 *     domain BECAUSE the NSURLError* mass was not declared; §56 declared it, so the failure is now
 *     NSURLErrorDomain / NSURLErrorHTTPTooManyRedirects and a caller can read what happened.) */
- (void)URLProtocol:(NSURLProtocol *)protocol
    wasRedirectedToRequest:(NSURLRequest *)request
	 redirectResponse:(NSURLResponse *)redirectResponse
{
	NSURLRequest *follow = [_session fnAskAboutRedirectForTask:_task
							  response:(NSHTTPURLResponse *)redirectResponse
							  proposed:request];

	(void)protocol;
	if (follow == nil) {
		/* WHAT WE HAVE IS THE ANSWER, and no body comes with it because the transfer was stopped at the head
		 * of the answer. */
		_pendingAction = 2;
		_pendingResponse = [redirectResponse retain];
		return;
	}
	if (_redirects >= 20) {
		_pendingAction = 3;
		return;
	}
	_pendingAction = 1;
	_pendingFollow = [follow retain];
}

- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse
{
	/* NO CACHE EXISTS IN THIS LIBRARY YET, so there is never a cached answer to validate. */
	(void)protocol;
	(void)cachedResponse;
}

- (void)dealloc
{
	[_task release];
	[_decision release];
	/* THE RECORD'S OWN STORAGE IS THIS OBJECT'S (§52), so it goes with it: the array of transactions and the
	 * instant the task began. The records themselves are retained by the array, so releasing it releases
	 * them - and once the task has ended there is nothing left that reads either. */
	[_transactions release];
	[_started release];
	/* AND A DECISION THAT WAS NEVER TAKEN (§54): the pending consequence holds the request or the response it
	 * was decided about, so it is released here - a task cancelled between the redirect's report and its
	 * record would otherwise leak both. */
	[_pendingFollow release];
	[_pendingResponse release];
	[super dealloc];
}

@end

NSString * const NSURLSessionDownloadTaskResumeData = @"NSURLSessionDownloadTaskResumeData";

@implementation NSURLSession

/* THE SHARED SESSION, made once and answered by identity from then on: a singleton that answered a new
 * object each time would make "shared" a lie, and the probe pins the identity rather than the equality. */
+ (NSURLSession *)sharedSession
{
	static NSURLSession *shared = nil;

	if (shared == nil) {
		/* BUILT FROM THE DEFAULT CONFIGURATION, through the SAME initializer as every other door: a
		 * session made with plain -init (which this class does not define) would answer a nil
		 * configuration, and the probe's shared-session check is what caught exactly that here. */
		shared = [[NSURLSession alloc]
			initWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]
				delegate:nil
			delegateQueue:nil];
	}
	return shared;
}

+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration
{
	return [[[self alloc] initWithConfiguration:configuration
					   delegate:nil
				      delegateQueue:nil] autorelease];
}

+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration
				  delegate:(id <NSURLSessionDelegate>)delegate
			      delegateQueue:(NSOperationQueue *)queue
{
	return [[[self alloc] initWithConfiguration:configuration
					   delegate:delegate
				      delegateQueue:queue] autorelease];
}

/* FNX: THE REAL INITIALIZER, private to the library because Apple exposes no `-init` on a session either
 * (a session is made by one of the three doors above). Declared here rather than in the header: nothing
 * outside this file calls it. */
- (instancetype)initWithConfiguration:(NSURLSessionConfiguration *)configuration
			     delegate:(id <NSURLSessionDelegate>)delegate
			delegateQueue:(NSOperationQueue *)queue
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE SNAPSHOT: the session's own copy, so a caller editing the configuration it handed over cannot
	 * reach a session that is already running. */
	_configuration = [configuration copy];
	_delegate = [delegate retain];
	_delegateQueue = [queue retain];
	_nextTaskIdentifier = 1;
	_tasks = [[NSMutableArray alloc] init];
	_protocols = [[NSMutableArray alloc] init];
	return self;
}

- (NSURLSessionConfiguration *)configuration { return _configuration; }
- (id <NSURLSessionDelegate>)delegate { return _delegate; }
- (NSOperationQueue *)delegateQueue { return _delegateQueue; }
- (NSString *)sessionDescription { return _sessionDescription; }
- (void)setSessionDescription:(NSString *)description
{
	NSString *old = _sessionDescription;

	_sessionDescription = [description copy];
	[old release];
}

- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request
{
	NSURLSessionDataTask *task;

	/* AN INVALIDATED SESSION ANSWERS nil RATHER THAN A TASK THAT COULD NEVER RUN (the header's rule). */
	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionDataTask alloc] fnInitWithRequest:request
						  identifier:_nextTaskIdentifier++];
	/* AND IT IS LINKED TO ITS SESSION, WHICH THIS DOOR DID NOT DO. The completion-handler factory did,
	 * so row 3's checks passed while this one handed back a task with no session: -resume flipped the
	 * state to running, asked NIL to do the work, and nothing ever moved. The DELEGATE half of the row
	 * found it, because that is the half that drives this door. */
	[task fnSetSession:self];
	[_tasks addObject:task];	/* the session KEEPS its tasks, which is what makes the enumeration below
					 * mean anything */
	return [task autorelease];
}

/* THE URL FORM IS THE REQUEST FORM with a convenience request around it, so the two cannot drift. */
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url
{
	return [self dataTaskWithRequest:[NSURLRequest requestWithURL:url]];
}

/* THE COMPLETION-HANDLER FORMS: the same task, told where its ending goes. The block is copied by the
 * task, and the task is linked to this session either way — a task that could not reach a session could
 * not run. */
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request
			    completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler
{
	NSURLSessionDataTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionDataTask alloc] fnInitWithRequest:request
						   identifier:_nextTaskIdentifier++
					    completionHandler:completionHandler];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url
			completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler
{
	return [self dataTaskWithRequest:[NSURLRequest requestWithURL:url]
		       completionHandler:completionHandler];
}

/* THE DOWNLOAD FACTORIES MIRROR THE DATA ONES, because the model is the same: the session assigns the
 * identifier, keeps the task and links it. What differs is the class and the shape of the handler. */
- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request
{
	NSURLSessionDownloadTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionDownloadTask alloc] fnInitWithRequest:request
							identifier:_nextTaskIdentifier++];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

/* THE STREAM TASK'S FACTORY (§58), and it is the same three steps every other factory here takes: the session
 * assigns the identifier, KEEPS the task, and LINKS it - which is what lets the task reach the delegate and
 * the delegate queue it reports through. What differs is only what the task is made of: no request, a host
 * and a port, and a class that owns a socket. */
- (NSURLSessionStreamTask *)streamTaskWithHostName:(NSString *)hostname port:(NSInteger)port
{
	NSURLSessionStreamTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionStreamTask alloc] fnInitWithHostName:hostname
							    port:port
						      identifier:_nextTaskIdentifier++];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

/* THE WEBSOCKET FACTORIES, and the same four steps the stream factory takes: build it through the class's own
 * fn door (a task is created BY a session), link it, keep it, hand it back autoreleased. THE URL IS CHECKED FOR
 * ws:/wss: HERE rather than left to fail in the handshake with a worse message - Apple's rule is that the scheme
 * must be one of those two, and a nil answer is this library's way of saying so. (The request form takes its URL
 * from the request and passes any protocols through the header, which the handshake layer already does.) */
- (NSURLSessionWebSocketTask *)webSocketTaskWithURL:(NSURL *)url protocols:(NSArray *)protocols
{
	NSURLSessionWebSocketTask *task;
	NSString *scheme = [[url scheme] lowercaseString];

	if(_invalid || (scheme == nil ||
	   (![scheme isEqualToString:@"ws"] && ![scheme isEqualToString:@"wss"]))) {
		return nil;
	}
	task = [[NSURLSessionWebSocketTask alloc] fnInitWithURL:url
						     protocols:protocols
						    identifier:_nextTaskIdentifier++];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

- (NSURLSessionWebSocketTask *)webSocketTaskWithURL:(NSURL *)url
{
	return [self webSocketTaskWithURL:url protocols:nil];
}

- (NSURLSessionWebSocketTask *)webSocketTaskWithRequest:(NSURLRequest *)request
{
	return [self webSocketTaskWithURL:[request URL] protocols:nil];
}

- (NSURLSessionDownloadTask *)downloadTaskWithResumeData:(NSData *)resumeData
{
	NSURLSessionDownloadTask *task;

	if (_invalid) {
		return nil;
	}
	/* NIL FROM THE TASK MEANS THE BLOB WAS NOT OURS, and this door passes that on rather than inventing a task:
	 * a resume of something this library did not write is not a resume (see -fnInitWithResumeData:…). */
	task = [[NSURLSessionDownloadTask alloc] fnInitWithResumeData:resumeData
							   identifier:_nextTaskIdentifier++
						      downloadHandler:nil];
	if (task == nil) {
		return nil;
	}
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

- (NSURLSessionDownloadTask *)downloadTaskWithResumeData:(NSData *)resumeData
				      completionHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))completionHandler
{
	NSURLSessionDownloadTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionDownloadTask alloc] fnInitWithResumeData:resumeData
							   identifier:_nextTaskIdentifier++
						      downloadHandler:completionHandler];
	if (task == nil) {
		return nil;
	}
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request
				    completionHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))completionHandler
{
	NSURLSessionDownloadTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionDownloadTask alloc] fnInitWithRequest:request
							identifier:_nextTaskIdentifier++
						   downloadHandler:completionHandler];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

/* THE UPLOAD FACTORIES PUT THE BODY ON THE REQUEST, because NSURLRequest is immutable and a caller holding
 * one cannot add a body to it. Everything else is the data task's own model. */
- (NSURLSessionUploadTask *)fnUploadTaskWithRequest:(NSURLRequest *)request
					    handler:(void (^)(NSData *, NSURLResponse *, NSError *))handler
{
	NSURLSessionUploadTask *task;

	if (_invalid) {
		return nil;
	}
	task = [[NSURLSessionUploadTask alloc] fnInitWithRequest:request
						      identifier:_nextTaskIdentifier++
					       completionHandler:handler];
	[task fnSetSession:self];
	[_tasks addObject:task];
	return [task autorelease];
}

- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromData:(NSData *)bodyData
{
	NSMutableURLRequest *withBody = [request mutableCopy];
	NSURLSessionUploadTask *task;

	/* THE BODY GOES ON A COPY, so the caller's request is untouched. */
	[withBody setHTTPBody:bodyData];
	task = [self fnUploadTaskWithRequest:withBody handler:nil];
	[withBody release];
	return task;
}

- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request
					 fromData:(NSData *)bodyData
				completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler
{
	NSMutableURLRequest *withBody = [request mutableCopy];
	NSURLSessionUploadTask *task;

	[withBody setHTTPBody:bodyData];
	task = [self fnUploadTaskWithRequest:withBody handler:completionHandler];
	[withBody release];
	return task;
}

/* THE EXECUTION ITSELF, and every step of it is a decision this row makes:
 *
 *   1. THE CONFIGURATION'S protocolClasses COME FIRST — a caller's explicit list — and slice 2a's registry
 *      second, both through the seam's own door;
 *   2. NO CLASS AT ALL IS AN ERROR, not a hang: a task whose request nothing claims ends with
 *      NSURLErrorUnsupportedURL (-1002) and its completion handler is called;
 *   3. THE PROTOCOL IS KEPT ALIVE BY THE SESSION for the flight (a protocol deallocated mid-transfer
 *      would report into freed memory), and the per-transfer client is what knows the task. */
/* THE REDIRECT DECISION (§54), ASKED ONCE AND ANSWERED SYNCHRONOUSLY - the same deviation the challenge door
 * carries (§48.6) and for the same reason: the transport is WAITING at the head of an answer, so an
 * asynchronous answer would be unwaitable. The handler is therefore called before this method returns, which
 * is its contract HERE rather than Apple's.
 *
 * NO DOOR MEANS FOLLOW, WITHOUT WAITING: a delegate that does not implement this must not stop a redirect
 * Apple's own loading system would have followed - and the proposal the loading system made is already the
 * right request, because the transport applied RFC 9110's method rules to it before proposing it.
 *
 * THE ANSWER IS RETAINED AND AUTORELEASED because it travels back through the bridge and out to whoever
 * asked: in MRC a value that outlives the call that produced it has to say so. */
- (NSURLRequest *)fnAskAboutRedirectForTask:(NSURLSessionTask *)task
				  response:(NSHTTPURLResponse *)response
				  proposed:(NSURLRequest *)request
{
	id delegate = _delegate;
	__block NSURLRequest *answer = nil;

	if (delegate != nil &&
	    [delegate respondsToSelector:
		@selector(URLSession:task:willPerformHTTPRedirection:newRequest:completionHandler:)]) {
		[(id)delegate URLSession:self
				    task:task
	    willPerformHTTPRedirection:response
			      newRequest:request
		       completionHandler:^(NSURLRequest *chosen) {
			answer = [chosen retain];
		}];
	} else {
		answer = [request retain];
	}
	return [answer autorelease];
}

/* THE CHALLENGE DOOR, AND THE ORDER IS APPLE'S: the task-level delegate is more specific, so it is asked
 * first and the session's door is the fallback. IT IS SYNCHRONOUS - unlike the data and completion callbacks,
 * which hop to the delegate queue - because the transport WAITS for the answer, exactly as it waits at the
 * head of a response; hopping would make the wait unwaitable.
 *
 * A DELEGATE THAT IMPLEMENTS NEITHER DOOR IS NOT WAITED FOR: it is answered with PerformDefaultHandling,
 * which is also what a delegate that implements one of them returns when it has no opinion. That keeps the
 * rule the response-disposition door established - NO DOOR MEANS THE DEFAULT, WITHOUT WAITING. */
- (void)fnAskForCredentialForTask:(NSURLSessionTask *)task
			challenge:(NSURLAuthenticationChallenge *)challenge
		completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
					    NSURLCredential *credential))completionHandler
{
	id delegate = _delegate;

	if (completionHandler == nil) {
		return;
	}
	if ([delegate respondsToSelector:
			@selector(URLSession:task:didReceiveChallenge:completionHandler:)]) {
		[(id)delegate URLSession:self
				    task:task
		      didReceiveChallenge:challenge
		       completionHandler:completionHandler];
		return;
	}
	if ([delegate respondsToSelector:
			@selector(URLSession:didReceiveChallenge:completionHandler:)]) {
		[(id)delegate URLSession:self
	       didReceiveChallenge:challenge
		 completionHandler:completionHandler];
		return;
	}
	completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)fnTaskDidResume:(NSURLSessionTask *)task
{
	/* A RESUMED DOWNLOAD IS ANNOUNCED BEFORE ANY CHUNK ARRIVES (§62.31), which is Apple's order and the only
	 * one that makes sense: a delegate that wants to show "continuing from N" has to hear it before the
	 * chunks start arriving at N. The offset is the task's, and the expected total is what the interrupted
	 * transfer had learned - 0 when it never knew. */
	if ([task isKindOfClass:[NSURLSessionDownloadTask class]] &&
	    [(NSURLSessionDownloadTask *)task fnResumeOffset] > 0) {
		id <NSURLSessionDownloadDelegate> downloadDelegate =
			(id <NSURLSessionDownloadDelegate>)[self delegate];

		if ([downloadDelegate respondsToSelector:
			@selector(URLSession:downloadTask:didResumeAtOffset:expectedTotalBytes:)]) {
			[downloadDelegate URLSession:self
					downloadTask:(NSURLSessionDownloadTask *)task
				 didResumeAtOffset:[(NSURLSessionDownloadTask *)task fnResumeOffset]
				expectedTotalBytes:[task countOfBytesExpectedToReceive]];
		}
	}

	FNSessionTransfer *client = [[FNSessionTransfer alloc] initWithTask:task session:self];

	/* AND THE START IS NOT WRITTEN HERE ANY MORE (§54): a followed redirect starts the next request on the
	 * SAME task through the SAME door, so the one place a transfer begins is the one place that knows how
	 * (see -fnStartTransferForTask:request:client:). */
	[self fnStartTransferForTask:task request:[task currentRequest] client:client];
	[client release];
}

/* WHERE A TRANSFER BEGINS, ONCE (§54), and every step of it is a decision this row has made all along:
 *
 *   1. THE CONFIGURATION'S protocolClasses COME FIRST - a caller's explicit list - and the registry second,
 *      both through the seam's own door;
 *   2. NO CLASS AT ALL IS AN ERROR, not a hang: a task whose request nothing claims ends with
 *      NSURLErrorUnsupportedURL (-1002) and its completion handler is called;
 *   3. THE PROTOCOL IS KEPT ALIVE BY THE SESSION for the flight (a protocol deallocated mid-transfer would
 *      report into freed memory), and the per-transfer client is what knows the task - including the one a
 *      redirect REUSES. */
- (void)fnStartTransferForTask:(NSURLSessionTask *)task
		       request:(NSURLRequest *)request
			client:(FNSessionTransfer *)client
{
	NSArray *classes = [_configuration protocolClasses];
	NSURLProtocol *protocol = nil;
	Class protocolClass = nil;
	NSUInteger i;

	for (i = 0; classes != nil && i < [classes count] && protocolClass == nil; i++) {
		Class candidate = [classes objectAtIndex:i];

		if ([candidate canInitWithRequest:request]) {
			protocolClass = candidate;
		}
	}
	if (protocolClass == nil) {
		protocolClass = [NSURLProtocol fnProtocolClassForRequest:request];
	}
	if (protocolClass == nil) {
		NSError *unsupported = [[NSError alloc] initWithDomain:@"NSURLErrorDomain"
								 code:NSURLErrorUnsupportedURL
							     userInfo:nil];

		[task fnProtocolDidFinishWithError:unsupported];
		[unsupported release];
		return;
	}
	protocol = [[protocolClass alloc] initWithRequest:request
					   cachedResponse:nil
						   client:client];
	[_protocols addObject:protocol];	/* the session keeps the flight alive */
	[protocol startLoading];
	[protocol release];			/* the array holds it now */
}

/* THE ENDING'S OTHER HALF: the transfer is dropped, which releases the protocol, which releases the
 * client, which releases the task — and that is the cycle broken at exactly the moment it should be. */
- (void)fnTransferDidEnd:(NSURLProtocol *)protocol
{
	[_protocols removeObjectIdenticalTo:protocol];
}

- (void)getTasksWithCompletionHandler:(void (^)(NSArray *, NSArray *, NSArray *))completionHandler
{
	if (completionHandler == nil) {
		return;
	}
	/* ONE ARRAY IS REAL AND TWO ARE ALWAYS EMPTY: the upload and download task classes are not shipped,
	 * so no session here can be holding one. Said in the header, asserted by the probe. */
	completionHandler([NSArray arrayWithArray:_tasks],
			  [NSArray array],
			  [NSArray array]);
}

- (void)invalidateAndCancel
{
	NSUInteger i;

	/* CANCEL FIRST, THEN INVALIDATE: a task cancelled after the session is marked invalid would be a task
	 * the session had already stopped owning. */
	for (i = 0; i < [_tasks count]; i++) {
		[[_tasks objectAtIndex:i] cancel];
	}
	_invalid = YES;
}

- (void)finishTasksAndInvalidate
{
	/* NO EXECUTION IN THIS ROW, so "finish" has nothing to wait for: the tasks keep whatever state they
	 * are in and the session stops accepting new ones. The execution row is where this gains teeth. */
	_invalid = YES;
}

- (void)dealloc
{
	[_configuration release];
	[_delegate release];
	[_delegateQueue release];
	[_sessionDescription release];
	[_tasks release];
	[_protocols release];
	[super dealloc];
}

@end
