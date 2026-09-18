/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nslock.m — the locking classes over pthreads (F13.17). ARC file.
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
 */

#import <foundation/NSLock.h>
#import <foundation/NSDate.h>
#import <foundation/NSString.h>
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
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
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
		struct timespec now;

		if ([self tryLock]) {
			return YES;
		}
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > deadline.tv_sec ||
		    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
			return NO;
		}
		{
			struct timespec nap;

			nap.tv_sec = 0;
			nap.tv_nsec = 1000000;	/* a millisecond */
			nanosleep(&nap, NULL);
		}
	}
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	_name = name;
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
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
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
		struct timespec now;

		if ([self tryLock]) {
			return YES;
		}
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > deadline.tv_sec ||
		    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
			return NO;
		}
		{
			struct timespec nap;

			nap.tv_sec = 0;
			nap.tv_nsec = 1000000;
			nanosleep(&nap, NULL);
		}
	}
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	_name = name;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end

@implementation NSCondition

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
	if (_condition != NULL) {
		pthread_cond_destroy((pthread_cond_t *)_condition);
		free(_condition);
	}
	if (_mutex != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *)_mutex);
		free(_mutex);
	}
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
	_name = name;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @""];
}

@end
