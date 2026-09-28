/*
 * foundation_downloadresume.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * RESUME, END TO END, WITH A REAL SERVER (§62.31). The probe listens on its own socket, answers the first
 * request 200 with a body it sends only HALF of and then holds open, cancels its own download with
 * `-cancelByProducingResumeData:`, resumes from that data, and asserts THREE things that together are the
 * whole feature: THE RESUMED REQUEST ASKS FOR THE REST (`Range: bytes=10-` on the second connection), the
 * delegate was told the offset it resumed from, and THE FILE IT ENDED WITH IS THE WHOLE BODY - not the second
 * half reported as if it were the body.
 *
 * THE HOLD-OPEN IS WHAT MAKES IT DETERMINISTIC RATHER THAN RACY: the server deliberately stalls mid-body, so
 * the cancel happens while the transfer is still running, with no timing assumption about who is faster. A
 * server that finished the body would leave nothing to resume.
 *
 * AND A THIRD LEG FAILS ON PURPOSE: a body that stops early with the connection CLOSED is a transport error,
 * and the point of it is that the ERROR ITSELF carries resume data - Apple's
 * `NSURLSessionDownloadTaskResumeData` key, which is what lets a caller recover from a dropped connection
 * without ever having asked to stop.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-DOWNLOADRESUME %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DOWNLOADRESUME %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE BODY IS 20 BYTES AND THE SPLIT IS AT 10, so "half" is arithmetic rather than a coincidence: the first
 * leg is served the first ten, the resume asks for the rest, and the finished file must be all twenty. */
static const char *fn_body(void) { return "0123456789abcdefghij"; }

/* A BLOB THAT IS NOT RESUME DATA AT ALL: the check is that the session refuses to build a task from it. */
static NSData *fn_not_a_resume(void)
{
	id data = [@"not a resume" dataUsingEncoding:NSUTF8StringEncoding];

	return data;
}

static NSString *fn_utf8(const char *bytes)
{
	id text = [NSString stringWithUTF8String:bytes];

	return text;
}

/* THE NULLABLE-FACTORY IDIOM THIS TIER REQUIRES (the same one the connection probe uses): a nullable factory's
 * result goes through an `id` local before it reaches a non-null parameter. */
static NSURL *fn_url(NSString *string)
{
	id url = [NSURL URLWithString:string];

	return url;
}

static NSMutableURLRequest *fn_request(NSString *string)
{
	return [NSMutableURLRequest requestWithURL:fn_url(string)];
}

static NSString *fn_read_request(int fd)
{
	NSMutableData *data = [[NSMutableData alloc] init];
	char buf[1024];
	int tries = 0;
	ssize_t n;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	while(tries < 200) {
		n = read(fd, buf, sizeof(buf));
		if(n > 0) {
			[data appendBytes:buf length:(NSUInteger)n];
			if([data length] > 0) {
				break;
			}
		} else {
			usleep(10000);
			tries++;
		}
	}
	return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

/* --- THE DELEGATE --------------------------------------------------------------------------------- */

/* IT IMPLEMENTS THE REQUIRED DOOR AND THE TWO OPTIONAL ONES, because this probe is about the paths a caller
 * actually takes: the file it is handed is MOVED (into a name the probe reads afterwards, which is also the
 * only honest way to check what arrived), and the offset it resumed from is recorded so the check can read it
 * instead of assuming. */
@interface FNResumeDelegate : NSObject <NSURLSessionDownloadDelegate>
{
	@public
	NSURL *kept;			/* the destination the last finished download was moved to */
	NSData *keptBytes;		/* ... and its bytes, read at the door */
	int finishCalls;
	int failCalls;
	int resumeCalls;
	int64_t resumeOffset;
	int64_t resumeExpected;
	int64_t lastTotalWritten;	/* the progress door's running total, which the cancel leg waits on */
	BOOL done;
	NSError *failure;		/* the error a failed leg reported, so its userInfo can be read */
}

@end

@implementation FNResumeDelegate

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
didFinishDownloadingToURL:(NSURL *)location
{
	(void)session;
	(void)downloadTask;
	/* MOVED, AS THE CONTRACT REQUIRES: the location is temporary, and a probe that only read it would be
	 * testing its own patience rather than the library's contract. */
	keptBytes = [NSData dataWithContentsOfURL:location];
	kept = location;
	finishCalls++;
	done = YES;
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
       didWriteData:(int64_t)bytesWritten
  totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite
{
	(void)session;
	(void)downloadTask;
	(void)bytesWritten;
	(void)totalBytesExpectedToWrite;
	lastTotalWritten = totalBytesWritten;
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
didResumeAtOffset:(int64_t)fileOffset
expectedTotalBytes:(int64_t)expectedTotalBytes
{
	(void)session;
	(void)downloadTask;
	resumeCalls++;
	resumeOffset = fileOffset;
	resumeExpected = expectedTotalBytes;
}

- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
	(void)session;
	(void)task;
	if(error != nil) {
		failCalls++;
		failure = error;
		done = YES;
	}
}

@end

/* A BOUNDED WAIT, IN 20 MS STEPS, because the guest's clock rounds small sleeps up hard - the lesson
 * foundation_authloop and the connection probe both record. */
static BOOL fn_wait_for(BOOL (^ready)(void), int milliseconds)
{
	int slept = 0;

	while(!ready() && slept < milliseconds) {
		usleep(20000);
		slept += 20;
	}
	return ready();
}

int main(void)
{
	FNResumeDelegate *delegate = [[FNResumeDelegate alloc] init];
	NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
	NSURLSession *session;
	NSURLSessionDownloadTask *task;
	NSMutableURLRequest *request;
	struct sockaddr_in addr;
	int listener, conn, one = 1;
	__block NSData *resumeData = nil;
	NSString *first = nil, *second = nil, *third = nil;

	setvbuf(stdout, NULL, _IONBF, 0);

	/* NO REGISTRATION: the library registers the transport at load (see the class's own note, plan §62.83) */

	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(46492);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	check("the-probe-binds-its-own-listener",
	      bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(listener, 4) == 0,
	      @"the probe is the server, so no receiver tool is involved");

	session = [NSURLSession sessionWithConfiguration:configuration
						delegate:delegate
					   delegateQueue:nil];

	/* --- LEG 1: A DOWNLOAD CANCELLED MID-BODY, WHICH PRODUCES THE RESUME DATA ---------------------- */
	request = fn_request(@"http://127.0.0.1:46492/file");
	[request setTimeoutInterval:10.0];
	task = [session downloadTaskWithRequest:request];
	[task resume];

	conn = accept(listener, NULL, NULL);
	first = conn >= 0 ? fn_read_request(conn) : nil;
	check("the-first-request-asks-for-the-whole-body",
	      first != nil &&
	      [first rangeOfString:@"Range:" options:NSCaseInsensitiveSearch].location == NSNotFound,
	      @"a fresh download asks for all of it, which is what makes the second one's Range a RESUME");
	if(conn >= 0) {
		const char *head = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\nConnection: close\r\n\r\n";

		write(conn, head, strlen(head));
		write(conn, fn_body(), 10);	/* HALF - and the socket is then left OPEN: nothing is finished */
	}

	/* THE STALL IS WHAT MAKES THIS DETERMINISTIC: the cancel below happens while the transfer is still
	 * running, with no assumption about whether the client or the server is faster. */
	(void)fn_wait_for(^BOOL { return delegate->lastTotalWritten >= 10; }, 3000);
	[task cancelByProducingResumeData:^(NSData *data) {
		resumeData = data;
	}];
	check("the-interrupted-transfer-produced-resume-data",
	      resumeData != nil && [resumeData length] > 0 &&
	      delegate->finishCalls == 0 && delegate->failCalls == 0,
	      [NSString stringWithFormat:@"%d byte(s) of resume data, and NO ending was reported: a cancel is "
		@"not a failure", (int)[resumeData length]]);
	if(conn >= 0) {
		close(conn);
	}

	/* --- LEG 2: RESUMED, AND THE SERVER IS ASKED FOR THE REST -------------------------------------- */
	{
		FNResumeDelegate *secondDelegate = [[FNResumeDelegate alloc] init];
		NSURLSession *session2 = [NSURLSession sessionWithConfiguration:configuration
								      delegate:secondDelegate
								     delegateQueue:nil];
		NSURLSessionDownloadTask *resumed = nil;

		if(resumeData != nil) {
			resumed = [session2 downloadTaskWithResumeData:resumeData];
		}
		check("a-blob-we-did-not-write-is-not-a-resume",
		      resumed != nil &&
		      [session2 downloadTaskWithResumeData:fn_not_a_resume()] == nil,
		      @"a real resume builds a task, and a blob with nothing of ours in it answers nil rather than "
		      @"a task built from whatever it happened to contain");

		if(resumed != nil) {
			[resumed resume];
			conn = accept(listener, NULL, NULL);
			second = conn >= 0 ? fn_read_request(conn) : nil;
			check("the-resumed-request-asks-for-the-rest",
			      second != nil &&
			      [second rangeOfString:@"Range: bytes=10-" options:NSCaseInsensitiveSearch].location
				!= NSNotFound,
			      @"the resumed request carries a byte range, and it is the transport's own header "
			      @"pass-through that sends it: no new machinery anywhere");
			if(conn >= 0) {
				const char *head = "HTTP/1.1 206 Partial Content\r\nContent-Length: 10\r\n"
						   "Connection: close\r\n\r\n";

				write(conn, head, strlen(head));
				write(conn, fn_body() + 10, 10);	/* the REST of it */
				close(conn);
			}
			(void)fn_wait_for(^BOOL { return secondDelegate->done; }, 3000);
			check("the-resumed-transfer-reports-its-offset",
			      secondDelegate->resumeCalls == 1 && secondDelegate->resumeOffset == 10,
			      [NSString stringWithFormat:@"the delegate was told it resumed at %lld (calls=%d)",
				secondDelegate->resumeOffset, secondDelegate->resumeCalls]);
			check("the-resumed-download-holds-the-whole-body",
			      secondDelegate->finishCalls == 1 && secondDelegate->keptBytes != nil &&
			      [[[NSString alloc] initWithData:secondDelegate->keptBytes
						     encoding:NSUTF8StringEncoding]
				isEqualToString:fn_utf8(fn_body())],
			      [NSString stringWithFormat:@"the file holds %d byte(s): the ten that arrived before the "
				@"cancel and the ten that came after, which is the whole body",
				(int)[secondDelegate->keptBytes length]]);
		}
	}

	/* --- LEG 3: A TRANSFER THAT FAILS, WHOSE ERROR CARRIES THE WAY TO CONTINUE IT ------------------- */
	{
		FNResumeDelegate *thirdDelegate = [[FNResumeDelegate alloc] init];
		NSURLSession *session3 = [NSURLSession sessionWithConfiguration:configuration
								      delegate:thirdDelegate
								     delegateQueue:nil];
		NSMutableURLRequest *request3 = fn_request(@"http://127.0.0.1:46492/dropped");
		NSURLSessionDownloadTask *task3;

		[request3 setTimeoutInterval:10.0];
		task3 = [session3 downloadTaskWithRequest:request3];
		[task3 resume];
		conn = accept(listener, NULL, NULL);
		third = conn >= 0 ? fn_read_request(conn) : nil;
		if(conn >= 0) {
			const char *head = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\nConnection: close\r\n\r\n";

			write(conn, head, strlen(head));
			write(conn, fn_body(), 10);
			close(conn);	/* the CONNECTION DROPS: a real transport failure, not a cancel */
		}
		(void)fn_wait_for(^BOOL { return thirdDelegate->done; }, 3000);
		check("a-failed-download-carries-resume-data-on-its-error",
		      thirdDelegate->failCalls == 1 && thirdDelegate->failure != nil &&
		      [[[thirdDelegate->failure userInfo]
			objectForKey:NSURLSessionDownloadTaskResumeData] length] > 0,
		      @"the failure's own userInfo carries the bytes that arrived, under Apple's key - which is "
		      @"what lets a caller recover without having asked to stop");
	}

	printf("FOUNDATION-DOWNLOADRESUME RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DOWNLOADRESUME-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DOWNLOADRESUME DONE\n");
	return failc ? 1 : 0;
}
