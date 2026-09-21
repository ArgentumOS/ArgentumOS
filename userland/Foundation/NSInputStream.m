/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInputStream.m — reading, from data or from a descriptor. The design is in NSInputStream.h.
 *
 * MRC, LIKE THE REST OF THIS LIBRARY: what this class stores it retains, and -dealloc releases it. Nothing
 * here is __weak and nothing is ARC-owned.
 */

#import <Foundation/NSInputStream.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* THE TWO QUESTIONS A DESCRIPTOR ANSWERS ABOUT READINESS, and they are different questions: `poll` says a
 * read WOULD NOT BLOCK, and FIONREAD says whether anything is there. Ready-and-empty is END OF STREAM, and
 * that distinction is the whole of asynchronous reading. */
static int fn_descriptor_ready(int fd)
{
	struct pollfd p;

	p.fd = fd;
	p.events = POLLIN;
	p.revents = 0;
	return poll(&p, 1, 0) > 0;
}

static int fn_descriptor_empty(int fd)
{
	int available = 0;

	if (ioctl(fd, FIONREAD, &available) != 0) {
		return 0;		/* cannot tell: treat it as data rather than inventing an end */
	}
	return available == 0;
}

/* THE HOUSE SPELLING, as NSFileHandle and NSFileManager have it: the domain is a STRING literal, because
 * this tree declares NSErrorDomain as a typedef and not one constant per domain (NSFileHandle.h:55 says so). */
static NSError *fn_stream_error(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:nil];
}

@implementation NSInputStream

+ (nullable instancetype)inputStreamWithData:(NSData *)data
{
	return [[[self alloc] initWithData:data] autorelease];
}

+ (nullable instancetype)inputStreamWithFileAtPath:(NSString *)path
{
	return [[[self alloc] initWithFileAtPath:path] autorelease];
}

+ (nullable instancetype)inputStreamWithURL:(NSURL *)url
{
	return [[[self alloc] initWithURL:url] autorelease];
}

- (nullable instancetype)initWithData:(NSData *)data
{
	self = [super init];
	if (self != nil) {
		if (data == nil) {
			[self release];
			return nil;
		}
		_data = [data retain];
		_fd = -1;
	}
	return self;
}

- (nullable instancetype)initWithFileAtPath:(NSString *)path
{
	self = [super init];
	if (self != nil) {
		if (path == nil) {
			[self release];
			return nil;
		}
		_path = [path retain];		/* OPENED AT -open, because Apple opens the resource at open time */
		_fd = -1;
	}
	return self;
}

- (nullable instancetype)initWithURL:(NSURL *)url
{
	NSString *path = [url path];

	self = [super init];
	if (self != nil) {
		if (path == nil) {
			[self release];
			return nil;
		}
		_path = [path retain];
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
	[_data release];
	[_path release];
	[super dealloc];
}

/* ---- opening and closing the source ---- */

- (void)open
{
	if ([self streamStatus] != NSStreamStatusNotOpen) {
		return;				/* Apple: a stream opens ONCE */
	}
	[self fnStreamSetStatus:NSStreamStatusOpening error:nil];
	if (_data != nil) {
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
		return;
	}
	if (_path == nil) {
		[self fnStreamSetStatus:NSStreamStatusError
				  error:fn_stream_error(EBADF)];
		return;
	}
	/* -UTF8String, NOT -fileSystemRepresentation: the latter is DECLARED and not implemented in this
	 * tree (it aborts), and -UTF8String is what NSTask already execs through. The buffer is borrowed and
	 * is used here and not kept. */
	_fd = open([_path UTF8String], O_RDONLY);
	if (_fd < 0) {
		int err = errno;

		[self fnStreamSetStatus:NSStreamStatusError error:fn_stream_error(err)];
		return;
	}
	/* Open SETS the status, and the base registers the descriptor with the run loop on that transition -
	 * which is how a stream scheduled before -open still becomes asynchronous. */
	[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
}

- (void)close
{
	[super close];				/* unregisters the source */
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
	return _fd;				/* -1 for a memory stream, WHICH IS A REFUSAL TO INVENT AN EVENT */
}

- (BOOL)fnStreamWatchesReadable
{
	return YES;
}

- (NSStreamEvent)fnStreamEventForReadiness
{
	if (_fd < 0 || [self streamStatus] == NSStreamStatusAtEnd) {
		return NSStreamEventNone;
	}
	/* THE RUN LOOP ALREADY DECIDED READINESS - it fired this because select(2) said the descriptor was
	 * ready - so asking poll(2) again here can only SUPPRESS the event, and does: this kernel does not
	 * report a REGULAR FILE as readable to poll, while select does. What is left to decide is whether the
	 * readiness is bytes or the END, and that is FIONREAD's answer alone. */
	if (fn_descriptor_empty(_fd)) {
		[self fnStreamSetStatus:NSStreamStatusAtEnd error:nil];
		return NSStreamEventEndEncountered;
	}
	return NSStreamEventHasBytesAvailable;
}

/* ---- reading ---- */

- (NSInteger)read:(uint8_t *)buffer maxLength:(NSUInteger)len
{
	NSUInteger available, taken;

	if (buffer == NULL) {
		return -1;
	}
	/* THE CALLER OWES -open: reading a stream that is not open is a BAD DESCRIPTOR, which is exactly what
	 * it is, rather than a silent zero that reads as end-of-stream. */
	if ([self streamStatus] != NSStreamStatusOpen && [self streamStatus] != NSStreamStatusAtEnd) {
		[self fnStreamSetStatus:NSStreamStatusError error:fn_stream_error(EBADF)];
		return -1;
	}
	if (len == 0) {
		return 0;
	}
	if (_data != nil) {
		available = [_data length] - _offset;
		taken = len < available ? len : available;
		if (taken == 0) {
			[self fnStreamSetStatus:NSStreamStatusAtEnd error:nil];
			return 0;
		}
		memcpy(buffer, (const uint8_t *)[_data bytes] + _offset, taken);
		_offset += taken;
		return (NSInteger)taken;
	}
	{
		ssize_t got;

		[self fnStreamSetStatus:NSStreamStatusReading error:nil];
		got = read(_fd, buffer, len);
		if (got < 0) {
			int err = errno;

			[self fnStreamSetStatus:NSStreamStatusError error:fn_stream_error(err)];
			return -1;
		}
		if (got == 0) {
			[self fnStreamSetStatus:NSStreamStatusAtEnd error:nil];
			return 0;
		}
		[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
		return (NSInteger)got;
	}
}

- (BOOL)getBuffer:(uint8_t * _Nullable * _Nonnull)buffer length:(NSUInteger *)len
{
	if (buffer == NULL || len == NULL) {
		return NO;
	}
	if (_data == nil) {
		return NO;			/* the bytes are not in memory, which is Apple's answer too */
	}
	*buffer = (uint8_t *)[_data bytes] + _offset;
	*len = [_data length] - _offset;
	return YES;
}

- (BOOL)hasBytesAvailable
{
	if (_data != nil) {
		return _offset < [_data length];
	}
	if (_fd < 0) {
		return NO;
	}
	/* APPLE'S SENTENCE IS "a read would not block". FOR A REGULAR FILE THAT IS ALWAYS TRUE, and this
	 * kernel's poll(2) will not say so, so the file case is answered from fstat(2) and everything else from
	 * poll - and at end of stream a read does not block either, so YES is right there too: the read that
	 * follows answers 0, and the delegate has already been told the stream ended. */
	{
		struct stat st;

		if (fstat(_fd, &st) == 0 && S_ISREG(st.st_mode)) {
			return YES;
		}
	}
	return fn_descriptor_ready(_fd) ? YES : NO;
}

/* ---- the two property keys this library ACTS on ---- */

- (nullable id)propertyForKey:(NSStreamPropertyKey)key
{
	if ([key isEqualToString:NSStreamFileCurrentOffsetKey]) {
		if (_data != nil) {
			return [NSNumber numberWithUnsignedInteger:_offset];
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
		if (_data != nil) {
			long long offset = [where longLongValue];

			if (offset < 0 || (unsigned long long)offset > [_data length]) {
				return NO;
			}
			_offset = (NSUInteger)offset;
			return YES;
		}
		if (_fd >= 0) {
			return lseek(_fd, (off_t)[where longLongValue], SEEK_SET) >= 0 ? YES : NO;
		}
		return NO;
	}
	return [super setProperty:property forKey:key];
}

@end
