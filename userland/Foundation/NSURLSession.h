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

@class NSArray;
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

@interface NSURLSession : NSObject
{
	NSURLSessionConfiguration *_configuration;
	id <NSURLSessionDelegate> _delegate;
	NSOperationQueue *_delegateQueue;
	NSString *_sessionDescription;
	NSUInteger _nextTaskIdentifier;
	NSMutableArray *_tasks;
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

/* THE DATA FACTORIES. The completion-handler forms are refused with the other execution doors: they
 * promise a transfer this row does not perform. */
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url;

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
