/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLConnection — THE OLDER WAY TO PERFORM AN EXCHANGE, LANDED BECAUSE THE DEPRECATION GROUND WAS
 * RETIRED. docs/design/foundation-plan.md §62.24 (the user's policy, 2026-09-26): "to support porting
 * older Mac applications, all items removed for being deprecated are un-deprecated in Argentum
 * Foundation, and added to the work list." THIS CLASS IS THE FIRST PAYMENT ON THAT LIST: Apple
 * deprecated the whole NSURLConnection family when NSURLSession arrived, and an older application that
 * is being ported very often calls it — including `+sendSynchronousRequest:returningResponse:error:`,
 * which is what that kind of code uses to fetch one thing.
 *
 * ⚠⚠ IT DRIVES `NSURLProtocol`, WHICH IS WHAT A 10.2 CONNECTION DID (§63.145, the user's decision
 * dec-148b4598987d58c5). The loading system already exists (§46 NSURLRequest/NSURLResponse, §52 the
 * protocol, and the bridge to libcurl behind it): this class finds the protocol class that claims the
 * request, creates one instance WITH ITSELF AS THE CLIENT, calls `-startLoading`, and TRANSLATES the
 * client doors into this class's delegate protocol. Writing a second byte mover would have meant two
 * places where a transfer's semantics live, and the second one would be the one nobody tested.
 *
 * AND THE CHANGE IS NOT COSMETIC, WHICH IS WHY THIS COMMENT OPENS WITH IT: the class was 566 lines of
 * `NSURLSessionDataDelegate` — a 10.2 class built on a class seven years newer, and therefore on the one
 * family the 10.2 surface cut removes. THE ORIGINAL DESIGN IS ALSO THE CHEAPER ONE: 10.2's connection ran
 * on `NSURLProtocol`, and this one does too.
 *
 * WHAT IT THEREFORE INHERITS, AND ONE BY ONE:
 *   * STREAMING IS REAL. The bridge reports `-URLProtocol:didLoadData:` once per chunk curl hands over,
 *     and that becomes `-connection:didReceiveData:` once per chunk. A completion-handler shape could
 *     not do this (it hands over the whole body at the end), which is why the doors are the client's.
 *   * THE REDIRECT DOOR MAPS, and it needs one sentence because THE SEAM REPORTS RATHER THAN DECIDES:
 *     `-URLProtocol:wasRedirectedToRequest:redirectResponse:` is a NOTIFICATION (the bridge stops at the
 *     3xx by design), so the connection asks its delegate and, if the delegate answers a request, ISSUES
 *     THE NEXT TRANSFER ITSELF. Apple's contract is unchanged — the delegate's value is the request to
 *     run next and `nil` means "do not follow" — but the following here is this class's act.
 *
 * THE TWO DEVIATIONS, AND BOTH ARE NECESSARY RATHER THAN CHOSEN (§11.6's register):
 *
 *   (1) THE DELEGATE IS CALLED ON THE TRANSPORT'S THREAD - NOT ON "the thread whose run loop you
 *       started the connection on", which is Apple's contract for this class. THE GROUND IS MEASURED:
 *       `NSRunLoop.h` names its own absence — "run-loop sources and observers, `-performSelector:…`
 *       and the block [forms]" are not shipped — so there is NO door to hand work to a run loop from
 *       another thread, which is exactly what Apple's delivery contract is built on. `-setDelegateQueue:`
 *       IS honoured and is the way a caller names a thread: when a queue is set, every delegate call goes
 *       through `-addOperationWithBlock:`, and a SERIAL queue preserves the order the transfer reported in.
 *       A caller that needs Apple's exact rule does not have it, and this header says so instead of
 *       pretending.
 *   (2) `-scheduleInRunLoop:forMode:` AND `-unscheduleFromRunLoop:` ARE REFUSED BY NAME, for the same
 *       measured reason: they exist to direct the delivery above, and there is nothing to direct.
 *       DECLARING THEM AS NO-OPS WOULD BE THE WORSE OPTION — a caller would believe it had arranged
 *       delivery it had not.
 *
 * AUTHENTICATION, WITH APPLE'S OWN PRECEDENCE WRITTEN OUT: the MODERN door
 * (`-connection:willSendRequestForAuthenticationChallenge:`) SUPERSEDES the deprecated pair, so a delegate
 * that implements it is the only one asked; otherwise `-connection:canAuthenticateAgainstProtectionSpace:`
 * is asked FIRST as a gate (a `NO` means "do not authenticate", and the transfer continues without
 * credentials), and then `-connection:didReceiveAuthenticationChallenge:`. A DELEGATE THAT IMPLEMENTS ONE OF
 * THESE MUST ANSWER, THROUGH THE CHALLENGE'S OWN SENDER — `[challenge.sender useCredential:…]` — because a
 * connection's delegate has no completion handler to answer with, and the transport is BLOCKED until the
 * challenge is answered. THAT LAST FACT IS NOW TRUE OF THIS SEAM RATHER THAN OF A SESSION: the bridge asks
 * its client through `-URLProtocol:didReceiveAuthenticationChallenge:completionHandler:` and waits, which
 * is §48.6's registered deviation for that door.
 *
 * AND TWO OF THE FIVE AUTHENTICATION DOORS ARE STILL REFUSED, WITH GROUNDS THAT REMAIN MEASURED:
 *   * `-connectionShouldUseCredentialStorage:` — THE LOADING SYSTEM CONSULTS NO CREDENTIAL STORE, so there is
 *     nothing for a delegate to permit or forbid: the transport's challenge carries a nil proposed credential
 *     and the store (`NSURLCredentialStorage`) is the caller's business, not the transfer's.
 *   * `-connection:didCancelAuthenticationChallenge:` — ONE CHALLENGE AT A TIME, ANSWERED SYNCHRONOUSLY: the
 *     transport waits for an answer, so a live challenge is never superseded by the connection and there is no
 *     cancellation for it to report. (A delegate's OWN cancel is its own act, and it answers with it.)
 *
 * THE DOWNLOAD HALF IS KEPT, WHICH IS WHY THE PROTOCOL BELOW IS DECLARED: a delegate that implements
 * `-connectionDidFinishDownloading:destinationURL:` downloads — this class writes the body to a file in
 * `NSTemporaryDirectory()` and the caller moves it, exactly as Apple's contract says — and that delegate is
 * NOT fed by the data doors. THE DISPATCH RULE IS STATED BECAUSE APPLE PUBLISHES THE DOOR AND NOT THE
 * DISPATCH: asked by SELECTOR, and the reason is a measurement this tree already records (foundation_url.m):
 * a protocol's metadata exists only when something in the process ADOPTS it, so a conformance test would
 * make the dispatch depend on a linker detail.
 *
 * AND ONE REFUSAL'S GROUND IS STRUCTURAL RATHER THAN INHERITED:
 * `-connectionDidResumeDownloading:totalBytesWritten:expectedTotalBytes:`. **AN `NSURLConnection` CANNOT BE
 * GIVEN RESUME DATA.** The class has no initializer that takes it — Apple never added one, and resume is the
 * newer API's story — so a connection can only ever start from the beginning, and a door that announces a
 * resume would be announcing something that cannot happen.
 *
 * THE REGISTER'S ONE WORK ITEM, NAMED WHERE A CALLER MEETS IT RATHER THAN QUIETLY KEPT: the download
 * protocol declares `-connection:willCacheResponse:` AND THE SEAM CANNOT CARRY IT. `NSURLProtocolClient`
 * offers `-URLProtocol:cachedResponseIsValid:`, which is a NOTIFICATION ("the cached answer I was handed is
 * still good") and not a QUESTION ("what should I store?"), so there is no door to ask a delegate through
 * and no transfer for an answer to change. It stays declared because porting source compiles against it and
 * because Apple's connection answers it; it is recorded here as a work item (§11.3) for whoever gives this
 * seam a cache-decision door. `refused-doors-are-absent` does not speak about it — that check is about the
 * DATA protocol, where the door is absent, and it stays absent.
 */

#ifndef FOUNDATION_NSURLCONNECTION_H
#define FOUNDATION_NSURLCONNECTION_H

#import <Foundation/NSObject.h>

@class NSData;
@class NSMutableData;
@class NSError;
@class NSOperationQueue;
@class NSInputStream;
@class NSCachedURLResponse;
@class NSURLAuthenticationChallenge;
@class NSURL;
@class NSURLProtectionSpace;
@class NSURLProtocol;
@class NSURLRequest;
@class NSURLResponse;

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

/* THE SERVER IS ASKING, AND THIS IS THE MODERN DOOR: it SUPERSEDES the two deprecated ones below, so a
 * delegate that implements it is the only one asked. ANSWER IT THROUGH THE CHALLENGE'S SENDER -
 * `[challenge.sender useCredential:credential forAuthenticationChallenge:challenge]` - because the transport
 * is blocked until the challenge is answered and this door has no completion handler to answer with. */
- (void)connection:(NSURLConnection *)connection
willSendRequestForAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

/* AND THE DEPRECATED PAIR, WHICH §62.24's POLICY PUT BACK IN SCOPE RATHER THAN LEFT AS HISTORY: an older
 * application's delegate implements THESE, so they are answered here with Apple's own precedence above.
 * `-connection:canAuthenticateAgainstProtectionSpace:` is the GATE - `NO` means do not authenticate, and the
 * transfer continues without credentials - and `-connection:didReceiveAuthenticationChallenge:` is the
 * challenge itself, answered the same way the modern door is. */
- (BOOL)connection:(NSURLConnection *)connection
canAuthenticateAgainstProtectionSpace:(NSURLProtectionSpace *)protectionSpace;
- (void)connection:(NSURLConnection *)connection
didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;

@end

/* THE DATA-SIDE PROTOCOL: what an exchange that RECEIVES bytes is told. WITH THE CONNECTION DRIVING A
 * PROTOCOL, EVERY DOOR LIVES HERE: the redirect is a QUESTION (the seam reports the 3xx and this class runs
 * the next transfer), the response and the chunks are notifications, and the ending is one of two. */
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

/* THE DOWNLOAD PROTOCOL: a delegate that wants the body WRITTEN SOMEWHERE rather than handed to it as
 * bytes. The destination is a file this class creates in `NSTemporaryDirectory()` — the caller is
 * expected to MOVE it, exactly as Apple's contract says, because the directory is temporary — and the
 * connection hands it over at the one door below.
 *
 * ITS BODY-STREAM AND PROGRESS DOORS ARE THE ONES THE SEAM CAN HONESTLY CARRY, and the cache-decision door
 * is the register's one work item (see the file's comment above): it is the only door here with no door on
 * the other side of the seam. */
@protocol NSURLConnectionDownloadDelegate <NSURLConnectionDelegate>

@optional

/* THE RE-SEND'S BODY, ASKED OF THE CLIENT FOR THE ONE ATTEMPT NO OTHER DOOR CAN REACH (§62.36): the
 * transport re-issues a 401 itself, so this class never sees the second attempt, and the bridge asks its
 * client through the first-party door this method answers. Apple's contract for the replacement is "a new,
 * UNOPENED stream". */
- (nullable NSInputStream *)connection:(NSURLConnection *)connection
	     needNewBodyStream:(NSURLRequest *)request;

- (nullable NSCachedURLResponse *)connection:(NSURLConnection *)connection
			  willCacheResponse:(NSCachedURLResponse *)cachedResponse;

/* THE UPLOAD'S PROGRESS (§62.32), WHICH THE TRANSPORT COUNTS AS THE BYTES LEAVE: libcurl's own progress
 * callback fires for every transfer and reports both directions, and the numbers travel through the seam's
 * first-party door. Apple's connection door spells them NSInteger, so that is the spelling here, and
 * `bytesWritten` is what left since the last report rather than the running total beside it. */
- (void)connection:(NSURLConnection *)connection
  didSendBodyData:(NSInteger)bytesWritten
totalBytesWritten:(NSInteger)totalBytesWritten
totalBytesExpectedToWrite:(NSInteger)totalBytesExpectedToWrite;

/* THE DOWNLOAD'S PROGRESS, AND IT IS THIS CLASS'S OWN COUNT RATHER THAN A TRANSLATION: the seam reports
 * neither progress nor the response's expected length, so these are the bytes written to the file so far.
 * `expectedTotalBytes` is 0 when the response published no length, which is Apple's own way of saying the
 * same thing (§62.29). */
- (void)connection:(NSURLConnection *)connection
	 didWriteData:(long long)bytesWritten
    totalBytesWritten:(long long)totalBytesWritten
    expectedTotalBytes:(long long)expectedTotalBytes;

/* THE BODY IS ON DISK AT `destinationURL`, AND A CALLER MUST MOVE IT BEFORE RETURNING: the file lives in
 * `NSTemporaryDirectory()` and nothing here keeps it alive afterwards.
 *
 * IT IS THE ONLY DOOR A DOWNLOAD DELEGATE HEARS. The data doors (`-connection:didReceiveResponse:`,
 * `-connection:didReceiveData:`, `-connectionDidFinishLoading:`) are NOT sent to it even when it
 * implements them, and it is NOT asked about a redirect - this protocol declares no such door, so the
 * redirect is followed. Both are stated because the transport delivers its per-chunk doors for every
 * transfer it runs; the exclusion is this class's, and it is asserted. */
- (void)connectionDidFinishDownloading:(NSURLConnection *)connection destinationURL:(NSURL *)destinationURL;

@end

/*
 * THE CONNECTION. Its behaviour is in NSURLConnection.m; what a caller needs to know is the shape:
 * A CONNECTION IS STARTED EXPLICITLY — creating it with `startImmediately:NO` gives the caller the
 * chance to `-setDelegateQueue:` first, which is the only reason that initializer exists.
 */
@interface NSURLConnection : NSObject
{
	NSURLRequest *_request;		/* what the caller asked for, and what a transfer starts from:
					 * `originalRequest` */
	NSURLRequest *_currentRequest;	/* moved by a followed redirect; see the property */
	id _delegate;			/* UNRETAINED, as Apple's is: the delegate owns the connection */
	NSURLProtocol *_protocol;	/* THE TRANSPORT INSTANCE this connection drives, retained; nil before
					 * `-start` and after the ending. IT RETAINS THIS OBJECT AS ITS CLIENT
					 * (§52's `-client` is a strong property), so the pair is a cycle and the
					 * ENDING is where it is broken. */
	NSOperationQueue *_delegateQueue;
	/* THE DOWNLOAD PATH'S STATE, non-empty only while a DOWNLOAD DELEGATE is being served: the body is
	 * accumulated because the seam hands over chunks and the door takes a FILE, and the path is made
	 * from NSTemporaryDirectory() at the moment the first chunk arrives. `_downloadExpected` is what the
	 * response published (0 or negative means the server never named a length), which is the same thing
	 * the progress door reports as `expectedTotalBytes`. */
	NSMutableData *_downloadData;
	NSString *_downloadPath;
	long long _downloadExpected;
	unsigned long long _downloadBytes;	/* what has gone into `_downloadData` so far, which is the
						 * running total the progress door reports */
	/* THE SYNCHRONOUS FORM'S WAIT, on the CALLER'S stack for as long as that call is blocked:
	 * non-NULL only while `+sendSynchronousRequest:…` waits, and the ending is what signals it. A
	 * raw pointer rather than an object because the struct's lifetime is the call's, not this
	 * object's - and the caller outlives the signal by construction. */
	void *_syncState;
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

/* WHETHER THE LOADING SYSTEM CAN RUN THIS REQUEST. THE ANSWER IS ABOUT WHAT IS REGISTERED, and the registry is
 * filled before a caller is reached: this library registers its own transport (FNCURLURLProtocol - `file`, `http`,
 * `https`) AT LOAD, so a fresh process answers YES for those schemes out of the box, as Apple's does, whose
 * built-in protocols are always there. It shipped an EMPTY registry until §62.83 and required every caller to
 * fill it, which turned a forgotten `+registerClass:` into NSURLErrorUnsupportedURL - a verdict about a URL that
 * was perfectly fine.
 *
 * THE REGISTRY STAYS THE AUTHORITY AND THE SEAM STAYS OPEN: this class asks the registry through
 * `+fnProtocolClassForRequest:` (the door §52 shipped for exactly this step), a caller's later
 * `+registerClass:` deliberately outranks the one made at load (the walk resolves most-recently-registered
 * first, NSURLProtocol.h), a caller that wants no transport can still `-unregisterClass:`, and a scheme no
 * registered class claims still answers NO - which is what keeps this from being a hardcoded list of schemes. */
+ (BOOL)canHandleRequest:(NSURLRequest *)request;

/* THE SYNCHRONOUS FORM, AND IT BLOCKS THE CALLING THREAD until the transfer ends — that is its whole
 * contract, and the reason it is deprecated in favour of a session's block-based task. `response` and
 * `error` are OUT-PARAMETERS and may be `NULL`; the data is nil on failure, and a FAILURE IS REPORTED
 * ONLY THROUGH `error`, never as an exception. It runs an ORDINARY CONNECTION WITH NO DELEGATE and waits
 * on a mutex and a condition variable that the connection's ending signals — there is no delegate to hand
 * events to, so streaming has no meaning here and the bytes are accumulated. */
+ (nullable NSData *)sendSynchronousRequest:(NSURLRequest *)request
			  returningResponse:(NSURLResponse *_Nullable *_Nullable)response
				      error:(NSError *_Nullable *_Nullable)error;

/* START IT. Idempotent: a connection already started (or finished, or cancelled) ignores this. */
- (void)start;

/* STOP IT. NO FAILURE CALL FOLLOWS — a cancel is not an error, and Apple's delegate contract for this
 * class is silent after one. Idempotent, and safe before `-start`. */
- (void)cancel;

/* WHERE THE DELEGATE IS CALLED. `nil` means the transport's own thread (deviation (1) in the header above);
 * a serial queue means that queue, and the order the transfer reported in is preserved. Only meaningful
 * before `-start`. */
- (void)setDelegateQueue:(nullable NSOperationQueue *)queue;

/* WHAT THE CALLER ASKED FOR, and what the connection is running NOW — the two differ after a followed
 * redirect, which is the whole reason both exist. */
@property (nullable, readonly, copy) NSURLRequest *originalRequest;
@property (nullable, readonly, copy) NSURLRequest *currentRequest;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLCONNECTION_H */
