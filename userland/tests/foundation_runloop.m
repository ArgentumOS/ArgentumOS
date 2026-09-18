/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_runloop, unit of 1 — F13.18's acceptance for NSRunLoop and NSTimer.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>.
 *
 * EVERY RUN IN THIS FILE IS BOUNDED BY A DEADLINE. `-run` never returns while a repeating timer is
 * live, so a probe that called it would hang rather than fail — the same rule the thread probe
 * follows, for the same reason.
 *
 * THE CHECKS THAT EARN THEIR PLACE:
 *   timer-repeats-until-invalidated  a repeating timer's COUNT rises, and then STOPS rising the
 *                                    moment it is invalidated — two numbers that must differ;
 *   timer-order-follows-dates        two timers scheduled out of order fire in DATE order, so the
 *                                    order recorded is the measurement;
 *   runloop-is-per-thread            a new thread's run loop is a different object from the main
 *                                    thread's, which is what makes +currentRunLoop per-thread at all.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

/* THE TARGET: what each check's timers tell, and in what order. */
@interface LoopProbe : NSObject
{
	NSUInteger _fires;
	NSUInteger _firstTimer;
	NSUInteger _secondTimer;
	NSMutableString *_order;
	NSRunLoop *_otherLoop;
}
- (void)count:(id)timer;
- (void)recordFirst:(id)timer;
- (void)recordSecond:(id)timer;
- (void)noteLoop:(id)ignored;
- (NSUInteger)fires;
- (NSUInteger)firstTimer;
- (NSUInteger)secondTimer;
- (NSString *)order;
- (NSRunLoop *)otherLoop;
@end

@implementation LoopProbe

- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_order = [[NSMutableString alloc] init];
	}
	return self;
}

- (void)count:(id)timer
{
	(void)timer;
	_fires++;
}

- (void)recordFirst:(id)timer
{
	(void)timer;
	_firstTimer = _fires;
	[_order appendString:@"first"];
	_fires++;
}

- (void)recordSecond:(id)timer
{
	(void)timer;
	_secondTimer = _fires;
	[_order appendString:@"second"];
	_fires++;
}

- (NSUInteger)fires { return _fires; }
- (NSUInteger)firstTimer { return _firstTimer; }
- (NSUInteger)secondTimer { return _secondTimer; }
- (NSString *)order { return _order; }

/* CALLED FROM THE DETACHED THREAD, which is the only way this file can see another thread's loop:
 * the pointer is captured where the OTHER thread is the current one. */
- (void)noteLoop:(id)ignored
{
	(void)ignored;
	_otherLoop = [NSRunLoop currentRunLoop];
}

- (NSRunLoop *)otherLoop { return _otherLoop; }

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-RUNLOOP %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-RUNLOOP %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:0.03
								  target:probe
								selector:@selector(count:)
								userInfo:nil
								 repeats:NO];

		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];
		check("timer-fires-once",
		      [probe fires] == 1 && ![timer isValid],
		      [NSString stringWithFormat:@"fires=%lu valid=%d", (unsigned long)[probe fires],
			(int)[timer isValid]]);
	}

	{
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:0.02
								  target:probe
								selector:@selector(count:)
								userInfo:nil
								 repeats:YES];
		NSUInteger before;
		NSUInteger after;

		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.15]];
		before = [probe fires];
		[timer invalidate];
		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		after = [probe fires];
		check("timer-repeats-until-invalidated",
		      before >= 3 && after == before && ![timer isValid],
		      [NSString stringWithFormat:@"before=%lu after=%lu valid=%d",
			(unsigned long)before, (unsigned long)after, (int)[timer isValid]]);
	}

	{
		LoopProbe *probe = [[LoopProbe alloc] init];

		/* SCHEDULED OUT OF ORDER ON PURPOSE: the later date first, so an order that simply recorded
		 * the scheduling order would be visibly wrong. */
		[NSTimer scheduledTimerWithTimeInterval:0.06
						 target:probe
					       selector:@selector(recordSecond:)
					       userInfo:nil
						repeats:NO];
		[NSTimer scheduledTimerWithTimeInterval:0.01
						 target:probe
					       selector:@selector(recordFirst:)
					       userInfo:nil
						repeats:NO];
		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];
		check("timer-order-follows-dates",
		      [[probe order] isEqualToString:@"firstsecond"] &&
		      [probe firstTimer] == 0 && [probe secondTimer] == 1,
		      [NSString stringWithFormat:@"order=%@ first=%lu second=%lu", [probe order],
			(unsigned long)[probe firstTimer], (unsigned long)[probe secondTimer]]);
	}

	{
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSString *info = @"carried";
		NSTimer *timer = [NSTimer timerWithTimeInterval:0.02
							 target:probe
						       selector:@selector(count:)
						       userInfo:info
							repeats:NO];

		check("timer-userinfo-and-interval",
		      timer != nil && [timer userInfo] == info &&
		      [timer timeInterval] == 0.02 && [timer fireDate] != nil &&
		      [[timer fireDate] timeIntervalSinceNow] > 0 &&
		      [timer isValid],
		      [NSString stringWithFormat:@"userInfo=%@ interval=%.3f",
			[timer userInfo], [timer timeInterval]]);
	}

	{
		/* NOT ADDED TO ANY LOOP, so the loop must not fire it — and -fire must, once. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSTimer *timer = [NSTimer timerWithTimeInterval:0.01
							 target:probe
						       selector:@selector(count:)
						       userInfo:nil
							repeats:NO];

		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		{
			NSUInteger afterLoop = [probe fires];

			[timer fire];
			check("timer-unscheduled-is-inert",
			      afterLoop == 0 && [probe fires] == 1 && ![timer isValid],
			      [NSString stringWithFormat:@"inLoop=%lu afterFire=%lu valid=%d",
				(unsigned long)afterLoop, (unsigned long)[probe fires],
				(int)[timer isValid]]);
		}
	}

	{
		/* THE OTHER THREAD RECORDS ITS OWN LOOP, and the main thread compares the two POINTERS:
		 * asking for the loop here would only ever answer this thread's. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *main = [NSRunLoop currentRunLoop];

		/* A BOUNDED POLL RATHER THAN A SLEEP: +sleepForTimeInterval: does not sleep on this kernel
		 * (F13.17 measured it), so waiting by sleeping waits for nothing. */
		[NSThread detachNewThreadSelector:@selector(noteLoop:) toTarget:probe withObject:nil];
		{
			double deadline = [[NSDate dateWithTimeIntervalSinceNow:2.0] timeIntervalSince1970];

			while ([probe otherLoop] == nil &&
			       [[NSDate date] timeIntervalSince1970] < deadline) {
				;
			}
		}
		check("runloop-is-per-thread",
		      main != nil && main == [NSRunLoop currentRunLoop] &&
		      [probe otherLoop] != nil && [probe otherLoop] != main &&
		      [NSRunLoop mainRunLoop] == main,
		      [NSString stringWithFormat:@"sameOnMain=%d otherDiffers=%d mainIsFirst=%d",
			(int)(main == [NSRunLoop currentRunLoop]),
			(int)([probe otherLoop] != nil && [probe otherLoop] != main),
			(int)([NSRunLoop mainRunLoop] == main)]);
	}

	{
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:0.01
								  target:probe
								selector:@selector(count:)
								userInfo:nil
								 repeats:NO];

		/* POLL UNTIL THE TIMER IS DUE, for the same reason as above: there is no sleep here to
		 * borrow, so the wait is on the CLOCK, which does advance. */
		{
			double deadline = [[NSDate dateWithTimeIntervalSinceNow:2.0] timeIntervalSince1970];

			while ([[timer fireDate] timeIntervalSinceNow] > 0 &&
			       [[NSDate date] timeIntervalSince1970] < deadline) {
				;
			}
		}
		/* ONE PASS, WHICH IS WHAT runMode:beforeDate: PROMISES. */
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
		check("runmode-one-pass",
		      [probe fires] == 1,
		      [NSString stringWithFormat:@"fires=%lu", (unsigned long)[probe fires]]);
	}

	printf("FOUNDATION-RUNLOOP RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-RUNLOOP DONE\n");
	return failc ? 1 : 0;
}
