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
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

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
	[_tasks addObject:task];	/* the session KEEPS its tasks, which is what makes the enumeration below
					 * mean anything */
	return [task autorelease];
}

/* THE URL FORM IS THE REQUEST FORM with a convenience request around it, so the two cannot drift. */
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url
{
	return [self dataTaskWithRequest:[NSURLRequest requestWithURL:url]];
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
	[super dealloc];
}

@end
