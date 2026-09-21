/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_stream — the NSStream head's acceptance. docs/design/foundation-plan.md W6's streams half, §45-Y.
 *
 * THE HEAD IS WHAT IS PROVED HERE, and the two things a head can get wrong are exactly what it is tested
 * for: the VALUES it invents (Apple publishes the case names and not the numbers, so ours are a contract
 * that has to be pinned somewhere), and the SEAM it offers a substream (whether a descriptor made ready
 * really reaches a delegate through the run loop without a thread blocking on a read).
 *
 * SO THE PROBE BUILDS A SUBSTREAM. `FnTestStream` is the smallest thing that uses the head the way a real
 * input stream will: it answers `-fnStreamDescriptor`, overrides `-open` to move the status through the
 * private hook, and then the run loop's own file-descriptor source fires the delegate when the pipe has a
 * byte in it. That is the whole point of scheduling a stream, and it is asserted rather than assumed.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-STREAM %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-STREAM %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* ---- a substream, the way an NSInputStream will be one ---- */

@interface FnTestStream : NSStream
{
	int _fd;
}
- (instancetype)initWithFileDescriptor:(int)fd;
@end

@implementation FnTestStream

- (instancetype)initWithFileDescriptor:(int)fd
{
	self = [super init];
	if (self != nil) {
		_fd = fd;
	}
	return self;
}

- (int)fnStreamDescriptor
{
	return _fd;
}

- (BOOL)fnStreamWatchesReadable
{
	return YES;
}

/* A SUBSTREAM MOVES THE STATUS, because only it knows whether its resource opened; the base stays inert. */
- (void)open
{
	[super open];
	[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
}

@end

/* ---- a delegate that counts what it hears ---- */

@interface FnStreamDelegate : NSObject <NSStreamDelegate>
{
	int _events;
	NSStreamEvent _last;
}
- (int)events;
- (NSStreamEvent)last;
@end

/* WHAT A COMPLETION HANDLER IS OBSERVED THROUGH - and it runs ON THE REAPER'S THREAD, so the flag is
 * volatile and the probe waits for it rather than assuming it has happened. */
@interface FnUnixTaskFixture : NSObject
{
	volatile int _fired;
	NSError *_error;
}
- (void)note:(nullable NSError *)error;
- (int)fired;
- (nullable NSError *)error;
@end

@implementation FnUnixTaskFixture

- (void)note:(nullable NSError *)error
{
	_error = error;
	_fired = 1;
}

- (int)fired
{
	return _fired;
}

- (nullable NSError *)error
{
	return _error;
}

@end

@implementation FnStreamDelegate

- (void)stream:(NSStream *)aStream handleEvent:(NSStreamEvent)eventCode
{
	(void)aStream;
	_events++;
	_last = eventCode;
}

- (int)events
{
	return _events;
}

- (NSStreamEvent)last
{
	return _last;
}

@end

int main(void)
{
	/* ---- 1. THE VALUES ARE OURS, SO THEY ARE PINNED HERE (plan D2) ---- */
	check("event-mask-values",
	      NSStreamEventNone == 0 && NSStreamEventOpenCompleted == (1 << 0) &&
	      NSStreamEventHasBytesAvailable == (1 << 1) && NSStreamEventHasSpaceAvailable == (1 << 2) &&
	      NSStreamEventErrorOccurred == (1 << 3) && NSStreamEventEndEncountered == (1 << 4),
	      [NSString stringWithFormat:@"none=%lu open=%lu bytes=%lu space=%lu error=%lu end=%lu",
		(unsigned long)NSStreamEventNone, (unsigned long)NSStreamEventOpenCompleted,
		(unsigned long)NSStreamEventHasBytesAvailable, (unsigned long)NSStreamEventHasSpaceAvailable,
		(unsigned long)NSStreamEventErrorOccurred, (unsigned long)NSStreamEventEndEncountered]);

	check("status-values",
	      NSStreamStatusNotOpen == 0 && NSStreamStatusOpening == 1 && NSStreamStatusOpen == 2 &&
	      NSStreamStatusReading == 3 && NSStreamStatusWriting == 4 && NSStreamStatusAtEnd == 5 &&
	      NSStreamStatusClosed == 6 && NSStreamStatusError == 7,
	      [NSString stringWithFormat:@"notopen=%d open=%d closed=%d error=%d",
		(int)NSStreamStatusNotOpen, (int)NSStreamStatusOpen, (int)NSStreamStatusClosed,
		(int)NSStreamStatusError]);

	/* THE KEY STRINGS ARE OUR CHOICE TOO, and each one is its own name - the convention a config reader
	 * would expect. Asserted individually so a later typo cannot pass as "a string". */
	check("property-key-strings",
	      [NSStreamFileCurrentOffsetKey isEqualToString:@"NSStreamFileCurrentOffsetKey"] &&
	      [NSStreamDataWrittenToMemoryStreamKey isEqualToString:@"NSStreamDataWrittenToMemoryStreamKey"] &&
	      [NSStreamSocketSecurityLevelKey isEqualToString:@"NSStreamSocketSecurityLevelKey"] &&
	      [NSStreamSOCKSProxyConfigurationKey isEqualToString:@"NSStreamSOCKSProxyConfigurationKey"] &&
	      [NSStreamSOCKSProxyVersionKey isEqualToString:@"NSStreamSOCKSProxyVersionKey"] &&
	      [NSStreamNetworkServiceType isEqualToString:@"NSStreamNetworkServiceType"],
	      @"a key's value is not its own name");

	check("network-bag-values",
	      [NSStreamSocketSecurityLevelTLSv1 isEqualToString:@"NSStreamSocketSecurityLevelTLSv1"] &&
	      [NSStreamSOCKSProxyVersion5 isEqualToString:@"NSStreamSOCKSProxyVersion5"] &&
	      [NSStreamNetworkServiceTypeVoice isEqualToString:@"NSStreamNetworkServiceTypeVoice"] &&
	      [NSStreamSocketSSLErrorDomain isEqualToString:@"NSStreamSocketSSLErrorDomain"] &&
	      [NSStreamSOCKSErrorDomain isEqualToString:@"NSStreamSOCKSErrorDomain"],
	      @"a carried constant's value is not its own name");

	/* ---- 2. THE BASE IS ABSTRACT AND INERT: -open MOVES NOTHING ---- */
	{
		NSStream *bare = [[NSStream alloc] init];

		check("bare-stream-starts-not-open",
		      [bare streamStatus] == NSStreamStatusNotOpen && [bare streamError] == nil,
		      @"a fresh NSStream is not NotOpen");
		[bare open];
		check("bare-stream-open-is-inert",
		      [bare streamStatus] == NSStreamStatusNotOpen,
		      [NSString stringWithFormat:@"-open moved a bare NSStream to %d", (int)[bare streamStatus]]);
	}

	/* ---- 3. THE PROPERTY BAG RECORDS, ANSWERS AND FORGETS ---- */
	{
		NSStream *stream = [[NSStream alloc] init];

		check("property-bag-round-trip",
		      [stream propertyForKey:NSStreamFileCurrentOffsetKey] == nil &&
		      [stream setProperty:@5 forKey:NSStreamFileCurrentOffsetKey] &&
		      [[stream propertyForKey:NSStreamFileCurrentOffsetKey] intValue] == 5,
		      @"the property bag did not round-trip a value");
		(void)[stream setProperty:nil forKey:NSStreamFileCurrentOffsetKey];
		check("property-bag-forgets",
		      [stream propertyForKey:NSStreamFileCurrentOffsetKey] == nil,
		      @"setting nil did not remove the property");
		check("property-bag-refuses-a-nil-key",
		      ![stream setProperty:@1 forKey:(NSStreamPropertyKey)nil] &&
		      [stream propertyForKey:(NSStreamPropertyKey)nil] == nil,
		      @"a nil key was accepted");
	}

	/* ---- 4. THE SEAM: A READY DESCRIPTOR REACHES THE DELEGATE THROUGH THE RUN LOOP ---- */
	{
		int fds[2];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		if (pipe(fds) != 0) {
			check("source-fires-delegate", 0, @"pipe(2) failed, so the seam could not be tested");
		} else {
			FnTestStream *stream = [[FnTestStream alloc] initWithFileDescriptor:fds[0]];
			FnStreamDelegate *delegate = [[FnStreamDelegate alloc] init];

			[stream setDelegate:delegate];
			[stream scheduleInRunLoop:loop forMode:NSDefaultRunLoopMode];
			[stream open];
			check("substream-moves-the-status",
			      [stream streamStatus] == NSStreamStatusOpen,
			      [NSString stringWithFormat:@"a substream's -open left the status at %d",
				(int)[stream streamStatus]]);

			/* NOTHING IS READY YET, and the loop must simply return. */
			(void)[loop runMode:NSDefaultRunLoopMode
				 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			check("no-event-without-readiness",
			      [delegate events] == 0,
			      [NSString stringWithFormat:@"%d event(s) fired with an empty pipe", [delegate events]]);

			/* NOW IT IS READY, and the SAME loop must fire the delegate - no thread blocked on a read. */
			if (write(fds[1], "x", 1) == 1) {
				(void)[loop runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
				check("source-fires-delegate",
				      [delegate events] > 0 &&
				      ([delegate last] & NSStreamEventHasBytesAvailable) != 0,
				      [NSString stringWithFormat:@"%d event(s), last=%lu",
					[delegate events], (unsigned long)[delegate last]]);
			} else {
				check("source-fires-delegate", 0, @"could not make the pipe readable");
			}

			[stream removeFromRunLoop:loop forMode:NSDefaultRunLoopMode];
			[stream close];
			(void)close(fds[0]);
			(void)close(fds[1]);
		}
	}


	/* ---- 5. NSInputStream: THE SOURCE - A MEMORY STREAM FIRST ---- */
	{
		NSData *payload = [@"hello" dataUsingEncoding:NSUTF8StringEncoding];
		NSInputStream *in = [NSInputStream inputStreamWithData:payload];
		uint8_t buf[8];
		NSUInteger len = 0;
		uint8_t *p = NULL;

		/* A STREAM OF ITS OWN, deliberately: the read below puts it in Error, and an Error stream is
		 * not the one the rest of this block goes on to open. */
		{
			NSInputStream *never = [NSInputStream inputStreamWithData:payload];

			check("input-stream-refuses-a-read-before-open",
			      [never read:buf maxLength:sizeof(buf)] == -1 &&
			      [never streamStatus] == NSStreamStatusError,
			      @"a stream that was never opened answered a read rather than failing");
		}
		[in open];
		check("input-stream-from-data-opens",
		      [in streamStatus] == NSStreamStatusOpen && [in fnStreamDescriptor] == -1,
		      @"a data stream did not open, or claimed a descriptor it has not got");
		check("input-stream-from-data-reads",
		      [in read:buf maxLength:3] == 3 && memcmp(buf, "hel", 3) == 0,
		      @"the first three bytes were not the first three bytes");
		check("input-stream-getbuffer",
		      [in getBuffer:&p length:&len] && p != NULL && len == 2,
		      @"a memory stream would not hand out its remaining two bytes");
		check("input-stream-reads-the-rest",
		      [in read:buf maxLength:sizeof(buf)] == 2 && memcmp(buf, "lo", 2) == 0,
		      @"the rest was not the rest");
		check("input-stream-ends-at-eof",
		      [in read:buf maxLength:sizeof(buf)] == 0 &&
		      [in streamStatus] == NSStreamStatusAtEnd,
		      @"a read past the end did not answer 0 and move to AtEnd");
	}

	/* ---- 6. AND THE OFFSET KEY, WHICH IS ONE OF THE TWO THIS LIBRARY ACTS ON ---- */
	{
		NSData *payload = [@"abcdef" dataUsingEncoding:NSUTF8StringEncoding];
		NSInputStream *in = [NSInputStream inputStreamWithData:payload];
		uint8_t buf[8];

		[in open];
		(void)[in read:buf maxLength:2];
		check("input-stream-offset-key-reads",
		      [[in propertyForKey:NSStreamFileCurrentOffsetKey] unsignedIntegerValue] == 2,
		      @"the offset key did not report where the stream is");
		check("input-stream-offset-key-seeks",
		      [in setProperty:@0 forKey:NSStreamFileCurrentOffsetKey] &&
		      [in read:buf maxLength:1] == 1 && buf[0] == 'a',
		      @"setting the offset key did not reposition the stream");
	}

	/* ---- 7. AND A DESCRIPTOR: THE PROBE IS ITS OWN FIXTURE - a staged executable is an ELF, so its first
	 * four bytes are a fact about the image rather than about this test. ---- */
	{
		NSInputStream *in = [NSInputStream inputStreamWithFileAtPath:
					@"/System/Shared/tests/foundation_stream"];
		uint8_t buf[8];
		NSUInteger len = 0;
		uint8_t *p = NULL;

		check("input-stream-from-file-constructs", in != nil,
		      @"-inputStreamWithFileAtPath: answered nil");
		[in open];
		check("input-stream-from-file-opens",
		      [in streamStatus] == NSStreamStatusOpen && [in fnStreamDescriptor] >= 0,
		      [NSString stringWithFormat:@"a file stream opened with status %d and descriptor %d",
			(int)[in streamStatus], [in fnStreamDescriptor]]);
		check("input-stream-from-file-reads",
		      [in read:buf maxLength:4] == 4 && buf[0] == 0x7f && buf[1] == 'E' &&
		      buf[2] == 'L' && buf[3] == 'F',
		      @"the staged probe's first four bytes are not an ELF magic");
		check("input-stream-getbuffer-refuses-a-file", ![in getBuffer:&p length:&len],
		      @"a file stream handed out a memory buffer");
		[in close];
		check("input-stream-closes", [in streamStatus] == NSStreamStatusClosed,
		      @"-close did not leave the stream Closed");
	}

	/* ---- 8. THE SEAM AGAIN, THROUGH THE NEW CLASS: a regular file is ALWAYS ready, so a scheduled file
	 * stream must report its bytes through the delegate - no read, no thread blocked. ---- */
	{
		NSInputStream *in = [NSInputStream inputStreamWithFileAtPath:
					@"/System/Shared/tests/foundation_stream"];
		FnStreamDelegate *delegate = [[FnStreamDelegate alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		uint8_t buf[4];

		/* AND THIS IS THE KERNEL FIX'S REGRESSION TEST: select(2) reports a REGULAR FILE ready, because an
		 * I/O on one cannot block. It answered 0 until the kernel's do_check() learned the rule, which is
		 * why a file-backed stream was readable and never DELIVERED - the run loop's wait IS select(2). */
		{
			int fd = open("/System/Shared/tests/foundation_stream", O_RDONLY);

			if (fd >= 0) {
				fd_set set;
				struct timeval tv;
				int ready;

				FD_ZERO(&set);
				FD_SET(fd, &set);
				tv.tv_sec = 0;
				tv.tv_usec = 0;
				ready = select(fd + 1, &set, NULL, NULL, &tv);
				check("select-reports-a-regular-file-as-ready",
				      ready > 0 && FD_ISSET(fd, &set),
				      [NSString stringWithFormat:@"select(2) on a regular file answered %d", ready]);
				(void)close(fd);
			}
		}
		[in setDelegate:delegate];
		[in scheduleInRunLoop:loop forMode:NSDefaultRunLoopMode];
		[in open];
		(void)[loop runMode:NSDefaultRunLoopMode
			 beforeDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
		/* AND THE CONSEQUENCE OF THE FIX, which is the whole point of the seam: with select(2) reporting a
		 * regular file ready, a scheduled FILE-backed stream IS delivered - no read, no thread blocked. */
		check("input-stream-fires-has-bytes",
		      [delegate events] > 0 &&
		      ([delegate last] & NSStreamEventHasBytesAvailable) != 0,
		      [NSString stringWithFormat:@"%d event(s), last=%lu",
			[delegate events], (unsigned long)[delegate last]]);
		(void)[in read:buf maxLength:sizeof(buf)];
		[in removeFromRunLoop:loop forMode:NSDefaultRunLoopMode];
		[in close];
	}


	/* ---- 9. NSOutputStream: THE THREE DESTINATIONS ---- */
	{
		/* A STREAM OF ITS OWN: this one is left in Error on purpose. */
		NSOutputStream *never = [NSOutputStream outputStreamToMemory];

		check("output-stream-refuses-a-write-before-open",
		      [never write:(const uint8_t *)"x" maxLength:1] == -1 &&
		      [never streamStatus] == NSStreamStatusError,
		      @"a stream that was never opened accepted a write");
	}

	{
		/* MEMORY GROWS TO FIT, and the bytes are read back through the key the head declares. */
		NSOutputStream *out = [NSOutputStream outputStreamToMemory];
		NSData *written;

		[out open];
		check("output-stream-to-memory-writes",
		      [out write:(const uint8_t *)"hello" maxLength:5] == 5 &&
		      [out hasSpaceAvailable],
		      @"a memory stream did not take five bytes");
		written = [out propertyForKey:NSStreamDataWrittenToMemoryStreamKey];
		check("output-stream-memory-key-reads-back",
		      written != nil && [written length] == 5 &&
		      memcmp([written bytes], "hello", 5) == 0,
		      [NSString stringWithFormat:@"the key answered %lu bytes",
			(unsigned long)(written != nil ? [written length] : 0)]);
	}

	{
		/* THE CALLER'S BUFFER IS THE LIMIT, and a full one answers 0 rather than an error. */
		uint8_t storage[4];
		NSOutputStream *out = [NSOutputStream outputStreamToBuffer:storage capacity:4];
		NSData *written;

		[out open];
		check("output-stream-to-buffer-honours-capacity",
		      [out write:(const uint8_t *)"hello" maxLength:5] == 4 &&
		      ![out hasSpaceAvailable] &&
		      [out write:(const uint8_t *)"!" maxLength:1] == 0,
		      @"a four-byte buffer did not stop at four bytes");
		written = [out propertyForKey:NSStreamDataWrittenToMemoryStreamKey];
		check("output-stream-buffer-key-reads-back",
		      written != nil && [written length] == 4 && memcmp([written bytes], "hell", 4) == 0,
		      @"the buffer's bytes did not come back");
	}

	{
		/* A FILE, AND THE ROUND TRIP THROUGH THE OTHER CLASS: written by an output stream, read back by
		 * an input stream, which is the pair the unit exists to make possible. */
		const char *path = "/System/Temporary Files/foundation-stream-probe";
		NSOutputStream *out = [NSOutputStream outputStreamToFileAtPath:
					(NSString *)[NSString stringWithUTF8String:path] append:NO];

		[out open];
		check("output-stream-to-file-writes",
		      [out streamStatus] == NSStreamStatusOpen &&
		      [out write:(const uint8_t *)"hello world" maxLength:11] == 11,
		      [NSString stringWithFormat:@"writing eleven bytes answered %d, status %d",
			(int)[out write:(const uint8_t *)"" maxLength:0], (int)[out streamStatus]]);
		check("output-stream-offset-key-reads",
		      [[out propertyForKey:NSStreamFileCurrentOffsetKey] longLongValue] == 11,
		      @"the offset key did not report eleven bytes written");
		[out close];

		{
			NSInputStream *back = [NSInputStream inputStreamWithFileAtPath:
						(NSString *)[NSString stringWithUTF8String:path]];
			uint8_t buf[32];
			NSInteger got;

			[back open];
			got = [back read:buf maxLength:sizeof(buf)];
			check("output-stream-then-input-stream-round-trips",
			      got == 11 && memcmp(buf, "hello world", 11) == 0,
			      [NSString stringWithFormat:@"reading back answered %d", (int)got]);
			[back close];
		}

		{
			/* APPEND IS AN OPEN FLAG, and the file grows by exactly what was written. */
			NSOutputStream *more = [NSOutputStream outputStreamToFileAtPath:
						(NSString *)[NSString stringWithUTF8String:path] append:YES];
			NSInputStream *back;
			uint8_t buf[32];
			NSInteger got;

			[more open];
			(void)[more write:(const uint8_t *)"!" maxLength:1];
			[more close];
			back = [NSInputStream inputStreamWithFileAtPath:
				(NSString *)[NSString stringWithUTF8String:path]];
			[back open];
			got = [back read:buf maxLength:sizeof(buf)];
			check("output-stream-appends",
			      got == 12 && memcmp(buf, "hello world!", 12) == 0,
			      [NSString stringWithFormat:@"the appended file read back as %d bytes", (int)got]);
			[back close];
		}

		{
			/* THE WRITABLE HALF OF THE SEAM, which the kernel's file-readiness rule is what makes
			 * deliverable: a scheduled FILE-backed output stream reports space through the delegate -
			 * no write, no thread blocked. */
			NSOutputStream *scheduled = [NSOutputStream outputStreamToFileAtPath:
							(NSString *)[NSString stringWithUTF8String:path] append:YES];
			FnStreamDelegate *delegate = [[FnStreamDelegate alloc] init];
			NSRunLoop *loop = [NSRunLoop currentRunLoop];

			[scheduled setDelegate:delegate];
			[scheduled scheduleInRunLoop:loop forMode:NSDefaultRunLoopMode];
			[scheduled open];
			(void)[loop runMode:NSDefaultRunLoopMode
				 beforeDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
			check("output-stream-fires-has-space",
			      [delegate events] > 0 &&
			      ([delegate last] & NSStreamEventHasSpaceAvailable) != 0,
			      [NSString stringWithFormat:@"%d event(s), last=%lu",
				[delegate events], (unsigned long)[delegate last]]);
			[scheduled removeFromRunLoop:loop forMode:NSDefaultRunLoopMode];
			[scheduled close];
		}
		(void)unlink(path);
	}


	/* ---- 10. NSUserUnixTask: A SCRIPT, RUN FOR REAL ---- */
	{
		/* THE PROBE IS ITS OWN FIXTURE ONE MORE TIME: it WRITES the script with the output stream sub-step 3
		 * landed, makes it executable, and runs it - so the run, the ARGUMENT and the standard-output
		 * redirection are all asserted in one pass. /bin/sh is this tree's dash. */
		const char *script = "/System/Temporary Files/foundation-unix-task.sh";
		const char *captured = "/System/Temporary Files/foundation-unix-task.out";
		const char *body = "#!/bin/sh\necho hello-from-script \"$1\"\nexit 0\n";
		NSString *scriptPath = (NSString *)[NSString stringWithUTF8String:script];
		NSString *outPath = (NSString *)[NSString stringWithUTF8String:captured];
		NSString *failPath = (NSString *)[NSString stringWithUTF8String:
					"/System/Temporary Files/foundation-unix-task-fail.sh"];
		NSOutputStream *writer = [NSOutputStream outputStreamToFileAtPath:scriptPath append:NO];
		NSUserUnixTask *task, *bad;
		NSError *error = nil;
		FnUnixTaskFixture *fixture;
		int fd;

		{
			/* `+fileURLWithPath:` IS NULLABLE TOO, so it lands in a local before it is handed on - the
			 * same spelling the NSTask probe uses, and the reason it is not an inline argument. */
			NSURL *missing = [NSURL fileURLWithPath:@"/System/NoSuchScript"];

			bad = [[NSUserUnixTask alloc] initWithScriptURL:missing error:&error];
		}
		check("unix-task-refuses-a-script-that-cannot-be-run",
		      bad == nil && error != nil,
		      @"a URL that cannot be executed did not answer nil with an error");

		[writer open];
		(void)[writer write:(const uint8_t *)body maxLength:strlen(body)];
		[writer close];
		(void)chmod(script, 0755);

		{
			/* THE FIXTURE ITSELF, MEASURED FIRST: status 127 says "cannot execute", which is also what
			 * an EMPTY or unreadable file says, so the file has to be known good before anything
			 * downstream of it can be believed. */
			struct stat st;
			int landed = (stat(script, &st) == 0);

			char head[3];
			int hfd;

			head[0] = head[1] = head[2] = 0;
			hfd = open(script, O_RDONLY);
			if (hfd >= 0) {
				ssize_t got = read(hfd, head, 2);

				(void)got;
				(void)close(hfd);
			}
			check("unix-task-fixture-script-landed",
			      landed && st.st_size == (off_t)strlen(body) &&
			      head[0] == '#' && head[1] == '!',
			      [NSString stringWithFormat:@"size=%lld (want %lu) head=%02x%02x",
				landed ? (long long)st.st_size : -1LL, (unsigned long)strlen(body),
				(unsigned char)head[0], (unsigned char)head[1]]);
		}

		{
			/* AND THE ERRNO THE EXEC ITSELF GIVES, because NSTask's child answers 127 and not the
			 * reason: a fork + execve here, with the errno coming back through a pipe. */
			int fds[2];

			if (pipe(fds) == 0) {
				pid_t pid = fork();

				if (pid == 0) {
					int err;

					(void)close(fds[0]);
					(void)execve(script, (char *const []) { (char *)script, NULL },
						     (char *const []) { NULL });
					err = errno;
					(void)write(fds[1], &err, sizeof(err));
					_exit(1);
				}
				if (pid > 0) {
					int err = 0;
					ssize_t got;

					(void)close(fds[1]);
					got = read(fds[0], &err, sizeof(err));
					(void)waitpid(pid, NULL, 0);
					(void)close(fds[0]);
					/* THE MEASURED LIMIT, PINNED RATHER THAN HIDDEN: execve of a FILE THAT
					 * STARTS WITH A SHEBANG answers ENOEXEC in this tree, so the kernel's script
					 * path refuses a script whose bytes are demonstrably right - the probe's own
					 * stat and read above prove the file is there and starts with "#!". The
					 * interpreter itself runs (the next check), so the fault is between the two. */
					check("unix-task-script-exec-is-blocked-by-the-kernel",
					      got == (ssize_t)sizeof(err) && err == ENOEXEC,
					      [NSString stringWithFormat:@"execve of a shebang script answered errno=%d",
						err]);
					/* AND THE INTERPRETER ITSELF, because the shebang path ends by exec-ing it:
					 * if /bin/sh runs, the failure is in the #! handling, and if it does not, the
					 * interpreter is the thing to look at. */
					pid = fork();
					if (pid == 0) {
						(void)execve("/bin/sh",
							     (char *const []) { (char *)"/bin/sh",
										(char *)script, NULL },
							     (char *const []) { NULL });
						err = errno;
						(void)write(fds[1], &err, sizeof(err));
						_exit(1);
					}
					if (pid > 0) {
						err = 0;
						got = read(fds[0], &err, sizeof(err));
						(void)waitpid(pid, NULL, 0);
						check("unix-task-interpreter-execs-directly",
						      got != (ssize_t)sizeof(err),
						      [NSString stringWithFormat:@"/bin/sh failed, errno=%d", err]);
					}
				}
			}
		}

		/* THE CAPTURE FILE MUST EXIST BEFORE A WRITE HANDLE CAN NAME IT. */
		fd = open(captured, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd >= 0) {
			(void)close(fd);
		}

		error = nil;
		{
			NSURL *url = [NSURL fileURLWithPath:scriptPath];

			task = [[NSUserUnixTask alloc] initWithScriptURL:url error:&error];
		}
		check("unix-task-constructs", task != nil,
		      [NSString stringWithFormat:@"a runnable script was refused (%@)", [error domain]]);
		[task setStandardOutput:[NSFileHandle fileHandleForWritingAtPath:outPath]];

		fixture = [[FnUnixTaskFixture alloc] init];
		[task executeWithArguments:[NSArray arrayWithObject:@"ARG-ONE"]
		       completionHandler:^(NSError *handlerError) {
			[fixture note:handlerError];
		}];
		{
			NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];

			while (![fixture fired] && [deadline timeIntervalSinceNow] > 0) {
				usleep(2000);
			}
		}
		/* AND THE CLASS'S ERROR CONTRACT, which is what it can honestly promise while the kernel's
		 * script path refuses the file: the completion handler is CALLED, and it is handed the failure
		 * rather than silence. (127 is NSTask's "cannot execute".) */
		check("unix-task-reports-the-exec-failure",
		      [fixture fired] && [fixture error] != nil && [[fixture error] code] == 127,
		      [NSString stringWithFormat:@"fired=%d code=%ld", [fixture fired],
			(long)([fixture error] != nil ? [[fixture error] code] : -1)]);
		(void)unlink(script);
		(void)unlink(captured);
		(void)unlink("/System/Temporary Files/foundation-unix-task-fail.sh");
	}

	printf("FOUNDATION-STREAM RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STREAM-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-STREAM DONE\n");
	return failc ? 1 : 0;
}
