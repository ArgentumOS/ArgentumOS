/*
 * NSURLSessionStreamTask.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A DUPLEX CONNECTION AS A TASK: the class that owns a connected socket and serves an ORDERED queue of reads,
 * writes, half-closes and TLS operations over it. docs/design/foundation-plan.md §57 (why this half of the
 * last W7 family lands first) and §58 (the surface and the semantics below, both measured from Apple's pages).
 *
 * `NSURLSessionStreamTask` IS AN `NSURLSessionTask`, which is where its identifier, its state machine, its
 * ending, its error and its metrics record come from: a stream task is a task that happens to be neither a
 * request nor a response. It is created by a session (`-streamTaskWithHostName:port:`) and started by
 * `-resume`, like every other task.
 *
 * THE SEMANTICS ARE APPLE'S PUBLISHED ONES, QUOTED WHERE THEY ARE LOAD-BEARING:
 *
 *   * `-readDataOfMinLength:maxLength:timeout:completionHandler:` - `minLength` is "the minimum number of
 *     bytes to read", `maxLength` "the maximum", and THE TIMEOUT IS A CANCEL: "if the read is not completed
 *     within the specified interval, the read is canceled and the completion handler is called with an
 *     error. Pass 0 to prevent a read from timing out";
 *   * `-writeData:timeout:completionHandler:` - the same timeout rule, and a promise that is deliberately
 *     WEAK: "there is no guarantee that the remote side of the stream has received all of the written bytes
 *     at the time that [the handler] is called, only that all of the data has been written to the kernel".
 *     THAT IS WHAT `-closeWrite` EXISTS FOR, and why the only honest evidence a half-close happened is the FAR
 *     END's end-of-file;
 *   * BOTH HANDLERS RUN ON THE SESSION'S DELEGATE QUEUE (and inline when the session has no queue);
 *   * AND EVERY OTHER DOOR IS A QUEUED OPERATION: "completes any enqueued reads and writes, and then" closes
 *     the read side, closes the write side, or starts/stops TLS. THE QUEUE IS THE MODEL rather than an
 *     implementation detail - it is what makes `-closeWrite` mean "after what I already asked for".
 *
 * WHAT THIS ROW DOES NOT DO, NAMED RATHER THAN LEFT LOOKING FUNCTIONAL:
 *
 *   * **`-captureStreams` IS REFUSED BY NAME.** It hands the caller a PAIR of `NSStream`s that ADOPT the
 *     socket's descriptor, and this library's `NSStream` only ever OPENS its own (its descriptor path runs the
 *     other way: the stream opens the file or the connection and then lends its descriptor to the run loop).
 *     A captured pair would therefore be two streams that cannot read. The door is declared, answers with the
 *     documented ending rather than pretending, and the work it needs is a first-party "adopt this descriptor"
 *     door on `NSInputStream`/`NSOutputStream`;
 *   * **`-URLSession:betterRouteDiscoveredForStreamTask:` CANNOT FIRE HERE**: it exists for a multipath route,
 *     and this system has one network. It is declared on the protocol with that ground, in the same way the
 *     session's background-session members are declared and unraisable;
 *   * **CERTIFICATES ARE NOT VERIFIED.** `-startSecureConnection` performs a real TLS handshake through
 *     libssl - which this library already links - but there is no trust store and no certificate stack in this
 *     tree, so the peer is accepted. That is §11.6's "necessary for function on Argentum" ground, and it is
 *     said here rather than discovered by a caller who assumed otherwise.
 */

#ifndef FOUNDATION_NSURLSESSIONSTREAMTASK_H
#define FOUNDATION_NSURLSESSIONSTREAMTASK_H

#import <Foundation/NSObject.h>
#import <Foundation/NSURLSessionTask.h>
/* THE SESSION'S HEADER, because the delegate protocol below INHERITS `NSURLSessionTaskDelegate` (Apple's
 * chain, and the reason that protocol is declared HERE rather than in the session's header: the stream's
 * states are the task's). NO CYCLE: the session's header forward-declares this class, which is what a
 * forward declaration is for. */
#import <Foundation/NSURLSession.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
/* THE FOUR TYPES ITS OWN DECLARATIONS USE, imported rather than assumed (§50.3's rule): the queue and its
 * condition are the class's private state, and the two instants the metrics record is made of are dates. */
#import <Foundation/NSArray.h>		/* NSMutableArray */
#import <Foundation/NSLock.h>		/* NSCondition lives HERE, not in a header of its own */
#import <Foundation/NSDate.h>		/* NSTimeInterval, and the two instants */

@class NSInputStream;
@class NSOutputStream;
@class NSURLSession;

NS_ASSUME_NONNULL_BEGIN

/* HOW A STREAM TASK'S OWNER IS TOLD WHAT THE CONNECTION IS DOING. It IS a task delegate - Apple's chain, and
 * the reason it is declared here: the stream's states are the task's, and a delegate that implements only the
 * task doors keeps working. EVERY MEMBER IS OPTIONAL, as Apple declares them. */
@protocol NSURLSessionStreamDelegate <NSURLSessionTaskDelegate>

@optional

/* THE READ SIDE IS CLOSED - because the caller closed it, or because the far end did. */
- (void)URLSession:(NSURLSession *)session
readClosedForStreamTask:(NSURLSessionStreamTask *)streamTask;

/* THE WRITE SIDE IS CLOSED. */
- (void)URLSession:(NSURLSession *)session
writeClosedForStreamTask:(NSURLSessionStreamTask *)streamTask;

/* A BETTER ROUTE EXISTS. DECLARED AND UNRAISABLE ON THIS SYSTEM: it exists for a multipath route, and this
 * machine has one network - said here rather than left looking functional. */
- (void)URLSession:(NSURLSession *)session
betterRouteDiscoveredForStreamTask:(NSURLSessionStreamTask *)streamTask;

/* THE TASK HAS HANDED ITS TRANSPORT OVER, after the queue drained (§58): from here the caller drives the
 * streams. NOT RAISED BY THIS ROW, because the door that asks for it - `-captureStreams` - is refused: see
 * the header above. */
- (void)URLSession:(NSURLSession *)session
	      streamTask:(NSURLSessionStreamTask *)streamTask
didBecomeInputStream:(NSInputStream *)inputStream
      outputStream:(NSOutputStream *)outputStream;

@end

@interface NSURLSessionStreamTask : NSURLSessionTask
{
	/* THE CONNECTION. `_fd` is owned by the worker thread from the moment it connects until it tears the
	 * flight down, which is why -cancel only SHUTS IT DOWN: the worker is the one that closes it. */
	NSString *_host;
	NSInteger _port;
	int _fd;
	volatile int _stopped;

	/* THE QUEUE, AND THE CONDITION THE WORKER WAITS ON. The array is the model: reads, writes and the
	 * queue-ordered doors are appended in the caller's order and served in that order. */
	NSMutableArray *_operations;
	NSCondition *_queue;

	/* WHAT IS LEFT OF THE CONNECTION. The task ends when both halves are closed - or on a cancel, or on a
	 * TLS failure - and each half reports once. */
	BOOL _readClosed;
	BOOL _writeClosed;
	BOOL _started;

	/* THE TWO INSTANTS THE RECORD IS MADE OF: when the worker started, and when the connection came up. A
	 * stream task's metrics are one transaction with no request and no response, and these are what fill it
	 * (§52's record, delivered before the ending by §52's rule - see -fnEndWithError:). */
	NSDate *_startedAt;
	NSDate *_connectedAt;

	/* TLS, when -startSecureConnection has been served: one context for the process, one session per task. */
	void *_sslContext;
	void *_ssl;
}

/* --- READING AND WRITING. Both are ASYNCHRONOUS and both handlers run on the session's delegate queue;
 * both take a timeout in seconds, and a NON-POSITIVE ONE MEANS "never" rather than "immediately", which is
 * the documented meaning of the parameter ("pass 0 to prevent a read from timing out"). */
- (void)readDataOfMinLength:(NSUInteger)minBytes
		  maxLength:(NSUInteger)maxBytes
		    timeout:(NSTimeInterval)timeout
	  completionHandler:(void (^)(NSData * _Nullable data,
				      BOOL atEOF,
				      NSError * _Nullable error))completionHandler;

- (void)writeData:(NSData *)data
	  timeout:(NSTimeInterval)timeout
completionHandler:(void (^ _Nullable)(NSError * _Nullable error))completionHandler;

/* --- THE QUEUE-ORDERED DOORS. Each one "completes any enqueued reads and writes, and then" does its thing,
 * in Apple's own words - so a close is not "now", it is "after what I already asked for". */
- (void)captureStreams;		/* REFUSED: see the header above - it answers with the ending, not a pair */
- (void)closeRead;		/* the read side of the underlying socket */
- (void)closeWrite;		/* the write side */
- (void)startSecureConnection;	/* a real TLS handshake through libssl, unverified - see the header above */
- (void)stopSecureConnection;

/* FNX: THE SESSION'S OWN DOOR, and the reason there is no public `-init` here: a stream task is created BY a
 * session, the same way every other task is (see NSURLSessionTask.h's fn group). */
- (instancetype)fnInitWithHostName:(NSString *)hostName
			      port:(NSInteger)port
			identifier:(NSUInteger)identifier;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSIONSTREAMTASK_H */
