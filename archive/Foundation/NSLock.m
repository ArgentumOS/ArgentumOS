/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSLock.m — the locking classes over pthreads (F13.17). MANUAL OWNERSHIP.
 *
 * TWO PLACES WHERE THE POSIX SHAPE IS NOT QUITE WHAT A CALLER EXPECTS, and both are answered here
 * rather than left to the caller to discover:
 *
 *   -lockBeforeDate:   POSIX has a TIMED lock, but it takes an absolute CLOCK_REALTIME deadline
 *                      while this door takes a date. The deadline is computed from the date, so the
 *                      conversion happens once, here.
 *   -waitUntilDate:    the same, for a condition variable, and the same conversion.
 *
 * A MUTEX THAT IS NOT RECURSIVE IS DESTROYED AT DEALLOC, and a condition with it. Nothing here holds
 * a file descriptor or a thread, so a lock can be released by the same object that took it and no
 * other bookkeeping is needed.
 *
 * TWO DEFECTS OF THIS FAMILY WERE FOUND AND FIXED WHILE NSConditionLock WAS ADDED (2026-09-26), both
 * in this file and both named by the tools rather than by taste:
 *
 *   1. EVERY -dealloc HERE OMITTED `[super dealloc]`, AND THAT IS A LEAK RATHER THAN A STYLE POINT:
 *      NSObject's own -dealloc is what FREES THE INSTANCE (the runtime sends -dealloc and does not free
 *      afterwards), so a subclass that does not chain to it leaves the object's memory behind. clang
 *      warns `-Wobjc-missing-super-calls` three times for this file — the warnings were there and read.
 *   2. EVERY -setName: ASSIGNED its argument where Apple declares the property `copy`. The setter now
 *      takes a real snapshot — through -initWithString: rather than -copy, for the reason the string
 *      store states (§11.6.1 D2's standing note that this library's -copy is not a value copy) — and
 *      the dealloc releases it.
 */

#import "FNCondition.h"	/* the private condition §63.170 re-homed here */
#import <Foundation/NSLock.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSString.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* A DATE AS AN ABSOLUTE DEADLINE, which is what both timed doors need and neither of them takes. */
static void fn_timespec_from_date(NSDate *limit, struct timespec *out)
{
	double seconds = limit != nil ? [limit timeIntervalSince1970] : 0;

	if (seconds <= 0) {
		seconds = 0;
	}
	out->tv_sec = (time_t)seconds;
	out->tv_nsec = (long)((seconds - (double)out->tv_sec) * 1000000000.0);
	if (out->tv_nsec < 0) {
		out->tv_nsec = 0;
	}
}

/* HAS THE DEADLINE PASSED? One place, because every polling door in this file asks the same question. */
static BOOL fn_deadline_passed(const struct timespec *deadline)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	return now.tv_sec > deadline->tv_sec ||
	       (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

/* A MILLISECOND, which is the poll interval the family uses (see -lockBeforeDate: below). */
static void fn_nap(void)
{
	struct timespec nap;

	nap.tv_sec = 0;
	nap.tv_nsec = 1000000;
	nanosleep(&nap, NULL);
}

@implementation NSLock

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mutex = malloc(sizeof(pthread_mutex_t));
	if (_mutex == NULL) {
		return nil;
	}
	if (pthread_mutex_init((pthread_mutex_t *)_mutex, NULL) != 0) {
		free(_mutex);
		_mutex = NULL;
		return nil;
	}
	return self;
}

- (void)dealloc
{
	[_name release];
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
	[super dealloc];		/* THE INSTANCE'S OWN MEMORY IS FREED THERE, not here */
}

- (void)lock
{
	pthread_mutex_lock((pthread_mutex_t *)_mutex);
}

- (void)unlock
{
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
}

- (BOOL)tryLock
{
	return pthread_mutex_trylock((pthread_mutex_t *)_mutex) == 0;
}

- (BOOL)lockBeforeDate:(NSDate *)limit
{
	struct timespec deadline;

	fn_timespec_from_date(limit, &deadline);
	/* PTHREAD_MUTEX_TIMEDLOCK IS NOT PORTABLE, so the wait is a bounded POLL: a short sleep between
	 * attempts, ending at the deadline. It is the same contract — NO means the deadline passed — and
	 * it needs nothing from the C library beyond trylock and nanosleep. */
	for (;;) {
		if ([self tryLock]) {
			return YES;
		}
		if (fn_deadline_passed(&deadline)) {
			return NO;
		}
		fn_nap();
	}
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	/* APPLE DECLARES THIS PROPERTY `copy`, so the lock holds a SNAPSHOT rather than the caller's string.
	 * Through -initWithString: and not -copy: this library's -copy is not a value copy, which is the same
	 * ground NSAttributedString's store records (§11.6.1 D2). */
	NSString *snapshot = name != nil ? [[NSString alloc] initWithString:name] : nil;

	[_name release];
	_name = snapshot;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end

@implementation NSRecursiveLock

- (instancetype)init
{
	pthread_mutexattr_t attributes;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mutex = malloc(sizeof(pthread_mutex_t));
	if (_mutex == NULL) {
		return nil;
	}
	if (pthread_mutexattr_init(&attributes) != 0) {
		free(_mutex);
		_mutex = NULL;
		return nil;
	}
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
	if (pthread_mutex_init((pthread_mutex_t *)_mutex, &attributes) != 0) {
		pthread_mutexattr_destroy(&attributes);
		free(_mutex);
		_mutex = NULL;
		return nil;
	}
	pthread_mutexattr_destroy(&attributes);
	return self;
}

- (void)dealloc
{
	[_name release];
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
	[super dealloc];		/* THE INSTANCE'S OWN MEMORY IS FREED THERE, not here */
}

- (void)lock
{
	pthread_mutex_lock((pthread_mutex_t *)_mutex);
}

- (void)unlock
{
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
}

- (BOOL)tryLock
{
	return pthread_mutex_trylock((pthread_mutex_t *)_mutex) == 0;
}

- (BOOL)lockBeforeDate:(NSDate *)limit
{
	struct timespec deadline;

	fn_timespec_from_date(limit, &deadline);
	for (;;) {
		if ([self tryLock]) {
			return YES;
		}
		if (fn_deadline_passed(&deadline)) {
			return NO;
		}
		fn_nap();
	}
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	NSString *snapshot = name != nil ? [[NSString alloc] initWithString:name] : nil;

	[_name release];
	_name = snapshot;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end

/* ⚠⚠ THIS WAS `NSCondition` (10.5) UNTIL §63.170 CUT IT: the 10.2 baseline removed the public class and
 * its body - musl pthreads - stays here for its three consumers (NSOperation.m twíce, NSTask.m once), which
 * reach it through an ivar. ITS `name`/`setName:`/`description` DOORS REMAIN IMPLEMENTED AND UNDECLARED: no
 * consumer sends them, and deleting a working body was not worth a mis-edit. */
@implementation FNCondition

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mutex = malloc(sizeof(pthread_mutex_t));
	_condition = malloc(sizeof(pthread_cond_t));
	if (_mutex == NULL || _condition == NULL) {
		free(_mutex);
		free(_condition);
		_mutex = NULL;
		_condition = NULL;
		return nil;
	}
	if (pthread_mutex_init((pthread_mutex_t *)_mutex, NULL) != 0 ||
	    pthread_cond_init((pthread_cond_t *)_condition, NULL) != 0) {
		free(_mutex);
		free(_condition);
		_mutex = NULL;
		_condition = NULL;
		return nil;
	}
	return self;
}

- (void)dealloc
{
	[_name release];
	if (_condition != NULL) {
		pthread_cond_destroy((pthread_cond_t *)_condition);
		free(_condition);
	}
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
	[super dealloc];		/* THE INSTANCE'S OWN MEMORY IS FREED THERE, not here */
}

- (void)lock
{
	pthread_mutex_lock((pthread_mutex_t *)_mutex);
}

- (void)unlock
{
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
}

- (void)wait
{
	pthread_cond_wait((pthread_cond_t *)_condition, (pthread_mutex_t *)_mutex);
}

- (BOOL)waitUntilDate:(NSDate *)limit
{
	struct timespec deadline;
	int result;

	fn_timespec_from_date(limit, &deadline);
	result = pthread_cond_timedwait((pthread_cond_t *)_condition, (pthread_mutex_t *)_mutex,
					&deadline);
	return result == 0;
}

- (void)signal
{
	pthread_cond_signal((pthread_cond_t *)_condition);
}

- (void)broadcast
{
	pthread_cond_broadcast((pthread_cond_t *)_condition);
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	NSString *snapshot = name != nil ? [[NSString alloc] initWithString:name] : nil;

	[_name release];
	_name = snapshot;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end

@implementation NSConditionLock

/* APPLE DECLARES ONLY -initWithCondition:, SO -init IS OURS AND SAYS SO: the same object with the initial
 * condition zero, because an object whose mutex was never initialised would crash at the first door rather
 * than answer. §11.6.1 D2 — Apple publishes no behaviour here. */
- (instancetype)init
{
	return [self initWithCondition:0];
}

- (instancetype)initWithCondition:(NSInteger)condition
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_mutex = malloc(sizeof(pthread_mutex_t));
	_condition = malloc(sizeof(pthread_cond_t));
	if (_mutex == NULL || _condition == NULL) {
		free(_mutex);
		free(_condition);
		_mutex = NULL;
		_condition = NULL;
		return nil;
	}
	if (pthread_mutex_init((pthread_mutex_t *)_mutex, NULL) != 0 ||
	    pthread_cond_init((pthread_cond_t *)_condition, NULL) != 0) {
		free(_mutex);
		free(_condition);
		_mutex = NULL;
		_condition = NULL;
		return nil;
	}
	_conditionValue = condition;
	return self;
}

- (void)dealloc
{
	[_name release];
	if (_condition != NULL) {
		pthread_cond_destroy((pthread_cond_t *)_condition);
		free(_condition);
	}
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
	[super dealloc];		/* THE INSTANCE'S OWN MEMORY IS FREED THERE, not here */
}

/* THE VALUE IS READ UNDER THE LOCK: a condition read without it says what was true at some moment nobody can
 * name, which is exactly the answer a handshake must not act on. */
- (NSInteger)condition
{
	NSInteger value;

	pthread_mutex_lock((pthread_mutex_t *)_mutex);
	value = _conditionValue;
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
	return value;
}

- (void)lock
{
	pthread_mutex_lock((pthread_mutex_t *)_mutex);
}

- (void)unlock
{
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
}

/* ACQUIRE AND WAIT AS ONE STEP. The lock is held across the TEST AND THE WAIT, so a signaller cannot slip
 * between them — that indivisibility is the whole reason this class exists rather than a caller writing the
 * three calls itself. `pthread_cond_wait` releases the mutex while it sleeps and takes it back on waking. */
- (void)lockWhenCondition:(NSInteger)condition
{
	pthread_mutex_lock((pthread_mutex_t *)_mutex);
	while (_conditionValue != condition) {
		pthread_cond_wait((pthread_cond_t *)_condition, (pthread_mutex_t *)_mutex);
	}
}

- (BOOL)tryLockWhenCondition:(NSInteger)condition
{
	if (pthread_mutex_trylock((pthread_mutex_t *)_mutex) != 0) {
		return NO;
	}
	if (_conditionValue != condition) {
		pthread_mutex_unlock((pthread_mutex_t *)_mutex);
		return NO;
	}
	return YES;
}

/* THE OTHER HALF: the value is SET and the lock released, and every waiter on the old value is woken to look
 * again. A BROADCAST rather than a signal, because waiters are waiting for different values and only those
 * whose value just arrived should proceed — a signal would wake one arbitrary waiter, which may be one
 * waiting for something else. */
- (void)unlockWithCondition:(NSInteger)condition
{
	_conditionValue = condition;
	pthread_cond_broadcast((pthread_cond_t *)_condition);
	pthread_mutex_unlock((pthread_mutex_t *)_mutex);
}

- (BOOL)tryLock
{
	return pthread_mutex_trylock((pthread_mutex_t *)_mutex) == 0;
}

- (BOOL)lockBeforeDate:(NSDate *)limit
{
	struct timespec deadline;

	fn_timespec_from_date(limit, &deadline);
	for (;;) {
		if ([self tryLock]) {
			return YES;
		}
		if (fn_deadline_passed(&deadline)) {
			return NO;
		}
		fn_nap();
	}
}

/* THE TIMED CONDITION DOOR POLLS, and the reasoning is the family's own (NSLock's -lockBeforeDate:): the only
 * timed mutex POSIX offers is not portable. Each attempt TAKES THE LOCK, TESTS AND RELEASES, so the signaller
 * is never shut out while this loop runs — a poll that held the lock while it waited would be a deadlock. */
- (BOOL)lockWhenCondition:(NSInteger)condition beforeDate:(NSDate *)limit
{
	struct timespec deadline;

	fn_timespec_from_date(limit, &deadline);
	for (;;) {
		if ([self tryLockWhenCondition:condition]) {
			return YES;
		}
		if (fn_deadline_passed(&deadline)) {
			return NO;
		}
		fn_nap();
	}
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	NSString *snapshot = name != nil ? [[NSString alloc] initWithString:name] : nil;

	[_name release];
	_name = snapshot;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p condition=%ld%@>", [self class], self,
				  (long)_conditionValue,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end
