/*
 * NSDistributedLock.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSDistributedLock` — A LOCK THAT IS A FILE (§62.55), the other self-contained class of the family §11.5 struck.
 *
 * APPLE'S IS A LOCK IN A NAMESPACE THE OPERATING SYSTEM PROVIDES; this system's is A FILE, and the substitution is
 * the honest one rather than an approximation: `open(2)` with `O_CREAT|O_EXCL` IS an atomic claim on a name, which
 * is exactly what a distributed lock needs and what the kernel guarantees without any help from this library. The
 * file's EXISTENCE is the lock.
 *
 * THREE RULES, EACH OF THEM A CHOICE:
 *
 *   * `-tryLock` answers YES when THIS OBJECT already holds the lock (calling it twice is not an error), and
 *     otherwise claims the name and answers YES, or answers NO when somebody else holds it.
 *   * `-unlock` RELEASES ONLY WHAT THIS OBJECT HOLDS, and does nothing otherwise. Taking away a lock somebody else
 *     holds is not release, it is theft — and it has its own door.
 *   * `-breakLock` TAKES IT FROM ANYBODY, which is what the name says and why Apple's documentation warns about it:
 *     a process that died holding a lock leaves the file behind, and breakLock is the only way past it.
 *
 * AND THE LOCK IS NOT RELEASED WHEN ITS OBJECT GOES AWAY, deliberately: a distributed lock that vanished when its
 * holder was deallocated would be a lock that disappears while the work it guards is still running. A caller
 * unlocks, or somebody breaks it.
 *
 * `-lockDate` READS THE FILE, so it answers for a lock SOMEONE ELSE holds as well as for this object's — the only
 * useful reading, and the reason it is not a field this object remembers.
 */

#ifndef FOUNDATION_NSDISTRIBUTEDLOCK_H
#define FOUNDATION_NSDISTRIBUTEDLOCK_H

#import <Foundation/NSObject.h>

@class NSString, NSDate;

NS_ASSUME_NONNULL_BEGIN

@interface NSDistributedLock : NSObject
{
	NSString *_path;		/* copied: the lock's name */
	BOOL _isLocked;			/* whether THIS object holds it */
}

+ (nullable NSDistributedLock *)lockWithPath:(NSString *)path;
- (instancetype)initWithPath:(NSString *)path;

- (BOOL)tryLock;
- (void)unlock;
- (void)breakLock;

/* The date the lock was acquired, or nil when the name is not claimed. */
- (nullable NSDate *)lockDate;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDISTRIBUTEDLOCK_H */
