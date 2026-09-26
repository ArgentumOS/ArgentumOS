/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLConnection — THE OLDER WAY TO PERFORMS AN EXCHANGE, LANDED BECAUSE THE DEPRECATION GROUND WAS
 * RETIRED. docs/design/foundation-plan.md §62.24 (the user's policy, 2026-09-26): "to support porting
 * older Mac applications, all items removed for being deprecated are un-deprecated in Argentum
 * Foundation, and added to the work list." THIS CLASS IS THE FIRST PAYMENT ON THAT LIST: Apple
 * deprecated the whole NSURLConnection family when NSURLSession arrived, and an older application that
 * is being ported very often calls it — including `+sendSynchronousRequest:returningResponse:error:`,
 * which is what that kind of code uses to fetch one thing.
 *
 * IT IS A FACADE, NOT A TRANSPORT, AND THAT IS DELIBERATE. The loading system already exists (§46
 * NSURLRequest/NSURLResponse, §52 the protocol, §53 the session, the bridge to libcurl): this class
 * runs an `NSURLSessionDataTask` and TRANSLATES the session's callbacks into the connection's delegate
 * protocol. Writing a second byte mover would have meant two places where a transfer's semantics live,
 * and the second one would be the one nobody tested.
 *
 * WHAT IT THEREFORE INHERITS, AND ONE BY ONE:
 *   * STREAMING IS REAL. The connection owns a PRIVATE `NSURLSession` with itself as its delegate, so
 *     the session's `-URLSession:dataTask:didReceiveData:` — which fires per chunk — becomes
 *     `-connection:didReceiveData:` per chunk. The completion-handler form of a data task cannot do
 *     this (it hands over the whole body at the end), which is why the private session exists at all.
 *   * THE REDIRECT DOOR MAPS EXACTLY, not approximately: `-connection:willSendRequest:
 *     redirectResponse:` RETURNS the request to run next and `nil` means "do not follow", which is
 *     literally the session's own completion-handler contract (`willPerformHTTPRedirection:newRequest:
 *     completionHandler:` takes `NSURLRequest *` and its `nil` is "do not follow"). No rule is
 *     re-invented here.
 *
 * THE TWO DEVIATIONS, AND BOTH ARE NECESSARY RATHER THAN CHOSEN (§11.6's register):
 *
 *   (1) THE DELEGATE IS CALLED ON THE LOADING SYSTEM'S THREAD - NOT ON "the thread whose run loop you
 *       started the connection on", which is Apple's contract for this class. THE GROUND IS MEASURED:
 *       `NSRunLoop.h` names its own absence — "run-loop sources and observers, `-performSelector:…`
 *       and the block [forms]" are not shipped — so there is NO door to hand work to a run loop from
 *       another thread, which is exactly what Apple's delivery contract is built on. `-setDelegateQueue:`
 *       IS honoured (the session already dispatches through a delegate queue when one is set), so a
 *       caller that needs a specific thread has a way to name it; a caller that needs Apple's exact
 *       rule does not, and the header says so instead of pretending.
 *   (2) `-scheduleInRunLoop:forMode:` AND `-unscheduleFromRunLoop:` ARE REFUSED BY NAME, for the same
 *       measured reason: they exist to direct the delivery above, and there is nothing to direct.
 *       DECLARING THEM AS NO-OPS WOULD BE THE WORSE OPTION — a caller would believe it had arranged
 *       delivery it had not.
 *
 * REFUSED BY NAME, WITH GROUNDS (each is asserted absent in the probe's `excluded` array, and the
 * grounds are the register's kind (ii), "a dependency this system does not have"):
 *   * THE AUTHENTICATION DOORS — `-connection:willSendRequestForAuthenticationChallenge:`,
 *     `-connection:didReceiveAuthenticationChallenge:`, `-connection:didCancelAuthenticationChallenge:`,
 *     `-connection:canAuthenticateAgainstProtectionSpace:` and `-connectionShouldUseCredentialStorage:`.
 *     A CHALLENGE CANNOT BE ANSWERED FROM A DELEGATE HERE: `NSURLAuthenticationChallenge` ships no
 *     `-sender` (its own header records that decision under §48.1), and `-sender` is what Apple's
 *     delegate protocol is built on — `[challenge.sender useCredential:forAuthenticationChallenge:]` is
 *     the delegate's half of the exchange. The session answers challenges through its OWN
 *     completion-handler door, which a connection delegate cannot reach. NAMED CONSEQUENCE: `-sender`
 *     is itself an owed row now (§62.24 retired the "Legacy" strike its header cites), and landing it is
 *     what would make these five doors implementable — that is the work, not this comment.
 *   * `-connection:needNewBodyStream:` — the transport does not consume `NSURLRequest`'s
 *     `HTTPBodyStream`, so there is no body to ask for again on a redirect.
 *   * `-connection:didSendBodyData:totalBytesWritten:totalBytesExpectedToWrite:` — upload progress. The
 *     session uploads from DATA (`-uploadTaskWithRequest:fromData:`), so the bytes are handed over
 *     before the transfer starts and there is no progressive upload to report.
 *   * `-connection:willCacheResponse:` — the session offers no cache-decision door for a connection to
 *     translate, so a delegate could not be asked.
 *
 * AND THE DOWNLOAD HALF IS NAMED AS THE NEXT SLICE RATHER THAN HALF-DONE: `NSURLConnectionDownloadDelegate`
 * and the download path over `NSURLSessionDownloadTask` are slice 2. The ledger tracks that protocol as
 * its OWN row, so it stays `open` while this class and its two data-side protocols go `shipped` — a
 * slice boundary the ledger can express, which is why it is drawn here.
 */

#ifndef FOUNDATION_NSURLCONNECTION_H
#define FOUNDATION_NSURLCONNECTION_H

#import <Foundation/NSObject.h>

@class NSData;
@class NSError;
@class NSOperationQueue;
@class NSURLRequest;
@class NSURLResponse;
@class NSURLSession;
@class NSURLSessionDataTask;

NS_ASSUME_NONNULL_BEGIN

@class NSURLConnection;

/* THE BASE PROTOCOL. Apple's contract: every member is optional, a connection with no delegate reports
 * to nobody, and this protocol is what the failure door belongs to (the data-side doors are the
 * subclass below it). */
@protocol NSURLConnectionDelegate <NSObject>

@optional

/* THE TRANSFER FAILED. Called at most once, and never after `-connectionDidFinishLoading:`. A CANCEL IS
 * NOT A FAILURE (see `-cancel`), so a cancelled connection does not reach this door. */
- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error;

@end

/* THE DATA-SIDE PROTOCOL: what an exchange that RECEIVES bytes is told. */
@protocol NSURLConnectionDataDelegate <NSURLConnectionDelegate>

@optional

/* SOMEWHERE ELSE, AND WHETHER TO GO THERE. THE RETURN VALUE IS THE DECISION: the request to run next, or
 * `nil` for "do not follow" — in which case the transfer finishes with the 3xx it just received, and
 * `-connection:didReceiveResponse:` delivers that response. A delegate that implements no such door is
 * not asked and the redirect IS followed. */
- (nullable NSURLRequest *)connection:(NSURLConnection *)connection
		       willSendRequest:(NSURLRequest *)request
		    redirectResponse:(nullable NSURLResponse *)response;

/* THE ANSWER'S HEAD. Delivered once per response, BEFORE any `-connection:didReceiveData:` for it, and
 * again after a followed redirect (once per response the connection sees). */
- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)response;

/* A CHUNK OF THE BODY. Called zero or more times, in order, between the response and the ending. */
- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data;

/* THE ENDING THAT SUCCEEDED. Called once, after the last chunk. */
- (void)connectionDidFinishLoading:(NSURLConnection *)connection;

@end

/*
 * THE CONNECTION. Its behaviour is in NSURLConnection.m; what a caller needs to know is the shape:
 * A CONNECTION IS STARTED EXPLICITLY — creating it with `startImmediately:NO` gives the caller the
 * chance to `-setDelegateQueue:` first, which is the only reason that initializer exists.
 */
@interface NSURLConnection : NSObject
{
	NSURLRequest *_request;		/* what the caller asked for: `originalRequest` */
	NSURLRequest *_currentRequest;	/* moved by a followed redirect; see the property */
	id _delegate;			/* UNRETAINED, as Apple's is: the delegate owns the connection */
	NSURLSession *_session;		/* the private session that runs this connection's task */
	NSURLSessionDataTask *_task;
	NSOperationQueue *_delegateQueue;
	BOOL _started;
	BOOL _finished;
	BOOL _cancelled;
}

/* THE COMMON PAIR. `+connectionWithRequest:delegate:` starts immediately; the designated initializer
 * does not, if the caller says so. BOTH MAY RETURN NIL: a connection needs a request with a URL. */
+ (nullable NSURLConnection *)connectionWithRequest:(NSURLRequest *)request
					   delegate:(nullable id)delegate;
- (nullable instancetype)initWithRequest:(NSURLRequest *)request
				delegate:(nullable id)delegate
			startImmediately:(BOOL)startImmediately;
- (nullable instancetype)initWithRequest:(NSURLRequest *)request
				delegate:(nullable id)delegate;

/* WHETHER THE LOADING SYSTEM CAN RUN THIS REQUEST. THE ANSWER IS ABOUT WHAT IS REGISTERED: this library
 * ships an EMPTY NSURLProtocol registry (its own rule is Apple's, "+registerClass: before starting any
 * URL loading"), so a FRESH PROCESS answers NO even for `http` until a transport is registered - a
 * deviation from Apple, whose built-in protocols are always there, and one a caller meets here rather
 * than discovers. The alternative - a hardcoded list of schemes in this class - would be a second copy of
 * knowledge the registry already holds, and would answer YES for a scheme nothing can actually run. */
+ (BOOL)canHandleRequest:(NSURLRequest *)request;

/* THE SYNCHRONOUS FORM, AND IT BLOCKS THE CALLING THREAD until the transfer ends — that is its whole
 * contract, and the reason it is deprecated in favour of a session's block-based task. `response` and
 * `error` are OUT-PARAMETERS and may be `NULL`; the data is nil on failure, and a FAILURE IS REPORTED
 * ONLY THROUGH `error`, never as an exception. It uses the SHARED session with a completion-handler
 * task: there is no delegate to hand events to, so streaming has no meaning here. */
+ (nullable NSData *)sendSynchronousRequest:(NSURLRequest *)request
			  returningResponse:(NSURLResponse *_Nullable *_Nullable)response
				      error:(NSError *_Nullable *_Nullable)error;

/* START IT. Idempotent: a connection already started (or finished, or cancelled) ignores this. */
- (void)start;

/* STOP IT. NO FAILURE CALL FOLLOWS — a cancel is not an error, and Apple's delegate contract for this
 * class is silent after one. Idempotent, and safe before `-start`. */
- (void)cancel;

/* WHERE THE DELEGATE IS CALLED. `nil` means the loading system's own thread (deviation (1) in the
 * header above); a serial queue means that queue. Only meaningful before `-start`. */
- (void)setDelegateQueue:(nullable NSOperationQueue *)queue;

/* WHAT THE CALLER ASKED FOR, and what the connection is running NOW — the two differ after a followed
 * redirect, which is the whole reason both exist (§54's rule, inherited through the task). */
@property (nullable, readonly, copy) NSURLRequest *originalRequest;
@property (nullable, readonly, copy) NSURLRequest *currentRequest;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLCONNECTION_H */
