/*
 * NSDistributedLock.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * See NSDistributedLock.h for the three rules and for why the lock is a file. What is here is `open(2)`, which is
 * where the atomicity comes from — no lock file in this class does any read-then-write that could race.
 */

#import <Foundation/NSDistributedLock.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSString.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

@implementation NSDistributedLock

+ (NSDistributedLock *)lockWithPath:(NSString *)path
{
	return [[[self alloc] initWithPath:path] autorelease];
}

- (instancetype)initWithPath:(NSString *)path
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if ([path length] == 0) {
		[self release];
		return nil;
	}
	_path = [path copy];
	_isLocked = NO;
	return self;
}

- (BOOL)tryLock
{
	int fd;

	/* ALREADY OURS IS NOT A FAILURE: a caller that calls this twice should not have to remember. */
	if (_isLocked) {
		return YES;
	}
	/* O_CREAT|O_EXCL IS THE WHOLE LOCK: the kernel either creates the name or tells us somebody has it, and there
	 * is no read-then-write here for another process to race with. */
	fd = open([_path UTF8String], O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd >= 0) {
		close(fd);
		_isLocked = YES;
		return YES;
	}
	return NO;		/* EEXIST, most likely; a permission error is also a no, and neither is a crash */
}

- (void)unlock
{
	/* ONLY WHAT THIS OBJECT HOLDS. Removing a lock somebody else holds is not release and has its own door. */
	if (!_isLocked) {
		return;
	}
	(void)unlink([_path UTF8String]);
	_isLocked = NO;
}

- (void)breakLock
{
	/* TAKES IT FROM ANYBODY, which is what the name says: a holder that died leaves the file, and this is the way
	 * past it. The object's own flag follows, so a later `-unlock` does not remove somebody else's fresh claim. */
	(void)unlink([_path UTF8String]);
	_isLocked = NO;
}

- (NSDate *)lockDate
{
	struct stat info;

	/* THE FILE'S OWN DATE, so this answers for a lock another process holds as well as for this object's — the
	 * only reading of "when was this acquired" that is useful across processes. */
	if (stat([_path UTF8String], &info) != 0) {
		return nil;
	}
	return [NSDate dateWithTimeIntervalSince1970:(double)info.st_mtime];
}

- (void)dealloc
{
	/* THE LOCK IS NOT RELEASED HERE, DELIBERATELY — see the header: a lock that vanished with its holder would
	 * disappear while the work it guards is still running. */
	[_path release];
	[super dealloc];
}

@end
