/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProgress — how much of a job is done, and who is doing it. F13.20,
 * docs/design/foundation-plan.md §10, the last name in its mechanism table.
 *
 * IT IS A TREE, AND THE TREE IS THE POINT. A parent reports its OWN work plus a SHARE of each child's,
 * where the share is what the child was given with `-becomeCurrentWithPendingUnitCount:` or
 * `-addChild:withPendingUnitCount:`. So a download that is 2 of 4 chunks done contributes exactly half
 * of the 50 units its parent was told to expect — which is the arithmetic this class exists to do, and
 * the arithmetic a caller cannot do for themselves without knowing their children's scales.
 *
 * THE CHILD IS FOUND BY BEING CURRENT: `-becomeCurrentWithPendingUnitCount:` makes a progress object
 * the one that newly created children attach themselves to, per thread, until `-resignCurrent`. That is
 * how a method that creates work deep inside a call stack attaches it to the right parent without
 * being handed one.
 *
 * WHERE THE ARITHMETIC IS DELIBERATELY SIMPLE, and stated rather than implied: the getter for
 * `-completedUnitCount` answers own + children's shares, while the SETTER sets the own value; a child
 * with no total unit count contributes nothing rather than dividing by zero; and `-isFinished` means
 * the reported count has reached the total, with a total of zero never finished.
 *
 * WHAT IS NOT HERE, named: `-publish`/`-unpublish` and the subscriber doors (they need a reporting
 * coordinator this library has no home for), `-cancellationHandler` (no blocks), `-estimatedTimeRemaining`
 * and `-throughput`, and `NSProgress`'s KVO announcements for its own published properties beyond what
 * F13.9's registry would need wired here.
 */

#ifndef FOUNDATION_NSPROGRESS_H
#define FOUNDATION_NSPROGRESS_H

#import <Foundation/NSObject.h>
#include <stdint.h>

@class NSArray;
@class NSMutableArray;		/* the ivar needs the NAME, and NSArray is not NSMutableArray */
@class NSDictionary;
@class NSString;

@class NSProgress;

NS_ASSUME_NONNULL_BEGIN

typedef NSString *NSProgressKind;
typedef NSString *NSProgressFileOperationKind;
typedef NSString *NSProgressUserInfoKey;

/* THE TWO HANDLER TYPES ARE BLOCKS, which is what Apple declares: a publisher is handed the progress to
 * observe, and an unpublishing handler is handed the same. Neither is CALLED anywhere in this system yet -
 * -publish and -unpublish have no implementation to call them from - so they are declared as the signatures
 * they are, with that said rather than implied. */
typedef void (^NSProgressPublishingHandler)(NSProgress *progress);
typedef void (^NSProgressUnpublishingHandler)(void);

@interface NSProgress : NSObject
{
	int64_t _totalUnitCount;
	int64_t _ownUnitCount;
	NSMutableArray *_children;	/* alternating NSProgress and NSNumber(pending units) */
	NSDictionary *_userInfo;
	NSString *_kind;
	NSString *_localizedDescription;
	NSString *_localizedAdditionalDescription;
	BOOL _cancellable;
	BOOL _cancelled;
	BOOL _pausable;
	BOOL _paused;
}

+ (nullable NSProgress *)discreteProgressWithTotalUnitCount:(int64_t)unitCount;
+ (nullable NSProgress *)progressWithTotalUnitCount:(int64_t)unitCount;
+ (nullable NSProgress *)progressWithTotalUnitCount:(int64_t)unitCount
					     parent:(NSProgress *)parent
				   pendingUnitCount:(int64_t)unitCount;

/* THE THREE NUMBERS, and the count the caller reads is not always the one they set. */
- (int64_t)totalUnitCount;
- (void)setTotalUnitCount:(int64_t)unitCount;
- (int64_t)completedUnitCount;
- (void)setCompletedUnitCount:(int64_t)unitCount;
- (double)fractionCompleted;
- (BOOL)isFinished;

/* THE TREE. */
- (void)becomeCurrentWithPendingUnitCount:(int64_t)unitCount;
- (void)addChild:(NSProgress *)child withPendingUnitCount:(int64_t)unitCount;
- (void)resignCurrent;
+ (nullable NSProgress *)currentProgress;

- (void)cancel;
- (BOOL)isCancelled;
- (BOOL)isCancellable;
- (void)setCancellable:(BOOL)cancellable;
- (void)pause;
- (void)resume;
- (BOOL)isPaused;
- (BOOL)isPausable;
- (void)setPausable:(BOOL)pausable;

- (NSString *)kind;
- (void)setKind:(NSString *)kind;
- (NSString *)localizedDescription;
- (void)setLocalizedDescription:(NSString *)description;
- (NSString *)localizedAdditionalDescription;
- (void)setLocalizedAdditionalDescription:(NSString *)description;

- (NSDictionary *)userInfo;
- (void)setUserInfoObject:(nullable id)object forKey:(NSString *)key;

- (NSString *)description;

@end


/*
 * NSProgressReporting (W2h) - the protocol an object adopts to PUBLISH its progress: one required
 * property, and nothing else. It is a protocol rather than a class, so a consumer declares
 * conformance and this library needs no storage for it.
 *
 * IT INHERITS THE NSObject PROTOCOL, which is why it is here second rather than first: that
 * protocol was the missing dependency when this row was opened (§12: a dependency is ADDED, not
 * refused), and a protocol that inherited an undeclared one could not be written at all.
 */
@protocol NSProgressReporting <NSObject>
@property (readonly) NSProgress *progress;
@end

/* ---- THE FILE-OPERATION KIND AND THE USER-INFO KEYS (the coverage slice) ---------------------------
 *
 * A KIND AND ITS VALUES ARE A WIRE PAIR: NSProgressFileOperationKindKey is asked for with a kind, and the kinds
 * below are the answers - so the check asserts both that each answers its own name (this library's convention
 * for user-info keys, see NSLocale.h) and that the key exists to be asked with. Nothing in this system publishes
 * progress yet; the vocabulary ships ahead of the door, as elsewhere. */
extern NSProgressUserInfoKey const NSProgressEstimatedTimeRemainingKey;
extern NSProgressUserInfoKey const NSProgressFileAnimationImageKey;
extern NSProgressUserInfoKey const NSProgressFileAnimationImageOriginalRectKey;
extern NSProgressUserInfoKey const NSProgressFileCompletedCountKey;
extern NSProgressUserInfoKey const NSProgressFileIconKey;
extern NSProgressUserInfoKey const NSProgressFileOperationKindCopying;
extern NSProgressUserInfoKey const NSProgressFileOperationKindDecompressingAfterDownloading;
/* §62.103: the kind a copy answers with. Declared beside the two that were here, and valued as its name. */
extern NSProgressUserInfoKey const NSProgressFileOperationKindDuplicating;
extern NSProgressUserInfoKey const NSProgressFileOperationKindDownloading;
extern NSProgressUserInfoKey const NSProgressFileOperationKindKey;
extern NSProgressUserInfoKey const NSProgressFileOperationKindReceiving;
extern NSProgressUserInfoKey const NSProgressFileOperationKindUploading;
extern NSProgressUserInfoKey const NSProgressFileTotalCountKey;
extern NSProgressUserInfoKey const NSProgressFileURLKey;
extern NSProgressUserInfoKey const NSProgressKindFile;
extern NSProgressUserInfoKey const NSProgressThroughputKey;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPROGRESS_H */
