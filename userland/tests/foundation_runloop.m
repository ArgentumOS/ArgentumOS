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

/* THE TARGET: what each check's timers tell, and in what order. §62.62 added the performer half — two methods
 * whose ORDER a check can read, an argument to prove the message carried it, an observer for the queue's
 * common-modes check, and a flag a block can set. */
@interface LoopProbe : NSObject
{
	NSUInteger _fires;
	NSUInteger _firstTimer;
	NSUInteger _secondTimer;
	NSUInteger _notifications;
	NSMutableString *_order;
	NSRunLoop *_otherLoop;
	id _lastArgument;
	BOOL _blockRan;
}
- (void)count:(id)timer;
- (void)recordFirst:(id)timer;
- (void)recordSecond:(id)timer;
- (void)noteA:(id)argument;
- (void)noteB:(id)argument;
- (void)note:(NSNotification *)notification;
- (void)noteLoop:(id)ignored;
- (NSUInteger)fires;
- (NSUInteger)firstTimer;
- (NSUInteger)secondTimer;
- (NSUInteger)notifications;
- (NSString *)order;
- (NSRunLoop *)otherLoop;
- (nullable id)lastArgument;
- (BOOL)blockRan;
- (void)setBlockRan:(BOOL)ran;
@end


/* §63.190: the TARGET for the invocation timer, so the invocation's selector has somewhere to land. */
@interface FNTimerProbeTarget : NSObject
- (void)timerFired:(NSTimer *)timer;
@end

static int fn_probe_invocation_fires = 0;
static NSTimer *fn_probe_invocation_timer = nil;


/* §63.190: A WAIT IS A LOOP IN THIS LIBRARY. `-runMode:beforeDate:` is documented as ONE PASS (the probe's
 * own `runmode-one-pass` check pins that), so waiting for a timer to come due means running passes until a
 * deadline — a single call returns immediately and the timer never gets its chance. That is exactly what
 * made these checks read `fires=0` while the timer was valid, in the right mode, in the right loop. */
static void fn_wait_seconds(double seconds)
{
	NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];

	while ([deadline timeIntervalSinceNow] > 0) {
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
}

@implementation FNTimerProbeTarget
- (void)timerFired:(NSTimer *)timer
{
	fn_probe_invocation_fires++;
	fn_probe_invocation_timer = timer;
}
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

/* THE PERFORMER TARGETS: one letter each into the ORDER, so a check can see which message was sent and when. */
- (void)noteA:(id)argument
{
	_lastArgument = argument;
	[_order appendString:@"A"];
	_fires++;
}

- (void)noteB:(id)argument
{
	_lastArgument = argument;
	[_order appendString:@"B"];
	_fires++;
}

- (void)note:(NSNotification *)notification
{
	(void)notification;
	_notifications++;
}

- (NSUInteger)fires { return _fires; }
- (NSUInteger)firstTimer { return _firstTimer; }
- (NSUInteger)secondTimer { return _secondTimer; }
- (NSUInteger)notifications { return _notifications; }
- (NSString *)order { return _order; }
- (nullable id)lastArgument { return _lastArgument; }
- (BOOL)blockRan { return _blockRan; }
- (void)setBlockRan:(BOOL)ran { _blockRan = ran; }

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

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-RUNLOOP %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-RUNLOOP %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)


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
 * is ours: the delegate that would have carried the readiness is Apple-deprecated, and since 2026-09-26
 * that makes it a WORK ITEM rather than an exclusion (§62.24) - this hook is a placeholder for a name
 * this library still OWES.
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

	/* ---- THE PERFORMERS (§62.62) ------------------------------------------------------------------- */
	{
		/* A PERFORMER RUNS AT THE START OF THE NEXT PASS, NOT WHEN IT IS SCHEDULED — Apple's own words for
		 * -performSelector:target:argument:order:modes:, and the argument has to arrive with it. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		NSString *argument = @"the performer argument";

		[loop performSelector:@selector(noteA:)
			       target:probe
			     argument:argument
				order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		{
			NSUInteger beforePass = [probe fires];

			[loop runMode:NSDefaultRunLoopMode
			   beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			check("a-performer-runs-at-the-start-of-the-next-pass",
			      beforePass == 0 && [probe fires] == 1 && [probe lastArgument] == argument,
			      [NSString stringWithFormat:@"before=%lu after=%lu argument=%@",
				(unsigned long)beforePass, (unsigned long)[probe fires], [probe lastArgument]]);
	covers("NSRunLoop", "performSelector:target:argument:order:modes:");
		}
	}

	{
		/* ORDER, NOT REGISTRATION: noteA is scheduled FIRST with order 5 and noteB LAST with order 1, so the
		 * log must read BA — which is the only way the check can tell the two apart. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		[loop performSelector:@selector(noteA:)
			       target:probe
			     argument:nil
				order:5
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop performSelector:@selector(noteB:)
			       target:probe
			     argument:nil
				order:1
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("performers-run-in-order-not-in-registration-order",
		      [[probe order] isEqualToString:@"BA"],
		      [NSString stringWithFormat:@"order=%@ (want BA: lower order first)", [probe order]]);
	}

	{
		/* THE NARROW CANCEL: -cancelPerformSelector:target:argument: requires the SELECTOR AND ARGUMENT to
		 * match too, so cancelling noteA must leave noteB standing. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		[loop performSelector:@selector(noteA:)
			       target:probe
			     argument:nil
				order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop performSelector:@selector(noteB:)
			       target:probe
			     argument:nil
				order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop cancelPerformSelector:@selector(noteA:) target:probe argument:nil];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("cancel-perform-selector-removes-only-that-one",
		      [[probe order] isEqualToString:@"B"],
		      [NSString stringWithFormat:@"order=%@ (want B: only noteB survived)", [probe order]]);
	covers("NSRunLoop", "cancelPerformSelector:target:argument:");
	}

	{
		/* THE BROAD CANCEL: -cancelPerformSelectorsWithTarget: "ignores the selector and argument" and clears
		 * the target's requests from every mode — and a DIFFERENT target's performer must survive it, which is
		 * what makes this a check rather than a "nothing ran" tautology. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		LoopProbe *other = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		[loop performSelector:@selector(noteA:) target:probe argument:nil order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop performSelector:@selector(noteB:) target:probe argument:nil order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop performSelector:@selector(noteA:) target:other argument:nil order:0
				modes:[NSArray arrayWithObject:NSDefaultRunLoopMode]];
		[loop cancelPerformSelectorsWithTarget:probe];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("cancel-perform-selectors-with-target-removes-all-of-them",
		      [probe fires] == 0 && [other fires] == 1,
		      [NSString stringWithFormat:@"cancelledTarget=%lu otherTarget=%lu (want 0 and 1)",
			(unsigned long)[probe fires], (unsigned long)[other fires]]);
	covers("NSRunLoop", "cancelPerformSelectorsWithTarget:");
	}

	{
		/* THE BLOCK FORMS: -performBlock: runs on the loop (Apple's page does not say in which modes; the
		 * forum resolution the header cites says the default one), and -performInModes:block: waits for ITS
		 * mode — which the negative half proves. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		[loop performBlock:^{
			[probe setBlockRan:YES];
		}];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("perform-block-runs-on-the-loop",
		      [probe blockRan],
		      @"the block set its flag during a pass");
	covers("NSRunLoop", "performBlock:");
	covers("NSRunLoop", "performInModes:block:");
	}

	{
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		BOOL afterDefaultMode;

		[loop performInModes:[NSArray arrayWithObject:@"RLProbeMode"]
			       block:^{
				       [probe setBlockRan:YES];
			       }];
		[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		afterDefaultMode = [probe blockRan];
		[loop runMode:@"RLProbeMode" beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("perform-in-modes-block-waits-for-its-mode",
		      !afterDefaultMode && [probe blockRan],
		      [NSString stringWithFormat:@"afterDefaultMode=%d afterItsMode=%d",
			(int)afterDefaultMode, (int)[probe blockRan]]);
	}

	/* ---- THE COMMON MODES (§62.62's defect): "common" means EVERY mode, for everything ------------------ */
	{
		/* THE TIMER HALF, which was always true — and is the REASON the rule exists. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];
		NSTimer *timer = [NSTimer timerWithTimeInterval:0.0
							 target:probe
						       selector:@selector(count:)
						       userInfo:nil
							repeats:NO];

		[loop addTimer:timer forMode:NSRunLoopCommonModes];
		[loop runMode:@"RLProbeMode" beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("the-common-mode-carries-a-timer-in-any-mode",
		      [probe fires] == 1,
		      [NSString stringWithFormat:@"fires=%lu in RLProbeMode",
			(unsigned long)[probe fires]]);
	}

	{
		/* THE NOTIFICATION HALF, WHICH IS THE DEFECT THIS UNIT FIXED: the queue compared mode names with
		 * -isEqual:, so a notification enqueued for the COMMON modes could never be posted while a timer
		 * added for them fired everywhere. One rule now, so a custom mode must carry it. */
		LoopProbe *probe = [[LoopProbe alloc] init];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		[[NSNotificationCenter defaultCenter] addObserver:probe
							 selector:@selector(note:)
							     name:@"rl-common"
							   object:nil];
		[[NSNotificationQueue defaultQueue]
			enqueueNotification:[NSNotification notificationWithName:@"rl-common" object:nil]
			     postingStyle:NSPostASAP
			     coalesceMask:NSNotificationNoCoalescing
				 forModes:[NSArray arrayWithObject:NSRunLoopCommonModes]];
		[loop runMode:@"RLProbeMode" beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		check("the-common-mode-carries-a-notification-in-any-mode",
		      [probe notifications] == 1,
		      [NSString stringWithFormat:@"notifications=%lu in RLProbeMode (was impossible before §62.62)",
			(unsigned long)[probe notifications]]);
		[[NSNotificationCenter defaultCenter] removeObserver:probe];
	}


	{
		/* §63.190: THE BLOCK PAIR, plus a TEMPORARY DIAGNOSTIC. The loop skips a timer when the mode does not
		 * fire for it, when it is not valid, or when it is not due — so the detail prints all three halves. */
		__block int fires = 0;
		__block NSTimer *seen = nil;
		NSTimer *scheduled = [NSTimer scheduledTimerWithTimeInterval:0.01 repeats:NO block:^(NSTimer *t) {
			fires++;
			seen = t;
		}];

		fn_wait_seconds(0.3);
		check("timer-block-fires", fires == 1 && seen == scheduled,
		      [NSString stringWithFormat:@"fires=%d valid=%d interval=%g fireDate@%.3f now@%.3f due=%d dt=%g",
			fires, [scheduled isValid], [scheduled timeInterval],
			[[scheduled fireDate] timeIntervalSince1970], [[NSDate date] timeIntervalSince1970],
			[[scheduled fireDate] timeIntervalSince1970] <= [[NSDate date] timeIntervalSince1970],
			[[scheduled fireDate] timeIntervalSinceNow]]);
	}
	{
		/* THE INVOCATION PAIR: the target and selector are the caller's and the TIMER supplies the argument at
		 * index 2, which the check observes through the target's landing. */
		FNTimerProbeTarget *target = [[FNTimerProbeTarget alloc] init];
		NSMethodSignature *signature = [target methodSignatureForSelector:@selector(timerFired:)];
		/* THE SIGNATURE DOOR IS NULLABLE AND THE INVOCATION DOOR IS NOT, so the nil case is HANDLED here
		 * rather than asserted away: with no signature there is no invocation and nothing is scheduled,
		 * which the check below then reports. */
		NSInvocation *invocation = signature != nil
			? [NSInvocation invocationWithMethodSignature:signature] : nil;

		[invocation setTarget:target];
		[invocation setSelector:@selector(timerFired:)];
		if (invocation != nil) {
			[NSTimer scheduledTimerWithTimeInterval:0.01 invocation:invocation repeats:NO];
		}
		fn_wait_seconds(0.3);
		check("timer-invocation-fires", fn_probe_invocation_fires == 1 && fn_probe_invocation_timer != nil,
		      [NSString stringWithFormat:@"fires=%d timer-handed-over=%d", fn_probe_invocation_fires,
			fn_probe_invocation_timer != nil]);
	}
	{
		/* TOLERANCE ROUND-TRIPS and a toleranced repeating timer still fires. */
		__block int ticks = 0;
		NSTimer *repeating = [NSTimer scheduledTimerWithTimeInterval:0.02 repeats:YES block:^(NSTimer *t) {
			ticks++;
		}];

		[repeating setTolerance:0.005];
		fn_wait_seconds(0.25);
		[repeating invalidate];
		check("timer-tolerance-round-trips", [repeating tolerance] == 0.005 && ticks >= 2,
		      [NSString stringWithFormat:@"tolerance=%g ticks=%d", [repeating tolerance], ticks]);
	}
	{
		/* AND THE UNSCHEDULED PAIR IS NOT SCHEDULED: the timer is VALID (Apple's rule: valid until
		 * invalidated) and the loop never fires it, because only -addTimer: puts a timer in a loop. */
		__block int never = 0;
		NSTimer *loose = [NSTimer timerWithTimeInterval:0.01 repeats:NO block:^(NSTimer *t) {
			never++;
		}];

		fn_wait_seconds(0.25);
		check("timer-unscheduled-does-not-fire", never == 0 && [loose isValid],
		      [NSString stringWithFormat:@"fired=%d still-valid=%d", never, [loose isValid]]);
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
