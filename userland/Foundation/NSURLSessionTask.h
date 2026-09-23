/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionTask — A UNIT OF WORK, AND ITS STATE. docs/design/foundation-plan.md W7; the transport
 * plan's §4, slice 2c.
 *
 * THE TASK IS A MODEL HERE, NOT A TRANSFER: it carries an identity, the request it was made from, a
 * description, a priority and a state, and it can be resumed, suspended and cancelled. NOTHING MOVES
 * YET — the execution row of this slice is what runs a task through the bridge — and the header says
 * that rather than leaving a caller to discover it.
 *
 * THE STATE MACHINE IS THE PART WORTH READING, because it is the part that survives execution:
 *
 *   * A NEWLY CREATED TASK IS SUSPENDED. That is Apple's rule and it is why `-resume` exists as an
 *     explicit act rather than a side effect of creation; `resume-refuses-what-it-cannot-run` and
 *     `task-starts-suspended-and-resumes` pin both ends of it.
 *   * `-cancel` MOVES A TASK TO Completed IN THIS SLICE, and that is a simplification stated rather than
 *     hidden: with no transfer running there is no Canceling period to pass through, so the task goes
 *     straight to the state a cancel ends in. When the execution row lands, the Canceling state gets its
 *     real meaning and this comment changes with it.
 *   * A DELEGATE THE SESSION WAS NOT GIVEN IS NOT A FAILURE. Apple's delegate parameters are nullable
 *     throughout, and a task with no delegate and no completion handler simply reports to nobody.
 *
 * NSURLSessionTaskState'S VALUES ARE OURS UNDER §11.6.1 D2, as every enum's in this library are: Apple
 * publishes the case names, and the probe pins the numbers.
 *
 * REFUSED BY NAME: the coder doors (every class here refuses them) and `-init` — a task is created BY a
 * session, so it has no public initializer of its own and the inventory asserts that it has none.
 */

#ifndef FOUNDATION_NSURLSESSIONTASK_H
#define FOUNDATION_NSURLSESSIONTASK_H

#import <Foundation/NSObject.h>

@class NSData;
@class NSError;
@class NSMutableData;
@class NSString;
@class NSURL;
@class NSURLRequest;
@class NSURLResponse;

NS_ASSUME_NONNULL_BEGIN

/* WHERE A TASK IS IN ITS LIFE. The values are ours (D2); the case names are Apple's. */
typedef NS_ENUM(NSInteger, NSURLSessionTaskState) {
	NSURLSessionTaskStateRunning = 0,
	NSURLSessionTaskStateSuspended = 1,
	NSURLSessionTaskStateCanceling = 2,
	NSURLSessionTaskStateCompleted = 3,
};

@interface NSURLSessionTask : NSObject
{
	NSUInteger _taskIdentifier;
	NSURLRequest *_originalRequest;
	NSURLRequest *_currentRequest;
	NSURLResponse *_response;
	NSError *_error;
	NSString *_taskDescription;
	float _priority;
	NSURLSessionTaskState _state;
	int64_t _countOfBytesReceived;
	int64_t _countOfBytesExpectedToReceive;
	NSMutableData *_receivedData;
	void (^_completionHandler)(NSData *, NSURLResponse *, NSError *);
	id _session;		/* UNRETAINED: the session retains ITS TASKS, so retaining it here would
					 * make a cycle out of an ownership that already runs one way. */
}

/* FNX: THE SESSION'S OWN DOOR, and the reason -init is not public here: a task is created BY a session.
 * This library's convention for a first-party door on a public class is the `fn` prefix (see
 * +[NSURLProtocol fnProtocolClassForRequest:]). */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request identifier:(NSUInteger)identifier;

/* AND THE COMPLETION-HANDLER FORM, which is what makes a task do something without a delegate: the block
 * is COPIED, because a task outlives the call that made it. */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request
		       identifier:(NSUInteger)identifier
		completionHandler:(nullable void (^)(NSData *data,
						     NSURLResponse *response,
						     NSError *error))completionHandler;

/* FNX: THE SESSION'S SIDE OF A TASK, in one group, because they are one conversation and splitting them
 * across files without a header to hold them would be worse than naming them here.
 *
 * -fnSetSession: LINKS the two without owning: a task asks its session to run it (that is what -resume
 * does when a session is present), and the session reports back through the four doors below. APPLE HAS
 * NO PUBLIC COUNTERPART for any of these — its loader owns the whole conversation — and this library's
 * convention for a first-party door on a public class is the `fn` prefix (see
 * +[NSURLProtocol fnProtocolClassForRequest:]). */
- (void)fnSetSession:(nullable id)session;
- (void)fnProtocolDidReceiveResponse:(NSURLResponse *)response;
- (void)fnProtocolDidLoadData:(NSData *)data;
- (void)fnProtocolDidFinishWithError:(nullable NSError *)error;

/* UNIQUE WITHIN THE SESSION THAT MADE IT, and assigned in creation order — which is what makes it usable
 * as a key, the only thing Apple promises about it. */
@property (readonly) NSUInteger taskIdentifier;
@property (nullable, readonly, copy) NSURLRequest *originalRequest;
@property (nullable, readonly, copy) NSURLRequest *currentRequest;
@property (nullable, readonly, copy) NSURLResponse *response;
@property (nullable, readonly, copy) NSError *error;
/* A caller's own label, readwrite and never interpreted here. */
@property (nullable, copy) NSString *taskDescription;
/* Apple's range is 0.0–1.0 with a default of 0.5; CARRIED, with nothing scheduling on it yet. */
@property float priority;
@property (readonly) NSURLSessionTaskState state;
/* 0 until a transfer runs: there is no transfer in this row, and these say so by staying zero. */
@property (readonly) int64_t countOfBytesReceived;
@property (readonly) int64_t countOfBytesExpectedToReceive;

- (void)resume;
- (void)suspend;
- (void)cancel;

@end

/* THE DATA TASK IS THE ONE THAT CARRIES A REQUEST, which is the only thing it adds here. */
@interface NSURLSessionDataTask : NSURLSessionTask

@end

/* THE UPLOAD TASK ADDS NOTHING TO THE DATA TASK BUT ITS MEANING - Apple's hierarchy rather than this tree's
 * convenience: NSURLSessionUploadTask IS an NSURLSessionDataTask, its completion handler has the data task's
 * own signature, and the answer arrives the same way. What differs is only how the REQUEST was made, which
 * is why the class is one line and the factories do the work. */
@interface NSURLSessionUploadTask : NSURLSessionDataTask

@end

/* THE DOWNLOAD TASK'S ONE NEW SURFACE IS THE DESTINATION: the transfer is the same one every task runs, and
 * what differs is that the body is WRITTEN SOMEWHERE and the caller is handed that location rather than the
 * bytes. The file goes in NSTemporaryDirectory() - the FSH's answer, asked for rather than named here - and
 * a caller is expected to MOVE it, exactly as Apple's contract says, because the directory is temporary.
 *
 * ITS HANDLER TAKES A LOCATION WHERE THE DATA TASK'S TAKES DATA, which is why it is the task's own ivar
 * rather than the base's: one ivar cannot be two signatures, and a subclass that silently reused the base's
 * would hand a caller an NSURL where it expected an NSData. */
@interface NSURLSessionDownloadTask : NSURLSessionTask
{
	NSURL *_location;
	void (^_downloadHandler)(NSURL *, NSURLResponse *, NSError *);
}

/* FNX: the session's own doors, in the group NSURLSessionTask documents. */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request
		       identifier:(NSUInteger)identifier
		  downloadHandler:(nullable void (^)(NSURL *location,
						     NSURLResponse *response,
						     NSError *error))handler;
- (void)fnSetDownloadHandler:(nullable void (^)(NSURL *location,
						NSURLResponse *response,
						NSError *error))handler;

/* non-nil once the body has been written; the caller moves the file. */
@property (nullable, readonly, copy) NSURL *location;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSIONTASK_H */
