/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionTask.m — the state machine and the fields. The design is in NSURLSessionTask.h.
 */
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>

@implementation NSURLSessionTask

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
	[super dealloc];
}

@end

/* THE DATA TASK ADDS ONLY ITS MEANING — it is the task that carries a request, and the request fields are
 * declared on the base because Apple declares them there. An empty @implementation is the honest one. */
@implementation NSURLSessionDataTask

@end
