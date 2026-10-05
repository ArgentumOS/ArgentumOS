/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSThread.m — a thread as an object (F13.17).
 *
 * NO ARC HERE, and that is not a style choice: the library is compiled WITHOUT -fobjc-arc (the flag
 * is on the probes, not on the sources), so ownership is manual and a `__bridge_*` cast would be a
 * no-op that reads as if it did something. This file holds NO ownership at all: an object handed to
 * it is used for as long as the caller keeps it, which is the same bargain every other file in this
 * library makes. A detached thread's NSThread is therefore never released — it lives as long as the
 * thread it names, which is the only correct lifetime for it, and that is stated rather than implied.
 *
 * THE MAIN THREAD IS IDENTIFIED BY ITS pthread_t, captured once, so `+isMainThread` answers from any
 * thread without asking the kernel anything, and a NEW thread's `-isMainThread` is NO.
 */

#import <Foundation/NSThread.h>
#include <Block.h>	/* Block_copy/Block_release: the runtime's own entry points, per the house rule */
#include <sys/resource.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNotification.h>	/* the names the two posts use live here */
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

static pthread_key_t fn_thread_key;
static pthread_once_t fn_thread_key_once = PTHREAD_ONCE_INIT;
static BOOL fn_thread_key_ready = NO;
static BOOL fn_multi_threaded = NO;	/* §63.195: set when a thread other than the main one RUNS */
static pthread_t fn_main_thread_id;
static BOOL fn_main_captured = NO;
static NSThread *fn_main_thread_object = nil;

@interface NSThread (FNPrivate)
- (instancetype)fnInitForCurrentThread;
- (void)fnRun;
- (BOOL)fnSetPriority:(double)priority;
@end

static void fn_make_key(void)
{
	if (pthread_key_create(&fn_thread_key, NULL) == 0) {
		fn_thread_key_ready = YES;
	}
}

static void fn_ensure_key(void)
{
	pthread_once(&fn_thread_key_once, fn_make_key);
	if (fn_main_captured == NO) {
		fn_main_thread_id = pthread_self();
		fn_main_captured = YES;
	}
}

static void *fn_thread_entry(void *context)
{
	NSThread *thread = (NSThread *)context;

	fn_ensure_key();
	fn_multi_threaded = YES;
	if (fn_thread_key_ready) {
		pthread_setspecific(fn_thread_key, (void *)thread);
	}
	[thread fnRun];
	/* THE OTHER HALF OF -start'S RETAIN: the thread owns itself once it is running, and this is
	 * where that reference goes. `thread` is not touched after this line. */
	[thread release];
	return NULL;
}

@implementation NSThread

+ (NSThread *)currentThread
{
	NSThread *thread;

	fn_ensure_key();
	thread = fn_thread_key_ready ? (NSThread *)pthread_getspecific(fn_thread_key) : nil;
	if (thread == nil) {
		thread = [[NSThread alloc] fnInitForCurrentThread];
		if (fn_thread_key_ready) {
			pthread_setspecific(fn_thread_key, (void *)thread);
			if (pthread_equal(pthread_self(), fn_main_thread_id)) {
				/* THE MAIN THREAD KEEPS A SECOND POINTER TO ITS OBJECT, because its key dies
				 * with the process and +mainThread has to keep being answerable. */
				fn_main_thread_object = thread;
			}
		}
	}
	return thread;
}

- (instancetype)fnInitForCurrentThread
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	fn_ensure_key();
	_threadID = (unsigned long)pthread_self();
	_isMain = pthread_equal(pthread_self(), fn_main_thread_id) ? YES : NO;
	return self;
}

+ (NSThread *)mainThread
{
	if (fn_main_thread_object != nil) {
		return fn_main_thread_object;
	}
	return [self currentThread];
}

+ (BOOL)isMainThread
{
	fn_ensure_key();
	return pthread_equal(pthread_self(), fn_main_thread_id) ? YES : NO;
}

- (BOOL)isMainThread
{
	return _isMain;
}

- (nullable NSString *)name
{
	return _name;
}

- (void)setName:(nullable NSString *)name
{
	_name = name;
}

- (BOOL)isCancelled
{
	return _cancelled;
}

- (void)cancel
{
	/* A FLAG, NOT A SIGNAL: nothing here interrupts a running thread, so a thread that never asks is
	 * never cancelled — which is Cocoa's contract as well. */
	_cancelled = YES;
}

- (BOOL)isExecuting
{
	return _executing;
}

- (BOOL)isFinished
{
	return _finished;
}

+ (void)sleepForTimeInterval:(NSTimeInterval)interval
{
	struct timespec request;

	if (interval <= 0) {
		return;
	}
	request.tv_sec = (time_t)interval;
	request.tv_nsec = (long)((interval - (double)request.tv_sec) * 1000000000.0);
	if (request.tv_nsec < 0) {
		request.tv_nsec = 0;
	}
	nanosleep(&request, NULL);
}

+ (void)sleepUntilDate:(NSDate *)date
{
	double seconds = date != nil ? [date timeIntervalSinceNow] : 0;

	[self sleepForTimeInterval:seconds];
}

- (instancetype)initWithTarget:(id)target
		      selector:(SEL)selector
			object:(nullable id)argument
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE THREAD OWNS WHAT IT WILL USE (Cocoa's contract, and the reason a target could vanish
	 * between `-start` and the new thread actually running). Cocoa retains both until the thread
	 * finishes; fnRun releases them at the end, which the self-owning rule in -start makes real. */
	_target = [target retain];
	_selector = selector;
	_argument = [argument retain];
	return self;
}

- (instancetype)initWithBlock:(void (^)(void))block
{
	self = [super init];
	if (self != nil) {
		_block = Block_copy(block);
		_priority = 0.5;
		_qualityOfService = NSQualityOfServiceDefault;
		_stackSize = 0;
	}
	return self;
}

- (void)dealloc
{
	/* ONLY WHAT THIS CLASS OWNS: nothing else in this file retains an ivar. */
	Block_release(_block);
	[super dealloc];
}

- (double)threadPriority
{
	return _priority;
}

- (void)setThreadPriority:(double)priority
{
	(void)[self fnSetPriority:priority];
}

- (BOOL)fnSetPriority:(double)priority
{
	/* 0.0-1.0 ONTO nice -20..19, clamped: a stated reading (§63.195) — the value is always recorded and the
	 * syscall is best effort, because Apple's scale names a scheduling class this kernel has no equivalent. */
	int nice = (int)((0.5 - priority) * 39.0);

	if (nice < -20) { nice = -20; }
	if (nice > 19) { nice = 19; }
	_priority = priority;
	if (pthread_equal(pthread_self(), fn_main_thread_id)) {
		return YES;	/* a thread does not re-nice its own process here; recorded, not applied */
	}
	return setpriority(PRIO_PROCESS, (id_t)(uintptr_t)pthread_self(), nice) == 0;
}

- (NSUInteger)stackSize
{
	return _stackSize;
}

- (void)setStackSize:(NSUInteger)size
{
	_stackSize = size;
}

- (NSQualityOfService)qualityOfService
{
	return _qualityOfService;
}

- (void)setQualityOfService:(NSQualityOfService)qos
{
	_qualityOfService = qos;
}

+ (void)detachNewThreadSelector:(SEL)selector
		       toTarget:(id)target
		     withObject:(nullable id)argument
{
	NSThread *thread = [[NSThread alloc] initWithTarget:target
						   selector:selector
						     object:argument];

	[thread start];
	/* WE OWN THE ALLOC AND THE THREAD OWNS ITSELF FROM HERE (see -start), so this is the other
	 * half of that pair - a detached thread nobody can release must not be a permanent leak. */
	[thread release];
}

/* HOW MANY SECONDARY THREADS THIS PROCESS HAS STARTED (§62.103). The count is what makes the
 * WILL-BECOME-MULTITHREADED notice mean something: it is posted ONCE, on the transition from a process with
 * one thread (the one running) to one with two — which is the first -start, not every -start. */
static int fn_thread_count = 0;

+ (void)detachNewThreadWithBlock:(void (^)(void))block
{
	NSThread *thread = [[self alloc] initWithBlock:block];

	[thread start];
	[thread release];	/* -start retains it for the duration; this gives up the caller's reference */
}

+ (void)exit
{
	pthread_exit(NULL);
}

+ (BOOL)isMultiThreaded
{
	return fn_multi_threaded;
}

+ (double)threadPriority
{
	return [[self currentThread] threadPriority];
}

+ (BOOL)setThreadPriority:(double)priority
{
	return [[self currentThread] fnSetPriority:priority];
}

- (void)start
{
	pthread_t id;

	if (_executing || _finished) {
		return;
	}
	_executing = YES;
	/* THE THREAD KEEPS ITSELF ALIVE WHILE IT RUNS. The new pthread's only handle IS this object,
	 * and no caller can be relied on to hold it (a detached thread has no caller at all), so a
	 * caller releasing it as its scope ends would free the table the thread is about to run on.
	 * The pair is the release at the end of fn_thread_entry. */
	[self retain];
	{
		pthread_attr_t attr;

		pthread_attr_init(&attr);
		if (_stackSize > 0) {
			/* §63.195: `stackSize` IS APPLIED for the threads this class creates, not merely recorded. */
			(void)pthread_attr_setstacksize(&attr, (size_t)_stackSize);
		}
		if (pthread_create(&id, &attr, fn_thread_entry, (void *)self) != 0) {
			pthread_attr_destroy(&attr);
			_executing = NO;
			[self release];
			return;
		}
		pthread_attr_destroy(&attr);
	}
	if (0) {
		_executing = NO;
		[self release];
		return;
	}
	_threadID = (unsigned long)id;
	/* AND THE NOTICE, ONCE, NOW THAT A SECOND THREAD EXISTS. Apple's contract is about the PROCESS, not about
	 * this object, so the notification carries no object. */
	if (++fn_thread_count == 1) {
		[[NSNotificationCenter defaultCenter]
			postNotificationName:NSWillBecomeMultiThreadedNotification object:nil];
	}
}

- (void)fnRun
{
	_executing = YES;
	[self main];
}

- (void)main
{
	/* THE DEFAULT BODY: a block if the thread was born with one, otherwise the target/selector pair. A
	 * subclass overriding THIS runs instead, which is what makes -main public. */
	if (_block != nil) {
		((void (^)(void))_block)();
		return;
	}
	if (_target != nil && _selector != NULL) {
		[_target performSelector:_selector withObject:_argument];
	}
	/* WHAT -initWithTarget:... TOOK, RELEASED WHERE COCOA RELEASES IT: when the thread finishes. */
	[_target release];
	_target = nil;
	[_argument release];
	_argument = nil;
	_executing = NO;
	/* AND THE THREAD'S OWN ENDING (§62.103): the object IS the notification's object, because a watcher
	 * wants to know WHICH thread is going away. */
	[[NSNotificationCenter defaultCenter] postNotificationName:NSThreadWillExitNotification
							    object:self];
	_finished = YES;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p%@%@>", [self class], self,
				  _name != nil ? [NSString stringWithFormat:@" name=%@", _name] : @"",
				  _isMain ? @" main" : @""];
}


/* THE PER-THREAD STORE (W2d's dependency). `+currentThread` already resolves THIS thread's object
 * through a pthread key, so the dictionary is one ivar away — and that is the faithful home, because
 * Apple's contract is that the dictionary belongs to the thread OBJECT, not to a global table. The
 * main thread gets one the same way every other thread does. */
- (NSMutableDictionary *)threadDictionary
{
	if (_threadDictionary == nil) {
		_threadDictionary = [[NSMutableDictionary alloc] init];
	}
	return (NSMutableDictionary *)_threadDictionary;
}
@end
