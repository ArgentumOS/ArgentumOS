/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSession — THE THING A CALLER ACTUALLY USES, as a MODEL in this row of slice 2c.
 * docs/design/foundation-plan.md W7; the transport plan's §4, slice 2c.
 *
 * IT CREATES TASKS AND IT OWNS THEM. That is the whole of this row: a session is made from a
 * configuration (which it SNAPSHOTS — see the copy rule below), it hands out tasks with unique
 * identifiers, it keeps them, and it can be invalidated. NOTHING TRANSFERS YET; running a task through
 * `FNCURLURLProtocol` is the next row, and the header says so instead of leaving a caller to find out.
 *
 * THE CONFIGURATION IS COPIED, AND THIS IS THE RULE WORTH STATING: a session takes a SNAPSHOT of the
 * configuration it was made from, so a caller who keeps editing that configuration does not reach into a
 * session that is already running. Apple documents exactly this, and it is the same snapshot discipline
 * the request headers and the cached response keep — `session-snapshots-its-configuration` pins it.
 *
 * `+sharedSession` IS A SINGLETON AND IS BUILT FROM THE DEFAULT CONFIGURATION, with no delegate and no
 * delegate queue; the probe pins the identity, because "shared" that answers a new object each time would
 * be a lie.
 *
 * REFUSED BY NAME, each because the class or the row behind it is not shipped:
 *   * the AUTHENTICATION member of the delegate protocol and Apple's two challenge factories — they take
 *     `NSURLAuthenticationChallenge`, its own family and its own ledger rows;
 *   * the DOWNLOAD, UPLOAD, STREAM and WEBSOCKET task classes and their factories: each is its own ledger
 *     row (`NSURLSessionDownloadTask`, `NSURLSessionUploadTask`, `NSURLSessionStreamTask`,
 *     `NSURLSessionWebSocketTask`), and they are not part of the session-and-data half this row ships;
 *   * `-getTasksWithCompletionHandler:`'s upload and download arrays are therefore ALWAYS EMPTY here,
 *     which is stated rather than left as a surprise;
 *   * the coder doors, as everywhere in this library.
 */

#ifndef FOUNDATION_NSURLSESSION_H
#define FOUNDATION_NSURLSESSION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLSessionConfiguration.h>
#import <Foundation/NSURLProtocol.h>

@class NSArray;
@class NSData;
@class NSMutableArray;
@class NSError;
@class NSOperationQueue;
@class NSString;
@class NSURL;
@class NSURLRequest;

NS_ASSUME_NONNULL_BEGIN

@class NSURLSession;

/* HOW A SESSION'S OWNER IS TOLD THINGS ABOUT THE SESSION rather than about one task. EVERY MEMBER IS
 * OPTIONAL, as Apple declares them, so an implementation answers what it cares about and a session asks
 * `-respondsToSelector:` before each door (the same rule the keyed-archiver delegates keep). */
@protocol NSURLSessionDelegate <NSObject>

@optional

/* THE SESSION IS DONE and cannot be used again. */
- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(nullable NSError *)error;

/* A background session finished handing over its events. Carried: this library has no background
 * machinery yet, so nothing calls it — stated rather than implied. */
- (void)URLSessionDidFinishEventsForBackgroundURLSession:(NSURLSession *)session;

@end

/* HOW A SESSION'S OWNER IS TOLD ABOUT ONE TASK. EVERY MEMBER IS OPTIONAL, as Apple declares them, so a
 * delegate answers what it cares about and the session asks -respondsToSelector: before each door.
 *
 * AND THE DATA DELEGATE IS A TASK DELEGATE, which is Apple's hierarchy rather than a convenience: a data
 * delegate can be handed the task's ending too, so `NSURLSessionDataDelegate` inherits from
 * `NSURLSessionTaskDelegate` and a class that implements only the data door still receives it.
 *
 * THE TWO CALLBACKS THAT ARE HERE, and the ones that are NOT:
 *   -URLSession:task:didCompleteWithError:  THE ENDING, and there is exactly one per task - an error is
 *       nil on success, and it is called for both outcomes, which is why a completion handler and a
 *       delegate can both be told the same thing without either being special;
 *   -URLSession:dataTask:didReceiveData:    THE BODY, in as many calls as the transport hands over - the
 *       same rule the seam's own client keeps, because accumulating here would be a second buffer
 *       alongside the task's.
 *
 * REFUSED BY NAME, each because the TYPE or the ROW behind it is not shipped:
 *   * the AUTHENTICATION members (-URLSession:didReceiveChallenge:completionHandler: and its task/data
 *     forms) - NSURLAuthenticationChallenge is its own family and its own ledger rows;
 *   * the RESPONSE DECISION (-URLSession:dataTask:didReceiveResponse:completionHandler:) - it takes an
 *     NSURLSessionResponseDisposition, whose BecomeDownload/BecomeStream cases belong to the download and
 *     stream rows, so shipping it here would ship half an enum;
 *   * the UPLOAD/DOWNLOAD/STREAM members, whose classes are their own ledger rows.
 *
 * AND v1 DELIVERS THESE ON THE TRANSFER'S OWN THREAD. Apple's contract is the session's delegateQueue,
 * and this row carries the queue without using it yet: a delegate must therefore not assume the main
 * thread. Stated here because a caller reading only the signatures would assume otherwise. */
/* AND IT INHERITS THE SESSION'S PROTOCOL, which is Apple's chain rather than a convenience:
 * NSURLSessionDataDelegate < NSURLSessionTaskDelegate < NSURLSessionDelegate. Stopping at the first link
 * left a data delegate NON-CONFORMANT to `id <NSURLSessionDelegate>`, the type the session's three-argument
 * factory takes - and it compiled anyway, because a class pointer passed to a protocol-qualified parameter
 * is a warning here rather than an error. */
@protocol NSURLSessionTaskDelegate <NSURLSessionDelegate>

@optional

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(nullable NSError *)error;

@end

@protocol NSURLSessionDataDelegate <NSURLSessionTaskDelegate>

@optional

- (void)URLSession:(NSURLSession *)session
	 dataTask:(NSURLSessionDataTask *)dataTask
   didReceiveData:(NSData *)data;

@end

@interface NSURLSession : NSObject
{
	NSURLSessionConfiguration *_configuration;
	id <NSURLSessionDelegate> _delegate;
	NSOperationQueue *_delegateQueue;
	NSString *_sessionDescription;
	NSUInteger _nextTaskIdentifier;
	NSMutableArray *_tasks;
	NSMutableArray *_protocols;	/* the transfers in flight, kept alive and released at
						 * their ending, which is what breaks the client cycle */
	BOOL _invalid;
}

/* THE ONE EVERYBODY USES, built from the default configuration with no delegate. */
+ (NSURLSession *)sharedSession;
+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration;
+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration
				  delegate:(nullable id <NSURLSessionDelegate>)delegate
			      delegateQueue:(nullable NSOperationQueue *)queue;

/* THE SNAPSHOT of the configuration the session was made from (not the caller's object). */
@property (readonly, copy) NSURLSessionConfiguration *configuration;
@property (nullable, readonly, retain) id <NSURLSessionDelegate> delegate;
@property (nullable, readonly, retain) NSOperationQueue *delegateQueue;
/* A caller's own label, readwrite and never interpreted here. */
@property (nullable, copy) NSString *sessionDescription;

/* THE DATA FACTORIES, and the COMPLETION-HANDLER FORMS SHIP NOW: they were refused while nothing could
 * run a task, and this row is what runs one. A completion-handler task is the same task; the block is
 * simply a second place its ending is reported. */
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url;
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request
			    completionHandler:(void (^)(NSData *data,
							NSURLResponse *response,
							NSError *error))completionHandler;
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url
			completionHandler:(void (^)(NSData *data,
						    NSURLResponse *response,
						    NSError *error))completionHandler;

/* FNX: THE DOOR A TASK USES TO ASK ITS SESSION TO RUN IT. -resume on a task with a session arrives here
 * (see NSURLSessionTask.h's group of doors); a task WITHOUT one only changes state, which is what the
 * model's own probe exercises. */
- (void)fnTaskDidResume:(NSURLSessionTask *)task;

/* THE TASKS THIS SESSION HAS MADE, grouped the way Apple groups them. The upload and download arrays are
 * ALWAYS EMPTY here, because those classes are not shipped — see the header above. */
- (void)getTasksWithCompletionHandler:(void (^)(NSArray *dataTasks,
						NSArray *uploadTasks,
						NSArray *downloadTasks))completionHandler;

/* CANCELS EVERY TASK AND INVALIDATES THE SESSION; the other lets them finish first. After either, a NEW
 * task is refused: `-dataTaskWithRequest:` answers nil rather than handing back a task that could never
 * run. (Apple's documentation says only that a session must not be used after invalidating; nil is this
 * library's answer, and the probe pins it.) */
- (void)invalidateAndCancel;
- (void)finishTasksAndInvalidate;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSION_H */
