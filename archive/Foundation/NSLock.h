/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLock / NSRecursiveLock / NSCondition — the locking half of the thread family. F13.17,
 * docs/design/foundation-plan.md §10.
 *
 * THE MECHANISM IS PTHREADS, which §10's table named before this slice existed ("pthreads
 * (`third_party/musl/src/thread`)"): the C library already ships a real mutex and a real condition
 * variable, and a second implementation would be a second answer to the same question. What these
 * classes add is the SHAPE a caller expects — an object with `-lock`/`-unlock`, and `-tryLock`
 * rather than an errno.
 *
 * ONE HEADER FOR THE FOUR, which Cocoa splits (`NSLock.h`, `NSRecursiveLock.h`, `NSCondition.h`,
 * `NSConditionLock.h`): they share a protocol and a mutex, and reading them together is worth more than
 * matching a file count. `NSLocking` is the protocol they share, and it is the one thing the family agrees on.
 *
 * THE DIFFERENCE BETWEEN THE TWO LOCKS IS OWNERSHIP: `NSLock` is NOT recursive, so locking it twice
 * from one thread is a deadlock rather than a mistake the class reports, and `-tryLock` is the door
 * that asks without waiting. `NSRecursiveLock` counts, so the thread that holds it may lock it again
 * as many times as it unlocks.
 */

#ifndef FOUNDATION_NSLOCK_H
#define FOUNDATION_NSLOCK_H

#import <Foundation/NSObject.h>

@class NSDate;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@protocol NSLocking

- (void)lock;
- (void)unlock;

@end

@interface NSLock : NSObject <NSLocking>
{
	void *_mutex;			/* a pthread_mutex_t, owned so pthreads stay out of this header */
	NSString *_name;
}

- (BOOL)tryLock;
- (BOOL)lockBeforeDate:(NSDate *)limit;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;

- (NSString *)description;

@end

@interface NSRecursiveLock : NSObject <NSLocking>
{
	void *_mutex;
	NSString *_name;
}

- (BOOL)tryLock;
- (BOOL)lockBeforeDate:(NSDate *)limit;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;

- (NSString *)description;

@end

/* A LOCK WITH A VALUE IN IT (2026-09-26), and the family's most useful member for a handshake:
 * `-lockWhenCondition:` acquires the lock AND waits for the value to be the wanted one, as ONE indivisible
 * step. That indivisibility is the whole point — a caller that had to lock, test and wait by itself would have
 * to hold the lock across the test, and getting that wrong is the classic lost wakeup.
 * `-unlockWithCondition:` is therefore the other half: it SETS the value and releases, and every waiter on the
 * old value is told to look again.
 *
 * THE TIMED DOORS POLL, for the family's recorded reason: `pthread_mutex_timedlock` is not portable (NSLock's
 * `-lockBeforeDate:` says so where it implements the same contract), so both take a deadline, try, sleep a
 * millisecond and try again. The contract is what matters — NO means the deadline passed. */
@interface NSConditionLock : NSObject <NSLocking>
{
	void *_mutex;
	void *_condition;
	NSInteger _conditionValue;
	NSString *_name;
}

- (instancetype)initWithCondition:(NSInteger)condition;

/* "The condition" - read UNDER THE LOCK, because a value read without it is a value that has already changed. */
- (NSInteger)condition;

- (void)lockWhenCondition:(NSInteger)condition;
- (BOOL)tryLockWhenCondition:(NSInteger)condition;
- (void)unlockWithCondition:(NSInteger)condition;

- (BOOL)lockBeforeDate:(NSDate *)limit;
- (BOOL)lockWhenCondition:(NSInteger)condition beforeDate:(NSDate *)limit;

- (BOOL)tryLock;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;

- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSLOCK_H */
