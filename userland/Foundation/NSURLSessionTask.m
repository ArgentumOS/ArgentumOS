/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionTask.m — the state machine and the fields. The design is in NSURLSessionTask.h.
 */
#import <Foundation/NSURLSessionTask.h>
/* AND THE SESSION'S OWN HEADER, because -resume messages a door declared there (-fnTaskDidResume:) and a
 * message to an `id` whose selector no visible header declares compiles with a WARNING rather than an error -
 * which is how this file called it undeclared for as long as the session header sat out of view. */
#import <Foundation/NSURLSession.h>
#include <Block.h>	/* Block_copy/Block_release: the runtime entry points, which
				 * need no isa on the block - see the copy site below */
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSData.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSError.h>
/* THE ERROR NAMES THE TASK REPORTS (§56): -cancel's code and the body-file write failure are constants now
 * rather than literals with the names in comments. */
#import <Foundation/NSURLError.h>
#import <Foundation/NSPropertyListSerialization.h>	/* the resume blob is a plist (§62.31) */
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

/* A FOLLOWED REDIRECT MOVES WHAT THE TASK IS ABOUT, and nothing else (§54): the identifier, the state, the
 * handler and the accumulated bytes all stay, because it is the SAME task - Apple's contract, and the reason
 * this is a door on the task rather than a new task in the session. `originalRequest` is deliberately
 * untouched: it is what the task was made from, and a caller reads it to find that out. */
- (void)fnProtocolDidRedirectToRequest:(NSURLRequest *)request
{
	NSURLRequest *old = _currentRequest;

	_currentRequest = [request copy];
	[old release];
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
							       code:NSURLErrorCancelled
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

/* THE DOWNLOAD TASK WRITES THE BODY ON ITS WAY OUT, and it does that by OVERRIDING THE ENDING rather than
 * by being special-cased by the session: -fnProtocolDidFinishWithError: is the single door every ending goes
 * through, so a task that must transform its result before reporting it overrides that door and calls
 * -super once the result exists. The session needs to know nothing about it. */
@implementation NSURLSessionDownloadTask

- (instancetype)fnInitWithRequest:(NSURLRequest *)request
		       identifier:(NSUInteger)identifier
		  downloadHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))handler
{
	self = [self fnInitWithRequest:request identifier:identifier];
	if (self != nil) {
		[self fnSetDownloadHandler:handler];
	}
	return self;
}

- (void)fnSetDownloadHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))handler
{
	/* A BLOCK-LOCAL NAME THAT COULD NOT BE ANYTHING ELSE, and that is deliberate: this file already holds
	 * `NSString *old` in -setTaskDescription:, and the gate's rule is NAME-based, so a local called `old`
	 * that happens to hold a BLOCK made that ordinary `[old release]` read as a block release. The names are
	 * scoped per file, so the fix is the name - and it is worth stating because a name-based rule is only as
	 * good as the names in the file it reads. */
	void (^previousHandler)(NSURL *, NSURLResponse *, NSError *) = _downloadHandler;

	/* Block_copy/Block_release, never -copy/-release: tools/foundation-gate.py refuses the message form for
	 * a block-typed name, and a block is not an ordinary object to own. */
	_downloadHandler = Block_copy(handler);
	if (previousHandler != NULL) {
		Block_release(previousHandler);
	}
}

- (NSURL *)location { return _location; }

- (int64_t)fnResumeOffset { return _resumeOffset; }

/* --- RESUME (§62.31) --------------------------------------------------------------------------------
 *
 * THE PACKING IS THIS LIBRARY'S OWN AND IT IS A PLIST, which is the honest choice for something APPLE DECLARES
 * OPAQUE: a caller never reads it, and a plist is a format this library already carries
 * (NSPropertyListSerialization) rather than a private byte layout nobody can inspect. It holds the URL, the
 * method, the body if there was one, HOW MANY BYTES ARRIVED, and those bytes - so a resumed task can rebuild
 * the request and continue the body rather than starting over.
 *
 * AND NIL MEANS NOTHING TO RESUME, which is a real answer rather than a failure: a task cancelled before its
 * first byte has no partial body, and Apple's own contract makes the resume data nullable for exactly that.
 */
- (NSData *)fnResumeData
{
	NSMutableDictionary *fields;
	NSURL *url = [[self currentRequest] URL];
	NSData *plist;
	NSError *error = nil;

	if (_receivedData == nil || [(_receivedData) length] == 0 || url == nil) {
		return nil;
	}
	fields = [NSMutableDictionary dictionary];
	[fields setObject:[url absoluteString] forKey:@"URL"];
	[fields setObject:[[self currentRequest] HTTPMethod] forKey:@"Method"];
	[fields setObject:[NSNumber numberWithLongLong:(long long)[(_receivedData) length]]
		   forKey:@"Offset"];
	[fields setObject:_receivedData forKey:@"Data"];
	if (_countOfBytesExpectedToReceive > 0) {
		[fields setObject:[NSNumber numberWithLongLong:_countOfBytesExpectedToReceive]
			   forKey:@"Total"];
	}
	if ([[self currentRequest] HTTPBody] != nil) {
		[fields setObject:[[self currentRequest] HTTPBody] forKey:@"Body"];
	}
	/* XML RATHER THAN BINARY, AND THAT IS A MEASUREMENT RATHER THAN A PREFERENCE: this library's plist WRITER
	 * supports only `NSPropertyListXMLFormat_v1_0` - it answers NIL for any other format
	 * (NSPropertyListSerialization.m says so in so many words) - so a binary request here produced a resume
	 * blob that was always nil, which the probe's first run showed as "0 byte(s) of resume data" while the
	 * transfer was demonstrably holding ten. The blob is OPAQUE, so its format is this library's business. */
	plist = [NSPropertyListSerialization dataWithPropertyList:fields
							  format:NSPropertyListXMLFormat_v1_0
							 options:0
							   error:&error];
	return plist;
}

- (void)cancelByProducingResumeData:(void (^)(NSData *))completionHandler
{
	/* THE DATA IS BUILT BEFORE THE CANCEL, because cancelling is what clears the task's state: asking after it
	 * would ask about a transfer that is already over. */
	NSData *resumeData = [[self fnResumeData] retain];

	[self cancel];
	if (completionHandler != nil) {
		completionHandler([resumeData autorelease]);
	}
}

- (instancetype)fnInitWithResumeData:(NSData *)resumeData
			  identifier:(NSUInteger)identifier
		     downloadHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))handler
{
	NSDictionary *fields;
	NSMutableURLRequest *request;
	NSData *partial;
	NSError *error = nil;

	/* A BLOB THIS LIBRARY DID NOT WRITE IS NOT A RESUME: the caller gets nil rather than a task built from
	 * whatever a plist happened to contain. */
	fields = [NSPropertyListSerialization propertyListWithData:resumeData
							  options:0
							   format:NULL
							    error:&error];
	if (![fields isKindOfClass:[NSDictionary class]]) {
		return nil;
	}
	request = [NSMutableURLRequest requestWithURL:
			[NSURL URLWithString:[fields objectForKey:@"URL"]]];
	if ([fields objectForKey:@"Method"] != nil) {
		[request setHTTPMethod:[fields objectForKey:@"Method"]];
	}
	if ([fields objectForKey:@"Body"] != nil) {
		[request setHTTPBody:[fields objectForKey:@"Body"]];
	}
	partial = [fields objectForKey:@"Data"];
	/* THE RANGE HEADER IS WHAT MAKES IT A RESUME RATHER THAN A RESTART, and it is set HERE rather than in the
	 * transport because the transport already sends a request's headers (the bridge passes them to libcurl):
	 * a byte range needs no new machinery anywhere. */
	[request setValue:[NSString stringWithFormat:@"bytes=%d-", (int)[partial length]]
	forHTTPHeaderField:@"Range"];

	self = [self fnInitWithRequest:request identifier:identifier downloadHandler:handler];
	if (self != nil) {
		_resumeOffset = (int64_t)[partial length];
		/* THE BYTES ALREADY HELD BECOME THE TASK'S OWN, so the body it reports is the WHOLE body: a resumed
		 * download that reported only its second half would be a trap for every caller. */
		[self fnProtocolDidLoadData:partial];
		_countOfBytesExpectedToReceive = [[fields objectForKey:@"Total"] longLongValue];
	}
	return self;
}

- (void)fnProtocolDidFinishWithError:(NSError *)error
{
	NSError *attached = nil;	/* the error carrying resume data, released after super has reported it */

	/* THE BODY BECOMES A FILE ONLY ON SUCCESS: a failed transfer has nothing to write, and a handler told
	 * about a location holding a partial body would be worse than one told nothing. */
	if (error == nil && _receivedData != nil) {
		NSString *path = [NSString stringWithFormat:@"%@fn-download-%lu",
				  NSTemporaryDirectory(), (unsigned long)_taskIdentifier];

		if ([_receivedData writeToFile:path atomically:YES]) {
			_location = [[NSURL fileURLWithPath:path] copy];
		} else {
			NSError *writeError = [[NSError alloc] initWithDomain:@"NSURLErrorDomain"
									code:NSURLErrorCannotCreateFile
								    userInfo:nil];

			error = writeError;	/* released below, after it has been reported */
			[writeError autorelease];
		}
	}
	/* A FAILED DOWNLOAD CARRIES ITS RESUME DATA ON THE ERROR ITSELF (§62.31), so a caller can recover from a
	 * dropped connection without having asked for anything: the transfer failed, and the error says how to
	 * continue it. The error is REBUILT rather than mutated - NSError is immutable - and RELEASED BELOW,
	 * after it has been reported, which is this file's existing idiom for the write failure above. */
	{
		NSData *resume = [self fnResumeData];

		if (error != nil && resume != nil) {
			NSMutableDictionary *fields = [[error userInfo] mutableCopy];
			NSError *withResume;

			if (fields == nil) {
				fields = [[NSMutableDictionary alloc] init];
			}
			[fields setObject:resume forKey:NSURLSessionDownloadTaskResumeData];
			withResume = [[NSError alloc] initWithDomain:[error domain]
								code:[error code]
							    userInfo:fields];
			[fields release];
			attached = withResume;
			error = attached;
		}
	}
	[super fnProtocolDidFinishWithError:error];
	[attached release];
	if (_downloadHandler != NULL) {
		_downloadHandler(_location, _response, error);
	}
}

- (void)dealloc
{
	[_location release];
	if (_downloadHandler != NULL) {
		Block_release(_downloadHandler);
	}
	[super dealloc];
}

@end

/* THE DATA TASK ADDS ONLY ITS MEANING — it is the task that carries a request, and the request fields are
 * declared on the base because Apple declares them there. An empty @implementation is the honest one. */
@implementation NSURLSessionDataTask

@end

/* THE UPLOAD TASK IS AN EMPTY IMPLEMENTATION, LIKE THE DATA TASK'S, AND IT IS NOT OPTIONAL: a class with an
 * @interface and no @implementation has NO CLASS OBJECT, so a reference to it links as an undefined symbol -
 * which is how this was found, as a link error rather than a runtime one. */
@implementation NSURLSessionUploadTask

@end
