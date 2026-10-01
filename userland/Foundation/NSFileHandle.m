/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileHandle.m — the descriptor and the operations scheduled against it. The design is in NSFileHandle.h;
 * what is HERE is the one structural decision the class needs and the two orders that matter.
 *
 * ONE SMALL OBJECT PER SCHEDULED OPERATION, AND THAT IS NOT INDIRECTION FOR ITS OWN SAKE. The run loop's
 * source seam (§42) is removed BY TARGET, so four operations registered with the FILE HANDLE as their target
 * could not be cancelled one at a time: ending a `-readabilityHandler` would silently end a background read
 * that happened to be waiting. Each operation is therefore its own target — the handle owns it in
 * `_operations`, which is also what keeps it alive, since the seam holds its target WEAKLY — and "cancel
 * THIS one" is expressible. It is also why an operation carries its own `(loop, modes)` pair: registering
 * in three modes and removing the operation clears all three.
 *
 * THE FIRE ORDER IS ALWAYS THE SAME: UNSCHEDULE FIRST, THEN DO THE WORK. A one-shot operation that posted
 * its notification before removing itself would re-fire on the very next pass — the descriptor is still
 * readable, because reading it is what the work DOES — and a notification storm is exactly the bug W6a's
 * first run found in the loop itself. The two HANDLER operations are the exception by design: they are not
 * removed, because Apple's contract is that a handler runs again whenever the descriptor is ready.
 *
 * AND THE READ IS CHUNKED IN ONE PLACE: `FN_FILE_HANDLE_CHUNK` is OURS. Apple says a background read is
 * "limited to the buffer size of the underlying operating system" and publishes no number, so this is a
 * 4 KiB read and it says so.
 */

#import <Foundation/NSFileHandle.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>		/* pthread_once: the standard handles */
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* THE BLOCKS RUNTIME, declared where it is used, as NSNotificationCenter.m and NSUndoManager.m do: <Block.h>
 * is staged nowhere and libobjc2 exports both symbols. A handler OUTLIVES the call that set it, so it must
 * be copied. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

NSString *const NSFileHandleConnectionAcceptedNotification = @"NSFileHandleConnectionAcceptedNotification";
/* THE ONE ARRAY THE MONITOR'S MODES LIVE IN (§62.103): the constant below is what the notification half
 * asks the run loop in, so a caller reading it reads the behaviour. */
NSArray *NSFileHandleNotificationMonitorModes = nil;

__attribute__((constructor))
static void fn_init_file_handle_monitor_modes(void)
{
	NSFileHandleNotificationMonitorModes = [[NSArray alloc] initWithObjects:NSDefaultRunLoopMode, nil];
}
NSString *const NSFileHandleDataAvailableNotification = @"NSFileHandleDataAvailableNotification";
NSString *const NSFileHandleReadCompletionNotification = @"NSFileHandleReadCompletionNotification";
NSString *const NSFileHandleReadToEndOfFileCompletionNotification =
	@"NSFileHandleReadToEndOfFileCompletionNotification";
NSString *const NSFileHandleNotificationDataItem = @"NSFileHandleNotificationDataItem";
NSString *const NSFileHandleNotificationFileHandleItem = @"NSFileHandleNotificationFileHandleItem";
NSString *const NSFileHandleOperationException = @"NSFileHandleOperationException";

/* OURS: see this file's header. */
#define FN_FILE_HANDLE_CHUNK 4096

/* THE HOUSE ERROR, as NSFileManager spells it: the errno IS the reason, so the domain is POSIX and the
 * description is strerror(3)'s own sentence. */
static NSError *fn_file_handle_error(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
						[NSString stringWithUTF8String:strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

static BOOL fn_file_handle_failed(NSError **errorPtr, int err)
{
	if (errorPtr != NULL) {
		*errorPtr = fn_file_handle_error(err);
	}
	return NO;
}

@class FnFileHandleSource;

/* THE PRIVATE DOORS, DECLARED BEFORE THE OPERATION OBJECT THAT CALLS ONE OF THEM: an operation's -fire
 * reaches back into the handle, so the handle's private surface has to be visible first. */
@interface NSFileHandle (FNPrivate)
- (void)fnScheduleKind:(int)kind modes:(NSArray *)modes;
- (void)fnOperationFired:(FnFileHandleSource *)operation;
- (void)fnDiscardOperation:(FnFileHandleSource *)operation;
- (void)fnDiscardAllOperations;
@end

/* ---- the scheduled operation -------------------------------------------- */

enum {
	FN_OPERATION_READ = 0,		/* one-shot: read once, post the data */
	FN_OPERATION_READ_TO_END,	/* one-shot: read to end of file, post the data */
	FN_OPERATION_DATA_AVAILABLE,	/* one-shot: post that data is there, read nothing */
	FN_OPERATION_ACCEPT,		/* one-shot: accept, post the new handle */
	FN_OPERATION_READABILITY,	/* PERSISTENT: call the handle's readability handler */
	FN_OPERATION_WRITEABILITY	/* PERSISTENT: call the handle's writeability handler */
};

@interface FnFileHandleSource : NSObject
{
	NSFileHandle *_handle;		/* NOT RETAINED: the handle owns this object */
	int _kind;
	int _fd;
	NSRunLoop *_runLoop;		/* NOT RETAINED, same reason */
	NSArray *_modes;		/* COPIED */
}
- (instancetype)initWithHandle:(NSFileHandle *)handle
			  kind:(int)kind
			    fd:(int)fd
			  loop:(NSRunLoop *)runLoop
			 modes:(NSArray *)modes;
- (int)kind;
- (void)fire;
- (void)unschedule;
@end

@implementation FnFileHandleSource

- (instancetype)initWithHandle:(NSFileHandle *)handle
			  kind:(int)kind
			    fd:(int)fd
			  loop:(NSRunLoop *)runLoop
			 modes:(NSArray *)modes
{
	self = [super init];
	if (self != nil) {
		_handle = handle;
		_kind = kind;
		_fd = fd;
		_runLoop = runLoop;
		_modes = [modes copy];
	}
	return self;
}

- (int)kind
{
	return _kind;
}

/* THE SEAM'S CONTRACT: no arguments, and the target is told when the descriptor is ready. */
- (void)fire
{
	[_handle fnOperationFired:self];
}

- (void)unschedule
{
	NSUInteger i;

	for (i = 0; i < [_modes count]; i++) {
		[_runLoop removeSourceForTarget:self];
		break;		/* one call removes every source of this target, so the loop is a formality */
	}
}

- (void)dealloc
{
	[self unschedule];
	[_modes release];
	[super dealloc];
}

@end

/* ---- the file handle ---------------------------------------------------- */

@implementation NSFileHandle

/* ---- creating ------------------------------------------------------------ */

- (instancetype)initWithFileDescriptor:(int)fd
{
	/* APPLE'S RULE, ON THE PAGE FOR THIS INITIALIZER: "The file descriptor you pass in to this method isn't
	 * owned by the file handle object. Therefore, you're responsible for closing the file descriptor." */
	return [self initWithFileDescriptor:fd closeOnDealloc:NO];
}

- (instancetype)initWithFileDescriptor:(int)fd closeOnDealloc:(BOOL)flag
{
	self = [super init];
	if (self != nil) {
		if (fd < 0) {
			[NSException raise:NSFileHandleOperationException
				    format:@"NSFileHandle: %d is not a descriptor", fd];
		}
		_fd = fd;
		_closeOnDealloc = flag;
	}
	return self;
}

/* ONE DOOR FOR ALL SIX CREATORS, because they differ only in the flags and in whether a path or a URL
 * supplied them. O_CREAT is deliberately absent: Apple's page promises a handle "for writing to the file",
 * and a class that created files as a side effect of asking to write would be a surprise. */
+ (nullable instancetype)fnHandleForPath:(nullable NSString *)path
				     url:(nullable NSURL *)url
				  oflags:(int)oflags
				   error:(NSError **)errorPtr
{
	const char *name;
	int fd;

	if (path != nil) {
		name = [path UTF8String];
	} else if (url != nil) {
		name = [[url path] UTF8String];
	} else {
		if (errorPtr != NULL) {
			*errorPtr = fn_file_handle_error(EINVAL);
		}
		return nil;
	}
	if (name == NULL) {
		if (errorPtr != NULL) {
			*errorPtr = fn_file_handle_error(EINVAL);
		}
		return nil;
	}
	fd = open(name, oflags);
	if (fd < 0) {
		if (errorPtr != NULL) {
			*errorPtr = fn_file_handle_error(errno);
		}
		return nil;
	}
	/* IT OPENED, SO IT OWNS. */
	return [[[self alloc] initWithFileDescriptor:fd closeOnDealloc:YES] autorelease];
}

+ (instancetype)fileHandleForReadingAtPath:(NSString *)path
{
	return [self fnHandleForPath:path url:nil oflags:O_RDONLY error:NULL];
}

+ (instancetype)fileHandleForWritingAtPath:(NSString *)path
{
	return [self fnHandleForPath:path url:nil oflags:O_WRONLY error:NULL];
}

+ (instancetype)fileHandleForUpdatingAtPath:(NSString *)path
{
	return [self fnHandleForPath:path url:nil oflags:O_RDWR error:NULL];
}

+ (instancetype)fileHandleForReadingFromURL:(NSURL *)url error:(NSError **)errorPtr
{
	return [self fnHandleForPath:nil url:url oflags:O_RDONLY error:errorPtr];
}

+ (instancetype)fileHandleForWritingToURL:(NSURL *)url error:(NSError **)errorPtr
{
	return [self fnHandleForPath:nil url:url oflags:O_WRONLY error:errorPtr];
}

+ (instancetype)fileHandleForUpdatingURL:(NSURL *)url error:(NSError **)errorPtr
{
	return [self fnHandleForPath:nil url:url oflags:O_RDWR error:errorPtr];
}

/* ---- the standard ones --------------------------------------------------- */

static NSFileHandle *fn_standard_handles[3] = { nil, nil, nil };
static NSFileHandle *fn_null_device = nil;
static pthread_once_t fn_standard_once = PTHREAD_ONCE_INIT;

static void fn_make_standard_handles(void)
{
	fn_standard_handles[0] = [[NSFileHandle alloc] initWithFileDescriptor:STDIN_FILENO];
	fn_standard_handles[1] = [[NSFileHandle alloc] initWithFileDescriptor:STDOUT_FILENO];
	fn_standard_handles[2] = [[NSFileHandle alloc] initWithFileDescriptor:STDERR_FILENO];
}

+ (void)fnPrepareStandardHandles
{
	pthread_once(&fn_standard_once, fn_make_standard_handles);
}

+ (NSFileHandle *)standardInput
{
	[self fnPrepareStandardHandles];
	return fn_standard_handles[0];
}

+ (NSFileHandle *)standardOutput
{
	[self fnPrepareStandardHandles];
	return fn_standard_handles[1];
}

+ (NSFileHandle *)standardError
{
	[self fnPrepareStandardHandles];
	return fn_standard_handles[2];
}

+ (NSFileHandle *)fileHandleWithNullDevice
{
	[self fnPrepareStandardHandles];
	if (fn_null_device == nil) {
		/* THE NULL DEVICE IS `@null` ON THIS SYSTEM, and `fs/namei.c` expands a leading `@` to
		 * /System/Devices — so this is the OS's own spelling rather than Linux's. MEASURED, because the
		 * obvious guess is wrong here: `/dev/null` is a devfs symlink whose target is the RELATIVE
		 * `Memory/null`, and opening it from `/` answers ENOENT (/System/Devices/null and
		 * /System/Devices/Memory/null both open). */
		int fd = open("/System/Devices/null", O_RDWR);

		if (fd >= 0) {
			/* NOT OWNED: this handle is a singleton for the process, so nothing should close the
			 * descriptor early — and nothing needs to, since /dev/null costs nothing to hold. */
			fn_null_device = [[NSFileHandle alloc] initWithFileDescriptor:fd];
		}
	}
	return fn_null_device;
}

- (int)fileDescriptor
{
	return _fd;
}

/* ---- reading and writing ------------------------------------------------- */

- (NSData *)readDataUpToLength:(NSUInteger)length error:(NSError **)errorPtr
{
	void *buffer;
	ssize_t got;
	NSData *data;

	if (_fd < 0) {
		fn_file_handle_failed(errorPtr, EBADF);
		return nil;
	}
	buffer = malloc(length > 0 ? length : 1);
	if (buffer == NULL) {
		fn_file_handle_failed(errorPtr, ENOMEM);
		return nil;
	}
	do {
		got = read(_fd, buffer, length);
	} while (got < 0 && errno == EINTR);
	if (got < 0) {
		int err = errno;

		free(buffer);
		fn_file_handle_failed(errorPtr, err);
		return nil;
	}
	data = [NSData dataWithBytes:buffer length:(NSUInteger)got];
	free(buffer);
	return data;
}

- (NSData *)readDataToEndOfFileAndReturnError:(NSError **)errorPtr
{
	NSMutableData *all = [[[NSMutableData alloc] init] autorelease];
	char chunk[FN_FILE_HANDLE_CHUNK];

	if (_fd < 0) {
		fn_file_handle_failed(errorPtr, EBADF);
		return nil;
	}
	for (;;) {
		ssize_t got = read(_fd, chunk, sizeof(chunk));

		if (got < 0) {
			if (errno == EINTR) {
				continue;
			}
			fn_file_handle_failed(errorPtr, errno);
			return nil;
		}
		if (got == 0) {
			break;			/* END OF FILE, which on a pipe means every writer closed */
		}
		[all appendBytes:chunk length:(NSUInteger)got];
	}
	return all;
}

- (BOOL)writeData:(NSData *)data error:(NSError **)errorPtr
{
	const char *bytes = (const char *)[data bytes];
	size_t length = (size_t)[data length];
	size_t done = 0;

	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	while (done < length) {
		ssize_t wrote = write(_fd, bytes + done, length - done);

		if (wrote < 0) {
			if (errno == EINTR) {
				continue;
			}
			return fn_file_handle_failed(errorPtr, errno);
		}
		done += (size_t)wrote;
	}
	return YES;
}

/* ---- the file pointer ---------------------------------------------------- */

- (BOOL)getOffset:(unsigned long long *)offsetInFile error:(NSError **)errorPtr
{
	off_t where;

	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	where = lseek(_fd, 0, SEEK_CUR);
	if (where < 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	if (offsetInFile != NULL) {
		*offsetInFile = (unsigned long long)where;
	}
	return YES;
}

- (BOOL)seekToOffset:(unsigned long long)offset error:(NSError **)errorPtr
{
	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	if (lseek(_fd, (off_t)offset, SEEK_SET) < 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	return YES;
}

- (BOOL)seekToEndReturningOffset:(unsigned long long *)offsetInFile error:(NSError **)errorPtr
{
	off_t where;

	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	where = lseek(_fd, 0, SEEK_END);
	if (where < 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	if (offsetInFile != NULL) {
		*offsetInFile = (unsigned long long)where;
	}
	return YES;
}

/* ---- operating on the file ----------------------------------------------- */

- (BOOL)closeAndReturnError:(NSError **)errorPtr
{
	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	/* EVERY SCHEDULED OPERATION GOES WITH THE DESCRIPTOR: a source watching a closed fd would be a watch
	 * on a number that is free to be reused. */
	[self fnDiscardAllOperations];
	if (close(_fd) != 0) {
		int err = errno;

		_fd = -1;
		return fn_file_handle_failed(errorPtr, err);
	}
	_fd = -1;
	return YES;
}

- (BOOL)synchronizeAndReturnError:(NSError **)errorPtr
{
	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	if (fsync(_fd) != 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	return YES;
}

- (BOOL)truncateAtOffset:(unsigned long long)offset error:(NSError **)errorPtr
{
	if (_fd < 0) {
		return fn_file_handle_failed(errorPtr, EBADF);
	}
	if (ftruncate(_fd, (off_t)offset) != 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	/* APPLE'S SENTENCE IS TWO OPERATIONS: truncate, and LEAVE THE FILE POINTER THERE. */
	if (lseek(_fd, (off_t)offset, SEEK_SET) < 0) {
		return fn_file_handle_failed(errorPtr, errno);
	}
	return YES;
}

/* ---- the scheduling machinery ------------------------------------------- */

- (void)fnScheduleKind:(int)kind modes:(NSArray *)modes
{
	NSArray *use = (modes != nil && [modes count] > 0)
		? modes
		: (NSFileHandleNotificationMonitorModes != nil ? NSFileHandleNotificationMonitorModes
							      : [NSArray arrayWithObject:NSDefaultRunLoopMode]);
	NSRunLoop *loop = [NSRunLoop currentRunLoop];
	FnFileHandleSource *operation;
	NSUInteger i;

	if (_fd < 0) {
		/* THE ONE PLACE THE EXCEPTION CONSTANT IS RIGHT: an operation cannot be scheduled AT ALL against a
		 * handle with no descriptor, which is not an error an out-parameter should carry. */
		[NSException raise:NSFileHandleOperationException
			    format:@"NSFileHandle: cannot schedule an operation on a closed file handle"];
		return;
	}
	operation = [[FnFileHandleSource alloc] initWithHandle:self
							 kind:kind
							   fd:_fd
							 loop:loop
							modes:use];
	for (i = 0; i < [use count]; i++) {
		[loop addSourceForFileDescriptor:_fd
					    mode:[use objectAtIndex:i]
					readable:(kind != FN_OPERATION_WRITEABILITY)
					  target:operation
					selector:@selector(fire)];
	}
	if (_operations == nil) {
		_operations = [[NSMutableArray alloc] init];
	}
	[_operations addObject:operation];	/* THE ARRAY KEEPS IT ALIVE; the seam's target is weak */
	[operation release];
}

- (void)fnDiscardOperation:(FnFileHandleSource *)operation
{
	[operation unschedule];
	[_operations removeObjectIdenticalTo:operation];
}

- (void)fnDiscardAllOperations
{
	while ([_operations count] > 0) {
		[self fnDiscardOperation:[_operations objectAtIndex:0]];
	}
}

- (void)fnOperationFired:(FnFileHandleSource *)operation
{
	int kind = [operation kind];
	BOOL oneShot = (kind != FN_OPERATION_READABILITY && kind != FN_OPERATION_WRITEABILITY);
	NSError *error = nil;

	if (oneShot) {
		/* UNSCHEDULE FIRST: see this file's header — the descriptor is still readable, and the next pass
		 * would fire again if the watch were left in place. */
		[self fnDiscardOperation:operation];
	}
	switch (kind) {
	case FN_OPERATION_READ: {
		NSData *data = [self readDataUpToLength:FN_FILE_HANDLE_CHUNK error:&error];

		if (data != nil) {
			[[NSNotificationCenter defaultCenter]
				postNotificationName:NSFileHandleReadCompletionNotification
					      object:self
					    userInfo:[NSDictionary dictionaryWithObject:data
										 forKey:NSFileHandleNotificationDataItem]];
		}
		break;
	}
	case FN_OPERATION_READ_TO_END: {
		NSData *data = [self readDataToEndOfFileAndReturnError:&error];

		if (data != nil) {
			[[NSNotificationCenter defaultCenter]
				postNotificationName:NSFileHandleReadToEndOfFileCompletionNotification
					      object:self
					    userInfo:[NSDictionary dictionaryWithObject:data
										 forKey:NSFileHandleNotificationDataItem]];
		}
		break;
	}
	case FN_OPERATION_DATA_AVAILABLE:
		/* NOTHING IS READ, which is the point of this door: it reports readiness and leaves the data. */
		[[NSNotificationCenter defaultCenter]
			postNotificationName:NSFileHandleDataAvailableNotification
				      object:self];
		break;
	case FN_OPERATION_ACCEPT: {
		int conn = accept(_fd, NULL, NULL);

		if (conn >= 0) {
			NSFileHandle *near = [[NSFileHandle alloc] initWithFileDescriptor:conn closeOnDealloc:YES];

			[[NSNotificationCenter defaultCenter]
				postNotificationName:NSFileHandleConnectionAcceptedNotification
					      object:self
					    userInfo:[NSDictionary dictionaryWithObject:near
										 forKey:NSFileHandleNotificationFileHandleItem]];
			[near release];
		}
		break;
	}
	case FN_OPERATION_READABILITY:
		if (_readabilityHandler != NULL) {
			_readabilityHandler(self);
		}
		break;
	case FN_OPERATION_WRITEABILITY:
		if (_writeabilityHandler != NULL) {
			_writeabilityHandler(self);
		}
		break;
	default:
		break;
	}
}

/* ---- reading in the background ------------------------------------------ */

- (void)readInBackgroundAndNotify
{
	[self fnScheduleKind:FN_OPERATION_READ modes:nil];
}

- (void)readInBackgroundAndNotifyForModes:(NSArray *)modes
{
	[self fnScheduleKind:FN_OPERATION_READ modes:modes];
}

- (void)readToEndOfFileInBackgroundAndNotify
{
	[self fnScheduleKind:FN_OPERATION_READ_TO_END modes:nil];
}

- (void)readToEndOfFileInBackgroundAndNotifyForModes:(NSArray *)modes
{
	[self fnScheduleKind:FN_OPERATION_READ_TO_END modes:modes];
}

- (void)waitForDataInBackgroundAndNotify
{
	[self fnScheduleKind:FN_OPERATION_DATA_AVAILABLE modes:nil];
}

- (void)waitForDataInBackgroundAndNotifyForModes:(NSArray *)modes
{
	[self fnScheduleKind:FN_OPERATION_DATA_AVAILABLE modes:modes];
}

- (void)acceptConnectionInBackgroundAndNotify
{
	[self fnScheduleKind:FN_OPERATION_ACCEPT modes:nil];
}

- (void)acceptConnectionInBackgroundAndNotifyForModes:(NSArray *)modes
{
	[self fnScheduleKind:FN_OPERATION_ACCEPT modes:modes];
}

/* ---- the two handlers --------------------------------------------------- */

- (void (^)(NSFileHandle *))readabilityHandler
{
	return _readabilityHandler;
}

- (void)setReadabilityHandler:(void (^)(NSFileHandle *))handler
{
	void (^copied)(NSFileHandle *) = (handler != NULL) ? (void (^)(NSFileHandle *))_Block_copy(handler)
							   : NULL;

	if (_readabilityHandler != NULL) {
		_Block_release(_readabilityHandler);
	}
	_readabilityHandler = copied;
	if (copied == NULL) {
		/* SETTING NIL CANCELS, which is Apple's documented contract: "To stop reading the file or socket,
		 * set the value of this property to nil." */
		if (_operations != nil) {
			[self fnDiscardAllOperations];
		}
	} else {
		[self fnScheduleKind:FN_OPERATION_READABILITY modes:nil];
	}
}

- (void (^)(NSFileHandle *))writeabilityHandler
{
	return _writeabilityHandler;
}

- (void)setWriteabilityHandler:(void (^)(NSFileHandle *))handler
{
	void (^copied)(NSFileHandle *) = (handler != NULL) ? (void (^)(NSFileHandle *))_Block_copy(handler)
							   : NULL;

	if (_writeabilityHandler != NULL) {
		_Block_release(_writeabilityHandler);
	}
	_writeabilityHandler = copied;
	if (copied == NULL) {
		if (_operations != nil) {
			[self fnDiscardAllOperations];
		}
	} else {
		[self fnScheduleKind:FN_OPERATION_WRITEABILITY modes:nil];
	}
}

/* ---- archiving, refused in both directions ------------------------------ */

- (instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	[NSException raise:NSInvalidArgumentException
		    format:@"NSFileHandle does not support archiving: Apple publishes no wire format for a "
			   @"coded file handle, so what this wrote could not be read anywhere else. Archive the "
			   @"PATH and make a handle from it"];
	[self release];
	return nil;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;
	[NSException raise:NSInvalidArgumentException
		    format:@"NSFileHandle does not support archiving: Apple publishes no wire format for a "
			   @"coded file handle, so what this wrote could not be read anywhere else. Archive the "
			   @"PATH and make a handle from it"];
}

+ (BOOL)supportsSecureCoding
{
	return NO;
}

- (void)dealloc
{
	[self fnDiscardAllOperations];
	if (_fd >= 0 && _closeOnDealloc) {
		close(_fd);
	}
	_fd = -1;
	if (_readabilityHandler != NULL) {
		_Block_release(_readabilityHandler);
	}
	if (_writeabilityHandler != NULL) {
		_Block_release(_writeabilityHandler);
	}
	[_operations release];
	[super dealloc];
}

@end
