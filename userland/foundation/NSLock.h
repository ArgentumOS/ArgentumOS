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
 * ONE HEADER FOR THE THREE, which Cocoa splits (`NSLock.h`, `NSRecursiveLock.h`, `NSCondition.h`):
 * they share a protocol and a mutex, and reading them together is worth more than matching a file
 * count. `NSLocking` is the protocol they share, and it is the one thing the family agrees on.
 *
 * THE DIFFERENCE BETWEEN THE TWO LOCKS IS OWNERSHIP: `NSLock` is NOT recursive, so locking it twice
 * from one thread is a deadlock rather than a mistake the class reports, and `-tryLock` is the door
 * that asks without waiting. `NSRecursiveLock` counts, so the thread that holds it may lock it again
 * as many times as it unlocks.
 */

#ifndef FOUNDATION_NSLOCK_H
#define FOUNDATION_NSLOCK_H

#import <foundation/NSObject.h>

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

/* A LOCK AND A WAIT, together, which is what makes it possible to wait for a condition WITHOUT
 * missing the signal: the waiter holds the lock while it checks and while it waits, and the signaller
 * holds it while it changes the condition. */
@interface NSCondition : NSObject <NSLocking>
{
	void *_mutex;
	void *_condition;
	NSString *_name;
}

- (void)wait;
- (BOOL)waitUntilDate:(NSDate *)limit;
- (void)signal;
- (void)broadcast;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;

- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSLOCK_H */
