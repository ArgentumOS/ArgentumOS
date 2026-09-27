/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlsession_task, unit of 1 — W7 slice 2c's session half, row 3 (the first half): THE
 * EXECUTION, through the COMPLETION-HANDLER path. docs/design/foundation-plan.md W7; the transport
 * plan's §4, slice 2c.
 *
 * A TASK NOW RUNS: `-resume` on a task that has a session asks it to do the work — the session picks a
 * protocol class (the configuration's own list first, then slice 2a's registry), starts it, and reports
 * the ending back into the task's state and, here, into a completion handler. The DELEGATE callbacks are
 * the other half of this row and land with the task/data delegate protocols.
 *
 * THE FETCHES ARE file://, as the bridge's probe's are: deterministic, no server, no network, and still a
 * real transfer through the same code path.
 *
 * THE CHECK THAT EARNS ITS PLACE is `task-with-no-protocol-class-fails-rather-than-hanging`: a request
 * nothing claims must END, with NSURLErrorUnsupportedURL (-1002) and its completion handler called. A
 * seam that answers "no protocol handles this" by never reporting is the worst failure mode of a loading
 * system, and it is the one a happy path cannot see.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLSESSION-TASK %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLSESSION-TASK %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

#define FIXTURE_PATH	"/System/Temporary Files/fn_session_fixture.txt"
static const char *const fixture_bytes = "a session moved these bytes\n";

static NSURL *fn_url(NSString *string)
{
	return [NSURL URLWithString:string];
}

/* AND THE FILE DOOR, for the same reason: +fileURLWithPath: is NULLABLE by contract, and a file-scope
 * function's own pointer types carry no annotation, so the probe's own fixture path needs no conversion
 * spelled out at each call site. */
static NSURL *fn_file_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static int fn_write_fixture(void)
{
	FILE *file = fopen(FIXTURE_PATH, "wb");

	if (file == NULL) {
		return 0;
	}
	fwrite(fixture_bytes, 1, strlen(fixture_bytes), file);
	fclose(file);
	return 1;
}


/* THE DELEGATE, and it records what it was told rather than asserting inside the callbacks: the callbacks
 * arrive on the transfer's thread, so the probe reads the record after the ending - which is also what
 * makes "exactly one ending" a thing that can be counted. */
@interface FnSessionDelegate : NSObject <NSURLSessionDataDelegate>
{
	NSMutableData *_dataBytes;
	int _dataCalls;
	int _endings;
	NSError *_lastError;
	id _lastSession;
	id _lastTask;
	id _callbackQueue;
	NSInteger _disposition;
	int _responseAsks;
}
- (NSData *)dataBytes;
- (int)dataCalls;
- (int)endings;
- (NSError *)lastError;
- (id)lastSession;
- (id)lastTask;
- (id)callbackQueue;
- (void)setDisposition:(NSInteger)disposition;
- (int)responseAsks;
@end

@implementation FnSessionDelegate

- (id)init
{
	self = [super init];
	if (self != nil) {
		_dataBytes = [[NSMutableData alloc] init];
	}
	return self;
}

/* THE DECISION DOOR: answers with what the probe configured - Allow (zero) unless told otherwise, so every
 * other check in this file keeps flowing. */
- (void)URLSession:(NSURLSession *)session
	 dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
	_responseAsks++;
	completionHandler((NSURLSessionResponseDisposition)_disposition);
	(void)response;
}

- (void)URLSession:(NSURLSession *)session
	 dataTask:(NSURLSessionDataTask *)dataTask
   didReceiveData:(NSData *)data
{
	_dataCalls++;
	[_dataBytes appendData:data];
	_lastSession = session;
	_lastTask = dataTask;
	_callbackQueue = [NSOperationQueue currentQueue];
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	_endings++;
	_lastError = error;
	_lastSession = session;
	_lastTask = task;
	_callbackQueue = [NSOperationQueue currentQueue];
}

- (NSData *)dataBytes { return _dataBytes; }
- (int)dataCalls { return _dataCalls; }
- (int)endings { return _endings; }
- (NSError *)lastError { return _lastError; }
- (id)lastSession { return _lastSession; }
- (id)lastTask { return _lastTask; }
- (id)callbackQueue { return _callbackQueue; }
- (void)setDisposition:(NSInteger)disposition { _disposition = disposition; }
- (int)responseAsks { return _responseAsks; }

@end


/* THE UPLOAD-PROGRESS DELEGATE (§62.32), AND IT IS ITS OWN CLASS BECAUSE THE LEG NEEDED A DELEGATE AT ALL: the
 * upload leg below used to create its session with NO delegate, so there was nobody for a task-delegate door to
 * reach. This one does nothing but record, which is what makes the check that reads it meaningful: the numbers
 * are read, not assumed. */
@interface FnUploadProgressDelegate : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int sendCalls;
	int64_t lastBytesSent;
	int64_t lastTotalSent;
	int64_t lastExpected;
}

@end

@implementation FnUploadProgressDelegate

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
   didSendBodyData:(int64_t)bytesSent
    totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{
	(void)session;
	(void)task;
	sendCalls++;
	lastBytesSent = bytesSent;
	lastTotalSent = totalBytesSent;
	lastExpected = totalBytesExpectedToSend;
}

@end

/* THE RE-SEND'S BODY (§62.35): a stream is consumed by being sent, so a redirected POST has to be given a NEW
 * one. This delegate hands over a fresh stream over the same bytes and counts the ask, which is what makes the
 * check a measurement rather than a hope. */
@interface FnBodyStreamDelegate : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int asks;
	NSData *bytes;
}

@end

@implementation FnBodyStreamDelegate

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
  needNewBodyStream:(void (^)(NSInputStream *))completionHandler
{
	(void)session;
	(void)task;
	asks++;
	completionHandler([NSInputStream inputStreamWithData:bytes]);
}

@end

/* A STREAM THAT FAILS TO READ (§62.36), which is what makes the error-versus-end distinction measurable: it
 * answers -1 from -read:maxLength:, exactly what this library's own NSInputStream answers for a stream that was
 * never opened. WITHOUT THE FIX the transfer SUCCEEDS with an empty body (no Content-Length means chunked, and
 * an immediate end is a legitimate empty chunked body); with it, the transfer FAILS. */
@interface FnFailingStream : NSInputStream
@end

@implementation FnFailingStream

- (NSInteger)read:(uint8_t *)buffer maxLength:(NSUInteger)len
{
	(void)buffer;
	(void)len;
	return -1;
}

@end

int main(void)
{
	/* UNBUFFERED, AND IT IS NOT A PREFERENCE: a probe that CRASHES loses everything printf put in a
	 * block-buffered stream, so the markers that exist to say where it died are exactly the output that
	 * disappears. Measured here: the first diagnostic run showed no marker at all and read as "it died
	 * before the first statement", which was false — the crash was simply later than the lost buffer. */
	setvbuf(stdout, NULL, _IONBF, 0);
	/* THE PROBE IS A SERVER AND IT WRITES TO SOCKETS THE CLIENT MAY HAVE CLOSED, so SIGPIPE must not be allowed
	 * to kill it: an aborted upload (§62.36) closes the connection before the answer is written, and the default
	 * action for that write is to TERMINATE THE PROCESS - which is how this leg's first green run turned into a
	 * dead probe printing nothing. A latent hazard for every socket-writing probe here, and named rather than
	 * worked around. */
	signal(SIGPIPE, SIG_IGN);
	printf("FOUNDATION-URLSESSION-TASK-DIAG 0) alive after setvbuf\n");
	printf("FOUNDATION-URLSESSION-TASK-DIAG 0b) NSURLSession class = %s\n",
		[NSURLSession class] != Nil ? "present" : "MISSING");
	printf("FOUNDATION-URLSESSION-TASK-DIAG a) before the session\n");
	/* THE BRIDGE MUST BE REGISTERED, EXACTLY AS A REAL CLIENT WOULD REGISTER ONE. A session consults the
	 * configuration's protocolClasses and then slice 2a's registry, and NOTHING is in either unless the
	 * process puts it there - so an unregistered bridge means every fetch ends with
	 * NSURLErrorUnsupportedURL. (That is what this file's own no-protocol-class check pins, and it is why
	 * the first run of this probe reported a handler that fired with zero bytes.) */
	[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
	NSURLSession *session = [NSURLSession sessionWithConfiguration:
					[NSURLSessionConfiguration defaultSessionConfiguration]];

	printf("FOUNDATION-URLSESSION-TASK-DIAG b) session made, before the fixture\n");
	if (!fn_write_fixture()) {
		check("task-resume-runs-the-transfer", 0, @"the fixture could not be written");
		printf("FOUNDATION-URLSESSION-TASK RESULT ok=%d fail=%d\n", okc, failc);
		printf("FOUNDATION-URLSESSION-TASK-STATUS=1\n");
		printf("FOUNDATION-URLSESSION-TASK DONE\n");
		return 1;
	}

	/* --- A TASK RUNS, AND ITS COMPLETION HANDLER IS WHAT SAYS SO ---------------------------------- */
	{
		__block BOOL called = NO;
		__block NSData *body = nil;
		__block NSURLResponse *response = nil;
		__block NSError *error = nil;
		NSURLSessionDataTask *task;
		int waited = 0;

		printf("FOUNDATION-URLSESSION-TASK-DIAG c) fixture written, before the task\n");
		task = [session dataTaskWithRequest:[NSURLRequest requestWithURL:
						fn_file_url(@FIXTURE_PATH)]
			    completionHandler:^(NSData *data, NSURLResponse *theResponse, NSError *theError) {
			called = YES;
			body = data;
			response = theResponse;
			error = theError;
		}];

		printf("FOUNDATION-URLSESSION-TASK-DIAG d) task made (id %d), before the first check\n",
			(int)[task taskIdentifier]);
		check("completion-handler-factory-answers-a-task",
		      task != nil && [task taskIdentifier] != 0 &&
		      [task state] == NSURLSessionTaskStateSuspended,
		      @"the completion-handler factory makes the same kind of task, suspended");

		printf("FOUNDATION-URLSESSION-TASK-DIAG e) resumed\n");
		[task resume];
		while (!called && waited < 100) {	/* 10 s */
			usleep(100000);
			waited++;
		}

		check("task-resume-runs-the-transfer",
		      called &&
		      body != nil &&
		      [body length] == strlen(fixture_bytes) &&
		      memcmp([body bytes], fixture_bytes, strlen(fixture_bytes)) == 0,
		      [NSString stringWithFormat:@"the handler was %@ and the body %d bytes (expected %d)",
			 called ? @"called" : @"never called",
			 (int)(body != nil ? [body length] : 0), (int)strlen(fixture_bytes)]);

		check("completion-handler-receives-response-and-data",
		      called && response != nil && error == nil &&
		      [response expectedContentLength] == (long long)strlen(fixture_bytes),
		      @"the handler gets the response, the bytes and no error");

		check("task-state-becomes-completed-and-counts-bytes",
		      [task state] == NSURLSessionTaskStateCompleted &&
		      [task error] == nil &&
		      [task response] != nil &&
		      [task countOfBytesReceived] == (int64_t)strlen(fixture_bytes),
		      @"the task's own state, response and byte count follow the transfer");
	}

	/* --- THE URL FORM RUNS TOO -------------------------------------------------------------------- */
	{
		__block BOOL called = NO;
		__block NSUInteger length = 0;
		NSURLSessionDataTask *task =
			[session dataTaskWithURL:fn_file_url(@FIXTURE_PATH)
		       completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)response;
			(void)error;
			called = YES;
			length = [data length];
		}];
		int waited = 0;

		[task resume];
		while (!called && waited < 100) {
			usleep(100000);
			waited++;
		}
		check("data-task-url-form-runs",
		      called && length == strlen(fixture_bytes),
		      @"-dataTaskWithURL:completionHandler: runs the same transfer");
	}

	/* --- A REQUEST NOTHING CLAIMS MUST END, NOT HANG ---------------------------------------------- */
	{
		__block BOOL called = NO;
		__block NSError *error = nil;
		__block NSData *body = nil;
		NSURLSessionDataTask *task =
			[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"ftp://nowhere/x")]
			    completionHandler:^(NSData *data, NSURLResponse *response, NSError *theError) {
			(void)response;
			called = YES;
			body = data;
			error = theError;
		}];
		int waited = 0;

		[task resume];
		while (!called && waited < 100) {
			usleep(100000);
			waited++;
		}
		check("task-with-no-protocol-class-fails-rather-than-hanging",
		      called && error != nil && [error code] == -1002 &&
		      [[error domain] isEqual:@"NSURLErrorDomain"] &&
		      [task state] == NSURLSessionTaskStateCompleted,
		      @"an unclaimable scheme ends with NSURLErrorUnsupportedURL and the handler is called");
	}

	/* --- CANCELLING BEFORE RESUMING MEANS THE TRANSFER NEVER STARTS ------------------------------- */
	{
		__block BOOL called = NO;
		NSURLSessionDataTask *task =
			[session dataTaskWithRequest:[NSURLRequest requestWithURL:
						fn_file_url(@FIXTURE_PATH)]
			    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			(void)error;
			called = YES;
		}];

		[task cancel];
		[task resume];
		usleep(500000);
		check("cancel-before-resume-never-starts",
		      [task state] == NSURLSessionTaskStateCompleted &&
		      [task error] != nil &&
		      [[task error] code] == -999 &&
		      called == NO,
		      @"a task cancelled while suspended ends, and the transfer it never started does not run");
	}

	/* --- THE DELEGATE IS TOLD THE SAME THINGS THE TASK IS ---------------------------------------- */
	{
		FnSessionDelegate *delegate = [[FnSessionDelegate alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		NSURLSession *watched;

		/* SERIAL, BECAUSE THE ORDER OF THE CALLBACKS DEPENDS ON IT - the contract the header states. */
		[queue setMaxConcurrentOperationCount:1];
		watched = [NSURLSession sessionWithConfiguration:
					[NSURLSessionConfiguration defaultSessionConfiguration]
						      delegate:delegate
						 delegateQueue:queue];
		NSURLSessionDataTask *task;
		int waited = 0;

		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [watched dataTaskWithRequest:[NSURLRequest requestWithURL:fn_file_url(@FIXTURE_PATH)]];
		[task resume];
		while ([delegate endings] == 0 && waited < 100) {
			usleep(100000);
			waited++;
		}

		check("delegate-receives-the-body",
		      [delegate dataCalls] >= 1 &&
		      [[delegate dataBytes] length] == strlen(fixture_bytes) &&
		      memcmp([[delegate dataBytes] bytes], fixture_bytes, strlen(fixture_bytes)) == 0,
		      @"-URLSession:dataTask:didReceiveData: is called with the body the task accumulates");

		check("delegate-receives-the-ending",
		      [delegate endings] == 1 && [delegate lastError] == nil,
		      @"-URLSession:task:didCompleteWithError: is called EXACTLY once, with a nil error on success");

		check("delegate-callbacks-arrive-on-the-delegate-queue",
		      [delegate callbackQueue] == queue,
		      @"with a delegate queue, every callback is delivered ON it - which is the hop the row owed");

		check("delegate-echoes-the-session-and-its-task",
		      [delegate lastSession] == watched && [delegate lastTask] == (id)task,
		      @"the callbacks hand back the session and the very task that was resumed");
	}

	/* --- A DOWNLOAD TASK HANDS BACK A LOCATION RATHER THAN BYTES -------------------------------- */
	{
		NSURLSession *downloading = [NSURLSession sessionWithConfiguration:
							[NSURLSessionConfiguration defaultSessionConfiguration]];
		NSURLSessionDownloadTask *task;
		__block NSURL *location = nil;
		__block NSError *downloadError = nil;
		__block BOOL downloadCalled = NO;

		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [downloading downloadTaskWithRequest:[NSURLRequest requestWithURL:fn_file_url(@FIXTURE_PATH)]
					  completionHandler:^(NSURL *theLocation, NSURLResponse *response, NSError *error) {
			(void)response;
			location = theLocation;
			downloadError = error;
			downloadCalled = YES;
		}];
		[task resume];
		{
			int waited = 0;

			while (!downloadCalled && waited < 100) {
				usleep(100000);
				waited++;
			}
		}

		check("download-handler-receives-a-location",
		      downloadCalled && location != nil && downloadError == nil &&
		      [location isKindOfClass:[NSURL class]],
		      @"-downloadTaskWithRequest:completionHandler: hands back a LOCATION, not the bytes");

		/* AND THE FILE IS REAL, WHICH IS THE ASSERTION THAT MATTERS: a location naming a file nothing wrote
		 * would pass the check above and be worse than useless. */
		{
			NSData *written = location != nil
				? [NSData dataWithContentsOfFile:[location path]]
				: nil;

			check("download-writes-the-body-where-it-says",
			      written != nil &&
			      [written length] == strlen(fixture_bytes) &&
			      memcmp([written bytes], fixture_bytes, strlen(fixture_bytes)) == 0,
			      @"the location holds the body the transfer carried");
		}
	}

	{
		FnSessionDelegate *delegate = [[FnSessionDelegate alloc] init];
		NSURLSession *session = [NSURLSession sessionWithConfiguration:
						[NSURLSessionConfiguration defaultSessionConfiguration]
								      delegate:delegate
								 delegateQueue:nil];
		NSURLSessionDataTask *task;
		int waited = 0;

		[delegate setDisposition:NSURLSessionResponseCancel];
		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_file_url(@FIXTURE_PATH)]];
		[task resume];
		while ([delegate endings] == 0 && waited < 100) { usleep(100000); waited++; }

		/* THE ASSERTION THAT MATTERS IS dataCalls == 0: a cancelled response must deliver NO body, and a
		 * session that asked and then let the bytes through anyway would still report an ending - so
		 * counting endings alone would pass either way. */
		check("disposition-cancel-withholds-the-body",
		      [delegate responseAsks] == 1 && [delegate endings] == 1 && [delegate dataCalls] == 0,
		      @"a CANCELLED response delivers no body at all - the wait is what holds it");
	}
	{
		FnSessionDelegate *delegate = [[FnSessionDelegate alloc] init];
		NSURLSession *session = [NSURLSession sessionWithConfiguration:
						[NSURLSessionConfiguration defaultSessionConfiguration]
								      delegate:delegate
								 delegateQueue:nil];
		NSURLSessionDataTask *task;
		int waited = 0;

		[delegate setDisposition:NSURLSessionResponseAllow];
		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_file_url(@FIXTURE_PATH)]];
		[task resume];
		while ([delegate endings] == 0 && waited < 100) { usleep(100000); waited++; }

		check("disposition-allow-lets-the-body-through",
		      [delegate responseAsks] == 1 && [delegate dataCalls] >= 1 &&
		      [[delegate dataBytes] length] == strlen(fixture_bytes),
		      @"an ALLOWED response delivers the body and then the ending");
	}

	/* --- AN UPLOAD, AND ITS BODY AT THE FAR END -----------------------------------------------------
	 * THE PROBE IS ITS OWN RECEIVER, AND THAT IS THE POINT: a receiver TOOL cannot listen in this guest
	 * (netcat's socket() fails and toybox hands the -1 straight to setsockopt), but curl's sockets work
	 * perfectly - so the listener is made HERE, in the same process. No tool, no fork, no shell anywhere in
	 * the path. The transfer runs on a detached thread, so listen-then-resume-then-accept suffices.
	 */
	{
		unsigned char rawBytes[8] = { 0xff, 0xfe, 0x00, 0x01, 0x80, 0x7f, 0xc3, 0x28 };
		NSData *sent = [NSData dataWithBytes:rawBytes length:8];
		FnUploadProgressDelegate *progress = [[FnUploadProgressDelegate alloc] init];
		NSURLSession *session = [NSURLSession sessionWithConfiguration:
						[NSURLSessionConfiguration defaultSessionConfiguration]
							      delegate:progress
							 delegateQueue:nil];
		NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:fn_url(@"http://127.0.0.1:46467/")];
		NSURLSessionUploadTask *task;
		__block BOOL called = NO;
		struct sockaddr_in addr;
		unsigned char buf[4096];
		size_t total = 0;
		int listener, conn, one = 1, tries = 0;
		BOOL found = NO;

		[request setHTTPMethod:@"POST"];
		/* A SHORT TIMEOUT, BECAUSE THE PROBE ANSWERS NOTHING: the transfer must END, and the bridge
		 * applies the request's own interval - which is the other half of what this unit found. */
		[request setTimeoutInterval:3.0];

		listener = socket(AF_INET, SOCK_STREAM, 0);
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(46467);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		check("receiver-binds", bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0,
		      @"the probe binds its own listening socket");
		check("receiver-listens", listen(listener, 1) == 0,
		      @"the probe listens on its own socket");

		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [session uploadTaskWithRequest:request
					     fromData:sent
				    completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			(void)error;
			called = YES;
		}];
		[task resume];

		conn = accept(listener, NULL, NULL);
		check("receiver-accepts-a-connection", conn >= 0,
		      @"the task's transfer connected to the probe's own listener");
		if (conn >= 0) {
			/* UP TO A SECOND OF COLLECTING: curl may write its headers and body as separate segments. */
			fcntl(conn, F_SETFL, O_NONBLOCK);
			while (tries < 100 && total < sizeof(buf)) {
				ssize_t n = read(conn, buf + total, sizeof(buf) - total);

				if (n > 0) {
					total += (size_t)n;
				} else {
					usleep(10000);
					tries++;
				}
			}
			close(conn);
		}
		close(listener);
		while (!called) {
			usleep(10000);
		}
		check("upload-run-ends", called, @"an upload task runs to an ending through the bridge");

		{
			size_t i;

			for (i = 0; i + [sent length] <= total; i++) {
				if (memcmp(buf + i, [sent bytes], [sent length]) == 0) {
					found = YES;
					break;
				}
			}
			printf("FOUNDATION-URLSESSION-TASK-DIAG upload: received %d bytes, body %s\n",
			       (int)total, found ? "PRESENT" : "ABSENT");
			check("upload-body-arrives-byte-for-byte", found,
			      @"a body of NON-UTF-8 bytes reaches the far end - the pre-fix bridge sent nothing");
		}
		/* AND THE UPLOAD'S PROGRESS WAS REPORTED (§62.32), which §62.25 refused the CONNECTION's door for on the
		 * ground that there was nothing progressive to report. The numbers are the transfer's, so the check reads
		 * them: the last report's running total and expected total are BOTH the body's length. */
	/* --- AND A BODY THAT ARRIVES AS A STREAM IS SENT (§62.34) ---------------------------------------- */
	{
		unsigned char rawBytes[8] = { 0x02, 0x03, 0x05, 0x07, 0x0b, 0x0d, 0x11, 0x13 };
		NSData *streamed = [NSData dataWithBytes:rawBytes length:8];
		id stream = [NSInputStream inputStreamWithData:streamed];
		NSURLSessionConfiguration *configuration2 = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session2 = [NSURLSession sessionWithConfiguration:configuration2];
		NSMutableURLRequest *streamRequest = [NSMutableURLRequest requestWithURL:fn_url(@"http://127.0.0.1:46468/")];
		NSURLSessionDataTask *streamTask;
		struct sockaddr_in addr2;
		unsigned char buf2[4096];
		size_t total2 = 0;
		int listener2, conn2 = -1, one2 = 1, tries2 = 0, foundStream = 0;
		__block BOOL called2 = NO;

		[streamRequest setHTTPMethod:@"POST"];
		/* A STREAM HAS NO LENGTH ANYONE CAN ASK FOR, so a caller PUBLISHES one - which is what Apple's own
		 * contract says, and what the bridge reads to set curl's upload size. */
		[streamRequest setValue:[NSString stringWithFormat:@"%d", (int)[streamed length]]
		     forHTTPHeaderField:@"Content-Length"];
		[streamRequest setHTTPBodyStream:stream];
		[streamRequest setTimeoutInterval:3.0];

		listener2 = socket(AF_INET, SOCK_STREAM, 0);
		memset(&addr2, 0, sizeof(addr2));
		addr2.sin_family = AF_INET;
		addr2.sin_port = htons(46468);
		addr2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		setsockopt(listener2, SOL_SOCKET, SO_REUSEADDR, &one2, sizeof(one2));
		check("the-stream-leg-binds-its-own-listener",
		      bind(listener2, (struct sockaddr *)&addr2, sizeof(addr2)) == 0 &&
		      listen(listener2, 1) == 0,
		      @"this leg is its own receiver too");

		streamTask = [session2 dataTaskWithRequest:streamRequest
			       completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			(void)error;
			called2 = YES;
		}];
		[streamTask resume];
		conn2 = accept(listener2, NULL, NULL);
		if(conn2 >= 0) {
			fcntl(conn2, F_SETFL, O_NONBLOCK);
			while(tries2 < 100 && total2 < sizeof(buf2)) {
				ssize_t n = read(conn2, buf2 + total2, sizeof(buf2) - total2);

				if(n > 0) {
					total2 += (size_t)n;
				} else {
					usleep(10000);
					tries2++;
				}
			}
			close(conn2);
		}
		close(listener2);
		while(!called2) {
			usleep(10000);
		}
		{
			size_t i;

			for(i = 0; i + [streamed length] <= total2; i++) {
				if(memcmp(buf2 + i, [streamed bytes], [streamed length]) == 0) {
					foundStream = 1;
					break;
				}
			}
		}
		/* THE BODY WAS OMITTED ENTIRELY BEFORE THIS ROW: the bridge read `HTTPBody` and knew nothing about
		 * `HTTPBodyStream`, so a caller who set one sent a request with no body at all - the same shape of
		 * defect as the UTF-8 body one two rows up. */
		check("a-stream-body-is-sent", foundStream,
		      [NSString stringWithFormat:@"the receiver saw %d byte(s) and the streamed body %s",
			(int)total2, foundStream ? "PRESENT" : "ABSENT"]);
	}

		check("the-upload-progress-door-is-reported",
		      progress->sendCalls >= 1 && progress->lastBytesSent > 0 &&
		      progress->lastTotalSent == (int64_t)[sent length] &&
		      progress->lastExpected == (int64_t)[sent length],
		      [NSString stringWithFormat:@"%d report(s): last delta %lld, total %lld of %lld, body %d",
			progress->sendCalls, progress->lastBytesSent, progress->lastTotalSent,
			progress->lastExpected, (int)[sent length]]);
	}

	/* --- A REDIRECTED POST: THE STREAM IS SPENT, SO A NEW ONE IS ASKED FOR (§62.35) ----------------- */
	{
		unsigned char rawBytes[8] = { 0x1d, 0x1f, 0x2b, 0x2f, 0x41, 0x43, 0x47, 0x53 };
		NSData *streamed = [NSData dataWithBytes:rawBytes length:8];
		FnBodyStreamDelegate *streamDelegate = [[FnBodyStreamDelegate alloc] init];
		NSURLSessionConfiguration *configuration3 = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session3 = [NSURLSession sessionWithConfiguration:configuration3
								       delegate:streamDelegate
								      delegateQueue:nil];
		NSMutableURLRequest *redirected = [NSMutableURLRequest requestWithURL:fn_url(@"http://127.0.0.1:46470/first")];
		NSURLSessionDataTask *redirectTask;
		struct sockaddr_in addr3;
		unsigned char firstBuf[4096], secondBuf[4096];
		size_t firstTotal = 0, secondTotal = 0;
		int listener3, conn3 = -1, one3 = 1, tries3 = 0, firstFound = 0, secondFound = 0;
		__block BOOL called3 = NO;

		streamDelegate->bytes = streamed;
		[redirected setHTTPMethod:@"POST"];
		[redirected setValue:[NSString stringWithFormat:@"%d", (int)[streamed length]]
		  forHTTPHeaderField:@"Content-Length"];
		[redirected setHTTPBodyStream:[NSInputStream inputStreamWithData:streamed]];
		[redirected setTimeoutInterval:5.0];

		listener3 = socket(AF_INET, SOCK_STREAM, 0);
		memset(&addr3, 0, sizeof(addr3));
		addr3.sin_family = AF_INET;
		addr3.sin_port = htons(46470);
		addr3.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		setsockopt(listener3, SOL_SOCKET, SO_REUSEADDR, &one3, sizeof(one3));
		check("the-redirect-leg-binds-its-own-listener",
		      bind(listener3, (struct sockaddr *)&addr3, sizeof(addr3)) == 0 && listen(listener3, 2) == 0,
		      @"this leg is its own receiver, and it must answer TWICE");

		redirectTask = [session3 dataTaskWithRequest:redirected
				      completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			(void)error;
			called3 = YES;
		}];
		[redirectTask resume];

		/* THE FIRST REQUEST IS ANSWERED 307, WHICH IS THE STATUS THAT EXISTS TO KEEP BOTH THE METHOD AND THE
		 * BODY - so the follow is a COPY of this request, carrying the stream this attempt has just spent. */
		conn3 = accept(listener3, NULL, NULL);
		if(conn3 >= 0) {
			const char *answer = "HTTP/1.1 307 Temporary Redirect\r\n"
					     "Location: http://127.0.0.1:46470/second\r\n"
					     "Content-Length: 0\r\nConnection: close\r\n\r\n";

			fcntl(conn3, F_SETFL, O_NONBLOCK);
			tries3 = 0;
			while(tries3 < 100 && firstTotal < sizeof(firstBuf)) {
				ssize_t n = read(conn3, firstBuf + firstTotal, sizeof(firstBuf) - firstTotal);

				if(n > 0) {
					firstTotal += (size_t)n;
				} else {
					usleep(10000);
					tries3++;
				}
			}
			write(conn3, answer, strlen(answer));
			close(conn3);
		}

		/* AND THE SECOND CARRIES THE BODY THE DELEGATE HANDED OVER, or the redirect lost it. */
		conn3 = accept(listener3, NULL, NULL);
		if(conn3 >= 0) {
			const char *answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";

			fcntl(conn3, F_SETFL, O_NONBLOCK);
			tries3 = 0;
			while(tries3 < 100 && secondTotal < sizeof(secondBuf)) {
				ssize_t n = read(conn3, secondBuf + secondTotal, sizeof(secondBuf) - secondTotal);

				if(n > 0) {
					secondTotal += (size_t)n;
				} else {
					usleep(10000);
					tries3++;
				}
			}
			write(conn3, answer, strlen(answer));
			close(conn3);
		}
		close(listener3);
		{
			int waited3 = 0;

			while(!called3 && waited3 < 300) {
				usleep(10000);
				waited3++;
			}
		}
		{
			size_t i;

			for(i = 0; i + [streamed length] <= firstTotal; i++) {
				if(memcmp(firstBuf + i, [streamed bytes], [streamed length]) == 0) {
					firstFound = 1;
					break;
				}
			}
			for(i = 0; i + [streamed length] <= secondTotal; i++) {
				if(memcmp(secondBuf + i, [streamed bytes], [streamed length]) == 0) {
					secondFound = 1;
					break;
				}
			}
		}
		check("the-redirected-post-kept-its-body-on-the-way-out", firstFound,
		      @"307 exists to keep the method and the body, and the first attempt sent both");
		check("the-delegate-was-asked-for-a-new-stream", streamDelegate->asks == 1,
		      @"the stream was spent by the first attempt, so the re-send asked for a fresh one");
		check("the-re-sent-request-carries-the-fresh-body", secondFound,
		      [NSString stringWithFormat:@"the second attempt carried %s (%d byte(s) received)",
			secondFound ? "the body" : "NO body", (int)secondTotal]);
	}
	/* --- A STREAM THAT CANNOT BE READ FAILS THE TRANSFER RATHER THAN TRUNCATING IT (§62.36) ---------- */
	{
		FnFailingStream *broken = [[FnFailingStream alloc] init];
		NSURLSessionConfiguration *configuration4 = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session4 = [NSURLSession sessionWithConfiguration:configuration4];
		NSMutableURLRequest *brokenRequest = [NSMutableURLRequest requestWithURL:fn_url(@"http://127.0.0.1:46471/")];
		NSURLSessionDataTask *brokenTask;
		struct sockaddr_in addr4;
		int listener4, conn4 = -1, one4 = 1;
		__block BOOL called4 = NO;
		__block NSError *brokenError = nil;

		/* THE LENGTH IS PUBLISHED, AND THE CHECK READS THE ERROR'S CODE RATHER THAN ITS PRESENCE - which is the
		 * only way this can discriminate. Without a length, curl never pulls the body at all for an upload of
		 * unknown size (measured: the read callback is not called even once), so there is no read to fail and the
		 * leg passes either way. With a length, a SHORT body is also an error - so "an error happened" proves
		 * nothing; WHAT PROVES SOMETHING IS WHICH error, and the two are distinguishable because the fix makes
		 * curl report its "aborted by callback" code instead of a send failure. */
		[brokenRequest setHTTPMethod:@"POST"];
		[brokenRequest setValue:@"8" forHTTPHeaderField:@"Content-Length"];
		[brokenRequest setHTTPBodyStream:broken];
		[brokenRequest setTimeoutInterval:3.0];

		listener4 = socket(AF_INET, SOCK_STREAM, 0);
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		addr4.sin_port = htons(46471);
		addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		setsockopt(listener4, SOL_SOCKET, SO_REUSEADDR, &one4, sizeof(one4));
		check("the-broken-stream-leg-binds-its-own-listener",
		      bind(listener4, (struct sockaddr *)&addr4, sizeof(addr4)) == 0 && listen(listener4, 1) == 0,
		      @"this leg is its own receiver as well");

		brokenTask = [session4 dataTaskWithRequest:brokenRequest
			     completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			brokenError = error;
			called4 = YES;
		}];
		[brokenTask resume];
		conn4 = accept(listener4, NULL, NULL);
		if(conn4 >= 0) {
			char drain[1024];
			int tries4 = 0;

			fcntl(conn4, F_SETFL, O_NONBLOCK);
			while(tries4 < 50) {
				if(read(conn4, drain, sizeof(drain)) <= 0) {
					usleep(10000);
					tries4++;
				}
			}
			/* THE SERVER ANSWERS, AND THAT IS WHAT MAKES THE CHECK MEAN ANYTHING: with the old callback the
			 * empty body was accepted and the transfer then SUCCEEDED - so the first version of this leg, which
			 * never answered, was passed even by the bug it exists to catch (the client timed out waiting for a
			 * response, and a timeout is also an NSError). A CHECK THAT CANNOT FAIL IS WORSE THAN NO CHECK. */
			{
				const char *answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";

				write(conn4, answer, strlen(answer));
			}
			close(conn4);
		}
		close(listener4);
		{
			int waited4 = 0;

			while(!called4 && waited4 < 300) {
				usleep(10000);
				waited4++;
			}
		}
		/* 42 IS `CURLE_ABORTED_BY_CALLBACK`, AND IT IS NAMED HERE RATHER THAN BECAUSE THE PROBE CANNOT IMPORT
		 * curl's HEADERS: it is the code this library reports for a read callback that said "stop", which is what
		 * a FAILED read now says. A short body would report a SEND failure instead - a different number - so this
		 * check can only pass when the read error is what ended the transfer. */
		check("a-stream-that-cannot-be-read-fails-the-transfer-with-the-reads-error",
		      called4 && brokenError != nil && [brokenError code] == 42,
		      [NSString stringWithFormat:@"the upload reported %@ (code %d) - a read that FAILS is not an empty "
			@"body, and the code says the read is what ended it",
			brokenError != nil ? @"an error" : @"SUCCESS", brokenError != nil ? (int)[brokenError code] : 0]);
	}

	/* --- A STREAM BODY WITH NO PUBLISHED LENGTH (§62.38) -------------------------------------------- */
	{
		unsigned char rawBytes[8] = { 0x31, 0x37, 0x43, 0x49, 0x59, 0x61, 0x6d, 0x79 };
		NSData *unpublished = [NSData dataWithBytes:rawBytes length:8];
		NSURLSessionConfiguration *configuration5 = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session5 = [NSURLSession sessionWithConfiguration:configuration5];
		NSMutableURLRequest *noLength = [NSMutableURLRequest requestWithURL:fn_url(@"http://127.0.0.1:46472/")];
		NSURLSessionDataTask *noLengthTask;
		struct sockaddr_in addr5;
		unsigned char buf5[4096];
		size_t total5 = 0;
		int listener5, conn5 = -1, one5 = 1, tries5 = 0, found5 = 0;
		__block BOOL called5 = NO;

		/* NO Content-Length: the caller is uploading something whose size it does not know (a pipe, a generated
		 * body), and HTTP/1.1's answer to that is chunked encoding. */
		[noLength setHTTPMethod:@"POST"];
		[noLength setHTTPBodyStream:[NSInputStream inputStreamWithData:unpublished]];
		[noLength setTimeoutInterval:3.0];

		listener5 = socket(AF_INET, SOCK_STREAM, 0);
		memset(&addr5, 0, sizeof(addr5));
		addr5.sin_family = AF_INET;
		addr5.sin_port = htons(46472);
		addr5.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		setsockopt(listener5, SOL_SOCKET, SO_REUSEADDR, &one5, sizeof(one5));
		check("the-unpublished-length-leg-binds-its-own-listener",
		      bind(listener5, (struct sockaddr *)&addr5, sizeof(addr5)) == 0 && listen(listener5, 1) == 0,
		      @"this leg is its own receiver as well");

		noLengthTask = [session5 dataTaskWithRequest:noLength
			       completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
			(void)data;
			(void)response;
			(void)error;
			called5 = YES;
		}];
		[noLengthTask resume];
		conn5 = accept(listener5, NULL, NULL);
		if(conn5 >= 0) {
			fcntl(conn5, F_SETFL, O_NONBLOCK);
			while(tries5 < 100 && total5 < sizeof(buf5)) {
				ssize_t n = read(conn5, buf5 + total5, sizeof(buf5) - total5);

				if(n > 0) {
					total5 += (size_t)n;
				} else {
					usleep(10000);
					tries5++;
				}
			}
			{
				const char *answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";

				write(conn5, answer, strlen(answer));
			}
			close(conn5);
		}
		close(listener5);
		{
			int waited5 = 0;

			while(!called5 && waited5 < 300) {
				usleep(10000);
				waited5++;
			}
		}
		{
			size_t i;

			for(i = 0; i + [unpublished length] <= total5; i++) {
				if(memcmp(buf5 + i, [unpublished bytes], [unpublished length]) == 0) {
					found5 = 1;
					break;
				}
			}
		}
		check("a-stream-body-with-no-published-length-is-sent",
		      found5,
		      [NSString stringWithFormat:@"the receiver saw %d byte(s) and the body was %s - an unpublishable "
			@"length means CHUNKED, not nothing", (int)total5, found5 ? "PRESENT" : "ABSENT"]);
	}

	printf("FOUNDATION-URLSESSION-TASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLSESSION-TASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLSESSION-TASK DONE\n");
	return failc ? 1 : 0;
}
