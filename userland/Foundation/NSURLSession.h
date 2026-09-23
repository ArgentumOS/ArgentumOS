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

@class NSURLAuthenticationChallenge;
@class NSURLCredential;
/* HOW A SESSION'S OWNER IS TOLD THINGS ABOUT THE SESSION rather than about one task. EVERY MEMBER IS
 * OPTIONAL, as Apple declares them, so an implementation answers what it cares about and a session asks
 * `-respondsToSelector:` before each door (the same rule the keyed-archiver delegates keep). */
/* WHAT A DELEGATE DECIDES WHEN A SERVER ASKS FOR CREDENTIALS, AND IT IS NOT DECLARED HERE ANY MORE (§50.3):
 * the enum lives in NSURLProtocol.h now, beside the client door that is its first user. It moved because
 * THIS header imports that one — declaring the door with the type here would have been a cycle, and
 * importing this header into that one would have put the enum's declaration on the wrong side of a guard.
 * The type is still reachable from here through the import this header already had, so nothing that
 * includes NSURLSession.h has to change.
 *
 * The one thing NOT in the enum is a case meaning "give me the credential": the delegate hands one over
 * THROUGH the completion handler, which is Apple's shape and this class's. */

@protocol NSURLSessionDelegate <NSObject>

@optional

/* THE SESSION IS DONE and cannot be used again. */
- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(nullable NSError *)error;

/* THE SERVER ASKED FOR CREDENTIALS, and the SESSION-level door is the fallback: the task-level door below
 * is the more specific one and is asked first (§48's resolution order, which the loop's own probe pins).
 *
 * DECLARED HERE BY §53, AND UNTIL THEN IT WAS DECLARED NOWHERE - the same defect §50.3 found on the client
 * side, one protocol over: the session has dispatched to this selector through an `(id)` cast since §48, and
 * Objective-C permits that whether or not a header declares it, so the loop passed its checks while a caller
 * could not read the contract and the compiler could not check a call to it. */
- (void)URLSession:(NSURLSession *)session
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
			     NSURLCredential * _Nullable credential))completionHandler;

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
 *   * the UPLOAD/DOWNLOAD/STREAM members, whose classes are their own ledger rows.
 *
 * AND TWO THINGS HAVE LEFT THIS LIST, EACH RECORDED WHERE IT WAS REMOVED because a refusal is a fact about the
 * TREE and a landing has to revisit it:
 *   * THE RESPONSE DECISION - its enum was never blocked, an enum case being a value rather than a class
 *     reference, and the door below holds the body until the delegate answers (the sixth time this session a
 *     refusal expired instead of being deleted);
 *   * THE TWO AUTHENTICATION CHALLENGE DOORS (§53) - they were refused "because NSURLAuthenticationChallenge
 *     is its own family", a reason that expired when §48 shipped that family, and the session had been
 *     disPATCHING to both selectors through an `(id)` cast ever since: declared nowhere, called anyway, with
 *     two `-Wobjc-method-access` warnings on every build as the only sign. They are declared on the two
 *     delegate protocols now, and `foundation_challengedoor` asserts the DECLARATIONS.
 *
 * WHERE THESE ARRIVE: on the session's DELEGATE QUEUE when it has one, and on the TRANSFER'S OWN THREAD
 * when it does not - so a delegate must not assume the main thread either way. AND THE QUEUE IS EXPECTED
 * TO BE SERIAL, exactly as Apple requires: a CONCURRENT delegate queue may run the callbacks in any order,
 * which would let a delegate see the ending before the body it was supposed to follow. */
/* AND IT INHERITS THE SESSION'S PROTOCOL, which is Apple's chain rather than a convenience:
 * NSURLSessionDataDelegate < NSURLSessionTaskDelegate < NSURLSessionDelegate. Stopping at the first link
 * left a data delegate NON-CONFORMANT to `id <NSURLSessionDelegate>`, the type the session's three-argument
 * factory takes - and it compiled anyway, because a class pointer passed to a protocol-qualified parameter
 * is a warning here rather than an error. */
/* WHAT A DELEGATE DECIDES WHEN THE HEAD OF AN ANSWER ARRIVES, and it is a VALUE TYPE SHIPPED BEFORE THE DOOR
 * THAT TAKES IT - the same order this unit took for the request and the response, and for the same reason:
 * the shape of the decision can be pinned without the machinery that acts on it.
 *
 * THE VALUES ARE OURS UNDER §11.6.1 D2, as every enum's in this library are: Apple publishes the CASE NAMES
 * and the case names only, and `response-disposition-values` pins the numbers.
 *
 * TWO OF THE FOUR ARE DECLARED AND NOT YET HONOURED, and the header says so rather than implying otherwise:
 * `BecomeDownload` and `BecomeStream` name conversions - turning a running data task into one of the
 * sibling task kinds - and the conversion is work that belongs with the sibling receiving it. `Allow` and
 * `Cancel` are the two the session can act on today. */
typedef NS_ENUM(NSInteger, NSURLSessionResponseDisposition) {
	NSURLSessionResponseAllow = 0,
	NSURLSessionResponseCancel = 1,
	NSURLSessionResponseBecomeDownload = 2,
	NSURLSessionResponseBecomeStream = 3,
};

/* THE TASK'S OWN RECORD OF WHAT IT COST, and the door Apple declares for it. THE TYPE IS FORWARD DECLARED
 * HERE because the protocol only passes a pointer: a delegate that reads the record includes Foundation.h,
 * where the metrics header sits. */
@class NSURLSessionTaskMetrics;
/* And the redirect door's response parameter (§54): the protocol hands the delegate the 3xx AS an HTTP
 * response, so that type is named here and declared in its own header. */
@class NSHTTPURLResponse;

@protocol NSURLSessionTaskDelegate <NSURLSessionDelegate>

@optional

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(nullable NSError *)error;

/* THE SERVER ASKED FOR CREDENTIALS, AND THIS IS THE DOOR THE SESSION ASKS FIRST (§48's resolution order: the
 * task-level delegate is the more specific one and the session's own door is the fallback).
 *
 * DECLARED HERE BY §53, AND UNTIL THEN IT WAS DECLARED NOWHERE: the session has dispatched to this selector
 * through an `(id)` cast since §48, which compiles whether or not a header declares it - so the loop passed
 * its checks while a caller could not read the contract and the compiler could not check a call to it. The
 * build's two `-Wobjc-method-access` warnings were exactly that, in the toolchain's own words. */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
			     NSURLCredential * _Nullable credential))completionHandler;

/* WHAT THE TASK COST, AND IT ARRIVES BEFORE THE ENDING ABOVE - Apple's order, and the reason the delivery is
 * a separate call rather than a parameter of it: a delegate that wants the numbers has them before it
 * decides what the outcome meant.
 *
 * THE RECORD IS THE LOADING SYSTEM'S (§52): the PROTOCOL measured its transaction and handed it up through
 * its own first-party door, and the SESSION assembled the task's record around it - its span, its redirect
 * count, and its transactions in order. A task with no transaction to report still gets this call with an
 * EMPTY LIST rather than no call at all: the door's contract is its PLACE in the sequence, not a promise that
 * something was measured. */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics;

/* THE ANSWER NAMED SOMEWHERE ELSE, AND WHETHER TO GO THERE (§54). `request` is the NEXT request the loading
 * system PROPOSES - a proposal, not a decision: RFC 9110's method rules are already applied to it (301, 302
 * and 303 become GET with no body; 307 and 308 keep both), and the caller's headers travel with it.
 *
 * THE ANSWER IS A REQUEST OR NOTHING, and that difference is Apple's contract rather than this library's:
 *   * A REQUEST is what the task will run next - the SAME task, with `currentRequest` moved and
 *     `originalRequest` left alone;
 *   * NIL MEANS DO NOT FOLLOW, AND IT IS NOT A FAILURE: the task finishes with the 3xx it received. No body
 *     arrives with it, because the transfer was stopped at the head of the answer - stated rather than left
 *     to be discovered;
 *   * A DELEGATE THAT IMPLEMENTS NO DOOR IS NOT ASKED, and the redirect IS followed - bounded by a hop limit
 *     this library documents as its own, because Apple publishes none. */
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
willPerformHTTPRedirection:(NSHTTPURLResponse *)response
	newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest * _Nullable))completionHandler;

@end

@protocol NSURLSessionDataDelegate <NSURLSessionTaskDelegate>

@optional

- (void)URLSession:(NSURLSession *)session
	 dataTask:(NSURLSessionDataTask *)dataTask
   didReceiveData:(NSData *)data;

/* THE DECISION DOOR, AND IT IS WHERE THE BODY WAITS FOR THE ANSWER. The session holds the transfer at the
 * head of the answer until the handler is called - Apple's contract, and the seam makes it literal, because
 * the transfer's callbacks run SYNCHRONOUSLY on the bridge's own thread. So:
 *
 *   * A DELEGATE THAT DOES NOT IMPLEMENT THIS DOOR IS NOT ASKED AND IS NOT WAITED FOR: the body flows, and
 *     Allow is the default because it is also zero;
 *   * THE HANDLER MUST BE CALLED EXACTLY ONCE, which is Apple's rule and the reason the wait can be a wait;
 *   * Cancel STOPS THE TRANSFER through the task's own -cancel;
 *   * BecomeDownload AND BecomeStream ARE HONOURED AS Allow AND ARE NOT CONVERSIONS YET: turning a running
 *     data task into one of the sibling kinds is work that belongs with the sibling receiving it, and the
 *     header says so rather than pretending. */
- (void)URLSession:(NSURLSession *)session
	 dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler;

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
/* THE UPLOAD DOORS. `fromData:` SUPPLIES THE BODY AND THE FACTORY PUTS IT ON THE REQUEST: NSURLRequest is
 * immutable, so the factory takes a mutable copy and sets the body on that - Apple's behaviour, and the
 * reason a caller does not set the body itself. THE REQUEST'S METHOD IS THE CALLER'S (a body with GET is odd
 * but legal), so nothing here decides POST for them. */
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromData:(NSData *)bodyData;
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request
					 fromData:(NSData *)bodyData
				completionHandler:(void (^)(NSData *data,
							    NSURLResponse *response,
							    NSError *error))completionHandler;

- (void)fnTaskDidResume:(NSURLSessionTask *)task;

/* THE INTERNAL CHALLENGE DOOR, for the transport to call when a server asks: it resolves the delegate (task
 * door first, then session) and answers SYNCHRONOUSLY, because the transport waits for the credential the
 * way it waits at the head of a response. Not an FNX door for the public surface - an internal one, so named
 * with the fn prefix this library uses for those. */
- (void)fnAskForCredentialForTask:(NSURLSessionTask *)task
			challenge:(NSURLAuthenticationChallenge *)challenge
		completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
					    NSURLCredential * _Nullable credential))completionHandler;

/* THE DOWNLOAD DOORS. The completion-handler form takes a LOCATION rather than bytes - that is the whole
 * difference from the data task, and it is why a download handler and a data handler cannot share a
 * signature. As with the data task, the plain form links the task to its session and the handler form is
 * the same task with a handler attached. */
- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request
				    completionHandler:(void (^)(NSURL *location,
								NSURLResponse *response,
								NSError *error))completionHandler;

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
