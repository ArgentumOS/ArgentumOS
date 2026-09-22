/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionTask.m — the state machine and the fields. The design is in NSURLSessionTask.h.
 */
#import <Foundation/NSURLSessionTask.h>
#include <Block.h>	/* Block_copy/Block_release: the runtime entry points, which
				 * need no isa on the block - see the copy site below */
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>

@implementation NSURLSessionTask

/* THE REPORTING DOORS ARE THE SESSION'S, and they are what turns a protocol's callbacks into a task's
 * state: the response is kept, the bytes are ACCUMULATED (a completion handler wants the whole body, while
 * the protocol still streams it), and the ending is one of finished or failed. */
- (void)fnSetSession:(id)session
{
	_session = session;		/* unretained, deliberately: see the ivar's comment */
}

- (void)fnProtocolDidReceiveResponse:(NSURLResponse *)response
{
	NSURLResponse *old = _response;

	_response = [response copy];
	[old release];
	_countOfBytesExpectedToReceive = [response expectedContentLength];
}

- (void)fnProtocolDidLoadData:(NSData *)data
{
	if (_receivedData == nil) {
		_receivedData = [[NSMutableData alloc] init];
	}
	[_receivedData appendData:data];
	_countOfBytesReceived = (int64_t)[_receivedData length];
}

- (void)fnProtocolDidFinishWithError:(NSError *)error
{
	if (_state == NSURLSessionTaskStateCompleted) {
		return;			/* an ending already happened: -cancel and a late failure cannot both win */
	}
	_state = NSURLSessionTaskStateCompleted;
	if (error != nil) {
		_error = [error retain];
	}
	if (_completionHandler != nil) {
		_completionHandler(_receivedData, _response, _error);
	}
}

/* FNX: THE SESSION'S OWN DOOR. A task is created BY a session — which is why -init is not public here —
 * so the session needs an initializer and this is it, named the way this library names a first-party door
 * on a public class (see +[NSURLProtocol fnProtocolClassForRequest:]). */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request identifier:(NSUInteger)identifier
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_taskIdentifier = identifier;
	_originalRequest = [request copy];
	_currentRequest = [request copy];
	_priority = 0.5f;			/* Apple's default */
	_state = NSURLSessionTaskStateSuspended;	/* A NEW TASK IS SUSPENDED */
	return self;
}

/* ONE CODE PATH FOR BOTH DOORS: the completion-handler form is the plain one plus a copied block, so a
 * change to either cannot leave the other behind. THE BLOCK IS COPIED because a task outlives the call
 * that made it — the same reason NSURLRequest copies its headers. */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request
		       identifier:(NSUInteger)identifier
		completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler
{
	self = [self fnInitWithRequest:request identifier:identifier];
	if (self != nil) {
		/* THE RUNTIME ENTRY POINT, NOT -copy, AND THAT IS THE FIX. `-copy` is a MESSAGE, so the runtime
		 * reads the block's ISA to find its class - and the fault was measured exactly there: a null read
		 * inside `objc_msgSend`, with a nil handler surviving and a real block faulting. Block_copy()
		 * performs the same stack-to-heap copy WITHOUT a message send, so it cannot depend on the isa
		 * being resolved. Block_release() pairs with it in -dealloc and in the setter below. */
		_completionHandler = Block_copy(completionHandler);
	}
	return self;
}

- (NSUInteger)taskIdentifier { return _taskIdentifier; }
- (NSURLRequest *)originalRequest { return _originalRequest; }
- (NSURLRequest *)currentRequest { return _currentRequest; }
- (NSURLResponse *)response { return _response; }
- (NSError *)error { return _error; }
- (NSString *)taskDescription { return _taskDescription; }
- (void)setTaskDescription:(NSString *)description
{
	NSString *old = _taskDescription;

	_taskDescription = [description copy];
	[old release];
}
- (float)priority { return _priority; }
- (void)setPriority:(float)priority { _priority = priority; }
- (NSURLSessionTaskState)state { return _state; }
- (int64_t)countOfBytesReceived { return _countOfBytesReceived; }
- (int64_t)countOfBytesExpectedToReceive { return _countOfBytesExpectedToReceive; }

- (void)resume
{
	/* ONLY A SUSPENDED TASK RESUMES. A running one stays running, and a COMPLETED one stays completed
	 * rather than being dragged back into life — "a task cannot be resumed after it completes", which is
	 * the rule Apple states and the reason this is a guarded transition rather than an assignment. */
	if (_state == NSURLSessionTaskStateSuspended) {
		_state = NSURLSessionTaskStateRunning;
	/* AND IT ACTUALLY RUNS: a task with a session asks it to do the work, which is
		 * what makes -resume an act rather than a flag. A task WITHOUT a session (the
		 * model's own case, which the state-machine probe uses) simply becomes running. */
	[(id)_session fnTaskDidResume:self];
	}
}

- (void)suspend
{
	/* AND SUSPENDING ONLY MEANS ANYTHING FOR A RUNNING TASK: a completed one is not made to wait. */
	if (_state == NSURLSessionTaskStateRunning) {
		_state = NSURLSessionTaskStateSuspended;
	}
}

- (void)cancel
{
	/* THE SIMPLIFICATION THIS ROW MAKES, stated where the state changes: with no transfer running there is
	 * no Canceling period to pass through, so a cancel goes straight to the state a cancel ends in. The
	 * execution row gives Canceling its meaning and this comment changes with it. */
	if (_state != NSURLSessionTaskStateCompleted) {
		NSError *cancelled = [[NSError alloc] initWithDomain:@"NSURLErrorDomain"
							       code:-999	/* NSURLErrorCancelled */
							   userInfo:nil];

		_state = NSURLSessionTaskStateCompleted;
		_error = cancelled;		/* retained by the assignment above; released in -dealloc */
	}
}

- (void)dealloc
{
	[_originalRequest release];
	[_currentRequest release];
	[_response release];
	[_error release];
	[_taskDescription release];
	[_receivedData release];
	if (_completionHandler != NULL) {
		Block_release(_completionHandler);
	}
	[super dealloc];
}

@end

/* THE DATA TASK ADDS ONLY ITS MEANING — it is the task that carries a request, and the request fields are
 * declared on the base because Apple declares them there. An empty @implementation is the honest one. */
@implementation NSURLSessionDataTask

@end
