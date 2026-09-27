/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_notificationqueue — §62.61's acceptance: NSNotificationQueue, and the run-loop seam it needed.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT IS BEING ASSERTED IS THE ONE THING A QUEUE HAS THAT A CENTER DOES NOT: WHEN a notification is delivered.
 * The three posting styles are three DIFFERENT moments — now, the end of this pass, and just before the loop
 * sleeps — so each check asks "has it arrived YET?" at a point where two of the three answers must differ. A
 * queue that ignored the style and posted immediately would fail the negative half of every one of them.
 *
 * AND COALESCING IS ASKED THE SAME WAY: a mask that matches must leave ONE delivery where two were enqueued, and
 * a mask that does not match must leave TWO — with the second half there so "coalescing works" cannot be
 * satisfied by dropping everything.
 *
 * THE OBSERVER IS HELD STRONGLY BY THE PROBE, because a center holds its observers weakly (the rule
 * NSRunLoop.m states where it drops a dead source target) and an observer that dies is dropped rather than
 * called — which would turn every check here into a green one that asserts nothing.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

/* ---- the observer: counts what arrived, and remembers enough to describe a failure ---------------- */
@interface Counter : NSObject
{
	NSInteger _count;
	id _lastSender;
	NSString *_lastName;
}
- (void)note:(NSNotification *)notification;
- (NSInteger)count;
- (void)reset;
- (nullable id)lastSender;
- (nullable NSString *)lastName;
@end

@implementation Counter

- (void)note:(NSNotification *)notification
{
	_count++;
	_lastSender = [notification object];
	_lastName = [notification name];
}

- (NSInteger)count { return _count; }

- (void)reset
{
	_count = 0;
	_lastSender = nil;
	_lastName = nil;
}

- (nullable id)lastSender { return _lastSender; }
- (nullable NSString *)lastName { return _lastName; }

@end

/* ---- a worker, so the DEFAULT QUEUE can be asked about two threads -------------------------------- */
@interface Worker : NSObject
{
	NSNotificationQueue *_queue;
}
- (void)capture:(id)ignored;
- (nullable NSNotificationQueue *)queue;
@end

@implementation Worker

- (void)capture:(id)ignored
{
	(void)ignored;
	_queue = [NSNotificationQueue defaultQueue];
}

- (nullable NSNotificationQueue *)queue { return _queue; }

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NOTIFICATIONQUEUE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NOTIFICATIONQUEUE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	NSNotificationQueue *queue = [NSNotificationQueue defaultQueue];
	/* THE SENDER FOR EVERY NOTIFICATION HERE. `self` is not available in a C function — which is what the
	 * first build of this probe said, once per use — and a sender has to be SOME object, so it is one object
	 * for the whole run. The one check that needs two DISTINCT senders makes its own. */
	id sender = [[NSObject alloc] init];

	check("the-default-queue-is-one-per-thread",
	      queue != nil && queue == [NSNotificationQueue defaultQueue],
	      [NSString stringWithFormat:@"queue=%@", queue]);
	{
		Worker *worker = [[Worker alloc] init];
		double started = 0;
		int spins = 0;

		[NSThread detachNewThreadSelector:@selector(capture:) toTarget:worker withObject:nil];
		started = [NSDate timeIntervalSinceReferenceDate];
		while ([worker queue] == nil && spins < 2000 &&
		       [NSDate timeIntervalSinceReferenceDate] - started < 3.0) {
			[NSThread sleepForTimeInterval:0.005];
			spins++;
		}
		check("another-thread-gets-its-own-default-queue",
		      [worker queue] != nil && [worker queue] != queue,
		      [NSString stringWithFormat:@"main=%@ other=%@", queue, [worker queue]]);
	}

	{
		/* NSPostNow: THE DELIVERY HAS ALREADY HAPPENED WHEN THE ENQUEUE RETURNS. */
		Counter *counter = [[Counter alloc] init];
		NSNotification *note = [NSNotification notificationWithName:@"nq-now" object:sender];

		[center addObserver:counter selector:@selector(note:) name:@"nq-now" object:nil];
		[queue enqueueNotification:note postingStyle:NSPostNow];
		check("post-now-delivers-before-the-call-returns",
		      [counter count] == 1,
		      [NSString stringWithFormat:@"count=%ld", (long)[counter count]]);
		[center removeObserver:counter];
	}

	{
		/* A QUEUED NOTIFICATION COALESCES, AND A "NOW" ONE STILL DOES IT: Apple — NSPostNow posts
		 * "synchronously and immediately after coalescing", which is what separated it from a bare
		 * -postNotification:. So the queued copy is dropped and exactly ONE delivery happens. */
		Counter *counter = [[Counter alloc] init];
		NSNotification *note = [NSNotification notificationWithName:@"nq-coalesce-now" object:sender];

		[center addObserver:counter selector:@selector(note:) name:@"nq-coalesce-now" object:nil];
		[queue enqueueNotification:note
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnName
				  forModes:nil];
		[queue enqueueNotification:note postingStyle:NSPostNow];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("post-now-coalesces-the-queued-one-away",
		      [counter count] == 1,
		      [NSString stringWithFormat:@"count=%ld (1 = the queued copy was dropped)",
			(long)[counter count]]);
		[center removeObserver:counter];
	}

	{
		/* NSPostASAP: NOT YET, THEN YES — one run-loop PASS later. The negative half is the check. */
		Counter *counter = [[Counter alloc] init];
		NSNotification *note = [NSNotification notificationWithName:@"nq-asap" object:sender];
		NSInteger beforePass;

		[center addObserver:counter selector:@selector(note:) name:@"nq-asap" object:nil];
		[queue enqueueNotification:note
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		beforePass = [counter count];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("post-asap-waits-for-the-pass-and-then-arrives",
		      beforePass == 0 && [counter count] == 1,
		      [NSString stringWithFormat:@"before=%ld after=%ld", (long)beforePass,
			(long)[counter count]]);
		[center removeObserver:counter];
	}

	{
		/* NSPostWhenIdle: the loop is entered and immediately has nothing but this to do, so the IDLE phase
		 * is the first thing it reaches. Run with a short limit — and note the loop only enters at all
		 * because a queued notification counts as live work (NSRunLoop's -fnHasLiveWorkForMode:). */
		Counter *counter = [[Counter alloc] init];
		NSNotification *note = [NSNotification notificationWithName:@"nq-idle" object:sender];
		NSInteger beforeRun;

		[center addObserver:counter selector:@selector(note:) name:@"nq-idle" object:nil];
		[queue enqueueNotification:note
			      postingStyle:NSPostWhenIdle
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		beforeRun = [counter count];
		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("post-when-idle-arrives-when-the-loop-is-about-to-wait",
		      beforeRun == 0 && [counter count] == 1,
		      [NSString stringWithFormat:@"before=%ld after=%ld", (long)beforeRun,
			(long)[counter count]]);
		[center removeObserver:counter];
	}

	{
		/* COALESCING, BOTH WAYS: same name + OnName leaves ONE delivery; the same two enqueues with
		 * NoCoalescing leave TWO. The second half is what stops "coalescing" passing by dropping things. */
		Counter *coalesced = [[Counter alloc] init];
		Counter *kept = [[Counter alloc] init];
		NSNotification *first = [NSNotification notificationWithName:@"nq-dup" object:sender];
		NSNotification *second = [NSNotification notificationWithName:@"nq-dup" object:sender];

		[center addObserver:coalesced selector:@selector(note:) name:@"nq-dup" object:nil];
		[queue enqueueNotification:first
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnName
				  forModes:nil];
		[queue enqueueNotification:second
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnName
				  forModes:nil];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		[center removeObserver:coalesced];

		[center addObserver:kept selector:@selector(note:) name:@"nq-dup" object:nil];
		[queue enqueueNotification:first
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		[queue enqueueNotification:second
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("coalescing-leaves-one-and-no-coalescing-leaves-both",
		      [coalesced count] == 1 && [kept count] == 2,
		      [NSString stringWithFormat:@"coalesced=%ld uncoalesced=%ld",
			(long)[coalesced count], (long)[kept count]]);
		[center removeObserver:kept];
	}

	{
		/* THE MASK DECIDES WHAT MATCHES: two notifications share a NAME and differ by SENDER, so
		 * OnSender coalesces neither and OnName coalesces one away. */
		Counter *bySender = [[Counter alloc] init];
		Counter *byName = [[Counter alloc] init];
		id one = [[NSObject alloc] init];
		id two = [[NSObject alloc] init];

		[center addObserver:bySender selector:@selector(note:) name:@"nq-mask" object:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-mask" object:one]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnSender
				  forModes:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-mask" object:two]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnSender
				  forModes:nil];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		[center removeObserver:bySender];

		[center addObserver:byName selector:@selector(note:) name:@"nq-mask" object:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-mask" object:one]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnName
				  forModes:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-mask" object:two]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationCoalescingOnName
				  forModes:nil];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("the-mask-decides-what-counts-as-a-match",
		      [bySender count] == 2 && [byName count] == 1,
		      [NSString stringWithFormat:@"bySender=%ld byName=%ld",
			(long)[bySender count], (long)[byName count]]);
		[center removeObserver:byName];
	}

	{
		/* DEQUEUEING REMOVES ONLY WHAT MATCHES, and what it removes is never posted. */
		Counter *counter = [[Counter alloc] init];

		[center addObserver:counter selector:@selector(note:) name:@"nq-keep" object:nil];
		[center addObserver:counter selector:@selector(note:) name:@"nq-drop" object:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-keep" object:sender]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		[queue enqueueNotification:[NSNotification notificationWithName:@"nq-drop" object:sender]
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:nil];
		[queue dequeueNotificationsMatching:[NSNotification notificationWithName:@"nq-drop"
										  object:nil]
				       coalesceMask:NSNotificationCoalescingOnName];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("dequeue-removes-only-what-matches-and-it-is-never-posted",
		      [counter count] == 1 && [[counter lastName] isEqualToString:@"nq-keep"],
		      [NSString stringWithFormat:@"count=%ld lastName=%@", (long)[counter count],
			[counter lastName]]);
		[center removeObserver:counter];
	}

	{
		/* THE MODES LIST GATES DELIVERY: queued for a mode the loop is not running, it waits; running THAT
		 * mode is what posts it. */
		Counter *counter = [[Counter alloc] init];
		NSNotification *note = [NSNotification notificationWithName:@"nq-modes" object:sender];
		NSInteger afterDefault;

		[center addObserver:counter selector:@selector(note:) name:@"nq-modes" object:nil];
		[queue enqueueNotification:note
			      postingStyle:NSPostASAP
			      coalesceMask:NSNotificationNoCoalescing
				  forModes:[NSArray arrayWithObject:@"NQProbeMode"]];
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		afterDefault = [counter count];
		[[NSRunLoop currentRunLoop] runMode:@"NQProbeMode"
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("a-mode-the-loop-is-not-running-withholds-the-post",
		      afterDefault == 0 && [counter count] == 1,
		      [NSString stringWithFormat:@"afterDefaultMode=%ld afterItsMode=%ld",
			(long)afterDefault, (long)[counter count]]);
		[center removeObserver:counter];
	}

	printf("FOUNDATION-NOTIFICATIONQUEUE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-NOTIFICATIONQUEUE DONE\n");
	return failc == 0 ? 0 : 1;
}
