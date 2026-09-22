/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSession.m — creation, the task factories and invalidation. The design is in NSURLSession.h.
 */
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#include <stdio.h>
#import <Foundation/NSOperationQueue.h>

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
@interface NSURLSession (FNSessionTransferControl)
- (void)fnTransferDidEnd:(NSURLProtocol *)protocol;
@end

@interface FNSessionTransfer : NSObject <NSURLProtocolClient>
{
	NSURLSessionTask *_task;
	NSURLSession *_session;
}
- (instancetype)initWithTask:(NSURLSessionTask *)task session:(NSURLSession *)session;
@end

/* THE PRIVATE DECLARATIONS THE TRANSFER NEEDS IN ITS OWN HEADER-LESS WAY: the class above is
 * declared, and this category is the ending helper its implementations call. */
@interface FNSessionTransfer (FNSessionTransferEnding)
- (void)fnTellTheTaskDelegate;
@end


@implementation FNSessionTransfer

- (instancetype)initWithTask:(NSURLSessionTask *)task session:(NSURLSession *)session
{
	self = [super init];
	if (self != nil) {
		_task = [task retain];
		_session = session;	/* unretained */
	}
	return self;
}

/* EVERY CALLBACK IS A TRANSLATION INTO THE TASK'S OWN STATE, and nothing more: the protocol streams, the
 * task accumulates, and the ending is what reports. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy
{
	(void)policy;
	[_task fnProtocolDidReceiveResponse:response];
}

- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data
{
	id <NSURLSessionDataDelegate> delegate;
	NSOperationQueue *queue;

	[_task fnProtocolDidLoadData:data];
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

- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol
{
	[_task fnProtocolDidFinishWithError:nil];
	[self fnTellTheTaskDelegate];
	[_session fnTransferDidEnd:protocol];
}

/* THE ENDING IS REPORTED TO THE TASK DELEGATE FOR BOTH OUTCOMES, and the error is the TASK'S rather than a
 * parameter of this method: that is what makes one door serve success and failure alike, which is how Apple
 * declares it and why a caller cannot forget the failure case. */
- (void)fnTellTheTaskDelegate
{
	id <NSURLSessionTaskDelegate> delegate = (id <NSURLSessionTaskDelegate>)[_session delegate];
	NSOperationQueue *queue;
	NSError *error;

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

/* A REDIRECT IS NOT FOLLOWED IN THIS ROW, AND THE FAILURE SAYS SO rather than leaving a task that never
 * ends. The bridge reports the redirect (CURLOPT_FOLLOWLOCATION=0) precisely so the DECISION is here, and
 * "follow it" is the next row's job: until then the honest answer is an error in this library's own
 * domain, which a caller can tell apart from a transport failure. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    wasRedirectedToRequest:(NSURLRequest *)request
	 redirectResponse:(NSURLResponse *)redirectResponse
{
	NSError *error = [[NSError alloc] initWithDomain:@"FNCURLURLProtocolErrorDomain"
						    code:1
						userInfo:nil];

	(void)request;
	(void)redirectResponse;
	[_task fnProtocolDidFinishWithError:error];
	[error release];
	[_session fnTransferDidEnd:protocol];
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
	[super dealloc];
}

@end

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
	printf("FNSESSION-DIAG factory: task made, before fnSetSession\n");
	[task fnSetSession:self];
	printf("FNSESSION-DIAG factory: before addObject\n");
	[_tasks addObject:task];
	printf("FNSESSION-DIAG factory: after addObject\n");
	return [task autorelease];
}

- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url
			completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler
{
	return [self dataTaskWithRequest:[NSURLRequest requestWithURL:url]
		       completionHandler:completionHandler];
}

/* THE EXECUTION ITSELF, and every step of it is a decision this row makes:
 *
 *   1. THE CONFIGURATION'S protocolClasses COME FIRST — a caller's explicit list — and slice 2a's registry
 *      second, both through the seam's own door;
 *   2. NO CLASS AT ALL IS AN ERROR, not a hang: a task whose request nothing claims ends with
 *      NSURLErrorUnsupportedURL (-1002) and its completion handler is called;
 *   3. THE PROTOCOL IS KEPT ALIVE BY THE SESSION for the flight (a protocol deallocated mid-transfer
 *      would report into freed memory), and the per-transfer client is what knows the task. */
- (void)fnTaskDidResume:(NSURLSessionTask *)task
{
	NSURLRequest *request = [task currentRequest];
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
								 code:-1002
							     userInfo:nil];

		[task fnProtocolDidFinishWithError:unsupported];
		[unsupported release];
		return;
	}
	{
		FNSessionTransfer *client = [[FNSessionTransfer alloc] initWithTask:task session:self];

		protocol = [[protocolClass alloc] initWithRequest:request
						   cachedResponse:nil
							   client:client];
		[client release];	/* the protocol retains its client */
	}
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
