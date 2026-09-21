/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_runloop, unit of 1 — F13.18's acceptance for NSRunLoop and NSTimer.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
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
 *
 * W6a ADDS THE SOURCES, which F13.18 shipped WITHOUT (the unit's own named gap): a source is a FILE
 * DESCRIPTOR the loop watches, and the five checks below establish that it is a WAIT and not a poll —
 * a byte waiting fires it, an empty pipe does not, the loop stays alive for it the way it does for a
 * timer, it WAKES ON THE DESCRIPTOR rather than on the deadline, and a source whose target has been
 * deallocated is SKIPPED rather than called. W6b THEN ADDS APPLE'S PORT DOOR over the same seam
 * (`-addPort:forMode:` and `-removePort:forMode:`, both CURRENT API and both unimplementable until a
 * port class existed).
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>		/* pipe(2), write(2): the SOURCE's descriptor */
#include <sys/socket.h>	/* socketpair(2): the PORT's descriptor */

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


/*
 * THE SOURCE'S TARGET. The seam's contract is why this class's method takes NO ARGUMENTS: a source exists
 * to say "ready now", and a caller that wants the descriptor already has it.
 */
@interface SourceProbe : NSObject
{
	NSUInteger _ready;
	double _firstFiredAt;
	int _fd;
	BOOL _drain;
}
- (void)noteReady;
- (NSUInteger)ready;
- (double)firstFiredAt;
- (void)readFrom:(int)fd drain:(BOOL)drain;
- (void)writeLater:(NSArray *)pair;
@end

@implementation SourceProbe

/* WHEN IT FIRST FIRED, which is the only way to show that the loop woke ON THE DESCRIPTOR: the return
 * time of `-runUntilDate:` is its DEADLINE whether or not anything happened, so the moment of the fire is
 * the measurement. */
- (void)noteReady
{
	if (_ready == 0) {
		_firstFiredAt = [[NSDate date] timeIntervalSince1970];
	}
	if (_drain) {
		char byte;

		(void)read(_fd, &byte, 1);	/* consume it: a level-triggered source that is not drained
						 * fires again, correctly, and forever */
	}
	_ready++;
}

- (NSUInteger)ready
{
	return _ready;
}

- (double)firstFiredAt
{
	return _firstFiredAt;
}

- (void)readFrom:(int)fd drain:(BOOL)drain
{
	_fd = fd;
	_drain = drain;
}

/* THE OTHER END OF A PIPE, WRITTEN FROM ITS OWN THREAD AFTER A DELAY. This is what makes
 * `source-waits-for-readiness` a measurement of the WAIT rather than of a poll: the loop has nothing to
 * do until the byte arrives, so when it returns early it is the descriptor that woke it. */
- (void)writeLater:(NSArray *)pair
{
	char byte = 'x';
	int fd = [(NSNumber *)[pair objectAtIndex:0] intValue];
	NSDate *until = [NSDate dateWithTimeIntervalSinceNow:
				[(NSNumber *)[pair objectAtIndex:1] doubleValue]];

	while ([until timeIntervalSinceNow] > 0) {
		;
	}
	(void)write(fd, &byte, 1);
}

@end


/*
 * A PORT THE RUN LOOP IS TOLD TO WATCH — through NSRunLoop's OWN door (`-addPort:forMode:`), which is
 * Apple's current API and was unimplementable until a port class existed (§43). `-portDidBecomeReadable`
 * is ours: the delegate that would have carried the readiness is Apple-deprecated and struck.
 */
@interface FnLoopPort : NSSocketPort
{
	NSUInteger _ready;
}
- (NSUInteger)ready;
- (void)portDidBecomeReadable;
@end

@implementation FnLoopPort

- (NSUInteger)ready
{
	return _ready;
}

- (void)portDidBecomeReadable
{
	_ready++;
}

@end

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

	/* ---- THE SOURCES (W6a): a file descriptor the loop WATCHES ------------ */

	/* WHAT THE FOUR CHECKS BELOW ESTABLISH TOGETHER is that a source is a WAIT and not a POLL: idle
	 * means silence, ready means a fire, the loop stays alive for a source the way it does for a timer,
	 * and a pass with an empty pipe returns at once rather than sleeping. */
	{
		int fds[2];
		char byte = 'x';
		SourceProbe *probe = [[SourceProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		if (pipe(fds) != 0) {
			check("source-fires-when-ready", 0, @"pipe(2) failed");
		} else {
			[loop addSourceForFileDescriptor:fds[0]
						    mode:NSDefaultRunLoopMode
						readable:YES
						  target:probe
						selector:@selector(noteReady)];
			(void)write(fds[1], &byte, 1);
			[loop runMode:NSDefaultRunLoopMode
			   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
			check("source-fires-when-ready", [probe ready] == 1,
			      [NSString stringWithFormat:@"ready=%lu with a byte waiting",
				(unsigned long)[probe ready]]);
			[loop removeSourceForTarget:probe];
			close(fds[0]);
			close(fds[1]);
		}
	}

	{
		int fds[2];
		SourceProbe *probe = [[SourceProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		if (pipe(fds) != 0) {
			check("source-idle-does-not-fire", 0, @"pipe(2) failed");
		} else {
			[loop addSourceForFileDescriptor:fds[0]
						    mode:NSDefaultRunLoopMode
						readable:YES
						  target:probe
						selector:@selector(noteReady)];
			/* NOTHING IS WRITTEN, so there is nothing to read and nothing to say. */
			[loop runMode:NSDefaultRunLoopMode
			   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
			check("source-idle-does-not-fire", [probe ready] == 0,
			      [NSString stringWithFormat:@"ready=%lu with an empty pipe",
				(unsigned long)[probe ready]]);
			[loop removeSourceForTarget:probe];
			close(fds[0]);
			close(fds[1]);
		}
	}

	{
		int fds[2];
		SourceProbe *probe = [[SourceProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		BOOL aliveWithSource, aliveAfterRemoval;

		if (pipe(fds) != 0) {
			check("source-keeps-loop-alive", 0, @"pipe(2) failed");
		} else {
			[loop addSourceForFileDescriptor:fds[0]
						    mode:NSDefaultRunLoopMode
						readable:YES
						  target:probe
						selector:@selector(noteReady)];
			aliveWithSource = [loop runMode:NSDefaultRunLoopMode
					     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			[loop removeSourceForTarget:probe];
			aliveAfterRemoval = [loop runMode:NSDefaultRunLoopMode
						beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			/* A PAIR, because "nothing is live" is only meaningful against "something was": with the
			 * source registered the pass has work and says so; with it gone there is nothing left. */
			check("source-keeps-loop-alive", aliveWithSource && !aliveAfterRemoval,
			      [NSString stringWithFormat:@"with-source=%d after-removal=%d",
				(int)aliveWithSource, (int)aliveAfterRemoval]);
			close(fds[0]);
			close(fds[1]);
		}
	}

	{
		int fds[2];
		SourceProbe *probe = [[SourceProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		NSDate *start = [NSDate date];
		double fired;

		if (pipe(fds) != 0) {
			check("source-waits-for-readiness", 0, @"pipe(2) failed");
		} else {
			[loop addSourceForFileDescriptor:fds[0]
						    mode:NSDefaultRunLoopMode
						readable:YES
						  target:probe
						  selector:@selector(noteReady)];
			[probe readFrom:fds[0] drain:YES];
			[probe writeLater:[NSArray arrayWithObjects:
						[NSNumber numberWithInt:fds[1]],
						[NSNumber numberWithDouble:0.06], nil]];
			[loop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];
			/* IT WOKE ON THE DESCRIPTOR: the byte is written at 0.06s and the deadline is 2s, so the
			 * FIRST FIRE has to fall between them — a fire before 0.06s could only be a poll that lied,
			 * and the deadline itself is not a wake. */
			fired = [probe firstFiredAt] - [start timeIntervalSince1970];
			check("source-waits-for-readiness",
			      [probe ready] == 1 && fired >= 0.04 && fired < 1.0,
			      [NSString stringWithFormat:@"ready=%lu, first fire %.3fs after the loop started "
				@"(byte at 0.06s, deadline 2s; ready-count 1 says it also DRAINED rather than "
				@"re-firing)", (unsigned long)[probe ready], fired]);
			[loop removeSourceForTarget:probe];
			close(fds[0]);
			close(fds[1]);
		}
	}

	{
		int fds[2];
		char byte = 'x';
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		if (pipe(fds) != 0) {
			check("source-dead-target-is-skipped", 0, @"pipe(2) failed");
		} else {
			{
				SourceProbe *dying = [[SourceProbe alloc] init];

				[loop addSourceForFileDescriptor:fds[0]
							    mode:NSDefaultRunLoopMode
							readable:YES
							  target:dying
							selector:@selector(noteReady)];
			}
			/* `dying` IS RELEASED HERE, and the seam holds it as a ZEROING WEAK reference (the runtime's
			 * own functions — this library is MRC). The loop must neither call it nor count it as work:
			 * a run loop outlives its sources' targets routinely, and a dangling call is the failure this
			 * design exists to prevent. */
			(void)write(fds[1], &byte, 1);
			check("source-dead-target-is-skipped",
			      ![loop runMode:NSDefaultRunLoopMode
				    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]],
			      @"the loop counted a dead target's source as live work");
			close(fds[0]);
			close(fds[1]);
		}
	}

	/* ---- APPLE'S PORT DOOR (W6b): `-addPort:forMode:` and `-removePort:forMode:` --------- */

	/* A PAIR, LIKE THE SOURCE CHECKS ABOVE: a port handed to the loop through Apple's door is WATCHED, and
	 * one taken back is not. The door itself is a forward — the port is told to schedule itself — so what
	 * these two establish is that the forward reaches the seam and that removal reaches it too. */
	{
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		int pair[2];
		int pairMade = socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
		FnLoopPort *port = (pairMade == 0)
			? [[FnLoopPort alloc] initWithProtocolFamily:AF_UNIX
							  socketType:SOCK_STREAM
							    protocol:0
							      socket:pair[0]]
			: nil;
		char byte = 'p';
		BOOL quietBefore;

		[loop addPort:port forMode:NSDefaultRunLoopMode];
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		quietBefore = [port ready] == 0;

		(void)write(pair[1], &byte, 1);
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
		check("runloop-addport-schedules",
		      pairMade == 0 && quietBefore && [port ready] == 1,
		      [NSString stringWithFormat:@"socketpair=%d quiet-before=%d ready=%lu",
			pairMade, (int)quietBefore, (unsigned long)[port ready]]);

		[loop removePort:port forMode:NSDefaultRunLoopMode];
		(void)write(pair[1], &byte, 1);
		[loop runMode:NSDefaultRunLoopMode
		   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		check("runloop-removeport-unschedules",
		      [port ready] == 1,
		      [NSString stringWithFormat:@"ready=%lu after -removePort: (was 1)",
			(unsigned long)[port ready]]);

		[port invalidate];
		if (pairMade == 0) {
			close(pair[1]);
		}
	}

	printf("FOUNDATION-RUNLOOP RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-RUNLOOP-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-RUNLOOP DONE\n");
	return failc ? 1 : 0;
}
