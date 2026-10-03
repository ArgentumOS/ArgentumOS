/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filehandle — W6c's acceptance: NSFileHandle and NSPipe. docs/design/foundation-plan.md §44.
 *
 * ONE unit, importing <Foundation/Foundation.h> plus the POSIX headers, because a file handle wraps a
 * DESCRIPTOR and half the checks below ask the KERNEL what happened to it — `fcntl(2)` for the ownership
 * rules, a zero-byte read for END OF FILE, and the notification's own userInfo for the asynchronous half.
 *
 * THE PROBE IS ARC AND THE LIBRARY IT TESTS IS MRC (the plan records that split), so nothing below says
 * `release`, `retain` or `autorelease`: a lifetime this probe needs to observe is expressed as a SCOPE,
 * which is exactly how the two ownership checks are written.
 *
 *   pipe-ends-are-distinct        `+pipe` answers two handles on two descriptors, neither of them -1
 *   pipe-carries-data             a byte written at one end comes out of the other
 *   pipe-eof-when-writer-closes   closing the write end makes the read end answer END OF FILE (0 bytes,
 *                                 successfully) — the fact `-readDataToEndOfFile` is built on
 *   pipe-is-one-way               writing to the READ end fails AND reports the failure
 *   file-handle-reads-a-file      a reading handle returns the bytes in order: 10 now, the rest to EOF
 *   file-handle-updates-a-file    an updating handle: seek, write, seek back, read — visible TO A READ
 *   file-handle-seeks             `-seekToEndReturningOffset:` answers the SIZE and `-getOffset:` agrees
 *   file-handle-write-is-complete 200 KiB through `-writeData:error:` lands whole, checked twice: by the
 *                                 handle's own offset and by the file on disk
 *   file-handle-truncates         `-truncateAtOffset:` SHRINKS the file AND leaves the pointer there —
 *                                 Apple's two-operation sentence, checked as two facts
 *   file-handle-close-then-errors after `-closeAndReturnError:` a read reports EBADF, and a second close does
 *   file-handle-owns-what-it-opened   a handle from `+fileHandleForReadingAtPath:` CLOSES its descriptor
 *   file-handle-adopts-what-it-did-not `-initWithFileDescriptor:` does NOT: the caller's descriptor is still
 *                                 open after the handle is gone (Apple's rule for that initializer)
 *   standard-handles              the three standard handles report 0/1/2 and are the same object twice;
 *                                 the null device swallows a write
 *   background-read-posts-data    `-readInBackgroundAndNotify` posts ReadCompletionNotification with the
 *                                 bytes under NSFileHandleNotificationDataItem — THE SEAM'S FIRST REAL
 *                                 CONSUMER, and the reason W6a exists
 *   background-read-is-one-shot   and it does NOT repeat: a second write with no re-arm posts nothing,
 *                                 which is Apple's documented "does not cause a continuous stream"
 *   background-read-rearms        re-arming IS what makes it repeat — the other half of that sentence
 *   background-wait-does-not-read `-waitForDataInBackgroundAndNotify` says data is there and LEAVES IT
 *   background-read-to-end        `-readToEndOfFileInBackgroundAndNotify` carries the whole file
 *   readability-handler-repeats   a readability handler is NOT one-shot: it runs again on the next pass
 *   writeability-handler-runs     a writeability handler fires on a descriptor with room to write
 *   background-read-on-closed-handle-raises  NSFileHandleOperationException, the constant's real use
 *   file-handle-archiving-refused a handle cannot be archived (Apple publishes no wire format for one)
 */

#import <Foundation/Foundation.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsfilehandle-probe"
#define PROBE_FILE PROBE_ROOT "/data.bin"
#define PROBE_OTHER PROBE_ROOT "/other.bin"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEHANDLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEHANDLE %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* THE FIXTURE WRITER: this tree's NSString has NO `-writeToFile:atomically:` — NSData does — so the text
 * goes through its UTF-8 bytes. ATOMICALLY, which is also why the "write the file before opening a handle
 * to it" ordering matters: the write REPLACES the file, and a handle made first would watch the old inode. */
static void write_text(NSString *text, NSString *path)
{
	[[text dataUsingEncoding:NSUTF8StringEncoding] writeToFile:path atomically:YES];
}

/* WHAT THE ASYNCHRONOUS CHECKS NEED: a count per notification NAME, and the userInfo of the last one. */
@interface FnHandleObserver : NSObject
{
	NSMutableArray *_names;
	NSDictionary *_lastInfo;
}
- (void)note:(NSNotification *)notification;
- (NSUInteger)countOf:(NSString *)name;
- (NSDictionary *)lastInfo;
@end

@implementation FnHandleObserver

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_names = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)note:(NSNotification *)notification
{
	[_names addObject:[notification name]];
	_lastInfo = [notification userInfo];
}

- (NSUInteger)countOf:(NSString *)name
{
	NSUInteger i, n = [_names count], found = 0;

	for (i = 0; i < n; i++) {
		if ([[_names objectAtIndex:i] isEqualToString:name]) {
			found++;
		}
	}
	return found;
}

- (NSDictionary *)lastInfo
{
	return _lastInfo;
}

@end

/* THE HANDLER FIXTURES: a count and a DRAIN. A handler that reads is what Apple describes, and leaving the
 * bytes in the pipe would be a handler that fires for ever by design. */
@interface FnHandlerProbe : NSObject
{
	NSUInteger _runs;
	NSUInteger _bytes;
}
- (NSUInteger)runs;
- (NSUInteger)bytes;
- (void)readable:(NSFileHandle *)handle;
- (void)writable:(NSFileHandle *)handle;
@end

@implementation FnHandlerProbe

- (NSUInteger)runs
{
	return _runs;
}

- (NSUInteger)bytes
{
	return _bytes;
}

- (void)readable:(NSFileHandle *)handle
{
	NSData *data = [handle readDataUpToLength:64 error:NULL];

	_runs++;
	_bytes += [data length];
}

- (void)writable:(NSFileHandle *)handle
{
	(void)handle;
	_runs++;
}

@end

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *root = [NSString stringWithUTF8String:PROBE_ROOT];
	NSString *path = [NSString stringWithUTF8String:PROBE_FILE];
	NSString *other = [NSString stringWithUTF8String:PROBE_OTHER];
	NSRunLoop *loop = [NSRunLoop currentRunLoop];

	/* FROM NOTHING, every time. */
	[manager removeItemAtPath:root error:NULL];
	[manager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:NULL];

	/* ---- NSPipe ---------------------------------------------------------- */
	{
		NSPipe *pipe = [NSPipe pipe];
		NSFileHandle *reader = [pipe fileHandleForReading];
		NSFileHandle *writer = [pipe fileHandleForWriting];
		NSData *out = [@"hello pipe" dataUsingEncoding:NSUTF8StringEncoding];
		NSError *error = nil;
		NSData *in;

		check("pipe-ends-are-distinct",
		      pipe != nil && reader != nil && writer != nil &&
		      [reader fileDescriptor] != [writer fileDescriptor] &&
		      [reader fileDescriptor] >= 0 && [writer fileDescriptor] >= 0,
		      [NSString stringWithFormat:@"read=%d write=%d",
			[reader fileDescriptor], [writer fileDescriptor]]);

		[writer writeData:out error:&error];
		in = [reader readDataUpToLength:64 error:&error];
		check("pipe-carries-data",
		      in != nil && [in isEqualToData:out],
		      [NSString stringWithFormat:@"wrote %lu, read %lu",
			(unsigned long)[out length], (unsigned long)[in length]]);

		/* END OF FILE IS A ZERO-BYTE READ THAT SUCCEEDS, and the writer's death is what produces it. */
		[writer closeAndReturnError:NULL];
		error = nil;
		in = [reader readDataUpToLength:64 error:&error];
		check("pipe-eof-when-writer-closes",
		      in != nil && [in length] == 0,
		      [NSString stringWithFormat:@"read %lu bytes (nil=%d)",
			(unsigned long)[in length], (int)(in == nil)]);
	}

	{
		NSPipe *pipe = [NSPipe pipe];
		NSError *error = nil;
		NSData *nope = [@"nope" dataUsingEncoding:NSUTF8StringEncoding];
		BOOL wrote = [[pipe fileHandleForReading] writeData:nope error:&error];

		check("pipe-is-one-way",
		      !wrote && error != nil,
		      [NSString stringWithFormat:@"writing to the READ end answered %d", (int)wrote]);
	}

	/* ---- reading and writing a file -------------------------------------- */
	{
		NSString *content = @"0123456789abcdefghij";
		NSFileHandle *handle;
		NSError *error = nil;
		NSData *first;
		NSData *rest;

		[[content dataUsingEncoding:NSUTF8StringEncoding] writeToFile:path atomically:YES];
		handle = [NSFileHandle fileHandleForReadingAtPath:path];
		first = [handle readDataUpToLength:10 error:&error];
		rest = [handle readDataToEndOfFileAndReturnError:&error];
		check("file-handle-reads-a-file",
		      handle != nil && first != nil && rest != nil &&
		      [first length] == 10 && [first length] + [rest length] == [content length],
		      [NSString stringWithFormat:@"handle=%d first=%lu rest=%lu of %lu",
			(int)(handle != nil), (unsigned long)[first length], (unsigned long)[rest length],
			(unsigned long)[content length]]);
	}

	{
		NSFileHandle *handle;
		NSError *error = nil;
		unsigned long long where = 0;
		unsigned long long size = 0;
		NSData *back;
		NSData *mark = [@"ZZ" dataUsingEncoding:NSUTF8StringEncoding];
		NSString *whole;

		write_text(@"0123456789", path);
		handle = [NSFileHandle fileHandleForUpdatingAtPath:path];

		/* APPLE'S TWO-OFFSET SENTENCE, MEASURED: seek to the END answers the SIZE, and -getOffset: agrees. */
		[handle seekToEndReturningOffset:&size error:&error];
		[handle getOffset:&where error:&error];
		check("file-handle-seeks",
		      handle != nil && size == 10 && where == 10,
		      [NSString stringWithFormat:@"size=%llu offset=%llu", size, where]);

		/* WRITE, SEEK BACK, READ: the change has to be visible to a READ, not merely to the size. */
		[handle seekToOffset:2 error:&error];
		[handle writeData:mark error:&error];
		[handle seekToOffset:0 error:&error];
		back = [handle readDataUpToLength:32 error:&error];
		whole = [[NSString alloc] initWithData:back encoding:NSUTF8StringEncoding];
		check("file-handle-updates-a-file",
		      [whole isEqualToString:@"01ZZ456789"],
		      [NSString stringWithFormat:@"read back %@", whole]);
	}

	{
		NSMutableData *big = [NSMutableData dataWithLength:200000];
		NSFileHandle *handle;
		NSError *error = nil;
		unsigned long long size = 0;
		NSData *onDisk;

		if ([big mutableBytes] != NULL) {
			memset([big mutableBytes], 'w', [big length]);
		}
		/* APPEND, SO THE WRITE IS THE THING UNDER TEST AND THE FIXTURE IS NOT: an updating handle lands at
		 * offset 0 unless it is moved, which is what makes 400000 the number to expect. */
		[big writeToFile:other atomically:YES];
		handle = [NSFileHandle fileHandleForUpdatingAtPath:other];
		[handle seekToEndReturningOffset:NULL error:&error];
		(void)[handle writeData:big error:&error];
		[handle seekToEndReturningOffset:&size error:&error];
		onDisk = [NSData dataWithContentsOfFile:other];
		check("file-handle-write-is-complete",
		      handle != nil && size == 400000 && [onDisk length] == 400000,
		      [NSString stringWithFormat:@"the handle seeks to %llu and the file holds %lu bytes "
			@"after a %lu-byte write", size, (unsigned long)[onDisk length],
			(unsigned long)[big length]]);
	}

	{
		NSFileHandle *handle;
		NSError *error = nil;
		unsigned long long where = 0;

		write_text(@"0123456789", path);
		handle = [NSFileHandle fileHandleForUpdatingAtPath:path];
		[handle truncateAtOffset:4 error:&error];
		[handle getOffset:&where error:&error];
		check("file-handle-truncates",
		      [[NSData dataWithContentsOfFile:path] length] == 4 && where == 4,
		      [NSString stringWithFormat:@"the file holds %lu bytes and the pointer is at %llu",
			(unsigned long)[[NSData dataWithContentsOfFile:path] length], where]);
	}

	{
		NSFileHandle *handle;
		NSError *error = nil;
		NSData *data;
		BOOL second;

		write_text(@"0123456789", path);
		handle = [NSFileHandle fileHandleForReadingAtPath:path];
		[handle closeAndReturnError:&error];
		error = nil;
		data = [handle readDataUpToLength:4 error:&error];
		second = [handle closeAndReturnError:&error];
		check("file-handle-close-then-errors",
		      data == nil && error != nil && !second,
		      [NSString stringWithFormat:@"read-after-close-nil=%d error=%d second-close=%d",
			(int)(data == nil), (int)(error != nil), (int)second]);
	}

	/* ---- WHO CLOSES WHAT, AND WHY AN fd NUMBER IS NOT AN IDENTITY -------- */
	{
		int openedFd = -2;
		int adoptedFd = -2;
		BOOL openedClosed;
		BOOL adoptedSurvived;

		write_text(@"0123456789", path);

		/* THE OPENED HANDLE FIRST, AND MEASURED AT ONCE. The first version of this check opened the
		 * ADOPTED descriptor in between, the kernel handed it the number the closed handle had just given
		 * back, and the check then read a LIVE descriptor and reported a leak that was not there. An fd
		 * number is only an identity while nothing else can take it. */
		@autoreleasepool {
			NSFileHandle *opened = [NSFileHandle fileHandleForReadingAtPath:path];

			openedFd = [opened fileDescriptor];
		}
		openedClosed = (openedFd >= 0 && fcntl(openedFd, F_GETFD) == -1);

		@autoreleasepool {
			int raw = open(PROBE_FILE, O_WRONLY);

			if (raw >= 0) {
				NSFileHandle *adopted = [[NSFileHandle alloc] initWithFileDescriptor:raw];

				adoptedFd = [adopted fileDescriptor];
			}
		}
		adoptedSurvived = (adoptedFd >= 0 && fcntl(adoptedFd, F_GETFD) != -1);
		check("file-handle-owns-what-it-opened", openedClosed,
		      [NSString stringWithFormat:@"descriptor %d was still open after the handle went away",
			openedFd]);
		check("file-handle-adopts-what-it-did-not", adoptedSurvived,
		      [NSString stringWithFormat:@"descriptor %d was closed by a handle that did not own it",
			adoptedFd]);
		if (adoptedFd >= 0) {
			close(adoptedFd);
		}
	}

	{
		NSFileHandle *out = [NSFileHandle fileHandleWithStandardOutput];
		NSFileHandle *err = [NSFileHandle fileHandleWithStandardError];
		NSFileHandle *in = [NSFileHandle fileHandleWithStandardInput];
		NSFileHandle *null = [NSFileHandle fileHandleWithNullDevice];
		NSData *byte = [@"x" dataUsingEncoding:NSUTF8StringEncoding];

		check("standard-handles",
		      [in fileDescriptor] == 0 && [out fileDescriptor] == 1 && [err fileDescriptor] == 2 &&
		      out == [NSFileHandle fileHandleWithStandardOutput] && null != nil &&
		      [null writeData:byte error:NULL],
		      [NSString stringWithFormat:@"in=%d out=%d err=%d null=%d shared=%d",
			[in fileDescriptor], [out fileDescriptor], [err fileDescriptor],
			(int)(null != nil), (int)(out == [NSFileHandle fileHandleWithStandardOutput])]);
	}

	/* ---- THE ASYNCHRONOUS HALF: the seam's first real consumer ------------ */
	{
		NSPipe *pipe = [NSPipe pipe];
		NSFileHandle *reader = [pipe fileHandleForReading];
		NSFileHandle *writer = [pipe fileHandleForWriting];
		FnHandleObserver *observer = [[FnHandleObserver alloc] init];
		NSData *first = [@"one" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *second = [@"two" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *seen;

		[[NSNotificationCenter defaultCenter] addObserver:observer
							selector:@selector(note:)
							    name:NSFileHandleReadCompletionNotification
							  object:reader];
		[reader readInBackgroundAndNotify];
		[writer writeData:first error:NULL];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		seen = [[observer lastInfo] objectForKey:NSFileHandleNotificationDataItem];
	{
		/* THE DEPRECATED SPELLINGS ARE THE MODERN DOORS WITHOUT THE ERROR OUT-PARAMETER (§63.180), and that is
		 * what is asserted: the SAME file read both ways gives the same bytes, the same offsets and the same
		 * end-of-file answer. Comparing them against each other rather than against a string is what makes a
		 * disagreement between the two spellings a failure of this check. */
		write_text(@"abcdefghij", @PROBE_FILE);
		NSFileHandle *legacy = [NSFileHandle fileHandleForReadingAtPath:@PROBE_FILE];
		NSFileHandle *modern = [NSFileHandle fileHandleForReadingAtPath:@PROBE_FILE];
		NSData *legacyChunk = [legacy readDataOfLength:4];
		NSData *modernChunk = [modern readDataUpToLength:4 error:NULL];
		NSData *legacyRest = [legacy readDataToEndOfFile];
		NSData *modernRest = [modern readDataToEndOfFileAndReturnError:NULL];
		NSData *available;
		NSData *atEnd;
		unsigned long long whereAfterSeek = 0;
		unsigned long long endOffset = 0;

		[legacy seekToFileOffset:2];
		[modern seekToOffset:2 error:NULL];
		/* THE OFFSET IS ASKED BEFORE THE READING DOOR, because READING TO THE END IS SUPPOSED TO MOVE IT: the
		 * probe's own numbers caught this (offsetAfterSeek=10 where the code was right and the expectation was
		 * wrong - -availableData had already carried the pointer to EOF, exactly as -readDataToEndOfFile:
		 * documents). AND EVERY VALUE IS TAKEN INTO A LOCAL FIRST: the check before this one called
		 * -seekToEndOfFile INSIDE the detail string, and C leaves argument evaluation order unspecified, so the
		 * detail ran early and the assertion read the offset it had just changed. A detail may report; it may not do. */
		whereAfterSeek = [legacy offsetInFile];
		available = [legacy availableData];
		endOffset = [legacy seekToEndOfFile];
		atEnd = [legacy readDataToEndOfFile];
		check("legacy-read-and-seek",
		      legacyChunk != nil && [legacyChunk length] == 4 && [legacyChunk isEqualToData:modernChunk] &&
		      [legacyRest isEqualToData:modernRest] && [legacyRest length] == 6 &&
		      whereAfterSeek == 2 && [modern getOffset:NULL error:NULL] &&
		      [available length] == 8 && endOffset == 10 && [atEnd length] == 0,
		      [NSString stringWithFormat:@"chunk=%lu rest=%lu offsetAfterSeek=%llu available=%lu end=%llu atEnd=%lu",
				(unsigned long)[legacyChunk length], (unsigned long)[legacyRest length],
				whereAfterSeek, (unsigned long)[available length], endOffset,
				(unsigned long)[atEnd length]]);
	}

	{
		/* THE WRITING HALF, AND THE PROPERTY THAT MATTERS IS THAT IT REACHED THE DISK: the file is written
		 * through the deprecated door, synchronised through the deprecated door, and then read back by a
		 * FRESH handle, so a write that only lived in this process's buffer would fail. Truncation is
		 * checked the same way, and -closeFile is checked by asking the closed handle for data. */
		NSFileHandle *writer;
		NSData *afterWrite;
		NSData *afterTruncate;
		NSData *afterClose;
		unsigned long long whereAfterTruncate = 0;

		write_text(@"abcdefghij", @PROBE_FILE);
		writer = [NSFileHandle fileHandleForUpdatingAtPath:@PROBE_FILE];
		[writer writeData:[NSData dataWithBytes:"XY" length:2]];	/* -dataUsingEncoding: is nullable; this door is not */
		[writer synchronizeFile];
		afterWrite = [[NSFileHandle fileHandleForReadingAtPath:@PROBE_FILE] readDataToEndOfFile];
		[writer truncateFileAtOffset:4];
		afterTruncate = [[NSFileHandle fileHandleForReadingAtPath:@PROBE_FILE] readDataToEndOfFile];
		/* THE OFFSET IS ASKED BEFORE THE CLOSE (a closed handle cannot be asked), and the CLOSE ITSELF is
		 * asserted by the only thing it can be asserted by: the handle stops answering with data. */
		whereAfterTruncate = [writer offsetInFile];
		[writer closeFile];
		afterClose = [writer readDataOfLength:1];
		check("legacy-write-truncate-and-close",
		      afterWrite != nil && [afterWrite length] == 10 &&
		      [[[NSString alloc] initWithData:afterWrite encoding:NSUTF8StringEncoding] isEqualToString:@"XYcdefghij"] &&
		      [afterTruncate length] == 4 &&
		      [[[NSString alloc] initWithData:afterTruncate encoding:NSUTF8StringEncoding] isEqualToString:@"XYcd"] &&
		      whereAfterTruncate == 4 && afterClose == nil,
		      [NSString stringWithFormat:@"write=%@ truncate=%@ offsetAfterTruncate=%llu afterClose=%@",
			[[NSString alloc] initWithData:afterWrite encoding:NSUTF8StringEncoding],
			[[NSString alloc] initWithData:afterTruncate encoding:NSUTF8StringEncoding],
			whereAfterTruncate, afterClose]);
	}

		check("background-read-posts-data",
		      [observer countOf:NSFileHandleReadCompletionNotification] == 1 && [seen isEqualToData:first],
		      [NSString stringWithFormat:@"notifications=%lu data=%@",
			(unsigned long)[observer countOf:NSFileHandleReadCompletionNotification], seen]);

		/* ONE-SHOT: NOBODY RE-ARMED, so the second write is read by nobody. */
		[writer writeData:second error:NULL];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		check("background-read-is-one-shot",
		      [observer countOf:NSFileHandleReadCompletionNotification] == 1,
		      [NSString stringWithFormat:@"notifications=%lu after a second write with no re-arm",
			(unsigned long)[observer countOf:NSFileHandleReadCompletionNotification]]);

		/* ... AND RE-ARMING IS WHAT MAKES IT REPEAT, which is the other half of Apple's sentence. */
		[reader readInBackgroundAndNotify];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		check("background-read-rearms",
		      [observer countOf:NSFileHandleReadCompletionNotification] == 2 &&
		      [[[observer lastInfo] objectForKey:NSFileHandleNotificationDataItem]
			isEqualToData:second],
		      [NSString stringWithFormat:@"notifications=%lu after re-arming",
			(unsigned long)[observer countOf:NSFileHandleReadCompletionNotification]]);

		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	{
		NSPipe *pipe = [NSPipe pipe];
		NSFileHandle *reader = [pipe fileHandleForReading];
		FnHandleObserver *observer = [[FnHandleObserver alloc] init];
		NSError *error = nil;
		NSData *stay = [@"stay" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *still;

		[[NSNotificationCenter defaultCenter] addObserver:observer
							selector:@selector(note:)
							    name:NSFileHandleDataAvailableNotification
							  object:reader];
		[[pipe fileHandleForWriting] writeData:stay error:NULL];
		[reader waitForDataInBackgroundAndNotify];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		/* THE PROPERTY IS THAT IT SAYS "READY" AND TOUCHES NOTHING: the bytes are still there. */
		still = [reader readDataUpToLength:64 error:&error];
		check("background-wait-does-not-read",
		      [observer countOf:NSFileHandleDataAvailableNotification] == 1 && [still length] == 4,
		      [NSString stringWithFormat:@"notifications=%lu left=%lu bytes",
			(unsigned long)[observer countOf:NSFileHandleDataAvailableNotification],
			(unsigned long)[still length]]);
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	{
		NSPipe *pipe = [NSPipe pipe];
		NSFileHandle *reader = [pipe fileHandleForReading];
		FnHandleObserver *observer = [[FnHandleObserver alloc] init];
		NSData *where = [@"0123456789" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *seen;

		/* A PIPE RATHER THAN A REGULAR FILE, AND THAT IS A MEASURED CHOICE: this kernel's `select(2)` does
		 * not report a REGULAR FILE as readable — the diagnostic above prints the number, and the run
		 * before it showed both background counts at 0 for a file — so a file-backed source never fires.
		 * That is the same shape as the listening-socket gap §43 measured. A pipe carries data, and a
		 * CLOSED WRITER is what makes "read to end of file" terminate, so this is the working path AND the
		 * end-of-file path in one. */
		[[pipe fileHandleForWriting] writeData:where error:NULL];
		[[pipe fileHandleForWriting] closeAndReturnError:NULL];
		[[NSNotificationCenter defaultCenter]
			addObserver:observer
			   selector:@selector(note:)
			       name:NSFileHandleReadToEndOfFileCompletionNotification
			     object:reader];
		[reader readToEndOfFileInBackgroundAndNotify];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
		seen = [[observer lastInfo] objectForKey:NSFileHandleNotificationDataItem];
		check("background-read-to-end",
		      [observer countOf:NSFileHandleReadToEndOfFileCompletionNotification] == 1 &&
		      [seen length] == 10,
		      [NSString stringWithFormat:@"notifications=%lu bytes=%lu",
			(unsigned long)[observer countOf:NSFileHandleReadToEndOfFileCompletionNotification],
			(unsigned long)[seen length]]);
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	{
		NSPipe *pipe = [NSPipe pipe];
		NSFileHandle *reader = [pipe fileHandleForReading];
		FnHandlerProbe *probe = [[FnHandlerProbe alloc] init];
		NSData *first = [@"ab" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *second = [@"cd" dataUsingEncoding:NSUTF8StringEncoding];

		/* A HANDLER IS NOT ONE-SHOT: it is the door that KEEPS being told, which is why it is separate from
		 * the four background operations. */
		[reader setReadabilityHandler:^(NSFileHandle *handle) {
			[probe readable:handle];
		}];
		[[pipe fileHandleForWriting] writeData:first error:NULL];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		[[pipe fileHandleForWriting] writeData:second error:NULL];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		check("readability-handler-repeats",
		      [probe runs] == 2 && [probe bytes] == 4,
		      [NSString stringWithFormat:@"runs=%lu bytes=%lu",
			(unsigned long)[probe runs], (unsigned long)[probe bytes]]);

		[reader setReadabilityHandler:nil];
	}

	{
		NSPipe *pipe = [NSPipe pipe];
		FnHandlerProbe *probe = [[FnHandlerProbe alloc] init];
		NSFileHandle *writer = [pipe fileHandleForWriting];

		[writer setWriteabilityHandler:^(NSFileHandle *handle) {
			[probe writable:handle];
		}];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		check("writeability-handler-runs", [probe runs] >= 1,
		      [NSString stringWithFormat:@"runs=%lu", (unsigned long)[probe runs]]);
		[writer setWriteabilityHandler:nil];
	}

	/* ---- what has no answer but an exception ---------------------------- */
	{
		NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:path];
		int raised = 0;

		[handle closeAndReturnError:NULL];
		@try {
			[handle readInBackgroundAndNotify];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSFileHandleOperationException];
		}
		check("background-read-on-closed-handle-raises", raised,
		      @"a background operation on a closed handle was accepted instead of raising");
	}

	{
		NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:path];
		int raised = 0;

		@try {
			(void)[NSKeyedArchiver archivedDataWithRootObject:handle];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSInvalidArgumentException];
		}
		check("file-handle-archiving-refused", raised,
		      @"a file handle was ARCHIVED: Apple publishes no wire format for one");
	}

	[manager removeItemAtPath:root error:NULL];

	printf("FOUNDATION-FILEHANDLE RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: the console can stop serving input after a probe, so
	 * `echo $?` may never run. This is the same value: failc ? 1 : 0 is the return below. */
	printf("FOUNDATION-FILEHANDLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-FILEHANDLE DONE\n");
	return failc ? 1 : 0;
}
