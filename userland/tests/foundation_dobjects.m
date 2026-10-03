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
 * AND WHAT CROSSES IS ANY `NSCoding` OBJECT, WHICH IS THE HALF §62.57 HAD TO RE-MEASURE: the coder carries a keyed
 * archive, so an object with its own state travels (its class crosses as a NAME the far side rebuilds) and an object
 * that conforms to nothing is refused by the archiver on the SENDING side. The two checks that end the client block
 * exist so that the correction is measured rather than asserted, and both of them count the service's calls — a
 * ticket that came back is a ticket that was built there, and an object refused here never reached it.
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
- (id)stamp:(id)ticket;		/* an object with its OWN STATE: only an archive can carry one */
- (id)opaque:(id)thing;		/* an object that is NOT NSCoding: refused on the SENDING side */
- (int)count;			/* a scalar RESULT: refused by the service */
- (id)doubleNumber:(int)number;	/* a scalar ARGUMENT: refused by the proxy */
@end

/* ---- AN OBJECT WITH ITS OWN STATE, AND ONE WITHOUT ---------------------------------------------- */

/* NOT ONE KIND HERE IS A PROPERTY-LIST KIND, and that is the point: `FnTicket` is a class this probe defines, its
 * only state is a string, and NOTHING about it is archivable except that it conforms to `NSCoding`. The carrier this
 * coder used before §62.57 could not have sent it, and `FnOpaque` is the control — the same sort of object without
 * the conformance, which the archiver must refuse by name. */
@interface FnTicket : NSObject <NSCoding>
{
	NSString *_tag;
}
- (instancetype)initWithTag:(NSString *)tag;
- (NSString *)tag;
@end

@implementation FnTicket

- (instancetype)initWithTag:(NSString *)tag
{
	self = [super init];
	if (self != nil) {
		/* A FRESH STRING AND NOT -copy, which is a fact about this library rather than a style: -copy is not a
		 * door every value class answers, and a probe that leaned on it would be measuring the wrong thing. */
		_tag = [NSString stringWithFormat:@"%@", tag];
	}
	return self;
}

- (NSString *)tag { return _tag; }

- (void)encodeWithCoder:(NSCoder *)coder { [coder encodeObject:_tag forKey:@"tag"]; }

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		id decoded = [coder decodeObjectForKey:@"tag"];

		_tag = decoded != nil ? [NSString stringWithFormat:@"%@", decoded] : @"(no tag)";
	}
	return self;
}

@end

@interface FnOpaque : NSObject
@end

@implementation FnOpaque
@end

/* ---- the service: doors that cross, and two that cannot ---- */

@interface FnEchoService : NSObject
{
	int _calls;			/* how many times ANY object door was invoked */
	int _scalarCalls;		/* how many times the scalar door was invoked — it must stay zero */
}
- (NSString *)greet:(NSString *)who;
- (NSString *)shout:(NSString *)word and:(NSString *)more;
- (id)nothing:(id)thing;
- (id)stamp:(id)ticket;
- (id)opaque:(id)thing;
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

/* THE SERVICE BUILDS THE OBJECT IT SENDS BACK, which is what makes this a round trip: the client never had this
 * object, and its class crossed as a name the far side rebuilt. */
- (id)stamp:(id)ticket
{
	_calls++;
	return [[FnTicket alloc] initWithTag:
		[NSString stringWithFormat:@"stamped-%@", [(FnTicket *)ticket tag]]];
}

- (id)opaque:(id)thing
{
	_calls++;			/* it must NOT reach this: the refusal is on the sending side */
	(void)thing;
	return @"opaque";
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

	{
		/* §63.116: THE TWO DOORS THAT COULD BE BUILT ON WHAT THE CLASS ALREADY KNOWS. ⚠ BOTH ARE ASSERTED BY
		 * PROPERTIES RATHER THAN BY SHAPE: the default connection is asserted to be THE SAME ONE TWICE (which is what
		 * "default" means), and the statistics dictionary to report the connection's OWN facts — including one that
		 * a caller has just changed, so a dictionary of constants cannot pass. */
		NSConnection *d1 = [NSConnection defaultConnection];
		NSConnection *d2 = [NSConnection defaultConnection];
		NSConnection *s = [NSConnection connectionWithReceivePort:nil sendPort:nil];
		NSDictionary *before;
		NSDictionary *after;

		[s addRequestMode:@"NSDefaultRunLoopMode"];
		before = [s statistics];
		[s addRequestMode:@"NSModalPanelRunLoopMode"];
		after = [s statistics];

		check("connection-default-connection-is-the-same-one-twice",
		      d1 != nil && d1 == d2,
		      [NSString stringWithFormat:@"first=%p second=%p", (void *)d1, (void *)d2]);
		check("connection-statistics-reports-its-own-state",
		      [[before objectForKey:@"NSConnectionRequestModeCount"] intValue] == 1 &&
		      [[after objectForKey:@"NSConnectionRequestModeCount"] intValue] == 2 &&
		      [[after objectForKey:@"NSConnectionIsValid"] boolValue] &&
		      [[after objectForKey:@"NSConnectionIsWaitingForReply"] boolValue] == NO,
		      [NSString stringWithFormat:@"before=%@ after=%@", before, after]);
		check("connection-statistics-values-are-numbers",
		      [[before objectForKey:@"NSConnectionRequestModeCount"] isKindOfClass:[NSNumber class]] &&
		      [[before objectForKey:@"NSConnectionIsValid"] isKindOfClass:[NSNumber class]],
		      @"the property's own type is a dictionary of numbers");
	}


	{
		/* §63.115: THE SEVEN STORED DOORS. ⚠ A connection with NO PORTS is what the class's own factory produces for
		 * nil/nil, and it is the right instrument here: these doors are state, and state does not need a wire. Each is
		 * asserted ALONE, and the two that are a pair (add/remove) are asserted to be INVERSES. */
		NSConnection *c = [NSConnection connectionWithReceivePort:nil sendPort:nil];

		[c setRequestTimeout:12.5];
		[c setIndependentConversationQueueing:YES];
		[c enableMultipleThreads];
		[c addRequestMode:@"NSDefaultRunLoopMode"];
		[c addRequestMode:@"NSModalPanelRunLoopMode"];
		[c addRequestMode:@"NSDefaultRunLoopMode"];	/* a duplicate, which must not appear twice */

		check("connection-request-timeout-round-trips",
		      [c requestTimeout] == 12.5,
		      [NSString stringWithFormat:@"timeout=%g", (double)[c requestTimeout]]);
		check("connection-conversation-queueing-round-trips",
		      [c independentConversationQueueing],
		      [NSString stringWithFormat:@"queueing=%d", (int)[c independentConversationQueueing]]);
		check("connection-multiple-threads-is-a-one-way-latch",
		      [c multipleThreadsEnabled],
		      [NSString stringWithFormat:@"enabled=%d", (int)[c multipleThreadsEnabled]]);
		check("connection-request-modes-are-a-set-that-keeps-order",
		      [[c requestModes] count] == 2 &&
		      [[[c requestModes] objectAtIndex:0] isEqualToString:@"NSDefaultRunLoopMode"] &&
		      [[[c requestModes] objectAtIndex:1] isEqualToString:@"NSModalPanelRunLoopMode"],
		      [NSString stringWithFormat:@"modes=%@", [c requestModes]]);
		[c removeRequestMode:@"NSDefaultRunLoopMode"];
		check("connection-remove-request-mode-inverts-add",
		      [[c requestModes] count] == 1 &&
		      [[[c requestModes] objectAtIndex:0] isEqualToString:@"NSModalPanelRunLoopMode"],
		      [NSString stringWithFormat:@"after-remove=%@", [c requestModes]]);
		check("connection-request-modes-answer-a-copy-not-the-store",
		      [[c requestModes] isKindOfClass:[NSArray class]] &&
		      ![[c requestModes] isKindOfClass:[NSMutableArray class]],
		      @"the property is declared copy and the getter answers a copy");
	}

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

		/* --- AN OBJECT WITH ITS OWN STATE, AND AN OBJECT THE ARCHIVER REFUSES ------------------------ */
		{
			FnTicket *sent = [[FnTicket alloc] initWithTag:@"42"];
			id back = nil;
			BOOL notConformingWasRefused = NO;
			NSString *refusal = nil;
			NSString *failure = nil;

			@try {
				back = [(id<FnEchoDoors>)proxy stamp:sent];
			} @catch (NSException *exception) {
				failure = [exception reason];
			}
			/* A DIFFERENT OBJECT WITH THE SERVICE'S OWN TAG: `!=` is half of the check, because a proxy that
			 * echoed back the ticket it was handed would pass a value comparison and fail this one. */
			check("an-object-with-its-own-state-crosses-both-ways",
			      back != nil && back != sent && [[(FnTicket *)back tag] isEqual:@"stamped-42"] &&
			      [service calls] == 4,
			      [NSString stringWithFormat:@"a ticket tagged \"42\" answered %@ tagged \"%@\"%s, and the "
				@"service recorded %d call(s)",
				back != nil ? (id)[back class] : (id)@"(nothing)",
				back != nil ? [(FnTicket *)back tag] : @"-",
				failure != nil ? [[@" with the error: " stringByAppendingString:failure] UTF8String] : "",
				[service calls]]);

			@try {
				(void)[(id<FnEchoDoors>)proxy opaque:[[FnOpaque alloc] init]];
			} @catch (NSException *exception) {
				notConformingWasRefused = YES;
				refusal = [exception reason];
			}
			/* AND THE COUNTER IS STILL 4: the archive is written BEFORE the message is sent, so an object the
			 * archiver refuses never leaves this side — which is the whole difference between refusing one and
			 * sending something the far side cannot rebuild. */
			check("an-object-that-is-not-nscoding-is-refused-by-the-coder",
			      notConformingWasRefused && refusal != nil &&
			      [refusal rangeOfString:@"FnOpaque"].location != NSNotFound && [service calls] == 4,
			      [NSString stringWithFormat:@"an object conforming to nothing raised: %@ — and the service's "
				@"counter is still %d", refusal != nil ? refusal : @"(nothing)", [service calls]]);
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

	/* --- THE CONNECTION REGISTRY, THE LOOKUP THAT STOPS ONE STEP EARLIER, AND THE CLOCK ---------------- */
	{
		/* `+allConnections` IS ASKED BEFORE ANY NEW CONNECTION IS MADE, so the serving connection it must hold is
		 * already there and `found` — the fresh connection made on the next line — is deliberately NOT in it. */
		NSArray *all = [NSConnection allConnections];
		NSConnection *found = [NSConnection connectionWithRegisteredName:@"probe.echo" host:nil];
		NSConnection *absent = [NSConnection connectionWithRegisteredName:@"probe.absent" host:nil];

		check("all-connections-answers-the-live-registry-and-holds-the-serving-connection",
		      [all isKindOfClass:[NSArray class]] && [all containsObject:serving],
		      [NSString stringWithFormat:@"+allConnections answered %lu connection(s) and the serving connection "
			@"is %@ among them", (unsigned long)[all count],
			([all containsObject:serving] ? @"one of them" : @"NOT one of them")]);

		check("a-registered-name-answers-a-self-contained-connection-and-an-absent-one-answers-nil",
		      found != nil && found != serving && [found receivePort] != nil && [found sendPort] != nil &&
		      absent == nil,
		      [NSString stringWithFormat:@"\"probe.echo\" answered a connection (receivePort=%@ sendPort=%@) "
			@"different from the service, and \"probe.absent\" answered %@",
			[found receivePort] != nil ? @"yes" : @"no",
			[found sendPort] != nil ? @"yes" : @"no",
			absent == nil ? @"nil" : @"a connection"]);

		check("a-connections-reply-timeout-defaults-to-60-and-round-trips",
		      [found replyTimeout] == 60.0 && ([found setReplyTimeout:12.5], [found replyTimeout] == 12.5),
		      [NSString stringWithFormat:@"the default reply timeout read %.1f and a value set to 12.5 read "
			@"back %.1f", 60.0, [found replyTimeout]]);
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

	{
		/* A PROXY DECLINES TO BE ARCHIVED, AND THE DECLINE IS A LEDGER ROW NOW (§63.19). This is an ABSENCE
		 * assertion, which this thread has twice caught as a bug — so it carries its own POSITIVE CONTROLS and
		 * the reason it is legitimate here: §63.17's `sort-refusals` asserted an absence that was only an
		 * artifact of work not yet done, while THIS absence IS the decision (NSDistantObject.h: a proxy's state
		 * is a live, per-process connection, so "the door is NOT DECLARED rather than declared and hollow"). If
		 * someone later declares it, this check FAILS and forces the decision to be revisited — which is
		 * exactly what a declined row should do.
		 *
		 * THE CONTROLS: the class must answer the doors it DOES have (so the absence cannot come from a class
		 * that is simply missing), and the two factories §63.19 landed must answer their own names.
		 *
		 * AND THE QUESTION GOES TO THE RUNTIME RATHER THAN TO `+instancesRespondToSelector:`, because
		 * `NSDistantObject` is an `NSProxy` SUBCLASS and this library's `NSProxy` is a root class WITHOUT that
		 * class method — the compiler refused it by name. `class_getInstanceMethod`/`class_getClassMethod` ask
		 * the same question and are the spelling that works for a proxy. */
		Class proxyClass = [NSDistantObject class];

		check("a-proxy-declines-to-be-archived-and-answers-its-own-doors",
		      class_getInstanceMethod(proxyClass, sel_registerName("initWithCoder:")) == NULL &&
		      class_getInstanceMethod(proxyClass, sel_registerName("encodeWithCoder:")) == NULL &&
		      class_getInstanceMethod(proxyClass, sel_registerName("connectionForProxy")) != NULL &&
		      class_getInstanceMethod(proxyClass, sel_registerName("setProtocolForProxy:")) != NULL &&
		      class_getInstanceMethod(proxyClass, sel_registerName("initWithTarget:connection:")) != NULL &&
		      class_getClassMethod(proxyClass, sel_registerName("proxyWithLocal:connection:")) != NULL &&
		      class_getClassMethod(proxyClass, sel_registerName("proxyWithTarget:connection:")) != NULL,
		      @"the coder doors are ABSENT by decision (a proxy's state is a live connection) while the class's own doors and the two factories §63.19 landed are all present");
	}

	printf("FOUNDATION-DOBJECTS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DOBJECTS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DOBJECTS DONE\n");
	return failc ? 1 : 0;
}
