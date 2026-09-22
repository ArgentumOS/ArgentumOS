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

	printf("FOUNDATION-URLSESSION-TASK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLSESSION-TASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLSESSION-TASK DONE\n");
	return failc ? 1 : 0;
}
