/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotificationQueue.m — the notification family's buffer, and the seam the run loop exposes to it (§62.61).
 * MANUAL OWNERSHIP.
 *
 * THE QUEUE ITSELF IS SMALL: a center, a FIFO list of pending posts, and a lock. The INTERESTING half is what
 * "pending" means, and it is the thing a naive version gets wrong:
 *
 *   * A pending notification is ONE THAT HAS NOT BEEN POSTED YET — so coalescing and dequeuing act on what is
 *     still waiting, never on what a center has already delivered, and a notification stops being coalescable
 *     the moment its phase arrives.
 *   * `NSPostNow` COALESCES FIRST AND THEN POSTS SYNCHRONOUSLY, which is the one thing that separates it from
 *     a plain `-postNotification:`; it never enters the pending list.
 *   * THE PHASE IS THE RUN LOOP'S, NOT OURS: this file posts only when NSRunLoop says which phase it reached,
 *     which is what FNRunLoopQueue.h is for. Nothing here sleeps, polls or guesses.
 *
 * THE REGISTRY IS PER THREAD AND HOLDS NO REFERENCES. A run loop flushes the queues ITS THREAD created, so a
 * queue joins its thread's registry on init — as an NSValue wrapping a NON-RETAINING pointer, and the queue
 * removes its own entry in -dealloc. A registry of strong references would be unremovable (the entry would keep
 * the queue alive, so -dealloc could never run), which is the classic way this pattern leaks.
 */

#import <Foundation/NSNotificationQueue.h>
#import <Foundation/FNRunLoopQueue.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSString.h>
#import <Foundation/NSRunLoop.h>

#include <stdlib.h>

/* THE TWO KEYS, one for the thread's queue registry and one for its default queue. A thread dictionary is the
 * per-thread store this library already has (NSThread owns it), so a per-thread default queue needs no new
 * mechanism — which is what Apple's own sentence describes: "every thread has a default notification queue". */
static NSString *const FNNQRegistryKey = @"NSNotificationQueue.FNRegistry";
static NSString *const FNNQDefaultKey = @"NSNotificationQueue.FNDefault";

/* ONE PENDING POST: the notification, the style it was queued with, the mask it coalesces by, and the modes that
 * may carry it. INTERNAL, and its ivars are public to this file only — there is no accessor nobody would call. */
@interface FNQueuedPost : NSObject
{
@public
	NSNotification *_notification;		/* retained */
	NSPostingStyle _style;
	NSUInteger _mask;
	NSArray *_modes;			/* retained; never nil and never empty (the enqueue door defaults it) */
}
@end

@implementation FNQueuedPost

- (void)dealloc
{
	[_notification release];
	[_modes release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

@end

/* THE QUEUE'S OWN PRIVATE DOORS, DECLARED SO THE CATEGORY BELOW CAN CALL THEM: a method defined in one
 * @implementation is not visible to another without a declaration, and this file has two (the class and the
 * run-loop seam). */
@interface NSNotificationQueue (FNPrivate)
- (void)fnDropPendingMatching:(NSNotification *)notification mask:(NSUInteger)mask;
- (void)fnTakeDueWhenIdle:(BOOL)idle mode:(NSString *)mode into:(NSMutableArray *)due;
- (void)fnDrainPendingWhenIdle:(BOOL)idle mode:(NSString *)mode;
- (BOOL)fnHasPendingForMode:(NSString *)mode;
@end

/* DOES A PENDING POST MATCH THE CRITERIA? The name is a string and compares with -isEqual:; a SENDER is "the same
 * object" and compares by IDENTITY, which is the only spelling that makes sense for each (ours, §11.6.1 D2).
 * `NSNotificationNoCoalescing` (0) matches NOTHING, so it removes nothing. */
static BOOL fn_matches(NSNotification *pending, NSNotification *criteria, NSUInteger mask)
{
	BOOL onName = (mask & NSNotificationCoalescingOnName) != 0;
	BOOL onSender = (mask & NSNotificationCoalescingOnSender) != 0;

	if (!onName && !onSender) {
		return NO;
	}
	if (onName) {
		NSString *a = [pending name];
		NSString *b = [criteria name];

		if (a == nil ? b != nil : ![a isEqual:b]) {
			return NO;
		}
	}
	if (onSender && [pending object] != [criteria object]) {
		return NO;
	}
	return YES;
}

/* MAY THIS POST GO OUT IN `mode`? An empty list is the default mode (the enqueue door makes that impossible by
 * substituting it, and the check is kept so a list mutated empty cannot silently start firing everywhere). */
static BOOL fn_modes_allow(NSArray *modes, NSString *mode)
{
	NSUInteger i;

	if (mode == nil || [modes count] == 0) {
		return NO;
	}
	for (i = 0; i < [modes count]; i++) {
		if ([[modes objectAtIndex:i] isEqual:mode]) {
			return YES;
		}
	}
	return NO;
}

/* THE CALLING THREAD'S REGISTRY — non-retaining wrappers, made on first ask. */
static NSMutableArray *fn_registry(BOOL create)
{
	NSMutableDictionary *store = [[NSThread currentThread] threadDictionary];
	NSMutableArray *queues = [store objectForKey:FNNQRegistryKey];

	if (queues == nil && create) {
		queues = [NSMutableArray array];
		[store setObject:queues forKey:FNNQRegistryKey];
	}
	return queues;
}

@implementation NSNotificationQueue

+ (NSNotificationQueue *)defaultQueue
{
	NSMutableDictionary *store = [[NSThread currentThread] threadDictionary];
	NSNotificationQueue *queue = [store objectForKey:FNNQDefaultKey];

	if (queue == nil) {
		queue = [[[self alloc] initWithNotificationCenter:[NSNotificationCenter defaultCenter]]
			  autorelease];
		[store setObject:queue forKey:FNNQDefaultKey];	/* the thread dictionary is what owns it */
	}
	return queue;
}

- (instancetype)initWithNotificationCenter:(NSNotificationCenter *)notificationCenter
{
	self = [super init];
	if (self != nil) {
		_center = [notificationCenter retain];
		_pending = [[NSMutableArray alloc] init];
		_lock = [[NSLock alloc] init];
		[fn_registry(YES) addObject:[NSValue valueWithPointer:self]];
	}
	return self;
}

- (void)dealloc
{
	NSMutableArray *queues = fn_registry(NO);
	NSUInteger i;

	/* LEAVE THE THREAD'S REGISTRY FIRST: the entry is non-retaining, so this is the only chance to drop it, and
	 * an entry left behind would hand the run loop a pointer to freed memory. */
	for (i = 0; queues != nil && i < [queues count]; i++) {
		if ([[queues objectAtIndex:i] pointerValue] == (void *)self) {
			[queues removeObjectAtIndex:i];
			break;
		}
	}
	[_pending release];
	[_center release];
	[_lock release];
	[super dealloc];
}

- (void)enqueueNotification:(NSNotification *)notification
	       postingStyle:(NSPostingStyle)postingStyle
{
	/* APPLE'S OWN WORDS for what this form means: it "coalesces only notifications that match both the
	 * notification's name and object", in the default mode. */
	[self enqueueNotification:notification
		     postingStyle:postingStyle
		     coalesceMask:(NSNotificationCoalescing)(NSNotificationCoalescingOnName |
							     NSNotificationCoalescingOnSender)
			 forModes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
}

- (void)enqueueNotification:(NSNotification *)notification
	       postingStyle:(NSPostingStyle)postingStyle
	       coalesceMask:(NSNotificationCoalescing)coalesceMask
		   forModes:(NSArray *)modes
{
	NSArray *effective = (modes != nil && [modes count] > 0)
		? [[[NSArray alloc] initWithArray:modes] autorelease]
		: [NSArray arrayWithObject:NSDefaultRunLoopMode];

	[_lock lock];
	[self fnDropPendingMatching:notification mask:coalesceMask];
	if (postingStyle == NSPostNow) {
		[_lock unlock];
		/* COALESCE, THEN POST — synchronously, so the observer has already run when this returns. */
		[_center postNotification:notification];
		return;
	}
	{
		FNQueuedPost *post = [[FNQueuedPost alloc] init];

		post->_notification = [notification retain];
		post->_style = postingStyle;
		post->_mask = coalesceMask;
		post->_modes = [[NSArray alloc] initWithArray:effective];
		[(NSMutableArray *)_pending addObject:post];
		[post release];
	}
	[_lock unlock];
}

- (void)dequeueNotificationsMatching:(NSNotification *)notification
			coalesceMask:(NSUInteger)coalesceMask
{
	[_lock lock];
	[self fnDropPendingMatching:notification mask:coalesceMask];
	[_lock unlock];
}

/* THE ONE REMOVAL RULE, shared by coalescing and by `-dequeueNotificationsMatching:` — because they ARE the same
 * question ("which waiting notifications does this criteria cover?") asked by two doors. The caller holds the
 * lock. */
- (void)fnDropPendingMatching:(NSNotification *)notification mask:(NSUInteger)mask
{
	NSMutableArray *pending = _pending;
	NSUInteger i = 0;

	if (mask == NSNotificationNoCoalescing) {
		return;
	}
	while (i < [pending count]) {
		FNQueuedPost *post = [pending objectAtIndex:i];

		if (fn_matches(post->_notification, notification, mask)) {
			[pending removeObjectAtIndex:i];	/* the array releases it */
			continue;
		}
		i++;
	}
}

/* TAKE WHAT IS DUE FOR THIS PHASE, under the lock, and post it OUTSIDE the lock: a center delivers
 * synchronously, and an observer that enqueued another notification while we held the lock would deadlock
 * against itself. */
- (void)fnTakeDueWhenIdle:(BOOL)idle mode:(NSString *)mode into:(NSMutableArray *)due
{
	NSMutableArray *pending = _pending;
	NSUInteger i = 0;

	[_lock lock];
	while (i < [pending count]) {
		FNQueuedPost *post = [pending objectAtIndex:i];
		BOOL wantsIdle = (post->_style == NSPostWhenIdle);

		if (wantsIdle == idle && fn_modes_allow(post->_modes, mode)) {
			[due addObject:post];
			[pending removeObjectAtIndex:i];
			continue;
		}
		i++;
	}
	[_lock unlock];
}

- (void)fnDrainPendingWhenIdle:(BOOL)idle mode:(NSString *)mode
{
	NSMutableArray *due = [NSMutableArray array];
	NSUInteger i;

	while (YES) {
		NSUInteger before = [due count];

		[self fnTakeDueWhenIdle:idle mode:mode into:due];
		for (i = before; i < [due count]; i++) {
			FNQueuedPost *post = [due objectAtIndex:i];

			[_center postNotification:post->_notification];
		}
		if ([due count] == before) {
			break;		/* nothing left for this phase */
		}
		[due removeAllObjects];
	}
}

- (BOOL)fnHasPendingForMode:(NSString *)mode
{
	NSMutableArray *pending = _pending;
	BOOL found = NO;
	NSUInteger i;

	[_lock lock];
	for (i = 0; i < [pending count]; i++) {
		FNQueuedPost *post = [pending objectAtIndex:i];

		if (fn_modes_allow(post->_modes, mode)) {
			found = YES;
			break;
		}
	}
	[_lock unlock];
	return found;
}

@end

/* ====================================================================================================
 * THE SEAM — what NSRunLoop asks, per phase, of the calling thread's queues
 * ==================================================================================================== */

@implementation NSNotificationQueue (FNRunLoopSeam)

+ (void)fnPostPendingWhenIdle:(BOOL)idle mode:(NSString *)mode
{
	NSMutableArray *queues = fn_registry(NO);
	NSUInteger i;

	for (i = 0; queues != nil && i < [queues count]; i++) {
		NSNotificationQueue *queue = (NSNotificationQueue *)[[queues objectAtIndex:i] pointerValue];

		if (queue != nil) {
			[queue fnDrainPendingWhenIdle:idle mode:mode];
		}
	}
}

+ (BOOL)fnHasPendingWorkForMode:(NSString *)mode
{
	NSMutableArray *queues = fn_registry(NO);
	NSUInteger i;

	for (i = 0; queues != nil && i < [queues count]; i++) {
		NSNotificationQueue *queue = (NSNotificationQueue *)[[queues objectAtIndex:i] pointerValue];

		if (queue != nil && [queue fnHasPendingForMode:mode]) {
			return YES;
		}
	}
	return NO;
}

@end
