/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPipe.m — pipe(2), and the two handles that are its ends. See NSPipe.h: the class is small because the
 * system call is, and what it does own is the OWNERSHIP decision — each handle owns one end, so the ends
 * close when the handles go away and a reader sees END OF FILE at exactly the moment the writer is gone.
 */

#import <Foundation/NSPipe.h>
#import <Foundation/NSFileHandle.h>
#import <Foundation/NSException.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

@implementation NSPipe

+ (NSPipe *)pipe
{
	return [[[self alloc] init] autorelease];
}

- (instancetype)init
{
	int ends[2];

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (pipe(ends) != 0) {
		int err = errno;

		[self release];
		[NSException raise:NSGenericException
			    format:@"NSPipe: pipe(2) failed: %s", strerror(err)];
		return nil;
	}
	/* EACH END OWNS ITS DESCRIPTOR: closeOnDealloc YES, because the pipe created them. */
	_fileHandleForReading = [[NSFileHandle alloc] initWithFileDescriptor:ends[0] closeOnDealloc:YES];
	_fileHandleForWriting = [[NSFileHandle alloc] initWithFileDescriptor:ends[1] closeOnDealloc:YES];
	return self;
}

- (NSFileHandle *)fileHandleForReading
{
	return _fileHandleForReading;
}

- (NSFileHandle *)fileHandleForWriting
{
	return _fileHandleForWriting;
}

- (void)dealloc
{
	[_fileHandleForReading release];
	[_fileHandleForWriting release];
	[super dealloc];
}

@end
