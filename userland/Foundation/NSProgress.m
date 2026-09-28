/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProgress.m — how much of a job is done (F13.20).
 *
 * THE ARITHMETIC IS IN ONE METHOD, `-fnCompletedIncludingChildren`, and it is deliberately integer
 * arithmetic: a child contributes `completed * pending / total`, which is exact for the counts a
 * progress tree carries and cannot produce a drifting fraction. A child with a total of ZERO
 * contributes nothing rather than dividing by zero, and a child is allowed to over-report without
 * inflating its parent — the share is what the parent was told to expect, so that is the ceiling.
 *
 * THE CURRENT STACK IS PER THREAD, through a pthread key, because that is what makes
 * `-becomeCurrentWithPendingUnitCount:` mean "children created HERE, by this thread" rather than "by
 * anyone anywhere".
 */

#import <Foundation/NSProgress.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#include <pthread.h>
#include <stdlib.h>

static pthread_key_t fn_stack_key;
static pthread_once_t fn_stack_key_once = PTHREAD_ONCE_INIT;
static BOOL fn_stack_key_ready = NO;

static void fn_make_stack_key(void)
{
	if (pthread_key_create(&fn_stack_key, NULL) == 0) {
		fn_stack_key_ready = YES;
	}
}

/* THE THREAD'S OWN STACK OF {progress, pending} PAIRS, made on first use. */
static NSMutableArray *fn_stack(void)
{
	NSMutableArray *stack;

	pthread_once(&fn_stack_key_once, fn_make_stack_key);
	if (!fn_stack_key_ready) {
		return nil;
	}
	stack = (NSMutableArray *)pthread_getspecific(fn_stack_key);
	if (stack == nil) {
		stack = [[NSMutableArray alloc] init];
		pthread_setspecific(fn_stack_key, (void *)stack);
	}
	return stack;
}

@implementation NSProgress

NSProgressUserInfoKey const NSProgressEstimatedTimeRemainingKey = @"NSProgressEstimatedTimeRemainingKey";
NSProgressUserInfoKey const NSProgressFileAnimationImageKey = @"NSProgressFileAnimationImageKey";
NSProgressUserInfoKey const NSProgressFileOperationKindDuplicating = @"NSProgressFileOperationKindDuplicating";
NSProgressUserInfoKey const NSProgressFileAnimationImageOriginalRectKey = @"NSProgressFileAnimationImageOriginalRectKey";
NSProgressUserInfoKey const NSProgressFileCompletedCountKey = @"NSProgressFileCompletedCountKey";
NSProgressUserInfoKey const NSProgressFileIconKey = @"NSProgressFileIconKey";
NSProgressUserInfoKey const NSProgressFileOperationKindCopying = @"NSProgressFileOperationKindCopying";
NSProgressUserInfoKey const NSProgressFileOperationKindDecompressingAfterDownloading = @"NSProgressFileOperationKindDecompressingAfterDownloading";
NSProgressUserInfoKey const NSProgressFileOperationKindDownloading = @"NSProgressFileOperationKindDownloading";
NSProgressUserInfoKey const NSProgressFileOperationKindKey = @"NSProgressFileOperationKindKey";
NSProgressUserInfoKey const NSProgressFileOperationKindReceiving = @"NSProgressFileOperationKindReceiving";
NSProgressUserInfoKey const NSProgressFileOperationKindUploading = @"NSProgressFileOperationKindUploading";
NSProgressUserInfoKey const NSProgressFileTotalCountKey = @"NSProgressFileTotalCountKey";
NSProgressUserInfoKey const NSProgressFileURLKey = @"NSProgressFileURLKey";
NSProgressUserInfoKey const NSProgressKindFile = @"NSProgressKindFile";
NSProgressUserInfoKey const NSProgressThroughputKey = @"NSProgressThroughputKey";

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_children = [[NSMutableArray alloc] init];
	_userInfo = [[NSDictionary alloc] init];
	return self;
}

+ (nullable NSProgress *)discreteProgressWithTotalUnitCount:(int64_t)unitCount
{
	NSProgress *progress = [[self alloc] init];

	[progress setTotalUnitCount:unitCount];
	return progress;
}

+ (nullable NSProgress *)progressWithTotalUnitCount:(int64_t)unitCount
{
	NSProgress *parent = [self currentProgress];

	if (parent == nil) {
		return [self discreteProgressWithTotalUnitCount:unitCount];
	}
	return [self progressWithTotalUnitCount:unitCount
					 parent:parent
			       pendingUnitCount:[parent fnCurrentPendingUnitCount]];
}

+ (nullable NSProgress *)progressWithTotalUnitCount:(int64_t)unitCount
					     parent:(NSProgress *)parent
				   pendingUnitCount:(int64_t)unitCount2
{
	NSProgress *progress = [self discreteProgressWithTotalUnitCount:unitCount];

	if (progress != nil && parent != nil) {
		[parent addChild:progress withPendingUnitCount:unitCount2];
	}
	return progress;
}

- (int64_t)totalUnitCount
{
	return _totalUnitCount;
}

- (void)setTotalUnitCount:(int64_t)unitCount
{
	_totalUnitCount = unitCount < 0 ? 0 : unitCount;
}

- (int64_t)fnCompletedIncludingChildren
{
	int64_t total = _ownUnitCount;
	NSUInteger i;

	for (i = 0; i + 1 < [_children count]; i += 2) {
		NSProgress *child = [_children objectAtIndex:i];
		int64_t pending = [(NSNumber *)[_children objectAtIndex:i + 1] longLongValue];
		int64_t childTotal = [child totalUnitCount];

		if (childTotal <= 0 || pending <= 0) {
			continue;		/* nothing to scale, and NOT a division by zero */
		}
		{
			int64_t share = ([child fnCompletedIncludingChildren] * pending) / childTotal;

			if (share > pending) {
				share = pending;	/* THE SHARE IS THE CEILING the parent was told to expect */
			}
			if (share > 0) {
				total += share;
			}
		}
	}
	return total;
}

- (int64_t)completedUnitCount
{
	return [self fnCompletedIncludingChildren];
}

- (void)setCompletedUnitCount:(int64_t)unitCount
{
	_ownUnitCount = unitCount < 0 ? 0 : unitCount;
}

- (double)fractionCompleted
{
	if (_totalUnitCount <= 0) {
		return 0.0;
	}
	return (double)[self fnCompletedIncludingChildren] / (double)_totalUnitCount;
}

- (BOOL)isFinished
{
	/* A TOTAL OF ZERO IS NEVER FINISHED: nothing was asked for, so nothing can be done. */
	return _totalUnitCount > 0 && [self fnCompletedIncludingChildren] >= _totalUnitCount;
}

- (void)addChild:(NSProgress *)child withPendingUnitCount:(int64_t)unitCount
{
	if (child == nil) {
		return;
	}
	[_children addObject:child];
	[_children addObject:[NSNumber numberWithLongLong:unitCount]];
}

- (void)fnPushCurrentWithPending:(int64_t)unitCount
{
	NSMutableArray *stack = fn_stack();

	if (stack == nil) {
		return;
	}
	[stack addObject:self];
	[stack addObject:[NSNumber numberWithLongLong:unitCount]];
}

/* THE STACK HOLDS PAIRS — {progress, pending} — SO THE PROGRESS OF THE LAST PAIR IS AT COUNT-2 AND
 * ITS PENDING COUNT AT COUNT-1. Reading `lastObject` as the progress is what made a child attach
 * itself to an NSNumber, and the runtime said so in as many words. */
- (int64_t)fnCurrentPendingUnitCount
{
	NSMutableArray *stack = fn_stack();

	if (stack == nil || [stack count] < 2 || [stack objectAtIndex:[stack count] - 2] != self) {
		return 0;
	}
	return [(NSNumber *)[stack lastObject] longLongValue];
}

- (void)becomeCurrentWithPendingUnitCount:(int64_t)unitCount
{
	[self fnPushCurrentWithPending:unitCount];
}

- (void)resignCurrent
{
	NSMutableArray *stack = fn_stack();

	if (stack == nil || [stack count] < 2) {
		return;
	}
	/* ONLY THE TOP PAIR RESIGNS: a stack that popped another frame's work would unhook it. */
	if ([stack objectAtIndex:[stack count] - 2] == self) {
		[stack removeLastObject];
		[stack removeLastObject];
	}
}

+ (nullable NSProgress *)currentProgress
{
	NSMutableArray *stack = fn_stack();

	return stack != nil && [stack count] >= 2 ? [stack objectAtIndex:[stack count] - 2] : nil;
}

- (void)cancel
{
	NSUInteger i;

	_cancelled = YES;
	/* CANCELLATION PROPAGATES DOWN: a parent that is cancelled is a parent whose children are. */
	for (i = 0; i + 1 < [_children count]; i += 2) {
		[[_children objectAtIndex:i] cancel];
	}
}

- (BOOL)isCancelled
{
	return _cancelled;
}

- (BOOL)isCancellable
{
	return _cancellable;
}

- (void)setCancellable:(BOOL)cancellable
{
	_cancellable = cancellable;
}

- (void)pause
{
	_paused = YES;
}

- (void)resume
{
	_paused = NO;
}

- (BOOL)isPaused
{
	return _paused;
}

- (BOOL)isPausable
{
	return _pausable;
}

- (void)setPausable:(BOOL)pausable
{
	_pausable = pausable;
}

- (NSString *)kind
{
	return _kind != nil ? _kind : @"";
}

- (void)setKind:(NSString *)kind
{
	_kind = kind;
}

- (NSString *)localizedDescription
{
	return _localizedDescription != nil ? _localizedDescription : @"";
}

- (void)setLocalizedDescription:(NSString *)description
{
	_localizedDescription = description;
}

- (NSString *)localizedAdditionalDescription
{
	return _localizedAdditionalDescription != nil ? _localizedAdditionalDescription : @"";
}

- (void)setLocalizedAdditionalDescription:(NSString *)description
{
	_localizedAdditionalDescription = description;
}

- (NSDictionary *)userInfo
{
	return _userInfo;
}

- (void)setUserInfoObject:(nullable id)object forKey:(NSString *)key
{
	NSMutableDictionary *mutable;

	if (key == nil) {
		return;
	}
	mutable = [[NSMutableDictionary alloc] init];
	if (_userInfo != nil) {
		[mutable addEntriesFromDictionary:_userInfo];
	}
	if (object != nil) {
		[mutable setObject:object forKey:key];
	} else {
		[mutable removeObjectForKey:key];
	}
	_userInfo = mutable;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p %lld/%lld (%.2f)%@%@>", [self class], self,
				  (long long)[self fnCompletedIncludingChildren],
				  (long long)_totalUnitCount, [self fractionCompleted],
				  _cancelled ? @" cancelled" : @"",
				  [self isFinished] ? @" finished" : @""];
}

@end
