/*
 * foundation_dobjects.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * DISTRIBUTED OBJECTS (§62.56): `NSConnection`, `NSDistantObject` and `NSPortCoder`, on this library's own
 * transport with this library's own name server.
 *
 * THE PROPERTY THAT MATTERS IS THAT A CALL GOES SOMEWHERE AND THE ANSWER COMES BACK, so nearly every check is a
 * round trip through a proxy whose result the probe can predict: the service computes a string from its argument,
 * and the client gets THAT STRING rather than something it built itself. A proxy that returned a plausible value
 * without travelling would pass a weaker check; it cannot pass this one.
 *
 * THE BOUNDARY IS MEASURED FROM BOTH SIDES, which is the other half of the design: a method whose RESULT is not an
 * object is refused by the SERVICE (the client raises with the service's own message), and a method whose ARGUMENT
 * is not an object is refused by the PROXY before anything is sent — which the service's call counter confirms,
 * because a refusal that still called the method would be no boundary at all.
 *
 * AND THE ONE NON-OBVIOUS RULE IS EXERCISED RATHER THAN ASSUMED: the service and the client share a run loop, and
 * both ports are watched in `NSConnectionReplyMode`, because the client's wait is what pumps the service.
 */
#import <Foundation/Foundation.h>
#include <signal.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-DOBJECTS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DOBJECTS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* ---- the doors a client may call, and the ones it may not ---- */

@protocol FnEchoDoors <NSObject>
- (NSString *)greet:(NSString *)who;
- (NSString *)shout:(NSString *)word and:(NSString *)more;
- (id)nothing:(id)thing;
- (int)count;			/* a scalar RESULT: refused by the service */
- (id)doubleNumber:(int)number;	/* a scalar ARGUMENT: refused by the proxy */
@end

/* ---- the service: two doors that cross, and two that cannot ---- */

@interface FnEchoService : NSObject
{
	int _calls;			/* how many times ANY object door was invoked */
	int _scalarCalls;		/* how many times the scalar door was invoked — it must stay zero */
}
- (NSString *)greet:(NSString *)who;
- (NSString *)shout:(NSString *)word and:(NSString *)more;
- (id)nothing:(id)thing;
- (int)count;				/* a scalar RESULT: refused by the service */
- (id)doubleNumber:(int)number;		/* a scalar ARGUMENT: refused by the proxy */
- (int)calls;
- (int)scalarCalls;
@end

@implementation FnEchoService

- (NSString *)greet:(NSString *)who
{
	_calls++;
	return [NSString stringWithFormat:@"hello %@", who];
}

- (NSString *)shout:(NSString *)word and:(NSString *)more
{
	_calls++;
	return [NSString stringWithFormat:@"%@! %@!", word, more];
}

- (id)nothing:(id)thing
{
	_calls++;
	(void)thing;
	return nil;			/* a nil RESULT crosses as "no value", which is not the same as an error */
}

- (int)count { return 42; }

- (id)doubleNumber:(int)number { _scalarCalls++; return [NSNumber numberWithInt:number * 2]; }

- (int)calls { return _calls; }
- (int)scalarCalls { return _scalarCalls; }

@end

/* ---- a listener for the connection's own notification ---- */

@interface FnDieWatcher : NSObject
{
	int _died;
}
- (int)died;
- (void)note:(NSNotification *)notification;
@end

@implementation FnDieWatcher

- (int)died { return _died; }

- (void)note:(NSNotification *)notification
{
	(void)notification;
	_died++;
}

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, SIG_IGN);

	FnEchoService *service = [[FnEchoService alloc] init];
	NSConnection *serving = [NSConnection serviceConnectionWithName:@"probe.echo" rootObject:service];
	FnDieWatcher *watcher = [[FnDieWatcher alloc] init];

	[[NSNotificationCenter defaultCenter] addObserver:watcher
						 selector:@selector(note:)
						     name:NSConnectionDidDieNotification
						   object:nil];

	/* --- A SERVICE, AND A CLIENT THAT FINDS IT --------------------------------------------------------- */
	{
		NSPort *published = [[NSPortNameServer defaultPortNameServer] portForName:@"probe.echo"];

		check("a-service-publishes-a-name-and-answers-for-its-root-object",
		      serving != nil && [serving isValid] && [serving rootObject] == service &&
		      [serving receivePort] != nil && published != nil,
		      @"the connection answers for its root object, and the name it was made with resolves through the "
		      @"default name server");
	}

	{
		id proxy = [NSConnection rootProxyForConnectionWithRegisteredName:@"probe.echo" host:nil];

		check("a-client-gets-a-proxy-for-the-published-name",
		      proxy != nil && [proxy connectionForProxy] != nil,
		      @"the name answers a proxy, and the proxy knows the connection it travels over");

		/* A CLIENT'S PROXY IS TOLD ITS PROTOCOL, which is what Apple's `-setProtocolForProxy:` is for and what
		 * this library needs it for too: an invocation is built from a SIGNATURE, and nothing but the protocol
		 * can describe a method the far side implements. */
		[proxy setProtocolForProxy:@protocol(FnEchoDoors)];

		/* --- THE ROUND TRIP: THE ANSWER IS THE SERVICE'S, NOT THE PROBE'S ---------------------------- */
		{
			/* THE CALL IS WRAPPED, because a raised error would ABORT the probe and report nothing — the
			 * lesson this thread keeps relearning: a faulting step becomes a check that PRINTS what it saw. */
			NSString *answer = nil;
			NSString *failure = nil;

			@try {
				answer = [(id<FnEchoDoors>)proxy greet:@"world"];
			} @catch (NSException *exception) {
				failure = [exception reason];
			}
			check("a-call-crosses-and-the-service-s-computed-answer-comes-back",
			      answer != nil && [answer isEqual:@"hello world"] && [service calls] == 1,
			      [NSString stringWithFormat:@"the proxy answered \"%@\"%s and the service recorded %d call(s) — "
				@"only the service builds that string, so a value here travelled",
				answer != nil ? answer : @"(nothing)",
				failure != nil ? [[@" with the error: " stringByAppendingString:failure] UTF8String] : "",
				[service calls]]);
		}

		/* --- TWO ARGUMENTS, AND A NIL RESULT --------------------------------------------------------- */
		{
			NSString *shouted = [(id<FnEchoDoors>)proxy shout:@"one" and:@"two"];
			id nothing = [(id<FnEchoDoors>)proxy nothing:@"something"];

			check("two-arguments-and-a-nil-result-cross",
			      [shouted isEqual:@"one! two!"] && nothing == nil && [service calls] == 3,
			      [NSString stringWithFormat:@"two arguments answered \"%@\" and a nil result arrived as nil "
				@"(not as an error), with the service's counter at %d", shouted, [service calls]]);
		}

		/* --- THE BOUNDARY, FROM BOTH SIDES ------------------------------------------------------------ */
		{
			BOOL refusedByService = NO;
			NSString *message = nil;

			@try {
				(void)[(id<FnEchoDoors>)proxy count];
			} @catch (NSException *exception) {
				refusedByService = YES;
				message = [exception reason];
			}
			check("a-scalar-result-is-refused-by-the-service",
			      refusedByService && message != nil &&
			      [message rangeOfString:@"object"].location != NSNotFound,
			      [NSString stringWithFormat:@"calling a method whose result is an int raised: %@",
				message != nil ? message : @"(nothing)"]);

			@try {
				(void)[(id<FnEchoDoors>)proxy doubleNumber:21];
			} @catch (NSException *exception) {
				refusedByService = NO;		/* reused as "the proxy refused" */
			}
			check("a-scalar-argument-is-refused-by-the-proxy-and-nothing-is-called",
			      !refusedByService && [service scalarCalls] == 0,
			      [NSString stringWithFormat:@"a method taking an int raised at the proxy, and the service's "
				@"scalar counter is still %d — a refusal that still called the method would be no boundary at "
				@"all", [service scalarCalls]]);
		}
	}

	/* --- AN ABSENT NAME, AND TAKING A SERVICE DOWN ----------------------------------------------------- */
	{
		id absent = [NSConnection rootProxyForConnectionWithRegisteredName:@"probe.absent" host:nil];

		check("an-unregistered-name-answers-no-proxy",
		      absent == nil,
		      @"a name nobody published is not a connection, so there is no proxy for it");

		[serving invalidate];
		check("invalidating-a-connection-posts-its-notification-and-withdraws-its-name",
		      ![serving isValid] && [watcher died] == 1 &&
		      [[NSPortNameServer defaultPortNameServer] portForName:@"probe.echo"] == nil,
		      [NSString stringWithFormat:@"isValid=%d, the die notification fired %d time(s), and the name now "
			@"resolves to nothing", (int)[serving isValid], [watcher died]]);
	}

	printf("FOUNDATION-DOBJECTS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DOBJECTS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DOBJECTS DONE\n");
	return failc ? 1 : 0;
}
