/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOutputStream.m — writing, to memory, to a buffer or to a descriptor. The design is in NSOutputStream.h.
 *
 * MRC, LIKE THE REST OF THIS LIBRARY: what this class stores it retains, and -dealloc releases it. The
 * CALLER'S buffer is the one thing here that is neither retained nor freed.
 */

#import <Foundation/NSOutputStream.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

/* THE HOUSE SPELLING, as NSFileHandle and NSFileManager have it: a domain is a string literal here, because
 * this tree declares NSErrorDomain as a typedef and not one constant per domain. */
static NSError *fn_out_error(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:nil];
}

static int fn_out_writable(int fd)
{
	struct pollfd p;

	p.fd = fd;
	p.events = POLLOUT;
	p.revents = 0;
	return poll(&p, 1, 0) > 0;
}

@interface NSOutputStream (FNSocketDescriptor)
- (instancetype)fnInitWithSocketDescriptor:(int)fd;
@end

@implementation NSOutputStream

+ (nullable instancetype)outputStreamToMemory
{
	return [[[self alloc] initToMemory] autorelease];
}

+ (nullable instancetype)outputStreamToBuffer:(uint8_t *)buffer capacity:(NSUInteger)capacity
{
	return [[[self alloc] initToBuffer:buffer capacity:capacity] autorelease];
}

+ (nullable instancetype)outputStreamToFileAtPath:(NSString *)path append:(BOOL)shouldAppend
{
	return [[[self alloc] initToFileAtPath:path append:shouldAppend] autorelease];
}

+ (nullable instancetype)outputStreamWithURL:(NSURL *)url append:(BOOL)shouldAppend
{
	return [[[self alloc] initWithURL:url append:shouldAppend] autorelease];
}

- (nullable instancetype)initToMemory
{
	self = [super init];
	if (self != nil) {
		_memory = [[NSMutableData alloc] init];
		_fd = -1;
	}
	return self;
}

- (nullable instancetype)initToBuffer:(uint8_t *)buffer capacity:(NSUInteger)capacity
{
	self = [super init];
	if (self != nil) {
		if (buffer == NULL || capacity == 0) {
			[self release];
			return nil;
		}
		_buffer = buffer;		/* THE CALLER'S MEMORY: borrowed, and never freed here */
		_capacity = capacity;
		_fd = -1;
	}
	return self;
}

- (nullable instancetype)initToFileAtPath:(NSString *)path append:(BOOL)shouldAppend
{
	self = [super init];
	if (self != nil) {
		if (path == nil) {
			[self release];
			return nil;
		}
		_path = [path retain];		/* OPENED AT -open, Apple's rule for a stream */
		_append = shouldAppend;
		_fd = -1;
	}
	return self;
}

- (nullable instancetype)initWithURL:(NSURL *)url append:(BOOL)shouldAppend
{
	NSString *path = [url path];

	self = [super init];
	if (self != nil) {
		if (path == nil) {
			[self release];
			return nil;
		}
		_path = [path retain];
		_append = shouldAppend;
		_fd = -1;
	}
	return self;
}

- (void)dealloc
{
	if (_fd >= 0) {
		(void)close(_fd);
		_fd = -1;
	}
	[_memory release];
	[_path release];
	[super dealloc];
}

/* ---- opening and closing ---- */

- (void)open
{
	if ([self streamStatus] != NSStreamStatusNotOpen) {
		return;				/* Apple: a stream opens ONCE */
	}
	[self fnStreamSetStatus:NSStreamStatusOpening error:nil];
	if (_path == nil) {
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];	/* memory or a caller's buffer */
		return;
	}
	/* -UTF8String, not -fileSystemRepresentation: the latter is declared and not implemented here. */
	_fd = open([_path UTF8String],
		   O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC), 0666);
	if (_fd < 0) {
		int err = errno;

		[self fnStreamSetStatus:NSStreamStatusError error:fn_out_error(err)];
		return;
	}
	[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
}

- (void)close
{
	[super close];
	if (_fd >= 0) {
		(void)close(_fd);
		_fd = -1;
	}
	if ([self streamStatus] != NSStreamStatusError) {
		[self fnStreamSetStatus:NSStreamStatusClosed error:nil];
	}
}

/* ---- the substream's answers to the head ---- */

- (int)fnStreamDescriptor
{
	return _fd;				/* -1 for memory and buffers, which have no descriptor to watch */
}

- (BOOL)fnStreamWatchesReadable
{
	return NO;				/* AN OUTPUT STREAM WATCHES WRITABILITY, so the base fires has-space */
}

/* ---- writing ---- */

- (NSInteger)write:(const uint8_t *)buffer maxLength:(NSUInteger)len
{
	NSUInteger taken;

	if (buffer == NULL) {
		return -1;
	}
	if ([self streamStatus] != NSStreamStatusOpen) {
		[self fnStreamSetStatus:NSStreamStatusError error:fn_out_error(EBADF)];
		return -1;
	}
	if (len == 0) {
		return 0;
	}
	[self fnStreamSetStatus:NSStreamStatusWriting error:nil];
	if (_memory != nil) {
		[_memory appendBytes:buffer length:len];
		_written += len;
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
		return (NSInteger)len;		/* A MEMORY STREAM GROWS TO FIT: it never comes up short */
	}
	if (_buffer != NULL) {
		/* THE CALLER'S CAPACITY IS THE LIMIT, and answering 0 when it is full is the honest answer rather
		 * than an error: the buffer has no room and the caller owns how much it has. */
		taken = (_written < _capacity) ? (_capacity - _written) : 0;
		if (taken > len) {
			taken = len;
		}
		if (taken > 0) {
			memcpy(_buffer + _written, buffer, taken);
			_written += taken;
		}
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
		return (NSInteger)taken;
	}
	{
		ssize_t put = write(_fd, buffer, len);

		if (put < 0) {
			int err = errno;

			[self fnStreamSetStatus:NSStreamStatusError error:fn_out_error(err)];
			return -1;
		}
		_written += (NSUInteger)put;
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
		return (NSInteger)put;
	}
}

- (BOOL)hasSpaceAvailable
{
	if (_memory != nil) {
		return YES;			/* a growing stream always has room */
	}
	if (_buffer != NULL) {
		return _written < _capacity;
	}
	if (_fd < 0) {
		return NO;
	}
	return fn_out_writable(_fd) ? YES : NO;
}

/* ---- the two property keys this library ACTS on ---- */

- (nullable id)propertyForKey:(NSStreamPropertyKey)key
{
	if ([key isEqualToString:NSStreamDataWrittenToMemoryStreamKey]) {
		/* APPLE'S CONTRACT FOR THIS KEY ON AN OUTPUT STREAM: the data written, which exists while the
		 * destination is memory or a caller's buffer. A file's bytes are in the file, so nil is the
		 * honest answer rather than a second copy of them. */
		if (_memory != nil) {
			return _memory;
		}
		if (_buffer != NULL) {
			return [NSData dataWithBytes:_buffer length:_written];
		}
		return nil;
	}
	if ([key isEqualToString:NSStreamFileCurrentOffsetKey]) {
		if (_buffer != NULL || _memory != nil) {
			return [NSNumber numberWithUnsignedInteger:_written];
		}
		if (_fd >= 0) {
			off_t where = lseek(_fd, 0, SEEK_CUR);

			return where < 0 ? nil : [NSNumber numberWithLongLong:(long long)where];
		}
		return nil;
	}
	return [super propertyForKey:key];
}

- (BOOL)setProperty:(nullable id)property forKey:(NSStreamPropertyKey)key
{
	if ([key isEqualToString:NSStreamFileCurrentOffsetKey]) {
		NSNumber *where = property;

		if (where == nil || ![where isKindOfClass:[NSNumber class]]) {
			return NO;
		}
		if (_fd >= 0) {
			return lseek(_fd, (off_t)[where longLongValue], SEEK_SET) >= 0 ? YES : NO;
		}
		if (_buffer != NULL || _memory != nil) {
			long long offset = [where longLongValue];

			if (offset < 0) {
				return NO;
			}
			if (_memory != nil) {
				if ((unsigned long long)offset > [_memory length]) {
					return NO;
				}
				[_memory setLength:(NSUInteger)offset];
				_written = (NSUInteger)offset;
				return YES;
			}
			if ((unsigned long long)offset > _capacity) {
				return NO;
			}
			_written = (NSUInteger)offset;
			return YES;
		}
		return NO;
	}
	return [super setProperty:property forKey:key];
}


- (instancetype)fnInitWithSocketDescriptor:(int)fd
{
	/* §63.232: TAKES OWNERSHIP OF `fd` — -dealloc closes it, which is exactly why a caller that wants two
	 * streams over ONE socket duplicates the descriptor first. */
	self = [super init];
	if (self != nil) {
		_fd = fd;
	}
	return self;
}
@end
