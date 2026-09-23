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


int main(void)
{
	/* UNBUFFERED, AND IT IS NOT A PREFERENCE: a probe that CRASHES loses everything printf put in a
	 * block-buffered stream, so the markers that exist to say where it died are exactly the output that
	 * disappears. Measured here: the first diagnostic run showed no marker at all and read as "it died
	 * before the first statement", which was false — the crash was simply later than the lost buffer. */
	setvbuf(stdout, NULL, _IONBF, 0);
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
		NSURLSession *session = [NSURLSession sessionWithConfiguration:
						[NSURLSessionConfiguration defaultSessionConfiguration]];
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
	}

	printf("FOUNDATION-URLSESSION-TASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLSESSION-TASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLSESSION-TASK DONE\n");
	return failc ? 1 : 0;
}
