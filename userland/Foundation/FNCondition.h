/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNCondition.h — THE CONDITION ITS CONSUMERS WAIT ON, FIRST-PARTY (§63.170).
 *
 * ⚠⚠ THE VOCABULARY MOVED RATHER THAN WENT, WHICH IS THE §63.164 PRECEDENT: `NSCondition` is a 10.5 CLASS
 * and the 10.2 baseline cut it, but ITS IMPLEMENTATION WAS ALREADY musl pthreads (`pthread_mutex_t` +
 * `pthread_cond_t`) AND THREE LIBRARY FILES WAIT ON IT — `NSOperation.m` twice and `NSTask.m` once, all
 * through an ivar. So the body stays and the PUBLIC NAME goes: this class is not Apple's API and is not
 * declared in any <Foundation/….h> header, so a caller cannot reach it.
 *
 * ONLY THE FOUR DOORS THE CONSUMERS ACTUALLY SEND were carried over — `-lock`, `-unlock`, `-wait`,
 * `-broadcast`. `-signal`, `-waitUntilDate:`, `-setName:` and `-name` were part of Apple's surface and none
 * of them has a caller left, so carrying them would be recreating the class under a new name.
 */
#import <Foundation/NSObject.h>
#import <Foundation/NSLock.h>

NS_ASSUME_NONNULL_BEGIN

@interface FNCondition : NSObject <NSLocking>
{
	void *_mutex;
	void *_condition;
	NSString *_name;	/* the leftover `-name`/`-setName:` body in NSLock.m reads it */
}

- (void)wait;
- (void)signal;
- (void)broadcast;

@end

NS_ASSUME_NONNULL_END
